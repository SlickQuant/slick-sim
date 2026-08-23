#include <gtest/gtest.h>
#include <venues/hyperliquid/hyperliquid_l2_diff.hpp>
#include <venues/hyperliquid/hyperliquid_wire_format.hpp>
#include <hyperliquid/utils/l2_diff.hpp>
#include <common/types.hpp>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <unordered_set>
#include <vector>

using namespace slick::sim;
using namespace slick::sim::md_publisher;
using json = nlohmann::json;

namespace {
// Expected values are written out in full rather than recomputed with the same
// formatter the code under test uses - a helper mirroring the implementation agrees
// with it however wrong both are, which is how "0.00011628" went out as "0.000116"
// unnoticed.

// Applies a diff the way the contract reads it: every "r" index resolved
// against the previous array before anything is removed.
L2Levels applyDiffAsIndexSet(L2Levels book, const json& removed, const json& changed, bool bid);

// Applies the same diff by splicing each "r" index off as it is read, so a
// deletion shifts every entry behind it - sound only for descending indices.
L2Levels applyDiffBySplicing(L2Levels book, const json& removed, const json& changed, bool bid);

L2Levels applyChangedAndSort(L2Levels book, const json& changed, bool bid) {
    for (const auto& level : changed) {
        auto price = to_price_t(std::stod(level["p"].get<std::string>()));
        auto qty = to_qty_t(std::stod(level["s"].get<std::string>()));
        auto it = std::find_if(book.begin(), book.end(),
                               [price](const auto& lv) { return lv.first == price; });
        if (it != book.end()) {
            it->second = qty;
        } else {
            book.emplace_back(price, qty);
        }
    }
    std::sort(book.begin(), book.end(), [bid](const auto& a, const auto& b) {
        return bid ? a.first > b.first : a.first < b.first;
    });
    return book;
}

L2Levels applyDiffAsIndexSet(L2Levels book, const json& removed, const json& changed, bool bid) {
    std::unordered_set<std::size_t> indices;
    for (const auto& idx : removed) {
        indices.insert(idx.get<std::size_t>());
    }
    L2Levels kept;
    for (std::size_t i = 0; i < book.size(); ++i) {
        if (!indices.contains(i)) {
            kept.push_back(book[i]);
        }
    }
    return applyChangedAndSort(std::move(kept), changed, bid);
}

L2Levels applyDiffBySplicing(L2Levels book, const json& removed, const json& changed, bool bid) {
    for (const auto& idx : removed) {
        auto i = idx.get<std::size_t>();
        // A splice past the end is the corruption this guards against, so fail
        // loudly rather than skipping quietly.
        EXPECT_LT(i, book.size());
        if (i >= book.size()) {
            continue;
        }
        book.erase(book.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return applyChangedAndSort(std::move(book), changed, bid);
}
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
    EXPECT_EQ(diff.l[0][0]["p"], "99.0");
    EXPECT_EQ(diff.l[0][0]["s"], "7.0");
    EXPECT_TRUE(diff.r[0].empty());
}

TEST(HyperliquidL2Diff, NewLevel_ReportedInL) {
    L2Levels prev_bid;
    L2Levels curr_bid = {{to_price_t(97.0), to_qty_t(2.0)}};
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    ASSERT_EQ(diff.l[0].size(), 1u);
    EXPECT_EQ(diff.l[0][0]["p"], "97.0");
    EXPECT_EQ(diff.l[0][0]["s"], "2.0");
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

TEST(HyperliquidL2Diff, MultipleBidRemovals_IndicesDescend) {
    // Regression: the live venue emits "r" in ascending *price* order, which on
    // the bid side - whose array runs highest price first - means descending
    // index. Ascending indices desynced any client that splices the removals off
    // one at a time: deleting index 0 shifts everything behind it, so the next
    // index landed a slot too far and took a level the venue had kept, while
    // leaving one it had dropped. That stale level then rested above the best
    // ask, crossing the book the client displayed.
    L2Levels prev_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(5.0)},
        {to_price_t(98.0),  to_qty_t(3.0)},
        {to_price_t(97.0),  to_qty_t(2.0)},
    };
    // 100 (index 0) and 98 (index 2) drop out.
    L2Levels curr_bid = {
        {to_price_t(99.0), to_qty_t(5.0)},
        {to_price_t(97.0), to_qty_t(2.0)},
    };
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    ASSERT_EQ(diff.r[0].size(), 2u);
    EXPECT_EQ(diff.r[0][0], 2);
    EXPECT_EQ(diff.r[0][1], 0);
}

TEST(HyperliquidL2Diff, MultipleAskRemovals_IndicesAscend) {
    // The ask array runs lowest price first, so ascending price is ascending
    // index there - the opposite of the bid side above, and what the venue sends.
    L2Levels prev_ask = {
        {to_price_t(101.0), to_qty_t(1.0)},
        {to_price_t(102.0), to_qty_t(2.0)},
        {to_price_t(103.0), to_qty_t(3.0)},
        {to_price_t(104.0), to_qty_t(4.0)},
    };
    // 101 (index 0) and 103 (index 2) drop out.
    L2Levels curr_ask = {
        {to_price_t(102.0), to_qty_t(2.0)},
        {to_price_t(104.0), to_qty_t(4.0)},
    };
    L2Levels empty;

    auto diff = compute_l2_diff(empty, prev_ask, empty, curr_ask);

    ASSERT_EQ(diff.r[1].size(), 2u);
    EXPECT_EQ(diff.r[1][0], 0);
    EXPECT_EQ(diff.r[1][1], 2);
}

TEST(HyperliquidL2Diff, RemovalsAsIndexSet_ReconstructCurrentBook) {
    // The contract itself: "r" names positions in the *previous* array, so a
    // consumer that resolves them all before removing any lands exactly on the
    // book the diff was computed from. Held on both sides, whatever the order
    // the indices arrive in.
    L2Levels prev_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(5.0)},
        {to_price_t(98.0),  to_qty_t(3.0)},
        {to_price_t(97.0),  to_qty_t(2.0)},
        {to_price_t(96.0),  to_qty_t(1.0)},
    };
    L2Levels curr_bid = {
        {to_price_t(99.0), to_qty_t(7.0)},   // changed
        {to_price_t(97.0), to_qty_t(2.0)},   // untouched
        {to_price_t(95.0), to_qty_t(4.0)},   // added
    };
    L2Levels prev_ask = {
        {to_price_t(101.0), to_qty_t(1.0)},
        {to_price_t(102.0), to_qty_t(2.0)},
        {to_price_t(103.0), to_qty_t(3.0)},
        {to_price_t(104.0), to_qty_t(4.0)},
    };
    L2Levels curr_ask = {
        {to_price_t(102.0), to_qty_t(6.0)},  // changed
        {to_price_t(104.0), to_qty_t(4.0)},  // untouched
    };

    auto diff = compute_l2_diff(prev_bid, prev_ask, curr_bid, curr_ask);

    EXPECT_EQ(applyDiffAsIndexSet(prev_bid, diff.r[0], diff.l[0], /*bid=*/true), curr_bid);
    EXPECT_EQ(applyDiffAsIndexSet(prev_ask, diff.r[1], diff.l[1], /*bid=*/false), curr_ask);
}

