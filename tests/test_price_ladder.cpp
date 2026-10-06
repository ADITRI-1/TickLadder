#include <gtest/gtest.h>

#include <random>
#include <set>
#include <vector>

#include "lob/price_ladder.hpp"

using namespace lob;

TEST(PriceLadder, EmptyHasNoBest) {
  PriceLadder<Side::Buy> bids(1, 1000);
  EXPECT_EQ(bids.best(), nullptr);
  EXPECT_EQ(bids.size(), 0u);
  EXPECT_EQ(bids.find(500), nullptr);
}

TEST(PriceLadder, BidsBestIsHighestAsksBestIsLowest) {
  PriceLadder<Side::Buy> bids(1, 1000);
  PriceLadder<Side::Sell> asks(1, 1000);
  for (Price p : {300, 700, 500}) {
    bids.level_for(p);
    asks.level_for(p);
  }
  EXPECT_EQ(bids.best()->price, 700);
  EXPECT_EQ(asks.best()->price, 300);
  EXPECT_EQ(bids.size(), 3u);
}

TEST(PriceLadder, RemovingBestFindsNextBestAcrossManyWords) {
  // Levels far apart: the scan must jump over many empty 64-bit words.
  PriceLadder<Side::Buy> bids(1, 100'000);
  bids.level_for(5);
  bids.level_for(64);       // word boundary
  bids.level_for(99'999);
  bids.remove_level(*bids.best());
  EXPECT_EQ(bids.best()->price, 64);
  bids.remove_level(*bids.best());
  EXPECT_EQ(bids.best()->price, 5);
  bids.remove_level(*bids.best());
  EXPECT_EQ(bids.best(), nullptr);
}

TEST(PriceLadder, BandEdgesAreUsable) {
  PriceLadder<Side::Sell> asks(100, 163);  // exactly one 64-bit word
  EXPECT_TRUE(asks.in_band(100));
  EXPECT_TRUE(asks.in_band(163));
  EXPECT_FALSE(asks.in_band(99));
  EXPECT_FALSE(asks.in_band(164));
  asks.level_for(163);
  asks.level_for(100);
  EXPECT_EQ(asks.best()->price, 100);
  asks.remove_level(*asks.best());
  EXPECT_EQ(asks.best()->price, 163);
}

TEST(PriceLadder, NextWorseWalksBestToWorst) {
  PriceLadder<Side::Sell> asks(1, 1000);
  for (Price p : {900, 10, 450, 11}) asks.level_for(p);
  std::vector<Price> seen;
  for (const PriceLevel* l = asks.best(); l; l = asks.next_worse(*l)) {
    seen.push_back(l->price);
  }
  EXPECT_EQ(seen, (std::vector<Price>{10, 11, 450, 900}));
}

// Random switch-on/switch-off, checked against a std::set after every step.
template <Side S>
void random_against_set(std::uint32_t seed) {
  PriceLadder<S> ladder(1, 5000);
  std::set<Price> ref;
  std::mt19937 rng(seed);
  for (int step = 0; step < 100'000; ++step) {
    const Price p = 1 + static_cast<Price>(rng() % 5000);
    if (ref.count(p)) {
      ladder.remove_level(const_cast<PriceLevel&>(*ladder.find(p)));
      ref.erase(p);
    } else {
      ladder.level_for(p);
      ref.insert(p);
    }
    ASSERT_EQ(ladder.size(), ref.size());
    if (ref.empty()) {
      ASSERT_EQ(ladder.best(), nullptr);
    } else {
      const Price expected = S == Side::Buy ? *ref.rbegin() : *ref.begin();
      ASSERT_EQ(ladder.best()->price, expected) << "step " << step;
    }
  }
}

TEST(PriceLadder, RandomBidsAgreeWithStdSet) { random_against_set<Side::Buy>(1); }
TEST(PriceLadder, RandomAsksAgreeWithStdSet) { random_against_set<Side::Sell>(2); }
