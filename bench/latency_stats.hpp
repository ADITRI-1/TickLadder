#pragma once

// Turns millions of individual timings into percentiles.
//
// Why percentiles and not the average: if 999 orders take 50 ns and one
// takes 50,000 ns, the average (~100 ns) hides the disaster. p99.9 shows it.
// In trading, the slow outlier is the one that loses money.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace lob::bench {

struct LatencySummary {
  std::size_t count{};
  double mean{}, p50{}, p90{}, p99{}, p999{}, p9999{}, max{};  // nanoseconds
};

// Nearest-rank percentile of an already SORTED vector, e.g. p = 0.99.
inline std::uint64_t percentile(const std::vector<std::uint64_t>& sorted,
                                double p) {
  if (sorted.empty()) return 0;
  const auto n = static_cast<double>(sorted.size());
  auto rank = static_cast<std::size_t>(std::ceil(p * n));
  rank = std::clamp<std::size_t>(rank, 1, sorted.size());
  return sorted[rank - 1];
}

// Sorts `ticks` in place and converts the results to nanoseconds.
inline LatencySummary summarize(std::vector<std::uint64_t>& ticks,
                                double ticks_per_ns) {
  LatencySummary s;
  s.count = ticks.size();
  if (ticks.empty()) return s;
  std::sort(ticks.begin(), ticks.end());
  const auto ns = [&](std::uint64_t t) {
    return static_cast<double>(t) / ticks_per_ns;
  };
  const double sum = std::accumulate(ticks.begin(), ticks.end(), 0.0,
                                     [](double acc, std::uint64_t t) {
                                       return acc + static_cast<double>(t);
                                     });
  s.mean = sum / static_cast<double>(ticks.size()) / ticks_per_ns;
  s.p50 = ns(percentile(ticks, 0.50));
  s.p90 = ns(percentile(ticks, 0.90));
  s.p99 = ns(percentile(ticks, 0.99));
  s.p999 = ns(percentile(ticks, 0.999));
  s.p9999 = ns(percentile(ticks, 0.9999));
  s.max = ns(ticks.back());
  return s;
}

}  // namespace lob::bench
