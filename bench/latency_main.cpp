// Layer 6: the latency benchmark.
//
// Replays a realistic stream of millions of orders and reports how long
// EACH individual operation took, as percentiles (p50 ... p99.99, max),
// plus overall throughput.
//
// Usage: ./build/release/lob_latency [--msgs N] [--orders N] [--cpu N]
//                                    [--seed N] [--diagnose 1]

#include <sched.h>
#include <sys/resource.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include "latency_stats.hpp"
#include "lob/order_book.hpp"
#include "order_flow.hpp"
#include "tsc_clock.hpp"

using namespace lob;
using namespace lob::bench;

// ---------------------------------------------------------------------------
// Allocation counter. Replacing the global operator new lets us count every
// time ANY code in this program asks the system for heap memory.
// ---------------------------------------------------------------------------
namespace {
std::uint64_t g_allocations = 0;
}  // namespace

void* operator new(std::size_t size) {
  ++g_allocations;
  if (void* p = std::malloc(size)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

// Page faults so far: times the OS had to hand us a fresh page of memory.
std::uint64_t page_faults() {
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
  return static_cast<std::uint64_t>(u.ru_minflt + u.ru_majflt);
}

struct Options {
  FlowConfig flow;
  int cpu = 2;  // a P-core, and not core 0 (which handles most interrupts)
  bool diagnose = false;
};

Options parse(int argc, char** argv) {
  Options o;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string key = argv[i];
    const unsigned long long v = std::strtoull(argv[i + 1], nullptr, 10);
    if (key == "--msgs") o.flow.num_msgs = v;
    else if (key == "--orders") o.flow.target_orders = v;
    else if (key == "--cpu") o.cpu = static_cast<int>(v);
    else if (key == "--seed") o.flow.seed = static_cast<std::uint32_t>(v);
    else if (key == "--diagnose") o.diagnose = v != 0;
    else std::fprintf(stderr, "unknown option %s\n", key.c_str());
  }
  return o;
}

// Keep this thread on ONE core. Otherwise the OS may move it mid-run,
// e.g. from a fast P-core to a slow E-core, and the numbers become random.
bool pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(static_cast<std::size_t>(cpu), &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}

std::string read_line(const std::string& path) {
  std::ifstream f(path);
  std::string s;
  std::getline(f, s);
  return s.empty() ? "unknown" : s;
}

// "AC" or "battery": laptops run much slower on battery, so every result
// must say which one it was measured on.
std::string power_source() {
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator("/sys/class/power_supply", ec)) {
    if (read_line(e.path().string() + "/type") == "Mains") {
      return read_line(e.path().string() + "/online") == "1" ? "AC"
                                                             : "battery";
    }
  }
  return "unknown";
}

// Cost of the stopwatch itself: time "nothing" a million times.
double timer_overhead_ns(double ticks_per_ns) {
  std::vector<std::uint64_t> t(1'000'000);
  for (auto& x : t) {
    const std::uint64_t a = tsc_start();
    x = tsc_stop() - a;
  }
  return summarize(t, ticks_per_ns).p50;
}

