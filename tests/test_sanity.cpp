#include <gtest/gtest.h>

#include <string_view>

#include "lob/version.hpp"

// Proves the whole chain works: our header is found, our library links,
// and GoogleTest runs.
TEST(Sanity, LibraryLinks) {
  EXPECT_EQ(std::string_view{lob::version()}, "0.1.0");
}

TEST(Sanity, IntegerPricesAreExact) {
  // Why we will store prices as integer ticks, not doubles.
  EXPECT_NE(0.1 + 0.2, 0.3);            // floating point: not equal!
  EXPECT_EQ(10 + 20, 30);               // ticks: exact
}
