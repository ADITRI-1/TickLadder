#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "lob/order_book.hpp"

using namespace lob;

using Trades = std::vector<Trade>;

// --- Limit orders that do NOT cross -----------------------------------------

TEST(Matching, NonCrossingOrdersJustRest) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Buy, 10000, 10);  // 100.00 < 101.00: no deal
  EXPECT_TRUE(book.trades().empty());
  EXPECT_EQ(book.best_bid(), 10000);
  EXPECT_EQ(book.best_ask(), 10100);
}

// --- Simple full fill -------------------------------------------------------

TEST(Matching, ExactMatchFillsBothOrders) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Buy, 10100, 10);

  EXPECT_EQ(book.trades(), (Trades{{2, 1, 10100, 10}}));
  EXPECT_EQ(book.order_count(), 0u);  // both completely gone
  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Matching, TradeHappensAtTheRestingPrice) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Buy, 10500, 10);  // willing to pay 105, seller asked 101

  // The resting order was there first, so ITS price is used.
  EXPECT_EQ(book.trades(), (Trades{{2, 1, 10100, 10}}));
}

// --- Partial fills ----------------------------------------------------------

TEST(Matching, IncomingOrderPartlyFilledRestsTheRest) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Buy, 10100, 8);  // wants 8, only 5 available

  EXPECT_EQ(book.trades(), (Trades{{2, 1, 10100, 5}}));
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.best_bid(), 10100);  // remaining 3 now wait as a bid
  EXPECT_EQ(book.find(2)->qty, 3u);
}

TEST(Matching, RestingOrderPartlyFilledKeepsItsPlace) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Sell, 10100, 10);
  book.add(3, Side::Buy, 10100, 4);  // takes 4 of order 1's 10

  EXPECT_EQ(book.trades(), (Trades{{3, 1, 10100, 4}}));
  EXPECT_EQ(book.find(1)->qty, 6u);
  EXPECT_EQ(book.find(1)->level->head->id, 1u);  // 1 is still first in line
  EXPECT_EQ(book.volume_at(Side::Sell, 10100), 16u);
}

// --- Price-time priority ----------------------------------------------------

TEST(Matching, SamePriceFillsOldestFirst) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Sell, 10100, 5);
  book.add(3, Side::Sell, 10100, 5);
  book.add(4, Side::Buy, 10100, 7);

  EXPECT_EQ(book.trades(), (Trades{{4, 1, 10100, 5}, {4, 2, 10100, 2}}));
  EXPECT_EQ(book.find(2)->qty, 3u);  // 2 partly filled
  EXPECT_EQ(book.find(3)->qty, 5u);  // 3 untouched
}

TEST(Matching, BetterPriceFillsFirstEvenIfItArrivedLater) {
  OrderBook book;
  book.add(1, Side::Sell, 10200, 5);  // arrived first, worse price
  book.add(2, Side::Sell, 10100, 5);  // arrived later, better price
  book.add(3, Side::Buy, 10200, 5);

  EXPECT_EQ(book.trades(), (Trades{{3, 2, 10100, 5}}));  // price beats time
  EXPECT_EQ(book.best_ask(), 10200);
}

TEST(Matching, BigOrderSweepsSeveralLevels) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Sell, 10200, 5);
  book.add(3, Side::Sell, 10300, 5);
  book.add(4, Side::Buy, 10200, 12);  // limit 102: may take 101 and 102 only

  EXPECT_EQ(book.trades(), (Trades{{4, 1, 10100, 5}, {4, 2, 10200, 5}}));
  EXPECT_EQ(book.best_ask(), 10300);  // 103 was too expensive
  EXPECT_EQ(book.best_bid(), 10200);  // leftover 2 rests at its limit
  EXPECT_EQ(book.volume_at(Side::Buy, 10200), 2u);
  EXPECT_EQ(book.level_count(Side::Sell), 1u);  // emptied levels removed
}

TEST(Matching, SellerHitsHighestBidsFirst) {
  OrderBook book;
  book.add(1, Side::Buy, 9900, 5);
  book.add(2, Side::Buy, 10000, 5);
  book.add(3, Side::Sell, 9900, 8);

  // buy_id is the buyer, sell_id the seller, whoever was the aggressor.
  EXPECT_EQ(book.trades(), (Trades{{2, 3, 10000, 5}, {1, 3, 9900, 3}}));
  EXPECT_EQ(book.find(1)->qty, 2u);
}

// --- Market orders ----------------------------------------------------------

