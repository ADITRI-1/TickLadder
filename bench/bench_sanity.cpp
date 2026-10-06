#include <benchmark/benchmark.h>

#include "lob/version.hpp"

// Smallest possible benchmark: proves Google Benchmark builds and runs.
static void BM_Version(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(lob::version());
  }
}
BENCHMARK(BM_Version);
