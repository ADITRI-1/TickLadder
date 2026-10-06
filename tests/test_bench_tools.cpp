// The benchmark is only trustworthy if its tools are correct, so they get
// tests too: the percentile maths and the order flow generator.

#include <gtest/gtest.h>

#include <numeric>
#include <vector>

#include "latency_stats.hpp"
#include "order_flow.hpp"

using namespace lob;
using namespace lob::bench;

TEST(LatencyStats, PercentilesOfOneToHundred) {
  std::vector<std::uint64_t> v(100);
  std::iota(v.begin(), v.end(), 1);  // 1, 2, ..., 100
  EXPECT_EQ(percentile(v, 0.50), 50u);
  EXPECT_EQ(percentile(v, 0.99), 99u);
  EXPECT_EQ(percentile(v, 1.00), 100u);
  EXPECT_EQ(percentile(v, 0.0), 1u);  // never reads before the start
}

TEST(LatencyStats, SummarizeSortsAndConvertsToNanoseconds) {
  std::vector<std::uint64_t> ticks{40, 10, 30, 20};  // unsorted on purpose
  const LatencySummary s = summarize(ticks, 2.0);   // 2 ticks per ns
  EXPECT_EQ(s.count, 4u);
  EXPECT_DOUBLE_EQ(s.p50, 10.0);  // 2nd smallest = 20 ticks = 10 ns
  EXPECT_DOUBLE_EQ(s.max, 20.0);
  EXPECT_DOUBLE_EQ(s.mean, 12.5);
}

TEST(OrderFlow, SameSeedGivesSameMessages) {
  FlowConfig cfg;
  cfg.num_msgs = 20'000;
  cfg.target_orders = 500;
  const Flow a = generate_flow(cfg);
  const Flow b = generate_flow(cfg);
  ASSERT_EQ(a.msgs.size(), b.msgs.size());
  for (std::size_t i = 0; i < a.msgs.size(); ++i) {
    ASSERT_EQ(a.msgs[i].id, b.msgs[i].id);
    ASSERT_EQ(a.msgs[i].price, b.msgs[i].price);
  }
}

// The generator promises: passive adds never trade, cancels always hit a
// live order, aggressive messages do trade. Check it really keeps them.
TEST(OrderFlow, MessagesDoWhatTheirTypeSays) {
  FlowConfig cfg;
  cfg.num_msgs = 50'000;
  cfg.target_orders = 1'000;
  const Flow flow = generate_flow(cfg);

  OrderBook book;
  for (const Msg& m : flow.prefill) apply(book, m);
  EXPECT_TRUE(book.trades().empty());  // prefill is all passive

  std::size_t aggressive = 0, aggressive_traded = 0;
  for (const Msg& m : flow.msgs) {
    book.clear_trades();
    if (m.type == MsgType::Cancel) {
      ASSERT_EQ(book.cancel(m.id), Status::Ok);
      continue;
    }
    apply(book, m);
    if (m.type == MsgType::Add) {
      ASSERT_TRUE(book.trades().empty()) << "passive add traded, id " << m.id;
    } else {
      ++aggressive;
      if (!book.trades().empty()) ++aggressive_traded;
    }
  }
  EXPECT_GT(aggressive, 0u);
  EXPECT_EQ(aggressive_traded, aggressive);  // every aggressive order traded
  // The book stays near its target size instead of growing forever.
  EXPECT_GT(book.order_count(), 800u);
  EXPECT_LT(book.order_count(), 1200u);
}
