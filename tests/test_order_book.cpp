#include <gtest/gtest.h>

#include <vector>

#include "lob/order_book.hpp"

using namespace lob;

// --- Empty book -------------------------------------------------------------

TEST(OrderBook, EmptyBookHasNoBestPrices) {
  OrderBook book;
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.order_count(), 0u);
}

// --- Adding -----------------------------------------------------------------

TEST(OrderBook, BestBidIsHighestBuyPrice) {
  OrderBook book;
  EXPECT_EQ(book.add(1, Side::Buy, 10000, 10), Status::Ok);
  EXPECT_EQ(book.add(2, Side::Buy, 10050, 10), Status::Ok);
  EXPECT_EQ(book.add(3, Side::Buy, 9950, 10), Status::Ok);
  EXPECT_EQ(book.best_bid(), 10050);
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, BestAskIsLowestSellPrice) {
  OrderBook book;
  book.add(1, Side::Sell, 10200, 10);
  book.add(2, Side::Sell, 10100, 10);
  book.add(3, Side::Sell, 10300, 10);
  EXPECT_EQ(book.best_ask(), 10100);
}

TEST(OrderBook, SamePriceSharesOneLevelInArrivalOrder) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.add(2, Side::Buy, 10000, 25);
  book.add(3, Side::Buy, 10000, 5);

  EXPECT_EQ(book.level_count(Side::Buy), 1u);
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 40u);

  // Walk the queue: must be 1 -> 2 -> 3 (time priority).
  const PriceLevel* lvl = book.find(1)->level;
  std::vector<OrderId> queue;
  for (const Order* o = lvl->head; o != nullptr; o = o->next) {
    queue.push_back(o->id);
  }
  EXPECT_EQ(queue, (std::vector<OrderId>{1, 2, 3}));
}

TEST(OrderBook, BidsAndAsksAreSeparate) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.add(2, Side::Sell, 10100, 7);
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 10u);
  EXPECT_EQ(book.volume_at(Side::Sell, 10000), 0u);
  EXPECT_EQ(book.volume_at(Side::Sell, 10100), 7u);
}

// --- Rejected adds ----------------------------------------------------------

TEST(OrderBook, RejectsDuplicateId) {
  OrderBook book;
  EXPECT_EQ(book.add(1, Side::Buy, 10000, 10), Status::Ok);
  EXPECT_EQ(book.add(1, Side::Sell, 10100, 5), Status::DuplicateId);
  EXPECT_EQ(book.order_count(), 1u);
  EXPECT_FALSE(book.best_ask().has_value());  // the rejected order left no trace
}

TEST(OrderBook, RejectsZeroQuantityAndBadPrice) {
  OrderBook book;
  EXPECT_EQ(book.add(1, Side::Buy, 10000, 0), Status::InvalidQuantity);
  EXPECT_EQ(book.add(2, Side::Buy, 0, 10), Status::InvalidPrice);
  EXPECT_EQ(book.add(3, Side::Buy, -5, 10), Status::InvalidPrice);
  EXPECT_EQ(book.order_count(), 0u);
}

// --- Cancelling -------------------------------------------------------------

TEST(OrderBook, CancelReducesVolume) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.add(2, Side::Buy, 10000, 25);
  EXPECT_EQ(book.cancel(1), Status::Ok);
  EXPECT_EQ(book.volume_at(Side::Buy, 10000), 25u);
  EXPECT_EQ(book.find(1), nullptr);
  EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, CancelLastOrderAtBestPriceMovesBestPrice) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Sell, 10200, 10);
  EXPECT_EQ(book.cancel(1), Status::Ok);
  EXPECT_EQ(book.best_ask(), 10200);           // next best takes over
  EXPECT_EQ(book.level_count(Side::Sell), 1u);  // empty level was removed
}

TEST(OrderBook, CancelEverythingLeavesEmptyBook) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.add(2, Side::Sell, 10100, 10);
  book.cancel(1);
  book.cancel(2);
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.level_count(Side::Buy), 0u);
  EXPECT_EQ(book.level_count(Side::Sell), 0u);
}

TEST(OrderBook, CancelFromMiddleKeepsOthersInOrder) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.add(2, Side::Buy, 10000, 10);
  book.add(3, Side::Buy, 10000, 10);
  book.cancel(2);

  const PriceLevel* lvl = book.find(1)->level;
  EXPECT_EQ(lvl->head->id, 1u);
  EXPECT_EQ(lvl->head->next->id, 3u);
  EXPECT_EQ(lvl->tail->id, 3u);
}

TEST(OrderBook, CancelUnknownOrTwiceIsRejected) {
  OrderBook book;
  EXPECT_EQ(book.cancel(42), Status::UnknownId);
  book.add(1, Side::Buy, 10000, 10);
  EXPECT_EQ(book.cancel(1), Status::Ok);
  EXPECT_EQ(book.cancel(1), Status::UnknownId);  // already gone
}

TEST(OrderBook, IdCanBeReusedAfterCancel) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 10);
  book.cancel(1);
  EXPECT_EQ(book.add(1, Side::Sell, 10100, 5), Status::Ok);
  EXPECT_EQ(book.best_ask(), 10100);
}

// --- Depth ------------------------------------------------------------------

TEST(OrderBook, DepthListsBestLevelsFirst) {
  OrderBook book;
  book.add(1, Side::Buy, 9900, 5);
  book.add(2, Side::Buy, 10000, 10);
  book.add(3, Side::Buy, 10000, 20);
  book.add(4, Side::Buy, 9800, 1);

  std::vector<LevelInfo> expected{{10000, 30, 2}, {9900, 5, 1}};
  EXPECT_EQ(book.depth(Side::Buy, 2), expected);
  EXPECT_EQ(book.depth(Side::Buy, 10).size(), 3u);  // only 3 levels exist
  EXPECT_TRUE(book.depth(Side::Sell, 5).empty());
}
