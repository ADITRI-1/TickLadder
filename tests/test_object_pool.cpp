#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "lob/object_pool.hpp"
#include "lob/types.hpp"

using namespace lob;

TEST(ObjectPool, ReservesUpFront) {
  ObjectPool<Order, 64> pool(100);
  EXPECT_GE(pool.capacity(), 100u);  // rounded up to whole chunks: 128
  EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPool, ReleasedSlotIsReused) {
  ObjectPool<Order, 64> pool(64);
  Order* a = pool.acquire();
  pool.release(a);
  EXPECT_EQ(pool.acquire(), a);  // same memory comes back: no new allocation
}

TEST(ObjectPool, ReusedSlotIsClean) {
  ObjectPool<Order, 64> pool(64);
  Order* a = pool.acquire();
  a->id = 99;
  a->qty = 7;
  a->next = a;  // pretend it was linked into a queue
  pool.release(a);
  Order* b = pool.acquire();
  EXPECT_EQ(b->id, 0u);       // old data wiped
  EXPECT_EQ(b->qty, 0u);
  EXPECT_EQ(b->next, nullptr);  // no stale pointers
}

TEST(ObjectPool, GrowsWithoutMovingExistingObjects) {
  ObjectPool<Order, 8> pool(8);
  std::vector<Order*> held;
  for (int i = 0; i < 8; ++i) {
    held.push_back(pool.acquire());
    held.back()->id = static_cast<OrderId>(i);
  }
  for (int i = 0; i < 100; ++i) held.push_back(pool.acquire());  // forces growth
  EXPECT_GE(pool.capacity(), 108u);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(held[static_cast<std::size_t>(i)]->id, static_cast<OrderId>(i));
  }
  // Every object handed out is a different slot.
  EXPECT_EQ(std::set<Order*>(held.begin(), held.end()).size(), held.size());
  EXPECT_EQ(pool.in_use(), 108u);
}
