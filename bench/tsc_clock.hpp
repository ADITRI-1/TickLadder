#pragma once

// A stopwatch built on the CPU's Time Stamp Counter (TSC).
//
// The TSC is a counter inside the CPU that ticks at a constant rate (on this
// machine ~2.5 billion ticks per second, whatever the current CPU speed is:
// "constant_tsc" / "nonstop_tsc" in /proc/cpuinfo). Reading it costs a few
// nanoseconds, far less than asking the operating system for the time,
// which matters when the thing being timed only takes ~50 ns.

#include <x86intrin.h>

#include <chrono>
#include <cstdint>

namespace lob::bench {

// lfence = "finish everything before this line first". Without it the CPU
// could start the timed code early or read the counter late (out-of-order
// execution) and the measurement would be wrong.
inline std::uint64_t tsc_start() noexcept {
  _mm_lfence();
  const std::uint64_t t = __rdtsc();
  _mm_lfence();
  return t;
}

// rdtscp waits for the timed code to finish before reading the counter.
inline std::uint64_t tsc_stop() noexcept {
  unsigned int core_id;
  const std::uint64_t t = __rdtscp(&core_id);
  _mm_lfence();
  return t;
}

// How many TSC ticks happen per nanosecond, measured against the OS clock
// over `ms` milliseconds of busy waiting.
inline double tsc_ticks_per_ns(int ms = 200) {
  using Clock = std::chrono::steady_clock;
  const auto c0 = Clock::now();
  const std::uint64_t t0 = tsc_start();
  while (Clock::now() - c0 < std::chrono::milliseconds(ms)) {
  }
  const std::uint64_t t1 = tsc_stop();
  const auto c1 = Clock::now();
  const double ns = std::chrono::duration<double, std::nano>(c1 - c0).count();
  return static_cast<double>(t1 - t0) / ns;
}

}  // namespace lob::bench
