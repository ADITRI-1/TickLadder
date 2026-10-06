#include <benchmark/benchmark.h>

#include "lob/types.hpp"

// Cost of joining and leaving a price level queue: should be a few ns.
static void BM_PushRemove(benchmark::State& state) {
  lob::PriceLevel lvl;
  lob::Order resting;
  resting.qty = 10;
  lvl.push_back(&resting);  // queue is not empty, like a real level

  lob::Order o;
  o.qty = 5;
  for (auto _ : state) {
    lvl.push_back(&o);
    lvl.remove(&o);
    benchmark::DoNotOptimize(lvl);
  }
}
BENCHMARK(BM_PushRemove);
