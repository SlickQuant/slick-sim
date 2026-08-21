#include <gtest/gtest.h>
#include <common/types.hpp>
#include <limits>

using namespace slick::sim;

class TypesTest : public ::testing::Test {
protected:
    static constexpr double kEpsilon = 1e-6;
};

TEST_F(TypesTest, PriceConversion_ToAndFrom) {
    double original = 123.45;
    price_t converted = to_price_t(original);
    double back = to_price_double(converted);

    EXPECT_NEAR(original, back, kEpsilon);
}

TEST_F(TypesTest, PriceConversion_Precision) {
    // Test that we preserve 8 decimal places
    double original = 123.456789012;
    price_t converted = to_price_t(original);
    double back = to_price_double(converted);

    // Should preserve 8 decimals (DOUBLE_MULTIPLIER = 1e8)
    EXPECT_NEAR(123.45678901, back, kEpsilon);
}

TEST_F(TypesTest, QuantityConversion_ToAndFrom) {
    double original = 987.65;
    qty_t converted = to_qty_t(original);
    double back = to_qty_double(converted);

    EXPECT_NEAR(original, back, kEpsilon);
}

TEST_F(TypesTest, QuantityConversion_Precision) {
    // Test 8 decimal place precision
    double original = 987.654321098;
    qty_t converted = to_qty_t(original);
    double back = to_qty_double(converted);

    EXPECT_NEAR(987.65432109, back, kEpsilon);
}

TEST_F(TypesTest, NullPrice_MaxValue) {
    EXPECT_EQ(NULL_PRICE, std::numeric_limits<price_t>::max());
}

TEST_F(TypesTest, VenueConversion_FromString_Coinbase) {
    EXPECT_EQ(to_venue("COINBASE"), Venue::COINBASE);
    EXPECT_EQ(to_venue("coinbase"), Venue::COINBASE);
}

TEST_F(TypesTest, VenueConversion_FromString_CME) {
    EXPECT_EQ(to_venue("CME"), Venue::CME);
    EXPECT_EQ(to_venue("cme"), Venue::CME);
}

TEST_F(TypesTest, VenueConversion_FromString_ICE) {
    EXPECT_EQ(to_venue("ICE"), Venue::ICE);
    EXPECT_EQ(to_venue("ice"), Venue::ICE);
}

TEST_F(TypesTest, VenueConversion_FromString_Stock) {
    EXPECT_EQ(to_venue("STOCK"), Venue::STOCK);
    EXPECT_EQ(to_venue("stock"), Venue::STOCK);
}

TEST_F(TypesTest, VenueConversion_FromString_Unknown) {
    EXPECT_EQ(to_venue("UNKNOWN"), Venue::UNKNOWN_VENUE);
    EXPECT_EQ(to_venue("invalid"), Venue::UNKNOWN_VENUE);
}

// The hand-written chain this replaced compared against the exact all-upper and
// exact all-lower spelling only, so a mixed-case config key silently resolved to
// UNKNOWN_VENUE - and an unknown venue is not diagnosed anywhere near the config.
TEST_F(TypesTest, VenueConversion_FromString_IsCaseInsensitive) {
    EXPECT_EQ(to_venue("CoinBase"), Venue::COINBASE);
    EXPECT_EQ(to_venue("HyperLiquid"), Venue::HYPERLIQUID);
    EXPECT_EQ(to_venue("Cme"), Venue::CME);
}

// A prefix or a suffix must not match: iequals compares lengths first, but that
// is exactly the kind of thing a later "optimisation" drops.
TEST_F(TypesTest, VenueConversion_FromString_RejectsPartialMatches) {
    EXPECT_EQ(to_venue("coinbaseX"), Venue::UNKNOWN_VENUE);
    EXPECT_EQ(to_venue("coinbas"), Venue::UNKNOWN_VENUE);
    EXPECT_EQ(to_venue(""), Venue::UNKNOWN_VENUE);
}

TEST_F(TypesTest, VenueConversion_HyperliquidRoundTrips) {
    EXPECT_EQ(to_venue("HYPERLIQUID"), Venue::HYPERLIQUID);
    EXPECT_EQ(to_venue("hyperliquid"), Venue::HYPERLIQUID);
    EXPECT_STREQ(to_string(Venue::HYPERLIQUID), "HYPERLIQUID");
}

// Venue is a wire field: it is written into every MarketDataUpdate frame and into
// the shared-memory md queue, so these numbers are a compatibility contract with
// any process reading a queue or a capture file. The X-macro list must be
// append-only - reordering it renumbers every venue after the insertion point.
TEST_F(TypesTest, VenueEnumeratorsAreStable) {
    EXPECT_EQ(static_cast<uint8_t>(Venue::UNKNOWN_VENUE), 0);
    EXPECT_EQ(static_cast<uint8_t>(Venue::CME), 1);
    EXPECT_EQ(static_cast<uint8_t>(Venue::ICE), 2);
    EXPECT_EQ(static_cast<uint8_t>(Venue::COINBASE), 3);
    EXPECT_EQ(static_cast<uint8_t>(Venue::HYPERLIQUID), 4);
    EXPECT_EQ(static_cast<uint8_t>(Venue::STOCK), 5);
    EXPECT_EQ(static_cast<uint8_t>(Venue::__COUNT__), 6);
}

TEST_F(TypesTest, VenueConversion_ToString_AllVenues) {
    EXPECT_STREQ(to_string(Venue::COINBASE), "COINBASE");
    EXPECT_STREQ(to_string(Venue::CME), "CME");
    EXPECT_STREQ(to_string(Venue::ICE), "ICE");
    EXPECT_STREQ(to_string(Venue::STOCK), "STOCK");
    EXPECT_STREQ(to_string(Venue::UNKNOWN_VENUE), "UNKNOWN");
}

TEST_F(TypesTest, PriceConversion_Zero) {
    price_t zero = to_price_t(0.0);
    EXPECT_EQ(zero, 0);
    EXPECT_DOUBLE_EQ(to_price_double(zero), 0.0);
}

TEST_F(TypesTest, PriceConversion_Negative) {
    // Prices can be negative in some contexts (spreads, etc.)
    double negative = -50.25;
    price_t converted = to_price_t(negative);
    double back = to_price_double(converted);

    EXPECT_NEAR(negative, back, kEpsilon);
}

TEST_F(TypesTest, QuantityConversion_Large) {
    // Test large quantity values
    double large_qty = 999999.99;
    qty_t converted = to_qty_t(large_qty);
    double back = to_qty_double(converted);

    EXPECT_NEAR(large_qty, back, kEpsilon);
}

TEST_F(TypesTest, PriceConversion_RoundTrip_Multiple) {
    // Test multiple round trips
    std::vector<double> test_values = {0.0, 1.0, 100.5, 12345.678, 0.00000001};

    for (double val : test_values) {
        price_t converted = to_price_t(val);
        double back = to_price_double(converted);
        EXPECT_NEAR(val, back, kEpsilon) << "Failed for value: " << val;
    }
}

TEST_F(TypesTest, QuantityConversion_RoundTrip_Multiple) {
    // Test multiple round trips for quantities
    std::vector<double> test_values = {0.0, 1.0, 100.5, 12345.678, 999999.999};

    for (double val : test_values) {
        qty_t converted = to_qty_t(val);
        double back = to_qty_double(converted);
        EXPECT_NEAR(val, back, kEpsilon) << "Failed for value: " << val;
    }
}
