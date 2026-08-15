#pragma once

#include <common/market_data.hpp>
#include <common/messages.hpp>
#include <nlohmann/json.hpp>

namespace slick::sim::md_publisher {

// Encoders for Coinbase's `market_trades` channel, kept free of the socket layer
// so the wire shape can be tested without standing up a WebSocket server.
//
// Every message carries the same envelope; only `sequence_num` differs per
// socket, so the caller stamps that in before sending:
//
//   {"channel":"market_trades","client_id":"","timestamp":"<iso8601>",
//    "sequence_num":N,
//    "events":[{"type":"update"|"snapshot","trades":[...]}]}
//
// Per-trade fields follow the venue: `price`, `size` and `trade_id` are strings,
// `side` is the aggressor as "BUY"/"SELL", and `time` is microsecond ISO 8601.

/// One executed trade, as a `"type":"update"` event.
nlohmann::json encode_market_trade(const char* product_id, const TradeSummary& summary);

/// A run of trades as a single event of the given type. `newest_first` reverses
/// the iteration: the venue orders snapshot trades newest first, while updates
/// stay chronological. Emits an empty `trades` array when `num_trades` is zero.
nlohmann::json encode_market_trades(const char* product_id, const char* event_type,
                                    const MDTrade* trades, uint32_t num_trades,
                                    bool newest_first);

}   // end namespace slick::sim::md_publisher
