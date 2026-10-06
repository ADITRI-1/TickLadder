// Layer 5: tests written by asking "how could someone break this engine?"
// Each test targets a boundary: the smallest or largest values, empty or
// huge books, rejected orders, and ids being reused.

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "lob/order_book.hpp"

using namespace lob;
using Trades = std::vector<Trade>;

constexpr Quantity kMaxQty = std::numeric_limits<Quantity>::max();
constexpr Price kMaxPrice = std::numeric_limits<Price>::max();

// --- Extreme values ---------------------------------------------------------

TEST(EdgeCases, SmallestValidPriceAndQuantity) {
  OrderBook book;
  EXPECT_EQ(book.add(1, Side::Buy, 1, 1), Status::Ok);  // Rs 0.01, 1 share
  EXPECT_EQ(book.best_bid(), 1);
}

TEST(EdgeCases, HugeQuantitiesDoNotOverflowTheLevelTotal) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, kMaxQty);
  book.add(2, Side::Sell, 10100, kMaxQty);
  // Two max-size orders: the total needs more than 32 bits.
  EXPECT_EQ(book.volume_at(Side::Sell, 10100), 2ull * kMaxQty);

  book.add(3, Side::Buy, 10100, kMaxQty);  // fully fills order 1
  EXPECT_EQ(book.trades(), (Trades{{3, 1, 10100, kMaxQty}}));
  EXPECT_EQ(book.volume_at(Side::Sell, 10100), std::uint64_t{kMaxQty});
}

TEST(EdgeCases, PricesOutsideTheBandAreRejected) {
  OrderBook book;  // default band: [1, max_price()]
  EXPECT_EQ(book.add(1, Side::Sell, book.max_price() + 1, 5),
            Status::InvalidPrice);
  EXPECT_EQ(book.add(2, Side::Buy, kMaxPrice, 5), Status::InvalidPrice);
  EXPECT_EQ(book.add(3, Side::Buy, book.min_price() - 1, 5),
            Status::InvalidPrice);
  EXPECT_EQ(book.order_count(), 0u);
}

TEST(EdgeCases, BothEndsOfTheBandWork) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Sell, book.max_price(), 5);  // highest allowed price
  book.add(3, Side::Buy, book.min_price(), 5);   // lowest allowed price
  EXPECT_EQ(book.best_bid(), book.min_price());

  book.add(4, Side::Buy, book.max_price(), 10);  // sweeps both asks
  EXPECT_EQ(book.trades().size(), 2u);
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(EdgeCases, CustomPriceBand) {
  BookConfig cfg;
  cfg.min_price = 50'000;
  cfg.max_price = 50'099;  // a band of only 100 ticks
  OrderBook book(cfg);
  EXPECT_EQ(book.add(1, Side::Buy, 49'999, 1), Status::InvalidPrice);
  EXPECT_EQ(book.add(2, Side::Buy, 50'000, 1), Status::Ok);
  EXPECT_EQ(book.add(3, Side::Sell, 50'099, 1), Status::Ok);
  EXPECT_EQ(book.add(4, Side::Sell, 50'100, 1), Status::InvalidPrice);
  EXPECT_EQ(book.market(5, Side::Buy, 1), Status::Ok);  // markets have no price
  EXPECT_EQ(book.trades().size(), 1u);
}

// --- Big books --------------------------------------------------------------

TEST(EdgeCases, MarketOrderSweepsAThousandLevelsInPriceOrder) {
  OrderBook book;
  for (OrderId i = 1; i <= 1000; ++i) {
    // Added in a scrambled price order on purpose.
    const auto price = static_cast<Price>(10000 + (i * 7919) % 1000);
    book.add(i, Side::Sell, price, 1);
  }
  book.market(5000, Side::Buy, 1000);

  ASSERT_EQ(book.trades().size(), 1000u);
  for (std::size_t i = 1; i < book.trades().size(); ++i) {
    EXPECT_LT(book.trades()[i - 1].price, book.trades()[i].price);  // cheapest first
  }
  EXPECT_EQ(book.level_count(Side::Sell), 0u);
  EXPECT_EQ(book.order_count(), 0u);
}

TEST(EdgeCases, LongQueueAtOnePriceIsFilledStrictlyInArrivalOrder) {
  OrderBook book;
  for (OrderId i = 1; i <= 10000; ++i) {
    book.add(i, Side::Buy, 10000, 1);
  }
  book.market(20000, Side::Sell, 10000);

  ASSERT_EQ(book.trades().size(), 10000u);
  for (std::size_t i = 0; i < book.trades().size(); ++i) {
    EXPECT_EQ(book.trades()[i].buy_id, i + 1);  // 1, 2, 3, ... 10000
  }
}

TEST(EdgeCases, MarketOrderExactlyDrainsTheBook) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 3);
  book.add(2, Side::Buy, 9900, 4);
  book.market(3, Side::Sell, 7);  // exactly what is there
  EXPECT_EQ(book.trades().size(), 2u);
  EXPECT_EQ(book.order_count(), 0u);
  EXPECT_FALSE(book.best_bid().has_value());
}

// --- Rejected orders must have NO side effects -------------------------------

TEST(EdgeCases, DuplicateIdIsRejectedBeforeItCanTrade) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.add(2, Side::Sell, 10100, 10);
  // Id 1 is taken. This sell WOULD cross the bid, but must be rejected first.
  EXPECT_EQ(book.add(1, Side::Sell, 10000, 10), Status::DuplicateId);
  EXPECT_TRUE(book.trades().empty());
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 10u);
}

TEST(EdgeCases, InvalidOrdersNeverTrade) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  EXPECT_EQ(book.add(2, Side::Buy, 0, 10), Status::InvalidPrice);
  EXPECT_EQ(book.add(3, Side::Buy, 10100, 0), Status::InvalidQuantity);
  EXPECT_EQ(book.market(4, Side::Buy, 0), Status::InvalidQuantity);
  EXPECT_TRUE(book.trades().empty());
  EXPECT_EQ(book.volume_at(Side::Sell, 10100), 10u);
}

// --- Ids after an order is gone ----------------------------------------------

TEST(EdgeCases, IdOfFullyFilledOrderCanBeReused) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Buy, 10100, 5);  // order 1 is now fully filled
  EXPECT_EQ(book.add(1, Side::Buy, 9900, 5), Status::Ok);
}

TEST(EdgeCases, MarketOrderIdIsNeverStored) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.market(2, Side::Buy, 10);            // 5 filled, 5 dropped
  EXPECT_EQ(book.find(2), nullptr);
  EXPECT_EQ(book.cancel(2), Status::UnknownId);
}

