#include "hyperliquid_l2_diff.hpp"

#include <openssl/evp.h>
#include <zlib.h>

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using namespace slick::sim;

namespace {

std::string format_price(price_t price) {
    return to_price_string(price);
}

std::string format_qty(qty_t qty) {
    return to_qty_string(qty);
}

// One side's contribution to "l" (changed/added, in current order) and "r"
// (indices into `prev` that are no longer present in `curr`).
void diff_side(const slick::sim::md_publisher::L2Levels& prev,
                const slick::sim::md_publisher::L2Levels& curr,
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
    for (size_t i = 0; i < prev.size(); ++i) {
        if (!curr_prices.contains(prev[i].first)) {
            r_out.push_back(i);
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

L2Diff compute_l2_diff(const L2Levels& prev_bid, const L2Levels& prev_ask,
                        const L2Levels& curr_bid, const L2Levels& curr_ask) {
    nlohmann::json l_bid, r_bid, l_ask, r_ask;
    diff_side(prev_bid, curr_bid, l_bid, r_bid);
    diff_side(prev_ask, curr_ask, l_ask, r_ask);

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