TEST(HyperliquidL2Diff, BidRemovalsSurviveOneAtATimeSplicing) {
    // The client-visible reason the bid ordering matters. A consumer that
    // splices each index off as it reads it - the obvious way to walk the list -
    // is correct only while the indices descend, because then no deletion moves
    // an entry the remaining indices still refer to. That is how Hyperliquid
    // sends bid removals, so such a consumer works against the venue; emitting
    // them ascending here desynced it, and the level it failed to drop stayed
    // resting above the best ask.
    //
    // No ask-side counterpart: the venue sends those ascending, so splicing them
    // one at a time is unsound there and no client may rely on it.
    L2Levels prev_bid = {
        {to_price_t(100.0), to_qty_t(10.0)},
        {to_price_t(99.0),  to_qty_t(5.0)},
        {to_price_t(98.0),  to_qty_t(3.0)},
        {to_price_t(97.0),  to_qty_t(2.0)},
        {to_price_t(96.0),  to_qty_t(1.0)},
    };
    L2Levels curr_bid = {
        {to_price_t(99.0), to_qty_t(7.0)},
        {to_price_t(97.0), to_qty_t(2.0)},
        {to_price_t(95.0), to_qty_t(4.0)},
    };
    L2Levels empty;

    auto diff = compute_l2_diff(prev_bid, empty, curr_bid, empty);

    EXPECT_EQ(applyDiffBySplicing(prev_bid, diff.r[0], diff.l[0], /*bid=*/true), curr_bid);
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
    EXPECT_EQ(diff.l[0][0]["p"], "99.0");
    EXPECT_EQ(diff.l[0][0]["s"], "7.0");
    EXPECT_EQ(diff.l[0][1]["p"], "97.0");
    EXPECT_EQ(diff.l[0][1]["s"], "2.0");

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
// split_book_snapshot — depth cap and frame decoding
// ===========================================================================

namespace {
// A BookSnapshot frame's levels are one flat block: every bid, then every ask.
std::vector<MDLevel> makeFrame(const std::vector<std::pair<double, double>>& bids,
                               const std::vector<std::pair<double, double>>& asks) {
    std::vector<MDLevel> frame;
    frame.reserve(bids.size() + asks.size());
    for (const auto& side : {bids, asks}) {
        for (const auto& [px, sz] : side) {
            MDLevel level{};
            level.price = to_price_t(px);
            level.qty = to_qty_t(sz);
            level.num_orders = 1;
            frame.push_back(level);
        }
    }
    return frame;
}
}   // namespace

TEST(HyperliquidL2Diff, SplitBookSnapshot_ShallowBook_PublishedWhole) {
    auto frame = makeFrame({{100.0, 10.0}, {99.0, 5.0}}, {{101.0, 8.0}});

    json::array_t bids, asks;
    L2Levels curr_bid, curr_ask;
    split_book_snapshot(frame.data(), 2, 1, bids, asks, curr_bid, curr_ask);

    ASSERT_EQ(bids.size(), 2u);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(bids[0]["px"], "100.0");
    EXPECT_EQ(bids[0]["sz"], "10.0");
    EXPECT_EQ(asks[0]["px"], "101.0");
    EXPECT_EQ(asks[0]["sz"], "8.0");
    EXPECT_EQ(curr_bid.size(), 2u);
    EXPECT_EQ(curr_ask.size(), 1u);
}

TEST(HyperliquidL2Diff, SplitBookSnapshot_DeepBook_TruncatedToVenueDepth) {
    // The simulator's book is not depth-limited; Hyperliquid publishes 20 a
    // side. Levels past the cap must not reach the wire, and the diff baseline
    // must be cut at the same place - "r" indexes the array the client holds.
    std::vector<std::pair<double, double>> deep_bids, deep_asks;
    for (uint32_t i = 0; i < L2_PUBLISH_DEPTH + 5; ++i) {
        deep_bids.emplace_back(100.0 - i, 1.0);
        deep_asks.emplace_back(101.0 + i, 1.0);
    }
    auto frame = makeFrame(deep_bids, deep_asks);

    json::array_t bids, asks;
    L2Levels curr_bid, curr_ask;
    split_book_snapshot(frame.data(), L2_PUBLISH_DEPTH + 5, L2_PUBLISH_DEPTH + 5,
                        bids, asks, curr_bid, curr_ask);

    EXPECT_EQ(bids.size(), L2_PUBLISH_DEPTH);
    EXPECT_EQ(asks.size(), L2_PUBLISH_DEPTH);
    EXPECT_EQ(curr_bid.size(), L2_PUBLISH_DEPTH);
    EXPECT_EQ(curr_ask.size(), L2_PUBLISH_DEPTH);

    // Kept from the top of book down, so the worst published level is the 20th.
    EXPECT_EQ(bids.front()["px"], "100.0");
    EXPECT_EQ(bids.back()["px"], to_hyperliquid_number(to_price_t(100.0 - (L2_PUBLISH_DEPTH - 1))));
    EXPECT_EQ(asks.front()["px"], "101.0");
    EXPECT_EQ(asks.back()["px"], to_hyperliquid_number(to_price_t(101.0 + (L2_PUBLISH_DEPTH - 1))));
}

TEST(HyperliquidL2Diff, SplitBookSnapshot_TruncatedBids_AsksReadFromFullOffset) {
    // Regression guard for the subtle half: the ask block begins after every bid
    // the frame carries, not after the ones published. Reading it from the
    // truncated count would hand subscribers the deepest bids as asks.
    std::vector<std::pair<double, double>> deep_bids;
    for (uint32_t i = 0; i < L2_PUBLISH_DEPTH + 3; ++i) {
        deep_bids.emplace_back(100.0 - i, 1.0);
    }
    auto frame = makeFrame(deep_bids, {{200.0, 7.0}, {201.0, 6.0}});

    json::array_t bids, asks;
    L2Levels curr_bid, curr_ask;
    split_book_snapshot(frame.data(), L2_PUBLISH_DEPTH + 3, 2, bids, asks, curr_bid, curr_ask);

    ASSERT_EQ(asks.size(), 2u);
    EXPECT_EQ(asks[0]["px"], "200.0");
    EXPECT_EQ(asks[1]["px"], "201.0");
    ASSERT_EQ(curr_ask.size(), 2u);
    EXPECT_EQ(curr_ask[0].first, to_price_t(200.0));
}

TEST(HyperliquidL2Diff, SplitBookSnapshot_ReusedOutputs_Overwritten) {
    // Both publish paths hand in fresh locals today, but the contract is that
    // the outputs are replaced rather than appended to.
    auto frame = makeFrame({{100.0, 10.0}}, {{101.0, 8.0}});

    json::array_t bids{json{{"px", "stale"}}}, asks{json{{"px", "stale"}}};
    L2Levels curr_bid{{to_price_t(1.0), to_qty_t(1.0)}}, curr_ask{{to_price_t(2.0), to_qty_t(2.0)}};
    split_book_snapshot(frame.data(), 1, 1, bids, asks, curr_bid, curr_ask);

    ASSERT_EQ(bids.size(), 1u);
    EXPECT_EQ(bids[0]["px"], "100.0");
    ASSERT_EQ(curr_bid.size(), 1u);
    EXPECT_EQ(curr_bid[0].first, to_price_t(100.0));
    ASSERT_EQ(curr_ask.size(), 1u);
    EXPECT_EQ(curr_ask[0].first, to_price_t(101.0));
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
