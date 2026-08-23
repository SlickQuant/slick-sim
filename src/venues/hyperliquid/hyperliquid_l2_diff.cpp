#include "hyperliquid_l2_diff.hpp"
#include "hyperliquid_wire_format.hpp"

#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using namespace slick::sim;

namespace {

std::string format_price(price_t price) {
    return slick::sim::md_publisher::to_hyperliquid_number(price);
}

std::string format_qty(qty_t qty) {
    return slick::sim::md_publisher::to_hyperliquid_number(qty);
}

// One side's contribution to "l" (changed/added, in current order) and "r"
// (indices into `prev` that are no longer present in `curr`).
//
// Both lists carry the live venue's own ordering, which a client is entitled to
// rely on: "l" in book order (best price first), and "r" in ascending *price*
// order - which is descending index on the bid side, whose array runs from the
// highest price down. Hence `descending_index`, set for bids only.
//
// Emitting bid removals in ascending index order instead broke any client that
// splices them off one at a time, because each deletion shifts the entries
// behind it and the next index then resolves one slot too far. Hyperliquid's
// own bid removals never arrive in that order, so such a client is correct
// against the venue and desynced only here - the failure surfaced as a stale
// level resting above the opposite side.
void diff_side(const slick::sim::md_publisher::L2Levels& prev,
                const slick::sim::md_publisher::L2Levels& curr,
                bool descending_index,
                nlohmann::json& l_out, nlohmann::json& r_out) {
    std::unordered_map<price_t, qty_t> prev_by_price;
    prev_by_price.reserve(prev.size());
    for (const auto& [price, qty] : prev) {
        prev_by_price.emplace(price, qty);
    }

    std::unordered_set<price_t> curr_prices;
    curr_prices.reserve(curr.size());
    for (const auto& [price, qty] : curr) {
        curr_prices.insert(price);
    }

    l_out = nlohmann::json::array();
    for (const auto& [price, qty] : curr) {
        auto it = prev_by_price.find(price);
        if (it == prev_by_price.end() || it->second != qty) {
            l_out.push_back({{"p", format_price(price)}, {"s", format_qty(qty)}});
        }
    }

    r_out = nlohmann::json::array();
    if (descending_index) {
        for (size_t i = prev.size(); i-- > 0; ) {
            if (!curr_prices.contains(prev[i].first)) {
                r_out.push_back(i);
            }
        }
    } else {
        for (size_t i = 0; i < prev.size(); ++i) {
            if (!curr_prices.contains(prev[i].first)) {
                r_out.push_back(i);
            }
        }
    }
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};
    std::string out(4 * ((data.size() + 2) / 3), '\0');
    int len = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(out.data()),
        data.data(), static_cast<int>(data.size()));
    out.resize(static_cast<size_t>(len));
    return out;
}

}   // namespace

namespace slick::sim::md_publisher {

void split_book_snapshot(const MDLevel* levels, uint32_t num_bid, uint32_t num_ask,
                         nlohmann::json::array_t& bids, nlohmann::json::array_t& asks,
                         L2Levels& curr_bid, L2Levels& curr_ask) {
    const uint32_t bid_count = std::min(num_bid, L2_PUBLISH_DEPTH);
    const uint32_t ask_count = std::min(num_ask, L2_PUBLISH_DEPTH);

    bids.clear();
    asks.clear();
    curr_bid.clear();
    curr_ask.clear();
    bids.reserve(bid_count);
    asks.reserve(ask_count);
    curr_bid.reserve(bid_count);
    curr_ask.reserve(ask_count);

    auto emit = [](const MDLevel& level, nlohmann::json::array_t& out, L2Levels& baseline) {
        out.push_back({
            {"px", format_price(level.price)},
            {"sz", format_qty(level.qty)},
            {"n",  level.num_orders}
        });
        baseline.emplace_back(level.price, level.qty);
    };

    for (uint32_t i = 0; i < bid_count; ++i) {
        emit(levels[i], bids, curr_bid);
    }
    // The ask block starts after every bid the frame carries, not after the ones
    // published: truncating the bids must not walk the read cursor back.
    for (uint32_t i = 0; i < ask_count; ++i) {
        emit(levels[num_bid + i], asks, curr_ask);
    }
}

L2Diff compute_l2_diff(const L2Levels& prev_bid, const L2Levels& prev_ask,
                        const L2Levels& curr_bid, const L2Levels& curr_ask) {
    nlohmann::json l_bid, r_bid, l_ask, r_ask;
    diff_side(prev_bid, curr_bid, /*descending_index=*/true, l_bid, r_bid);
    diff_side(prev_ask, curr_ask, /*descending_index=*/false, l_ask, r_ask);

    L2Diff diff;
    diff.l = nlohmann::json::array({std::move(l_bid), std::move(l_ask)});
    diff.r = nlohmann::json::array({std::move(r_bid), std::move(r_ask)});
    return diff;
}

std::string encode_l2_diff(const nlohmann::json& payload) {
    const std::string plain = payload.dump();

    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("Failed to initialize raw deflate encoder");
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(plain.data()));
    stream.avail_in = static_cast<uInt>(plain.size());

    std::vector<uint8_t> compressed(deflateBound(&stream, static_cast<uLong>(plain.size())));
    stream.next_out = compressed.data();
    stream.avail_out = static_cast<uInt>(compressed.size());

    int result = deflate(&stream, Z_FINISH);
    size_t produced = compressed.size() - stream.avail_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        throw std::runtime_error("Failed to deflate l2 diff payload");
    }
    compressed.resize(produced);

    return base64_encode(compressed);
}

}   // end namespace slick::sim::md_publisher
