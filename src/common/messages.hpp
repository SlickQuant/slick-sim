#pragma once

#include <string>
#include <cstddef>
#include <cstring>
#include <format>
#include <type_traits>
#include <common/order.hpp>

namespace slick::sim {

enum class ExecutionMessageType : uint16_t {
    UNKNOWN = 0,
    LOGON_REQUEST,
    TRADER_LOGON_REQUEST,
    LOGON_REPLY,
    LOGON_REJECT,
    TRADER_LOGON_REPLY,
    SESSION_REJECT,
    NEW_ORDER_REQUEST,
    ORDER_CANCEL_REPLACE_REQUEST,
    ORDER_CANCEL_REQUEST,
    ORDER_MASS_STATUS_REQUEST,
    BUSINESS_REJECT,
    EXECUTION_REPORT,
    ORDER_CANCEL_REJECT,
    ORDER_CANCEL_REPLACE_REJECT,
    HEARTBEAT,
    NEWS,
    RETRANSMIT_REQUEST,
    RETRANSMIT_RESPONSE
};

enum class BusinessRejectReason : uint16_t {
    UNKNOWN = 0,
    UNKNOWN_SECURITY_ID,
    DUPLICATE_REQUEST_ID
};

inline std::string to_string(BusinessRejectReason reject) {
    switch (reject) {
        case BusinessRejectReason::UNKNOWN_SECURITY_ID:
            return "UNKNOWN_SECURITY_ID";
        case BusinessRejectReason::DUPLICATE_REQUEST_ID:
            return "DUPLICATE_REQUEST_ID";
        case BusinessRejectReason::UNKNOWN:
            return "UNKNOWN";
    }
    return std::format("UNHANDLED BusinessRejectReason ({0})", static_cast<std::underlying_type<BusinessRejectReason>::type>(reject));
}

enum class SessionRejectReason : uint16_t {
    NONE,
    ALREADY_LOGGED_IN,
    NOT_LOGGED_IN,
    INVALID_SESSION_ID,
    UNKNOWN
};

inline std::string to_string(SessionRejectReason reject) {
    switch (reject) {
        case SessionRejectReason::NONE:
            return "NONE";
        case SessionRejectReason::ALREADY_LOGGED_IN:
            return "ALREADY_LOGGED_IN";
        case SessionRejectReason::NOT_LOGGED_IN:
            return "NOT_LOGGED_IN";
        case SessionRejectReason::INVALID_SESSION_ID:
            return "INVALID_SESSION_ID";
        case SessionRejectReason::UNKNOWN:
            return "UNKNOWN";
    }
    return std::format("UNHANDLED SessionRejectReason ({0})", static_cast<std::underlying_type<SessionRejectReason>::type>(reject));
}

enum class OrdRejectReason : uint16_t {
    NONE,
    UNKNOWN,
    UNKNOWN_CONTRACT,
    UNKNOWN_ORDER,
    MARKET_CLOSED,
    MARKET_HALTED,
    SMP,
    INVALID_DISPLAY_QTY,
    FAK_MUST_BE_LIMIT,
    ICEBERG_MUST_BE_LIMIT,
    ICEBERG_MUST_BE_DAY_OR_GTC,
    PRICE_CANT_BE_BELOW_STOP_PRICE,
    PRICE_CANT_BE_ABOVE_STOP_PRICE,
    STOP_PRICE_MUST_BE_BELOW_LAST_TRADE_PRICE,
    STOP_PRICE_MUST_BE_ABOVE_LAST_TRADE_PRICE,
    TIME_IN_FORCE_NOT_ALLOWED_IN_CURRENT_MARKET_STATE,
    ORDER_TYPE_NOT_ALLOWED_IN_CURRENT_MARKET_STATE,
    INVALID_TICK_SIZE,
    INVALID_QTY,
    INVALID_MIN_QTY,
    CLIENT_ORDER_ID_ALREADY_EXISTS,
    FOK_CANNOT_FILL,
};

inline std::string to_string(OrdRejectReason reject)
{
    switch (reject) {
        case OrdRejectReason::NONE:
            return "NONE";
        case OrdRejectReason::UNKNOWN:
            return "UNKNOWN";
        case OrdRejectReason::UNKNOWN_CONTRACT:
            return "UNKNOWN_CONTRACT";
        case OrdRejectReason::UNKNOWN_ORDER:
            return "UNKNOWN_ORDER";
        case OrdRejectReason::MARKET_CLOSED:
            return "MARKET_CLOSED";
        case OrdRejectReason::MARKET_HALTED:
            return "MARKET_HALTED";
        case OrdRejectReason::SMP:
            return "SMP";
        case OrdRejectReason::INVALID_DISPLAY_QTY:
            return "INVALID_DISPLAY_QTY";
        case OrdRejectReason::FAK_MUST_BE_LIMIT:
            return "FAK_MUST_BE_LIMIT";
        case OrdRejectReason::ICEBERG_MUST_BE_LIMIT:
            return "ICEBERG_MUST_BE_LIMIT";
        case OrdRejectReason::ICEBERG_MUST_BE_DAY_OR_GTC:
            return "ICEBERG_MUST_BE_DAY_OR_GTC";
        case OrdRejectReason::PRICE_CANT_BE_BELOW_STOP_PRICE:
            return "PRICE_CANT_BE_BELOW_STOP_PRICE";
        case OrdRejectReason::PRICE_CANT_BE_ABOVE_STOP_PRICE:
            return "PRICE_CANT_BE_ABOVE_STOP_PRICE";
        case OrdRejectReason::STOP_PRICE_MUST_BE_BELOW_LAST_TRADE_PRICE:
            return "STOP_PRICE_MUST_BE_BELOW_LAST_TRADE_PRICE";
        case OrdRejectReason::STOP_PRICE_MUST_BE_ABOVE_LAST_TRADE_PRICE:
            return "STOP_PRICE_MUST_BE_ABOVE_LAST_TRADE_PRICE";
        case OrdRejectReason::TIME_IN_FORCE_NOT_ALLOWED_IN_CURRENT_MARKET_STATE:
            return "TIME_IN_FORCE_NOT_ALLOWED_IN_CURRENT_MARKET_STATE";
        case OrdRejectReason::ORDER_TYPE_NOT_ALLOWED_IN_CURRENT_MARKET_STATE:
            return "ORDER_TYPE_NOT_ALLOWED_IN_CURRENT_MARKET_STATE";
        case OrdRejectReason::INVALID_TICK_SIZE:
            return "INVALID_TICK_SIZE";
        case OrdRejectReason::INVALID_QTY:
            return "INVALID_QTY";
        case OrdRejectReason::INVALID_MIN_QTY:
            return "INVALID_MIN_QTY";
        case OrdRejectReason::CLIENT_ORDER_ID_ALREADY_EXISTS:
            return "CLIENT_ORDER_ID_ALREADY_EXISTS";
        case OrdRejectReason::FOK_CANNOT_FILL:
            return "FOK_CANNOT_FILL";
    }
    return std::format("UNHANDLED OrdRejectReason ({0})", static_cast<std::underlying_type<OrdRejectReason>::type>(reject));
}

enum class CxlRejectReason : uint16_t {
    NONE,
    UNKNOWN,
    MARKET_CLOSED,
    MARKET_HALTED,
    PREOPEN,
    ORDER_NOT_FOUND,
    INVALID_DISPLAY_QTY,
    ICEBERG_REQUEST_ON_NON_ICEBERG,
    NON_ICEBERG_REQUEST_ON_ICEBERG,
    ORDER_TYPE_MISMATCH,
    TIME_IN_FORCE_MISMATCH,
    PRICE_CANT_BE_BELOW_STOP_PRICE,
    PRICE_CANT_BE_ABOVE_STOP_PRICE,
    STOP_PRICE_MUST_BE_BELOW_LAST_TRADE_PRICE,
    STOP_PRICE_MUST_BE_ABOVE_LAST_TRADE_PRICE,
    INVALID_TICK_SIZE,
    INVALID_QTY,
    INVALID_MIN_QTY,
    INTERCEPTED,
};

inline std::string to_string(CxlRejectReason reject) {
    switch (reject) {
        case CxlRejectReason::NONE:
            return "NONE";
        case CxlRejectReason::UNKNOWN:
            return "UNKNOWN";
        case CxlRejectReason::MARKET_CLOSED:
            return "MARKET_CLOSED";
        case CxlRejectReason::MARKET_HALTED:
            return "MARKET_HALTED";
        case CxlRejectReason::PREOPEN:
            return "PREOPEN";
        case CxlRejectReason::ORDER_NOT_FOUND:
            return "ORDER_NOT_FOUND";
        case CxlRejectReason::INVALID_DISPLAY_QTY:
            return "INVALID_DISPLAY_QTY";
        case CxlRejectReason::ICEBERG_REQUEST_ON_NON_ICEBERG:
            return "ICEBERG_REQUEST_ON_NON_ICEBERG";
        case CxlRejectReason::NON_ICEBERG_REQUEST_ON_ICEBERG:
            return "NON_ICEBERG_REQUEST_ON_ICEBERG";
        case CxlRejectReason::ORDER_TYPE_MISMATCH:
            return "ORDER_TYPE_MISMATCH";
        case CxlRejectReason::TIME_IN_FORCE_MISMATCH:
            return "TIME_IN_FORCE_MISMATCH";
        case CxlRejectReason::PRICE_CANT_BE_BELOW_STOP_PRICE:
            return "PRICE_CANT_BE_BELOW_STOP_PRICE";
        case CxlRejectReason::PRICE_CANT_BE_ABOVE_STOP_PRICE:
            return "PRICE_CANT_BE_ABOVE_STOP_PRICE";
        case CxlRejectReason::STOP_PRICE_MUST_BE_BELOW_LAST_TRADE_PRICE:
            return "STOP_PRICE_MUST_BE_BELOW_LAST_TRADE_PRICE";
        case CxlRejectReason::STOP_PRICE_MUST_BE_ABOVE_LAST_TRADE_PRICE:
            return "STOP_PRICE_MUST_BE_ABOVE_LAST_TRADE_PRICE";
        case CxlRejectReason::INVALID_TICK_SIZE:
            return "INVALID_TICK_SIZE";
        case CxlRejectReason::INVALID_QTY:
            return "INVALID_QTY";
        case CxlRejectReason::INVALID_MIN_QTY:
            return "INVALID_MIN_QTY";
        case CxlRejectReason::INTERCEPTED:
            return "INTERCEPTED";
    }
    return std::format("UNHANDLED CxlRejectReason ({0})", static_cast<std::underlying_type<CxlRejectReason>::type>(reject));
}

#pragma pack(push, 1)

enum class MessageType : uint8_t {
    UNKNOWN = 0,
    NEW_ORDER_SINGLE = 1,
    ORDER_CANCEL_REQUEST = 2,
    ORDER_REPLACE_REQUEST = 3,
    EXECUTION_REPORT = 4,
    ORDER_CANCEL_REJECT = 5,
    HEARTBEAT = 6,
    TEST_REQUEST = 7,
    RE_SEND_REQUEST = 8,
    REJECT = 9,
    MD_SUBSCRIPTION = 10,
    MD_UNSUBSCRIPTION = 11,
};

struct AddOrderMessage {
    char user_id[37];
    char client_order_id[37];
    Side side;
    OrderType type;
    TimeInForce time_in_force;
    price_t price = NULL_PRICE;
    qty_t qty = 0;
    price_t take_profit_price = NULL_PRICE;
    price_t stop_loss_price = NULL_PRICE;
    time_t expire_time;  // Unix timestamp in seconds
};

struct ModifyOrderMessage {
    char user_id[37];
    char order_id[37];
    char client_order_id[37];
    price_t new_price;
    qty_t new_qty;
};

struct CancelOrderMessage {
    char user_id[37];
    char order_id[37];
    char client_order_id[37];
};

struct MDSubscriptionMessage {
    uint8_t channel;
    void* client;
};

struct Request {
    time_t time_stamp;
    char symbol[32];
    MessageType msg_type;
    union {
        AddOrderMessage add_order;
        ModifyOrderMessage modify_order;
        CancelOrderMessage cancel_order;
        MDSubscriptionMessage md_subscription;
    };

