#include <gtest/gtest.h>
#include <utils/math.hpp>
#include <cstdint>
#include <limits>

using slick::sim::utils::mul_div_floor;

TEST(MulDivFloorTest, SmallValues) {
    EXPECT_EQ(mul_div_floor(10, 1, 3), 3u);
    EXPECT_EQ(mul_div_floor(10, 2, 3), 6u);
    EXPECT_EQ(mul_div_floor(0, 5, 7), 0u);
    EXPECT_EQ(mul_div_floor(7, 0, 7), 0u);
    EXPECT_EQ(mul_div_floor(7, 7, 7), 7u);
}

// The product overflows 64 bits; the quotient does not.
TEST(MulDivFloorTest, ProductAbove64Bits) {
    constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ(mul_div_floor(max, max, max), max);
    EXPECT_EQ(mul_div_floor(max, max - 1, max), max - 1);
    EXPECT_EQ(mul_div_floor(max, 1, 2), max / 2);
    // 2e17 * 1e17 / 4e17: fixed-point lots at 1e8 scale.
    EXPECT_EQ(mul_div_floor(200'000'000'000'000'000ull, 100'000'000'000'000'000ull,
                            400'000'000'000'000'000ull),
              50'000'000'000'000'000ull);
    // Floors rather than rounds: (2^63 + 1) * 3 / 4 = 3 * 2^61 + 0.75.
    EXPECT_EQ(mul_div_floor((1ull << 63) + 1, 3, 4), 3ull << 61);
}
