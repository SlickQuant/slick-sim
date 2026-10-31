#include <gtest/gtest.h>
#include <utils/decimal.hpp>

using namespace slick::sim;
using namespace slick::sim::utils;

TEST(DecimalTest, Decimal9ConvertsToTheFixedPointScale) {
    int64_t out = 0;
    ASSERT_TRUE(from_decimal9(75'230'000'000, out));   // 75.23
    EXPECT_EQ(out, to_price_t(75.23));
    ASSERT_TRUE(from_decimal9(-1'500'000'000, out));
    EXPECT_EQ(out, -150'000'000);
    EXPECT_EQ(to_decimal9(to_price_t(75.23)), 75'230'000'000);
}

// The 1e-8 scale cannot hold a digit at 1e-9. Rounding it would trade at a price
// the client never sent, so the conversion fails.
TEST(DecimalTest, ADigitBelowTheScaleIsRefused) {
    int64_t out = 42;
    EXPECT_FALSE(from_decimal9(75'230'000'001, out));
    EXPECT_FALSE(from_decimal9(-1, out));
    EXPECT_EQ(out, 42);
}
