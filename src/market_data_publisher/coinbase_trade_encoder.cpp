#include "coinbase_trade_encoder.hpp"

#include <common/order.hpp>
#include <common/types.hpp>
#include <utils/timestamp.hpp>
#include <string>

using json = nlohmann::json;

namespace slick::sim::md_publisher {

namespace {

// The venue reports per-trade times to microsecond precision, and the message
// envelope to nanosecond precision.
constexpr int TRADE_TIME_DIGITS = 6;
constexpr int ENVELOPE_TIME_DIGITS = 9;

json encode_trade(const char* product_id, uint64_t trade_id, uint64_t event_time,
                  price_t price, qty_t qty, Side aggressor_side) {
    return {
        {"trade_id",   std::to_string(trade_id)},
        {"product_id", product_id},
        {"price",      std::to_string(to_price_double(price))},
        {"size",       std::to_string(to_qty_double(qty))},
        {"side",       to_string(aggressor_side)},
        {"time",       utils::format_timestamp_iso8601(event_time, TRADE_TIME_DIGITS)}
    };
}

json encode_envelope(const char* event_type, json::array_t&& trades) {
    return {
        {"channel", "market_trades"},
        {"client_id", ""},
        {"timestamp", utils::format_timestamp_iso8601(ENVELOPE_TIME_DIGITS)},
        {"events", {{
            {"type", event_type},
            {"trades", std::move(trades)}
        }}}
    };
}

}   // namespace

json encode_market_trade(const char* product_id, const TradeSummary& summary) {
    json::array_t trades;
    trades.push_back(encode_trade(product_id, summary.trade_id,
                                  static_cast<uint64_t>(summary.timestamp),
                                  summary.price, summary.qty, summary.aggressor_side));
    return encode_envelope("update", std::move(trades));
}

json encode_market_trades(const char* product_id, const char* event_type,
                          const MDTrade* trades, uint32_t num_trades, bool newest_first) {
    json::array_t encoded;
    encoded.reserve(num_trades);
    for (uint32_t i = 0; i < num_trades; ++i) {
        const auto& trade = trades[newest_first ? num_trades - 1 - i : i];
        encoded.push_back(encode_trade(product_id, trade.trade_id, trade.event_time,
                                       trade.price, trade.qty, trade.side));
    }
    return encode_envelope(event_type, std::move(encoded));
}

}   // end namespace slick::sim::md_publisher
