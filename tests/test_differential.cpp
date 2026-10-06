// Differential testing: run the SAME random orders through the fast
// OrderBook and the simple ReferenceBook, and demand that they agree on
// every status, every trade and the whole book after every single order.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "lob/order_book.hpp"
#include "reference_book.hpp"

using namespace lob;

namespace {

// Compare everything we can see from the outside.
void expect_same_book(const OrderBook& fast, const test::ReferenceBook& ref,
                      OrderId after) {
  ASSERT_EQ(fast.best_bid(), ref.best(Side::Buy)) << "after order " << after;
  ASSERT_EQ(fast.best_ask(), ref.best(Side::Sell)) << "after order " << after;
  ASSERT_EQ(fast.depth(Side::Buy, 1'000'000), ref.depth(Side::Buy))
      << "after order " << after;
  ASSERT_EQ(fast.depth(Side::Sell, 1'000'000), ref.depth(Side::Sell))
      << "after order " << after;
  ASSERT_EQ(fast.order_count(), ref.order_count()) << "after order " << after;
}

// One random session. Narrow price band and small sizes on purpose: that
// makes prices collide and orders cross very often, which is where bugs live.
void run_session(std::uint32_t seed, int num_orders) {
  OrderBook fast;
  test::ReferenceBook ref;
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> action(0, 99), price(9990, 10010),
      qty(1, 15);
  std::vector<OrderId> ids;

  for (OrderId id = 1; id <= static_cast<OrderId>(num_orders); ++id) {
    const int a = action(rng);
    const Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
    const auto q = static_cast<Quantity>(qty(rng));

    Status s_fast{}, s_ref{};
    if (a < 55) {  // 55% limit orders
      const Price p = price(rng);
      s_fast = fast.add(id, side, p, q);
      s_ref = ref.add(id, side, p, q);
      ids.push_back(id);
    } else if (a < 85 && !ids.empty()) {  // 30% cancels (some already gone)
      const OrderId victim = ids[rng() % ids.size()];
      s_fast = fast.cancel(victim);
      s_ref = ref.cancel(victim);
    } else if (a < 95) {  // 10% market orders
      s_fast = fast.market(id, side, q);
      s_ref = ref.market(id, side, q);
    } else {  // 5% deliberately bad orders: duplicates, zero qty, bad price
      const OrderId dup = ids.empty() ? id : ids[rng() % ids.size()];
      if (a % 4 == 0) {  // market order reusing an id (may still be resting)
        s_fast = fast.market(dup, side, q);
        s_ref = ref.market(dup, side, q);
      } else {
        s_fast = fast.add(dup, side, (a % 2) ? 0 : 10000, (a % 3) ? q : 0);
        s_ref = ref.add(dup, side, (a % 2) ? 0 : 10000, (a % 3) ? q : 0);
      }
    }

    ASSERT_EQ(s_fast, s_ref) << "status differs at order " << id;
    ASSERT_EQ(fast.trades(), ref.trades()) << "trades differ at order " << id;
    fast.clear_trades();
    ref.clear_trades();
    expect_same_book(fast, ref, id);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

}  // namespace

TEST(Differential, AgreesWithReferenceBookSeed1) { run_session(1, 20000); }
TEST(Differential, AgreesWithReferenceBookSeed2) { run_session(2, 20000); }
TEST(Differential, AgreesWithReferenceBookSeed3) { run_session(3, 20000); }
TEST(Differential, AgreesWithReferenceBookSeed4) { run_session(4, 20000); }
