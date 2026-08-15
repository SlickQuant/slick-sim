#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <limits>
#include "types.hpp"
#include <utils/timestamp.hpp>

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
        break;
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
    case OrderType::UNKNOWN:
        break;
    }
    return "UNKNOWN";
}

enum class ExecType : char {
    NEW = '0',
    CANCELED = '4',
    REPLACED = '5',
    PENDING_CANCEL = '6',
    REJECTED = '8',
    EXPIRED = 'C',
    PENDING_REPLACE = 'E',
    TRADE = 'F',
    TRADE_CORRECT = 'G',
    TRADE_CANCEL = 'H',
    ORDER_STATUS = 'I',
    RESTATED = 'D',
};

inline constexpr std::string_view to_string(ExecType exec_type) {
    switch (exec_type) {
    case ExecType::NEW:
        return "NEW";
    case ExecType::CANCELED:
        return "CANCELED";
    case ExecType::REPLACED:
        return "REPLACED";
    case ExecType::PENDING_CANCEL:
        return "PENDING_CANCEL";
    case ExecType::REJECTED:
        return "REJECTED";
    case ExecType::EXPIRED:
        return "EXPIRED";
    case ExecType::PENDING_REPLACE:
        return "PENDING_REPLACE";
    case ExecType::TRADE:
        return "TRADE";
    case ExecType::TRADE_CORRECT:
        return "TRADE_CORRECT";
    case ExecType::TRADE_CANCEL:
        return "TRADE_CANCEL";
    case ExecType::ORDER_STATUS:
        return "ORDER_STATUS";
    case ExecType::RESTATED:
        return "RESTATED";
    }
    return "UNKNOWN";
}

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

inline constexpr std::string_view to_string(OrderStatus status) {
    switch (status) {
    case OrderStatus::NEW:
        return "NEW";
    case OrderStatus::PARTIALLY_FILLED:
        return "PARTIALLY_FILLED";
    case OrderStatus::FILLED:
        return "FILLED";
    case OrderStatus::DONE_FOR_DAY:
        return "DONE_FOR_DAY";
    case OrderStatus::CANCELED:
        return "CANCELLED";
    case OrderStatus::REPLACED:
        return "REPLACED";
    case OrderStatus::PENDING_CANCEL:
        return "PENDING_CANCEL";
    case OrderStatus::STOPPED:
        return "STOPPED";
    case OrderStatus::REJECTED:
        return "REJECTED";
    case OrderStatus::SUSPENDED:
        return "SUSPENDED";
    case OrderStatus::PENDING_NEW:
        return "PENDING_NEW";
    case OrderStatus::CALCULATED:
        return "CALCULATED";
    case OrderStatus::EXPIRED:
        return "EXPIRED";
    case OrderStatus::ACCEPTED_FOR_BIDDING:
        return "ACCEPTED_FOR_BIDDING";
    case OrderStatus::PENDING_REPLACE:
        return "PENDING_REPLACE";
    }
    return "UNKNOWN";
}

enum class TimeInForce : char {
    DAY = '0',
    GOOD_TILL_CANCEL = '1',
    AT_THE_OPENING = '2',
    IMMEDIATE_OR_CANCEL = '3',
    FILL_OR_KILL = '4',
    GOOD_TILL_CROSSING = '5',
    GOOD_TILL_DATE = '6'
};

inline std::string_view to_string(TimeInForce tif) {
    switch (tif) {
    case TimeInForce::DAY:
        return "DAY";
    case TimeInForce::GOOD_TILL_CANCEL:
        return "GOOD_TILL_CANCEL";
    case TimeInForce::AT_THE_OPENING:
        return "AT_THE_OPENING";
    case TimeInForce::IMMEDIATE_OR_CANCEL:
        return "IMMEDIATE_OR_CANCEL";
    case TimeInForce::FILL_OR_KILL:
        return "FILL_OR_KILL";
    case TimeInForce::GOOD_TILL_CROSSING:
        return "GOOD_TILL_CROSSING";
    case TimeInForce::GOOD_TILL_DATE:
        return "GOOD_TILL_DATE";
    }
    return "UNKNOWN";
}

enum class SelfMatchPreventionMode : uint8_t {
    NONE = 0,           // No SMP, allow self-matching
    CANCEL_RESTING = 1, // Cancel the resting order on the book
    CANCEL_NEWEST = 2,  // Cancel/reject the incoming order
};

inline constexpr std::string_view to_string(SelfMatchPreventionMode mode) {
    switch(mode) {
    case SelfMatchPreventionMode::NONE:
        return "none";
    case SelfMatchPreventionMode::CANCEL_RESTING:
        return "cancel_resting";
    case SelfMatchPreventionMode::CANCEL_NEWEST:
        return "cancel_newest";
    }
    return "none";
}

/// Parses the `self_match_prevention` configuration value. Returns NONE for an
/// unrecognised name, so a typo disables SMP rather than aborting startup.
inline constexpr SelfMatchPreventionMode to_smp_mode(std::string_view name) {
    if (name == "cancel_resting") {
        return SelfMatchPreventionMode::CANCEL_RESTING;
    }
    if (name == "cancel_newest") {
        return SelfMatchPreventionMode::CANCEL_NEWEST;
    }
    return SelfMatchPreventionMode::NONE;
}

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
    case ProductType::UNKNOWN:
        break;
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
    price_t avg_fill_price = 0;
    qty_t cum_quantity = 0;
    price_t last_fill_price = 0; 
    qty_t last_fill_qty = 0;
    uint32_t num_fills = 0;
    double fee = 0;
    double filled_value = 0;
    
    int client_id = -1;
    uint64_t created_time = utils::get_current_time_ns();
    uint64_t last_update_time = created_time;
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

    double cum_qty_double() const noexcept {
        return to_qty_double(cum_quantity);
    }

    double leaves_qty_double() const noexcept {
        return to_qty_double(leaves_quantity);
    }

    double last_fill_qty_double() const noexcept {
        return to_qty_double(last_fill_qty);
    }

    double last_fill_price_double() const noexcept {
        return to_price_double(last_fill_price);
    }

    double avg_fill_price_double() const noexcept {
        return to_price_double(avg_fill_price);
    }
};

struct Fill {
    std::string order_id;
    price_t filled_price;
    qty_t filled_qty;
    time_t timestamp;
};

} // namespace slick::sim::order_gateway