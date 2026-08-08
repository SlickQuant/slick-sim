#pragma once

#include <common/types.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace slick::sim::md_publisher {

// One side of an order book, ordered best price first — the same order the
// exchange/publisher already builds "bids"/"asks" arrays in.
using L2Levels = std::vector<std::pair<price_t, qty_t>>;

struct L2Diff {
    nlohmann::json l;  // [bid_changed_or_added[], ask_changed_or_added[]] — {"p","s"} entries
    nlohmann::json r;  // [bid_removed_indices[], ask_removed_indices[]] — indices into the *previous* array
};

// Computes the incremental diff between a previous and current order book
// state, matching Hyperliquid's undocumented `l2` channel diff shape:
// changed/added levels are reported by price+size, removed levels are
// reported by their index in the previous per-side array.
L2Diff compute_l2_diff(const L2Levels& prev_bid, const L2Levels& prev_ask,
                        const L2Levels& curr_bid, const L2Levels& curr_ask);

// Inverse of hyperliquid::decode_l2_diff() (SlickQuant/hyperliquid-cpp):
// JSON-dump, raw-deflate compress (no zlib/gzip header), base64-encode.
std::string encode_l2_diff(const nlohmann::json& payload);

}   // end namespace slick::sim::md_publisher