    Request() : msg_type(MessageType::NEW_ORDER_SINGLE), add_order{} {
    }

    ~Request() = default;
};

struct OrderResponse {
    char symbol[32];
    char user_id[37];
    char client_order_id[37];
    char order_id[37];
    char error_message[256];
    MessageType response_type = MessageType::UNKNOWN;
    ExecType exec_type = ExecType::NEW;
    OrderStatus order_status = OrderStatus::NEW;
    OrderType order_type = OrderType::UNKNOWN;
    TimeInForce time_in_force = TimeInForce::DAY;
    price_t price = NULL_PRICE;
    price_t last_fill_price = NULL_PRICE;
    price_t avg_fill_price = NULL_PRICE;
    qty_t qty = 0;
    qty_t last_qty = 0;
    qty_t cum_qty = 0;
    qty_t leaves_qty = 0;
    OrdRejectReason reject_reason = OrdRejectReason::NONE;
    time_t creation_time = 0;
    time_t request_time = 0;
    time_t timestamp = 0;
    Side side = Side::UNKNOWN_SIDE;
    bool post_only = false;
};

struct Trade {
    uint64_t order_id;
    qty_t qty;
};

struct TradeSummary {
    time_t timestamp = 0;
    uint64_t trade_id;
    int16_t security_id;
    Side aggressor_side;
    price_t price;
    qty_t qty;
    int32_t num_orders;
    Trade trades[0];
};

#pragma pack(pop)

// setOrderIdentity() copies an order's OrderIdentity over the head of an
// OrderResponse in one go, which is only valid while the two layouts agree byte
// for byte. Adding, reordering or resizing a field at either end fails the build
// here rather than silently shifting the ids on the wire.
static_assert(std::is_standard_layout_v<OrderResponse>);
static_assert(offsetof(OrderResponse, symbol) == offsetof(OrderIdentity, symbol));
static_assert(offsetof(OrderResponse, user_id) == offsetof(OrderIdentity, user_id));
static_assert(offsetof(OrderResponse, client_order_id) == offsetof(OrderIdentity, client_order_id));
static_assert(offsetof(OrderResponse, order_id) == offsetof(OrderIdentity, order_id));
static_assert(sizeof(OrderResponse::symbol) == decltype(OrderIdentity::symbol)::buffer_size());
static_assert(sizeof(OrderResponse::user_id) == decltype(OrderIdentity::user_id)::buffer_size());
static_assert(sizeof(OrderResponse::client_order_id) == decltype(OrderIdentity::client_order_id)::buffer_size());
static_assert(sizeof(OrderResponse::order_id) == decltype(OrderIdentity::order_id)::buffer_size());
static_assert(offsetof(OrderResponse, error_message) == sizeof(OrderIdentity));

/// Fills the symbol/user_id/client_order_id/order_id header of a response from an
/// order. One fixed-size copy, replacing four copies out of four heap blocks.
inline void setOrderIdentity(OrderResponse &response, const Order &order) noexcept {
    std::memcpy(&response, static_cast<const OrderIdentity *>(&order), sizeof(OrderIdentity));
}

struct TradeSummaryInfo {
    time_t timestamp = 0;
    uint64_t trade_id;
    Side aggressor_side;
    price_t price;
    qty_t qty;
    /// Portion of `qty` that was matched against phantom (market-mirroring)
    /// liquidity rather than the simulator's own resting orders. Internal only -
    /// the exchange uses it to avoid reducing a level twice when the venue's
    /// level update for the same trade arrives. Not part of the wire format.
    qty_t phantom_qty = 0;
    int32_t num_orders;
    std::vector<Trade> Trades;
};

}   // end namespace slick::sim