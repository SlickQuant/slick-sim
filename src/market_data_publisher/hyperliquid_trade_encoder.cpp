#include "hyperliquid_trade_encoder.hpp"

#include <common/order.hpp>
#include <common/types.hpp>
#include <utils/timestamp.hpp>
#include <string>

using json = nlohmann::json;

namespace slick::sim::md_publisher {

namespace {

json encode_trade(const char* coin, uint64_t trade_id, uint64_t event_time,
                  price_t price, qty_t qty, Side aggressor_side) {
    return {
        {"coin", coin},
        {"side", aggressor_side == Side::BUY ? "B" : "A"},
        {"px",   to_price_string(price)},
        {"sz",   to_qty_string(qty)},
        {"time", event_time / utils::ONE_MILLISECOND_NS},
        {"hash", "0x0"},
        {"tid",  trade_id}
    };
}

json encode_envelope(json::array_t&& trades) {
    return {{"channel", "trades"}, {"data", std::move(trades)}};
}

}   // namespace

json encode_trade_message(const char* coin, const TradeSummary& summary) {
    json::array_t trades;
    trades.push_back(encode_trade(coin, summary.trade_id,
                                  static_cast<uint64_t>(summary.timestamp),
                                  summary.price, summary.qty, summary.aggressor_side));
    return encode_envelope(std::move(trades));
}

json encode_trade_message(const char* coin, const MDTrade* trades, uint32_t num_trades) {
    json::array_t encoded;
    encoded.reserve(num_trades);
    for (uint32_t i = 0; i < num_trades; ++i) {
        const auto& trade = trades[i];
        encoded.push_back(encode_trade(coin, trade.trade_id, trade.event_time,
                                       trade.price, trade.qty, trade.side));
    }
    return encode_envelope(std::move(encoded));
}

}   // end namespace slick::sim::md_publisher
