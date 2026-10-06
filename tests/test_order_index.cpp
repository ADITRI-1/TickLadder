#include <gtest/gtest.h>

#include <random>
#include <unordered_map>
#include <vector>

#include "lob/order_index.hpp"

using namespace lob;

namespace {
// Fake, distinct Order addresses: the index only stores the pointers.
Order g_orders[100'000];
Order* ptr(OrderId id) { return &g_orders[id % 100'000]; }
}  // namespace

TEST(OrderIndex, InsertFindErase) {
  OrderIndex idx;
  EXPECT_EQ(idx.find(1), nullptr);
  EXPECT_TRUE(idx.insert(1, ptr(1)));
  EXPECT_EQ(idx.find(1), ptr(1));
  EXPECT_FALSE(idx.insert(1, ptr(2)));  // duplicate: rejected, unchanged
  EXPECT_EQ(idx.find(1), ptr(1));
  EXPECT_TRUE(idx.erase(1));
  EXPECT_FALSE(idx.erase(1));
  EXPECT_EQ(idx.find(1), nullptr);
  EXPECT_EQ(idx.size(), 0u);
}

TEST(OrderIndex, IdZeroIsAValidKey) {
  OrderIndex idx;
  EXPECT_TRUE(idx.insert(0, ptr(5)));
  EXPECT_EQ(idx.find(0), ptr(5));
}

TEST(OrderIndex, GrowsAndKeepsEverything) {
  OrderIndex idx;  // starts tiny: forces many rehashes
  for (OrderId id = 1; id <= 50'000; ++id) ASSERT_TRUE(idx.insert(id, ptr(id)));
  EXPECT_EQ(idx.size(), 50'000u);
  EXPECT_LE(2 * idx.size(), idx.capacity());  // never more than half full
  for (OrderId id = 1; id <= 50'000; ++id) ASSERT_EQ(idx.find(id), ptr(id));
}

TEST(OrderIndex, ReserveMeansNoGrowthLater) {
  OrderIndex idx(1000);
  const std::size_t cap = idx.capacity();
  for (OrderId id = 1; id <= 1000; ++id) idx.insert(id, ptr(id));
  EXPECT_EQ(idx.capacity(), cap);
}

// Erasing from the middle of a probe run is the tricky part. Ids that are
// multiples of a big power of two stress the hash, and a tiny table makes
// long runs, so this hits the backward-shift logic hard.
TEST(OrderIndex, EraseInsideLongProbeRuns) {
  OrderIndex idx;
  std::vector<OrderId> ids;
  for (OrderId k = 1; k <= 200; ++k) ids.push_back(k << 20);
  for (OrderId id : ids) idx.insert(id, ptr(id));
  for (std::size_t i = 0; i < ids.size(); i += 2) ASSERT_TRUE(idx.erase(ids[i]));
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i % 2 == 0) ASSERT_EQ(idx.find(ids[i]), nullptr);
    else ASSERT_EQ(idx.find(ids[i]), ptr(ids[i]));
  }
}

// The differential idea again: random inserts/erases/finds, checked against
// std::unordered_map after every single operation.
TEST(OrderIndex, AgreesWithStdUnorderedMap) {
  OrderIndex idx(64);
  std::unordered_map<OrderId, Order*> ref;
  std::mt19937_64 rng(7);
  for (int step = 0; step < 300'000; ++step) {
    const OrderId id = rng() % 5000;  // small key space: lots of collisions
    switch (rng() % 3) {
      case 0: {
        const bool ins = ref.emplace(id, ptr(id)).second;
        ASSERT_EQ(idx.insert(id, ptr(id)), ins) << "step " << step;
        break;
      }
      case 1:
        ASSERT_EQ(idx.erase(id), ref.erase(id) == 1) << "step " << step;
        break;
      default: {
        auto it = ref.find(id);
        ASSERT_EQ(idx.find(id), it == ref.end() ? nullptr : it->second)
            << "step " << step;
      }
    }
    ASSERT_EQ(idx.size(), ref.size());
  }
}
