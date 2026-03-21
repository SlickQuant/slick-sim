#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <limits>
#include "types.hpp"

using json = nlohmann::json;

namespace slick::sim {

enum Side : uint8_t {
    BUY,
    SELL,
    UNKNOWN_SIDE,
};

inline constexpr Side opposite_side(Side side) {
    switch(side) {
    case Side::BUY:
        return Side::SELL;
    case Side::SELL:
        return Side::BUY;
    case Side::UNKNOWN_SIDE:
        return Side::UNKNOWN_SIDE;
    }
    return Side::UNKNOWN_SIDE;
}

template<Side S>
inline constexpr Side opposite_side() {
    if constexpr (S == Side::BUY) {
        return Side::SELL;
    } else if constexpr (S == Side::SELL) {
        return Side::BUY;
    } else {
        return Side::UNKNOWN_SIDE;
    }
}

inline constexpr std::string_view to_string(Side side) {
    switch(side)
    {
    case Side::BUY:
        return "BUY";
    case Side::SELL:
        return "SELL";
    case Side::UNKNOWN_SIDE:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class OrderType : char {
    UNKNOWN = '0',
    MARKET = '1',
    LIMIT = '2',
    STOP = '3',
    STOP_LIMIT = '4',
    BRACKET = '5',
    TWAP = '6',
    ROLL_OPEN = '7',
    ROLL_CLOSE = '8',
    LIQUIDATION = '9',
    SCALED = 'A',
 };

 inline constexpr std::string_view to_string(OrderType type) {
    switch(type) {
    case OrderType::MARKET:
        return "MARKET";
    case OrderType::LIMIT:
        return "LIMIT";
    case OrderType::STOP:
        return "STOP";
    case OrderType::STOP_LIMIT:
        return "STOP_LIMIT";
    case OrderType::BRACKET:
        return "BRAKET";
    case OrderType::TWAP:
        return "TWAP";
    case OrderType::ROLL_OPEN:
        return "ROLL_OPEN";
    case OrderType::ROLL_CLOSE:
        return "ROLL_CLOSE";
    case OrderType::LIQUIDATION:
        return "LIQUIDATION";
    case OrderType::SCALED:
        return "SCALED";
    }
    return "UNKNOWN";
}

enum class ExecType : char {
    NEW = '0',
    PARTIAL_FILL = '1',
    FILL = '2',
    CANCELED = '3',
    REPLACED = '4',
    PENDING_NEW = '5',
    PENDING_CANCEL = '6',
    TRADE = '8',
    EXPIRED = 'C',
    PENDING_REPLACE = 'E',
    TRADE_CORRECT = 'G',
    TRADE_CANCEL = 'H',
    ORDER_STATUS = 'I'
};

enum class OrderStatus : char {
    NEW = '0',
    PARTIALLY_FILLED = '1',
    FILLED = '2',
    DONE_FOR_DAY = '3',
    CANCELED = '4',
    REPLACED = '5',
    PENDING_CANCEL = '6',
    STOPPED = '7',
    REJECTED = '8',
    SUSPENDED = '9',
    PENDING_NEW = 'A',
    CALCULATED = 'B',
    EXPIRED = 'C',
    ACCEPTED_FOR_BIDDING = 'D',
    PENDING_REPLACE = 'E'
};

enum class TimeInForce : char {
    DAY = '0',
    GOOD_TILL_CANCEL = '1',
    AT_THE_OPENING = '2',
    IMMEDIATE_OR_CANCEL = '3',
    FILL_OR_KILL = '4',
    GOOD_TILL_CROSSING = '5',
    GOOD_TILL_DATE = '6'
};

enum class SelfMatchPreventionMode : uint8_t {
    NONE = 0,           // No SMP, allow self-matching
    CANCEL_RESTING = 1, // Cancel the resting order on the book
    CANCEL_NEWEST = 2,  // Cancel/reject the incoming order
};

enum class ProductType : uint8_t {
    UNKNOWN,
    SPOT,
    FUTURE,
};

inline constexpr std::string_view to_string(ProductType type) {
    switch(type) {
    case ProductType::SPOT:
        return "SPOT";
    case ProductType::FUTURE:
        return "FUTURE";
    }
    return "UNKNOWN_PRODUCT_TYPE";
}

struct Order {
    std::string user_id;
    std::string client_order_id;
    std::string order_id;
    std::string symbol;
    std::string end_time;

    uint64_t id;
    uint64_t priority = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::MARKET;
    OrderStatus status = OrderStatus::NEW;
    TimeInForce time_in_force = TimeInForce::DAY;
    ProductType product_type = ProductType::UNKNOWN;
    bool post_only = false;
    bool pending_cancel = false;
    bool size_in_quote = false;
    
    price_t price = 0;
    qty_t quantity = 0;
    qty_t leaves_quantity = 0;
    price_t avg_filled_price = 0;
    qty_t filled_quantity = 0;
    price_t last_filled_price = 0; 
    qty_t last_filled_qty = 0;
    uint32_t num_fills = 0;
    double fee = 0;
    double filled_value = 0;
    
    int client_id = -1;
    uint64_t created_time = std::chrono::system_clock::now().time_since_epoch().count();
    uint64_t last_update_time = std::chrono::system_clock::now().time_since_epoch().count();
    std::optional<uint64_t> last_fill_time;

    std::string reject_message;
    std::string cancel_message;

    json edit_history = json::array();
    
    Order() = default;

    double price_double() const noexcept {
        return to_price_double(price);
    }

    double qty_double() const noexcept {
        return to_qty_double(quantity);
    }

    double filled_qty_double() const noexcept {
        return to_qty_double(filled_quantity);
    }

    double leaves_qty_double() const noexcept {
        return to_qty_double(leaves_quantity);
    }

    double last_filled_qty_double() const noexcept {
        return to_qty_double(last_filled_qty);
    }

    double last_filled_price_double() const noexcept {
        return to_price_double(last_filled_price);
    }

    double avg_filled_price_double() const noexcept {
        return to_price_double(avg_filled_price);
    }
};

struct Fill {
    std::string order_id;
    price_t filled_price;
    qty_t filled_qty;
    time_t timestamp;
};

} // namespace slick::sim::order_gateway