#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "lob/types.hpp"

using namespace lob;

namespace {

// Helper: print anything with operator<< into a string.
template <typename T>
std::string str(const T& x) {
  std::ostringstream ss;
  ss << x;
  return ss.str();
}

Order make(OrderId id, Quantity qty) {
  Order o;
  o.id = id;
  o.price = 10000;
  o.qty = qty;
  return o;
}

}  // namespace

TEST(Order, FitsInOneCacheLine) {
  EXPECT_LE(sizeof(Order), 64u);
}

TEST(PriceLevel, StartsEmpty) {
  PriceLevel lvl;
  EXPECT_TRUE(lvl.empty());
  EXPECT_EQ(lvl.count, 0u);
  EXPECT_EQ(lvl.total_qty, 0u);
}

TEST(PriceLevel, PushBackKeepsArrivalOrder) {
  PriceLevel lvl;
  Order a = make(1, 10), b = make(2, 20), c = make(3, 30);
  lvl.push_back(&a);
  lvl.push_back(&b);
  lvl.push_back(&c);

  EXPECT_EQ(lvl.head, &a);  // oldest at the front
  EXPECT_EQ(lvl.tail, &c);  // newest at the back
  EXPECT_EQ(lvl.count, 3u);
  EXPECT_EQ(lvl.total_qty, 60u);
  EXPECT_EQ(a.level, &lvl);
  EXPECT_EQ(str(lvl), "Level{price=0, total=60, count=3, queue=[1 -> 2 -> 3]}");
}

TEST(PriceLevel, RemoveFromMiddle) {
  PriceLevel lvl;
  Order a = make(1, 10), b = make(2, 20), c = make(3, 30);
  lvl.push_back(&a);
  lvl.push_back(&b);
  lvl.push_back(&c);

  lvl.remove(&b);

  EXPECT_EQ(a.next, &c);  // neighbours now point at each other
  EXPECT_EQ(c.prev, &a);
  EXPECT_EQ(lvl.count, 2u);
  EXPECT_EQ(lvl.total_qty, 40u);
  EXPECT_EQ(b.prev, nullptr);  // removed order is fully unhooked
  EXPECT_EQ(b.next, nullptr);
  EXPECT_EQ(b.level, nullptr);
}

TEST(PriceLevel, RemoveHeadAndTail) {
  PriceLevel lvl;
  Order a = make(1, 10), b = make(2, 20), c = make(3, 30);
  lvl.push_back(&a);
  lvl.push_back(&b);
  lvl.push_back(&c);

  lvl.remove(&a);
  EXPECT_EQ(lvl.head, &b);
  EXPECT_EQ(b.prev, nullptr);

  lvl.remove(&c);
  EXPECT_EQ(lvl.tail, &b);
  EXPECT_EQ(b.next, nullptr);
  EXPECT_EQ(str(lvl), "Level{price=0, total=20, count=1, queue=[2]}");
}

TEST(PriceLevel, RemoveOnlyOrderLeavesEmptyLevel) {
  PriceLevel lvl;
  Order a = make(1, 10);
  lvl.push_back(&a);
  lvl.remove(&a);

  EXPECT_TRUE(lvl.empty());
  EXPECT_EQ(lvl.tail, nullptr);
  EXPECT_EQ(lvl.total_qty, 0u);
}

TEST(PriceLevel, CanRejoinAfterRemove) {
  PriceLevel lvl;
  Order a = make(1, 10), b = make(2, 20);
  lvl.push_back(&a);
  lvl.push_back(&b);
  lvl.remove(&a);
  lvl.push_back(&a);  // a goes to the BACK: it loses its time priority
  EXPECT_EQ(str(lvl), "Level{price=0, total=30, count=2, queue=[2 -> 1]}");
}

TEST(Printing, OrderAndTrade) {
  Order o = make(7, 5);
  o.side = Side::Sell;
  EXPECT_EQ(str(o), "Order{id=7, SELL, price=10000, qty=5}");

  Trade t{1, 2, 10005, 3};
  EXPECT_EQ(str(t), "Trade{buy=1, sell=2, price=10005, qty=3}");
}