constexpr std::array<const char*, 4> kNames{"add (passive)", "cancel",
                                            "aggressive limit", "market"};

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parse(argc, argv);

  // ---- 1. Environment: everything that affects the numbers ----------------
  const bool pinned = pin_to_cpu(opt.cpu);
  const std::string cpu_dir =
      "/sys/devices/system/cpu/cpu" + std::to_string(opt.cpu) + "/cpufreq/";
  const std::string governor = read_line(cpu_dir + "scaling_governor");
  const double ticks_per_ns = tsc_ticks_per_ns();
  const double overhead = timer_overhead_ns(ticks_per_ns);

  std::printf("Environment\n");
  std::printf("  pinned to cpu %d: %s | governor: %s | energy pref: %s\n",
              opt.cpu, pinned ? "yes" : "NO",
              governor.c_str(),
              read_line(cpu_dir + "energy_performance_preference").c_str());
  std::printf("  power: %s | TSC: %.3f GHz | timer overhead: %.1f ns "
              "(included below)\n",
              power_source().c_str(), ticks_per_ns, overhead);
  if (governor != "performance") {
    std::printf("  NOTE: governor is not 'performance'; expect noisier "
                "results.\n");
  }

  // ---- 2. Generate the order stream (not timed) --------------------------
  const auto g0 = std::chrono::steady_clock::now();
  const Flow flow = generate_flow(opt.flow);
  const double gen_s = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - g0)
                           .count();

  std::array<std::size_t, 4> mix{};
  for (const Msg& m : flow.msgs) ++mix[static_cast<std::size_t>(m.type)];
  const auto pct = [&](std::size_t n) {
    return 100.0 * static_cast<double>(n) /
           static_cast<double>(flow.msgs.size());
  };
  std::printf("\nOrder flow (seed %u, generated in %.1f s)\n", opt.flow.seed,
              gen_s);
  std::printf("  %zu messages: %.1f%% add, %.1f%% cancel, %.1f%% aggressive "
              "limit, %.1f%% market\n",
              flow.msgs.size(), pct(mix[0]), pct(mix[1]), pct(mix[2]),
              pct(mix[3]));
  std::printf("  book starts with %zu resting orders\n", flow.prefill.size());

  // ---- 3. Throughput pass: no per-message stopwatch ----------------------
  // Also serves as a warm-up (code and memory pages are touched once).
  std::size_t trades_a = 0, orders_a = 0;
  double throughput = 0;
  {
    OrderBook book;
    for (const Msg& m : flow.prefill) apply(book, m);
    book.clear_trades();
    const std::uint64_t t0 = tsc_start();
    for (const Msg& m : flow.msgs) {
      apply(book, m);
      trades_a += book.trades().size();
      book.clear_trades();
    }
    const std::uint64_t t1 = tsc_stop();
    const double secs = static_cast<double>(t1 - t0) / ticks_per_ns / 1e9;
    throughput = static_cast<double>(flow.msgs.size()) / secs;
    orders_a = book.order_count();
  }

  // ---- 4. Latency pass: stopwatch around EVERY single message ------------
  std::array<std::vector<std::uint64_t>, 4> lat;
  for (std::size_t i = 0; i < 4; ++i) lat[i].reserve(mix[i]);
  std::vector<std::uint64_t> all;
  all.reserve(flow.msgs.size());
  std::size_t trades_b = 0, orders_b = 0;
  std::uint64_t allocs = 0, faults = 0;
  {
    OrderBook book;
    for (const Msg& m : flow.prefill) apply(book, m);
    book.clear_trades();
    const std::uint64_t a0 = g_allocations, f0 = page_faults();
    for (const Msg& m : flow.msgs) {
      const std::uint64_t t0 = tsc_start();
      apply(book, m);
      const std::uint64_t t1 = tsc_stop();
      lat[static_cast<std::size_t>(m.type)].push_back(t1 - t0);
      all.push_back(t1 - t0);
      trades_b += book.trades().size();
      book.clear_trades();  // outside the timed region
    }
    allocs = g_allocations - a0;
    faults = page_faults() - f0;
    orders_b = book.order_count();
  }

  // Both passes replay the same messages, so they must end identically.
  if (trades_a != trades_b || orders_a != orders_b) {
    std::printf("ERROR: the two passes disagree (trades %zu vs %zu)\n",
                trades_a, trades_b);
    return 1;
  }

  // ---- 5. Report --------------------------------------------------------
  std::printf("  %zu trades, %zu orders resting at the end\n\n", trades_b,
              orders_b);
  std::printf("Latency per message in nanoseconds\n\n");
  std::printf("| operation        |      count |  mean |   p50 |   p90 |"
              "   p99 | p99.9 | p99.99 |      max |\n");
  std::printf("|------------------|-----------:|------:|------:|------:|"
              "------:|------:|-------:|---------:|\n");
  const auto row = [&](const char* name, std::vector<std::uint64_t>& v) {
    const LatencySummary s = summarize(v, ticks_per_ns);
    std::printf("| %-16s | %10zu | %5.0f | %5.0f | %5.0f | %5.0f | %5.0f |"
                " %6.0f | %8.0f |\n",
                name, s.count, s.mean, s.p50, s.p90, s.p99, s.p999, s.p9999,
                s.max);
  };
  for (std::size_t i = 0; i < 4; ++i) row(kNames[i], lat[i]);
  row("ALL", all);

  std::printf("\nThroughput: %.2f million messages/second (%.0f ns per "
              "message on average, no stopwatch)\n",
              throughput / 1e6, 1e9 / throughput);
  std::printf("Heap allocations: %.2f per message | page faults: %llu\n",
              static_cast<double>(allocs) /
                  static_cast<double>(flow.msgs.size()),
              static_cast<unsigned long long>(faults));

  // ---- 6. Diagnosis: what do the SLOW messages have in common? ----------
  // A third replay. For every message we also record whether it allocated
  // memory or caused a page fault (getrusage is a system call, so this pass
  // is only for detective work, not for the headline numbers).
  if (opt.diagnose) {
    constexpr double kSlowNs = 1000.0;
    std::size_t slow = 0, slow_fault = 0, slow_alloc = 0;
    std::size_t fast_alloc = 0, fast = 0;
    OrderBook book;
    for (const Msg& m : flow.prefill) apply(book, m);
    book.clear_trades();
    for (const Msg& m : flow.msgs) {
      const std::uint64_t f0 = page_faults();
      const std::uint64_t a0 = g_allocations;
      const std::uint64_t t0 = tsc_start();
      apply(book, m);
      const std::uint64_t t1 = tsc_stop();
      const bool allocated = g_allocations != a0;
      const bool faulted = page_faults() != f0;
      book.clear_trades();
      if (static_cast<double>(t1 - t0) / ticks_per_ns >= kSlowNs) {
        ++slow;
        if (faulted) ++slow_fault;
        else if (allocated) ++slow_alloc;
      } else {
        ++fast;
        if (allocated) ++fast_alloc;
      }
    }
    const auto share = [](std::size_t part, std::size_t whole) {
      return whole == 0 ? 0.0
                        : 100.0 * static_cast<double>(part) /
                              static_cast<double>(whole);
    };
    std::printf("\nDiagnosis of messages slower than %.0f ns\n", kSlowNs);
    std::printf("  slow messages:            %zu (%.3f%% of all)\n", slow,
                share(slow, flow.msgs.size()));
    std::printf("  ...with a page fault:     %zu (%.1f%%)\n", slow_fault,
                share(slow_fault, slow));
    std::printf("  ...with an allocation:    %zu (%.1f%%)\n", slow_alloc,
                share(slow_alloc, slow));
    std::printf("  ...with neither:          %zu (%.1f%%)\n",
                slow - slow_fault - slow_alloc,
                share(slow - slow_fault - slow_alloc, slow));
    std::printf("  (for comparison, fast messages that allocated: %.1f%%)\n",
                share(fast_alloc, fast));
  }
  return 0;
}
