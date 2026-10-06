#include <benchmark/benchmark.h>

#include "lob/order_book.hpp"

// One add followed by one cancel, on a book that already holds 1000 orders
// spread over 100 price levels on each side.
static void BM_AddCancel(benchmark::State& state) {
  lob::OrderBook book;
  lob::OrderId id = 1;
  for (int i = 0; i < 1000; ++i) {
    book.add(id++, lob::Side::Buy, 10000 - i % 100, 10);
    book.add(id++, lob::Side::Sell, 10100 + i % 100, 10);
  }

  for (auto _ : state) {
    const lob::Price price = 10000 - static_cast<lob::Price>(id % 100);
    book.add(id, lob::Side::Buy, price, 10);
    book.cancel(id);
    ++id;
  }
}
BENCHMARK(BM_AddCancel);
