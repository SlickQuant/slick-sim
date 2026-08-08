#include <gtest/gtest.h>
#include <market_data_publisher/hyperliquid_l2_diff.hpp>
#include <hyperliquid/utils/l2_diff.hpp>
#include <common/types.hpp>

using namespace slick::sim;
using namespace slick::sim::md_publisher;
using json = nlohmann::json;

namespace {
std::string px(double p) { return std::to_string(to_price_double(to_price_t(p))); }
std::string sz(double q) { return std::to_string(to_qty_double(to_qty_t(q))); }
}

// ===========================================================================
// compute_l2_diff — pure diff-computation logic
// ===========================================================================

TEST(HyperliquidL2Diff, UnchangedLevel_NotReportedInL) {
    L2Levels prev_bid = {{to_price_t(100.0), to_qty_t(10.0)}};
    L2Levels curr_bid = {{to_price_t(100.0), to_qty_t(10.0)}};
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    EXPECT_TRUE(diff.l[0].empty());
    EXPECT_TRUE(diff.r[0].empty());
}

TEST(HyperliquidL2Diff, ChangedQuantity_ReportedInL) {
    L2Levels prev_bid = {{to_price_t(99.0), to_qty_t(5.0)}};
    L2Levels curr_bid = {{to_price_t(99.0), to_qty_t(7.0)}};
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    ASSERT_EQ(diff.l[0].size(), 1u);
    EXPECT_EQ(diff.l[0][0]["p"], px(99.0));
    EXPECT_EQ(diff.l[0][0]["s"], sz(7.0));
    EXPECT_TRUE(diff.r[0].empty());
}

TEST(HyperliquidL2Diff, NewLevel_ReportedInL) {
    L2Levels prev_bid;
    L2Levels curr_bid = {{to_price_t(97.0), to_qty_t(2.0)}};
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    ASSERT_EQ(diff.l[0].size(), 1u);
    EXPECT_EQ(diff.l[0][0]["p"], px(97.0));
    EXPECT_EQ(diff.l[0][0]["s"], sz(2.0));
}

TEST(HyperliquidL2Diff, RemovedLevel_ReportedAsIndexIntoPreviousArray) {
    // prev bid array, index 0=100, 1=99, 2=98 — 98 (index 2) drops out.
    L2Levels prev_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(5.0)},
        {to_price_t(98.0),  to_qty_t(3.0)},
    };
    L2Levels curr_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(5.0)},
    };
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    EXPECT_TRUE(diff.l[0].empty());
    ASSERT_EQ(diff.r[0].size(), 1u);
    EXPECT_EQ(diff.r[0][0], 2);
}

TEST(HyperliquidL2Diff, MixedChangeAddRemove_BothSidesIndependent) {
    // Bid side: 100 unchanged, 99 changes 5->7, 98 (index 2) removed, 97 added.
    L2Levels prev_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(5.0)},
        {to_price_t(98.0),  to_qty_t(3.0)},
    };
    L2Levels curr_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(7.0)},
        {to_price_t(97.0),  to_qty_t(2.0)},
    };
    // Ask side: fully unchanged.
    L2Levels prev_ask = {{to_price_t(101.0), to_qty_t(8.0)}};
    L2Levels curr_ask = {{to_price_t(101.0), to_qty_t(8.0)}};

    auto diff = compute_l2_diff(prev_bid, prev_ask, curr_bid, curr_ask);

    ASSERT_EQ(diff.l[0].size(), 2u);
    EXPECT_EQ(diff.l[0][0]["p"], px(99.0));
    EXPECT_EQ(diff.l[0][0]["s"], sz(7.0));
    EXPECT_EQ(diff.l[0][1]["p"], px(97.0));
    EXPECT_EQ(diff.l[0][1]["s"], sz(2.0));

    ASSERT_EQ(diff.r[0].size(), 1u);
    EXPECT_EQ(diff.r[0][0], 2);

    EXPECT_TRUE(diff.l[1].empty());
    EXPECT_TRUE(diff.r[1].empty());
}

TEST(HyperliquidL2Diff, EmptyBaseline_EverythingReportedAsAdded) {
    L2Levels empty;
    L2Levels curr = {{to_price_t(100.0), to_qty_t(10.0)}, {to_price_t(99.0), to_qty_t(5.0)}};

    auto diff = compute_l2_diff(empty, empty, curr, empty);

    EXPECT_EQ(diff.l[0].size(), 2u);
    EXPECT_TRUE(diff.r[0].empty());
}

// ===========================================================================
// encode_l2_diff — byte-compatibility round-trip through the real
// hyperliquid::decode_l2_diff() (SlickQuant/hyperliquid-cpp)
// ===========================================================================

TEST(HyperliquidL2Diff, EncodeThenRealDecode_RoundTrips) {
    json payload = {
        {"c", "BTC"},
        {"l", json::array({
            json::array({json{{"p", "64420.0"}, {"s", "1.09008"}}}),
            json::array({json{{"p", "64421.0"}, {"s", "9.29528"}}})
        })},
        {"r", json::array({json::array(), json::array({0, 2})})},
        {"t", 1786043634919ull}
    };

    std::string encoded = encode_l2_diff(payload);
    ASSERT_FALSE(encoded.empty());

    json decoded = hyperliquid::decode_l2_diff(encoded);
    EXPECT_EQ(decoded, payload);
}

TEST(HyperliquidL2Diff, EncodeThenRealDecode_EmptyDiff_RoundTrips) {
    json payload = {
        {"c", "ETH"},
        {"l", json::array({json::array(), json::array()})},
        {"r", json::array({json::array(), json::array()})},
        {"t", 1000ull}
    };

    std::string encoded = encode_l2_diff(payload);
    json decoded = hyperliquid::decode_l2_diff(encoded);
    EXPECT_EQ(decoded, payload);
}