// --- Price levels being emptied and created again ----------------------------

TEST(EdgeCases, LevelEmptiedByTradingCanBeCreatedAgain) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Buy, 10100, 5);     // level 10100 on asks disappears
  book.add(3, Side::Sell, 10100, 7);    // fresh level at the same price
  EXPECT_EQ(book.depth(Side::Sell, 5), (std::vector<LevelInfo>{{10100, 7, 1}}));
  EXPECT_EQ(book.find(3)->level->head->id, 3u);
}

TEST(EdgeCases, LeftoverOfAggressiveSellRestsOnTheAskSide) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 3);
  book.add(2, Side::Sell, 10000, 8);  // fills 3, rests 5 as a SELL
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_EQ(book.best_ask(), 10000);
  EXPECT_EQ(book.find(2)->side, Side::Sell);
  EXPECT_EQ(book.find(2)->qty, 5u);
}

TEST(EdgeCases, CancelHeadThenMatchGoesToTheNextInLine) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Sell, 10100, 5);
  book.cancel(1);
  book.add(3, Side::Buy, 10100, 5);
  EXPECT_EQ(book.trades(), (Trades{{3, 2, 10100, 5}}));
}

TEST(EdgeCases, PartFillThenCancelThenNewOrdersStayConsistent) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Buy, 10100, 4);  // 1 has 6 left
  book.cancel(1);
  book.clear_trades();
  book.add(3, Side::Sell, 10100, 2);
  book.add(4, Side::Buy, 10100, 2);
  EXPECT_EQ(book.trades(), (Trades{{4, 3, 10100, 2}}));
  EXPECT_EQ(book.order_count(), 0u);
}

// --- Views ------------------------------------------------------------------

TEST(EdgeCases, DepthOfZeroLevelsIsEmpty) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 5);
  EXPECT_TRUE(book.depth(Side::Buy, 0).empty());
}

TEST(EdgeCases, TradeLogKeepsGrowingUntilCleared) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Buy, 10100, 2);
  book.add(3, Side::Buy, 10100, 2);
  EXPECT_EQ(book.trades().size(), 2u);  // from two different add() calls
}
