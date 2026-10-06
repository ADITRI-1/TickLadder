#include <benchmark/benchmark.h>

#include "lob/order_book.hpp"

namespace {

// A book with 1000 orders spread over 100 price levels on each side.
lob::OrderId fill_book(lob::OrderBook& book) {
  lob::OrderId id = 1;
  for (int i = 0; i < 1000; ++i) {
    book.add(id++, lob::Side::Buy, 10000 - i % 100, 10);
    book.add(id++, lob::Side::Sell, 10100 + i % 100, 10);
  }
  return id;
}

}  // namespace

// One passive add followed by one cancel.
static void BM_AddCancel(benchmark::State& state) {
  lob::OrderBook book;
  lob::OrderId id = fill_book(book);

  for (auto _ : state) {
    const lob::Price price = 10000 - static_cast<lob::Price>(id % 100);
    book.add(id, lob::Side::Buy, price, 10);
    book.cancel(id);
    ++id;
  }
}
BENCHMARK(BM_AddCancel);

// One resting sell is added at the best ask, then a buy crosses and fully
// fills it: one trade per iteration, and the book returns to its start state.
static void BM_AddThenMatch(benchmark::State& state) {
  lob::OrderBook book;
  lob::OrderId id = fill_book(book);

  for (auto _ : state) {
    book.add(id++, lob::Side::Sell, 10050, 10);  // new best ask
    book.add(id++, lob::Side::Buy, 10050, 10);   // crosses it: 1 trade
    book.clear_trades();  // keeps capacity: no memory request next time
  }
}
BENCHMARK(BM_AddThenMatch);
