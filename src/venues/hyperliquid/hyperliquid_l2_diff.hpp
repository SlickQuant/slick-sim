#pragma once

#include <common/types.hpp>
#include <common/market_data.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace slick::sim::md_publisher {

// One side of an order book, ordered best price first — the same order the
// exchange/publisher already builds "bids"/"asks" arrays in.
using L2Levels = std::vector<std::pair<price_t, qty_t>>;

// Price levels Hyperliquid publishes per side on `l2Book` and `l2`. The
// simulator's own book is not depth-limited — it holds every level the venue
// ever told it about, plus anything a trade print left resting — so the publish
// path truncates to this. See split_book_snapshot().
inline constexpr uint32_t L2_PUBLISH_DEPTH = 20;

// Splits a BookSnapshot frame's flat level array into the venue's
// `[bids, asks]` JSON and the diff baseline those same levels form, each
// truncated to L2_PUBLISH_DEPTH.
//
// One function producing both, deliberately: a diff's "r" entries address
// positions in the array the subscriber last received, so a baseline truncated
// differently from the JSON would number levels the client never saw.
void split_book_snapshot(const MDLevel* levels, uint32_t num_bid, uint32_t num_ask,
                         nlohmann::json::array_t& bids, nlohmann::json::array_t& asks,
                         L2Levels& curr_bid, L2Levels& curr_ask);

struct L2Diff {
    nlohmann::json l;  // [bid_changed_or_added[], ask_changed_or_added[]] — {"p","s"} entries, book order
    nlohmann::json r;  // [bid_removed_indices[], ask_removed_indices[]] — indices into the *previous* array
};

// Computes the incremental diff between a previous and current order book
// state, matching Hyperliquid's undocumented `l2` channel diff shape:
// changed/added levels are reported by price+size, removed levels are
// reported by their index in the previous per-side array.
//
// Ordering is part of that shape, not an implementation detail. The venue emits
// "l" in book order and "r" in ascending price order — so bid removal indices
// descend and ask removal indices ascend — and a client is free to apply the
// removals in the order it receives them. See diff_side() in the .cpp.
L2Diff compute_l2_diff(const L2Levels& prev_bid, const L2Levels& prev_ask,
                        const L2Levels& curr_bid, const L2Levels& curr_ask);

// Inverse of hyperliquid::decode_l2_diff() (SlickQuant/hyperliquid-cpp):
// JSON-dump, raw-deflate compress (no zlib/gzip header), base64-encode.
std::string encode_l2_diff(const nlohmann::json& payload);

}   // end namespace slick::sim::md_publisher
