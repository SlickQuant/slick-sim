#pragma once

#include <common/types.hpp>
#include <common/order.hpp>
#include <array>
#include <cstdint>

namespace slick::sim {

#pragma pack(push, 1)

enum MDUpdateType : uint8_t {
    BOOK,
    LEVEL,
    ORDER,
    TRADE_SUMMARY,
    TRADE,
    BOOK_SNAPSHOT,
    ORDER_SNAPSHOT,
    SUB_RESPONSE,
};

enum UpdateFlags : uint8_t {
    F_NONE = 0,
    F_IS_SNAPSHOT = 1,
    F_END_EVENT = 1 << 1,
};

struct MDLevel {
    uint64_t event_time;
    uint64_t seq_num;
    price_t price;
    qty_t qty;
    uint32_t num_orders;
    UpdateFlags flgas;
    Side side;
};

struct MDBookUpdate {
    MDLevel sell[10];
    MDLevel buy[10];
    uint8_t update_index[2];
};

struct MDLevelUpdate {
    uint64_t event_time;
    uint32_t num_level_update;
    MDLevel levels[0];
};

struct MDOrder {
    char order_id[36];
    uint64_t event_time;
    uint64_t priority;
    price_t price;
    qty_t qty;
    Side side;
};

struct MDOrderUpdate {
    uint32_t num_orders;
    MDOrder orders[0];
};

struct MarketDataUpdate {
    char symbol[32];
    Venue venue;
    MDUpdateType type;
    uint8_t data[0];
};

enum MDSubscriptionRejectReason : uint8_t {
    NONE,
    UNKNOWN_CONTRACT,
    MARKET_CLOSED,
};

struct MDSubscriptionResponse {
    uint8_t channel;
    MDSubscriptionRejectReason reject_reason;
    uint8_t data[0];
};

struct BookSnapshot {
    uint32_t num_bid;
    uint32_t num_ask;
    MDLevel levels[0];
};

struct MDTrade {
    uint64_t event_time;
    uint64_t seq_num;
    price_t price;
    qty_t qty;
    UpdateFlags flgas;
    Side side;
};

struct MDTradeUpdate {
    uint32_t num_trades;
    MDTrade trades[0];
};

#pragma pack(pop)

}   // end namespace slick::sim