TEST(Matching, MarketOrderTakesWhatIsThereAndDropsTheRest) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Sell, 10900, 2);  // far away, but market takes any price
  EXPECT_EQ(book.market(3, Side::Buy, 10), Status::Ok);

  EXPECT_EQ(book.trades(), (Trades{{3, 1, 10100, 5}, {3, 2, 10900, 2}}));
  EXPECT_EQ(book.order_count(), 0u);  // the missing 3 did NOT rest
  EXPECT_FALSE(book.best_bid().has_value());
}

TEST(Matching, MarketOrderOnEmptyBookDoesNothing) {
  OrderBook book;
  EXPECT_EQ(book.market(1, Side::Sell, 10), Status::Ok);
  EXPECT_TRUE(book.trades().empty());
  EXPECT_EQ(book.order_count(), 0u);
}

TEST(Matching, MarketOrderValidation) {
  OrderBook book;
  book.add(1, Side::Buy, 10000, 5);
  EXPECT_EQ(book.market(2, Side::Sell, 0), Status::InvalidQuantity);
  EXPECT_EQ(book.market(1, Side::Sell, 5), Status::DuplicateId);
  EXPECT_TRUE(book.trades().empty());
}

// --- Cancel after matching --------------------------------------------------

TEST(Matching, FilledOrderCannotBeCancelled) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Buy, 10100, 5);
  EXPECT_EQ(book.cancel(1), Status::UnknownId);  // it already traded away
}

TEST(Matching, PartlyFilledOrderCanBeCancelled) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 10);
  book.add(2, Side::Buy, 10100, 4);
  EXPECT_EQ(book.cancel(1), Status::Ok);  // cancels the remaining 6
  EXPECT_EQ(book.order_count(), 0u);
  EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Matching, ClearTradesEmptiesTheLog) {
  OrderBook book;
  book.add(1, Side::Sell, 10100, 5);
  book.add(2, Side::Buy, 10100, 5);
  book.clear_trades();
  EXPECT_TRUE(book.trades().empty());
}

// --- Rules that must ALWAYS hold ---------------------------------------------
// Fire 20,000 random orders at the book and check, after every single one,
// that the rules of a correct book are never broken.

TEST(Matching, RandomOrdersNeverBreakTheRules) {
  OrderBook book;
  std::mt19937 rng(42);  // fixed seed: same "random" run every time
  std::uniform_int_distribution<int> action(0, 9), price(9950, 10050),
      qty(1, 20);
  std::vector<OrderId> ids;
  std::uint64_t added = 0, traded = 0, cancelled = 0, dropped = 0;

  for (OrderId id = 1; id <= 20000; ++id) {
    const int a = action(rng);
    const Side side = (a % 2 == 0) ? Side::Buy : Side::Sell;
    const auto q = static_cast<Quantity>(qty(rng));
    book.clear_trades();

    if (a < 6) {  // 60%: limit order
      ASSERT_EQ(book.add(id, side, price(rng), q), Status::Ok);
      added += q;
      ids.push_back(id);
    } else if (a < 9 && !ids.empty()) {  // 30%: cancel a random earlier id
      const OrderId victim = ids[static_cast<std::size_t>(rng()) % ids.size()];
      if (const Order* o = book.find(victim)) {
        cancelled += o->qty;
        ASSERT_EQ(book.cancel(victim), Status::Ok);
      }
    } else {  // 10%: market order
      ASSERT_EQ(book.market(id, side, q), Status::Ok);
      std::uint64_t filled = 0;
      for (const Trade& t : book.trades()) filled += t.qty;
      dropped += q - filled;
      added += q;
    }
    for (const Trade& t : book.trades()) traded += 2 * t.qty;  // both sides

    // Rule 1: the book is never crossed (a buyer never waits above a seller).
    if (book.best_bid() && book.best_ask()) {
      ASSERT_LT(*book.best_bid(), *book.best_ask()) << "after order " << id;
    }
    // Rule 2: every trade is for a positive quantity.
    for (const Trade& t : book.trades()) ASSERT_GT(t.qty, 0u);
  }

  // Rule 3: no shares appear or vanish. Everything that came in either
  // traded, was cancelled, was dropped (market leftover), or is still resting.
  std::uint64_t resting = 0;
  for (Side s : {Side::Buy, Side::Sell}) {
    for (const LevelInfo& l : book.depth(s, 1'000'000)) resting += l.total_qty;
  }
  EXPECT_EQ(added, traded + cancelled + dropped + resting);
}
