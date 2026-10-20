#include <gtest/gtest.h>
#include <utils/timestamp.hpp>
#include <chrono>
#include <cstdint>

using slick::sim::utils::format_timestamp_iso8601;

// ---------------------------------------------------------------------------
// format_timestamp_iso8601 used to convert its system_clock time_point to
// utc_clock before formatting. Apple's libc++ has neither utc_clock nor
// clock_cast, so the header broke every macOS build. system_clock is Unix time,
// which is UTC already, so the conversion is gone and the time_point formats
// directly. These tests pin the output so the strings stay byte-identical to
// what the utc_clock path produced.
// ---------------------------------------------------------------------------

namespace {
// 2023-11-14T22:13:20Z
constexpr uint64_t BASE_S = 1'700'000'000ULL;
}

TEST(TimestampTest, FormatsMillisecondInputAtEachPrecision) {
    const uint64_t ms = BASE_S * 1'000 + 123;
    EXPECT_EQ(format_timestamp_iso8601(ms, 0), "2023-11-14T22:13:20Z");
    EXPECT_EQ(format_timestamp_iso8601(ms, 3), "2023-11-14T22:13:20.123Z");
    EXPECT_EQ(format_timestamp_iso8601(ms, 6), "2023-11-14T22:13:20.123000Z");
    EXPECT_EQ(format_timestamp_iso8601(ms, 9), "2023-11-14T22:13:20.123000000Z");
}

TEST(TimestampTest, FormatsMicrosecondInput) {
    const uint64_t us = BASE_S * 1'000'000 + 123'456;
    EXPECT_EQ(format_timestamp_iso8601(us, 3), "2023-11-14T22:13:20.123Z");
    EXPECT_EQ(format_timestamp_iso8601(us, 6), "2023-11-14T22:13:20.123456Z");
}

TEST(TimestampTest, FormatsNanosecondInput) {
    // Only up to microseconds: macOS system_clock ticks in microseconds, so finer
    // digits differ by platform.
    const uint64_t ns = BASE_S * 1'000'000'000 + 123'456'789;
    EXPECT_EQ(format_timestamp_iso8601(ns, 3), "2023-11-14T22:13:20.123Z");
    EXPECT_EQ(format_timestamp_iso8601(ns, 6), "2023-11-14T22:13:20.123456Z");
}

TEST(TimestampTest, FormatsTimePointDirectly) {
    const std::chrono::system_clock::time_point tp{std::chrono::seconds{BASE_S}};
    EXPECT_EQ(format_timestamp_iso8601(tp, 3), "2023-11-14T22:13:20.000Z");
}

TEST(TimestampTest, NoLeapSecondOffsetAcrossLeapSecondBoundary) {
    // A leap second was inserted at the end of 2016-12-31. The output must still be
    // plain Unix-time UTC on both sides of it, with no 27 s utc_clock offset leaking in.
    constexpr uint64_t NEW_YEAR_2017_MS = 1'483'228'800'000ULL;
    EXPECT_EQ(format_timestamp_iso8601(NEW_YEAR_2017_MS - 1, 3), "2016-12-31T23:59:59.999Z");
    EXPECT_EQ(format_timestamp_iso8601(NEW_YEAR_2017_MS, 3), "2017-01-01T00:00:00.000Z");
}
