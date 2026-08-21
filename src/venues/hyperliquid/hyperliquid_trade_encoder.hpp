#pragma once

#include <common/market_data.hpp>
#include <common/messages.hpp>
#include <nlohmann/json.hpp>

namespace slick::sim::md_publisher {

// Encoders for Hyperliquid's `trades` channel, kept free of the socket layer so
// the wire shape can be tested without standing up a WebSocket server.
//
//   {"channel":"trades","data":[
//     {"coin":"ETH","side":"B","px":"3000.5","sz":"1.25","time":<ms>,
//      "hash":"0x0","tid":<id>}]}
//
// `side` is the aggressor: "B" for a buy, "A" otherwise. The simulator has no
// on-chain transaction to reference, so `hash` is always the literal "0x0".

/// One executed trade.
nlohmann::json encode_trade_message(const char* coin, const TradeSummary& summary);

/// A run of trades in the order given. Emits an empty `data` array when
/// `num_trades` is zero.
nlohmann::json encode_trade_message(const char* coin, const MDTrade* trades, uint32_t num_trades);

}   // end namespace slick::sim::md_publisher
