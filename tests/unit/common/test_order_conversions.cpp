#include <gtest/gtest.h>
#include <common/order.hpp>
#include <common/types.hpp>
#include <limits>
#include <slick/object_pool.h>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;

class OrderConversionsTest : public ::testing::Test {
protected:
    static constexpr double kEpsilon = 1e-6;
};

TEST_F(OrderConversionsTest, PriceConversion_RoundTrip) {
    std::vector<double> test_prices = {0.01, 1.0, 10.0, 100.0, 1000.0, 12345.67};

    for (double original_price : test_prices) {
        price_t converted = to_price_t(original_price);
        double back = to_price_double(converted);
        EXPECT_NEAR(original_price, back, kEpsilon) << "Failed for price: " << original_price;
    }
}

TEST_F(OrderConversionsTest, QuantityConversion_RoundTrip) {
    std::vector<double> test_quantities = {0.01, 1.0, 10.0, 100.0, 1000.0, 99999.99};

    for (double original_qty : test_quantities) {
        qty_t converted = to_qty_t(original_qty);
        double back = to_qty_double(converted);
        EXPECT_NEAR(original_qty, back, kEpsilon) << "Failed for quantity: " << original_qty;
    }
}

TEST_F(OrderConversionsTest, PriceConversion_EightDecimalPrecision) {
    // DOUBLE_MULTIPLIER = 1e8, so we can preserve 8 decimal places
    double original = 123.456789012345;
    price_t converted = to_price_t(original);
    double back = to_price_double(converted);

    // Should preserve 8 decimals
    EXPECT_NEAR(123.45678901, back, kEpsilon);
}

TEST_F(OrderConversionsTest, ConversionWithLargeValues) {
    double large_price = 999999.99;
    price_t converted = to_price_t(large_price);
    double back = to_price_double(converted);

    EXPECT_NEAR(large_price, back, kEpsilon);
}

TEST_F(OrderConversionsTest, ConversionWithZero) {
    price_t zero_price = to_price_t(0.0);
    EXPECT_EQ(zero_price, 0);
    EXPECT_DOUBLE_EQ(to_price_double(zero_price), 0.0);

    qty_t zero_qty = to_qty_t(0.0);
    EXPECT_EQ(zero_qty, 0);
    EXPECT_DOUBLE_EQ(to_qty_double(zero_qty), 0.0);
}

TEST_F(OrderConversionsTest, NegativePriceConversion) {
    // Negative prices can occur in spreads or other contexts
    double negative = -50.25;
    price_t converted = to_price_t(negative);
    double back = to_price_double(converted);

    EXPECT_NEAR(negative, back, kEpsilon);
}

TEST_F(OrderConversionsTest, VerySmallValues) {
    double small_price = 0.00000001;
    price_t converted = to_price_t(small_price);
    double back = to_price_double(converted);

    EXPECT_NEAR(small_price, back, kEpsilon);
}

TEST_F(OrderConversionsTest, PriceHelper_DoubleMultiplier) {
    // Verify DOUBLE_MULTIPLIER is 1e8
    EXPECT_EQ(DOUBLE_MULTIPLIER, 100000000);

    // Test that conversion uses the correct multiplier
    double test_value = 1.0;
    price_t converted = to_price_t(test_value);
    EXPECT_EQ(converted, 100000000);
}

TEST_F(OrderConversionsTest, QuantityHelper_DoubleMultiplier) {
    double test_value = 1.0;
    qty_t converted = to_qty_t(test_value);
    EXPECT_EQ(converted, 100000000);
}

TEST_F(OrderConversionsTest, BoundaryValues_Max) {
    // Test near maximum representable values
    double large = 1000000.0;
    price_t converted = to_price_t(large);
    double back = to_price_double(converted);

    EXPECT_NEAR(large, back, 0.1); // Slightly larger epsilon for very large values
}

TEST_F(OrderConversionsTest, PrecisionLoss_VeryLargeNumbers) {
    // Test that precision is maintained even with large numbers
    double large = 123456.78901234;
    price_t converted = to_price_t(large);
    double back = to_price_double(converted);

    // Should maintain 8 decimal precision
    EXPECT_NEAR(123456.78901234, back, kEpsilon);
}

// ===========================================================================
// Fixed-point rounding and exact rendering
// ===========================================================================

// `price * 1e8` is a binary floating-point multiply, and most decimal fractions
// have no exact binary form, so the product lands just under the intended integer.
// Truncating there cost a whole tick on very ordinary values.
TEST(FixedPointTest, ScalingRoundsToNearestTickRatherThanTruncating) {
    EXPECT_EQ(to_price_t(8.2), 820000000);      // 8.2 * 1e8 == 819999999.99999988
    EXPECT_EQ(to_price_t(0.29), 29000000);      // 28999999.999999996
    EXPECT_EQ(to_price_t(4.35), 435000000);     // 434999999.99999994
    EXPECT_EQ(to_price_t(0.07), 7000000);
    EXPECT_EQ(to_price_t(123.456), 12345600000);

    EXPECT_EQ(to_qty_t(8.2), 820000000);
    EXPECT_EQ(to_qty_t(0.29), 29000000);
}

TEST(FixedPointTest, ScalingRoundsNegativesAwayFromZero) {
    EXPECT_EQ(to_price_t(-8.2), -820000000);
    EXPECT_EQ(to_price_t(-0.29), -29000000);
}

TEST(FixedPointTest, ScalingRoundTripsThroughTheDoubleForm) {
    for (double v : {8.2, 0.29, 4.35, 0.07, 60455.46, 0.00011628, 3000.5}) {
        EXPECT_DOUBLE_EQ(to_price_double(to_price_t(v)), v) << "value " << v;
    }
}

// std::to_string(double) formats with %f - six fractional digits - but the scale
// carries eight. A size of 0.00011628 went out as "0.000116", and a single tick as
// "0.000000", which a receiver reads as zero.
TEST(FixedPointTest, RenderingKeepsAllEightFractionalDigits) {
    EXPECT_EQ(to_qty_string(to_qty_t(0.00011628)), "0.00011628");
    EXPECT_EQ(to_price_string(1), "0.00000001");
    EXPECT_EQ(to_price_string(to_price_t(0.12345678)), "0.12345678");
}

TEST(FixedPointTest, RenderingTrimsTrailingZerosAndWholeNumbers) {
    EXPECT_EQ(to_price_string(to_price_t(99.0)), "99");
    EXPECT_EQ(to_price_string(to_price_t(3000.5)), "3000.5");
    EXPECT_EQ(to_price_string(to_price_t(60455.46)), "60455.46");
    EXPECT_EQ(to_price_string(0), "0");
}

TEST(FixedPointTest, RenderingHandlesNegativesAndTheDomainEdge) {
    EXPECT_EQ(to_price_string(to_price_t(-8.2)), "-8.2");
    EXPECT_EQ(to_price_string(-1), "-0.00000001");
    // Negating through the unsigned domain keeps the most negative value intact.
    EXPECT_EQ(to_fixed_string(std::numeric_limits<int_fast64_t>::min()).front(), '-');
}
