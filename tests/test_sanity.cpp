#include <gtest/gtest.h>

TEST(Sanity, IntegerPricesAreExact) {
  // Why we store prices as integer ticks, not doubles.
  EXPECT_NE(0.1 + 0.2, 0.3);  // floating point: not equal!
  EXPECT_EQ(10 + 20, 30);     // ticks: exact
}
