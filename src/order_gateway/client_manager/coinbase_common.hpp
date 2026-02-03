#pragma once

#include <common/order.hpp>
#include <nlohmann/json.hpp>
#include <utils/timestamp.hpp>

using json = nlohmann::json;

namespace slick::sim::order_gateway::coinbase {

inline std::string to_string(OrderStatus status) {
    switch(status) {
    case OrderStatus::NEW:
        return "Open";
    case OrderStatus::PARTIALLY_FILLED:
        return "Filled";
    case OrderStatus::FILLED:
        return "Filled";
    case OrderStatus::DONE_FOR_DAY:
        return "Expired";
    case OrderStatus::CANCELED:
        return "CANCELLED";
    case OrderStatus::REPLACED:
        return "Open";
    case OrderStatus::PENDING_CANCEL:
        return "CANCEL_QUEUED";
    case OrderStatus::STOPPED:
        return "UNKNOWN_ORDER_STATUS";
    case OrderStatus::REJECTED:
        return "FAILED";
    case OrderStatus::SUSPENDED:
        return "UNKNOWN_ORDER_STATUS";
    case OrderStatus::PENDING_NEW:
        return "PENDING";
    case OrderStatus::CALCULATED:
        return "UNKNOWN_ORDER_STATUS";
    case OrderStatus::EXPIRED:
        return "Expired";
    case OrderStatus::ACCEPTED_FOR_BIDDING:
        return "UNKNOWN_ORDER_STATUS";
    case OrderStatus::PENDING_REPLACE:
        return "EDIT_QUEUED";
    }
    return "UNKNOWN_ORDER_STATUS";
}

inline std::string to_string(TimeInForce tif) {
    switch(tif) {
    case TimeInForce::GOOD_TILL_DATE:
        return "GOOD_UNTIL_DATE_TIME";
    case TimeInForce::GOOD_TILL_CANCEL:
        return "GOOD_UNTIL_CANCELLED";
    case TimeInForce::IMMEDIATE_OR_CANCEL:
        return "IMMEDIATE_OR_CANCEL";
    case TimeInForce::FILL_OR_KILL:
        return "FILL_OR_KILL";
    default:
        return "UNKNOWN_TIME_IN_FORCE";
    }
}

inline json to_json(const Order& order) {
    json order_config;
    if (order.type == OrderType::MARKET) {
        if (order.time_in_force == TimeInForce::IMMEDIATE_OR_CANCEL) {
            order_config = {
                {
                    "market_market_ioc", { 
                        {"base_size", std::to_string(order.qty_double())}
                    }
                }
            };
        }
        else if (order.time_in_force == TimeInForce::FILL_OR_KILL) {
            order_config = {
                {
                    "market_market_fok", { 
                        {"base_size", std::to_string(order.qty_double())}
                    }
                }
            };
        }
    }
    else if (order.type == OrderType::LIMIT) {
        if (order.time_in_force == TimeInForce::GOOD_TILL_CANCEL) {
            order_config = {
                {
                    "limit_limit_gtc", { 
                        {"base_size", std::to_string(order.qty_double())},
                        {"limit_price", std::to_string(order.price_double())},
                        {"post_only", order.post_only},
                    }
                }
            };
        }
        // TODO:
        // else if (order.time_in_force == TimeInForce::FILL_OR_KILL) {
        //     order_config = {
        //         {
        //             "market_market_fok", { 
        //                 {"base_size", std::to_string(order.qty_double())},
        //                 {"limit_price", std::to_string(order.price_double())},
        //                 {"end_time", order.end_time},
        //                 {"post_only", order.post_only},
        //             }
        //         }
        //     };
        // }
        else if (order.time_in_force == TimeInForce::IMMEDIATE_OR_CANCEL) {
            order_config = {
                {
                    "sor_limit_ioc", { 
                        {"base_size", std::to_string(order.qty_double())},
                        {"limit_price", std::to_string(order.price_double())}
                    }
                }
            };
        }
        else if (order.time_in_force == TimeInForce::FILL_OR_KILL) {
            order_config = {
                {
                    "market_market_fok", { 
                        {"base_size", std::to_string(order.qty_double())},
                        {"limit_price", std::to_string(order.price_double())},
                    }
                }
            };
        }
    }

    return {
        {"order_id", order.order_id},
        {"product_id", order.symbol},
        {"user_id", order.user_id},
        {"order_configuration", order_config},
        {"side", to_string(order.side)},
        {"client_order_id", order.client_order_id},
        {"status", to_string(order.status)},
        {"time_in_force", to_string(order.time_in_force)},
        {"created_time", utils::format_timestamp_iso8601()},
        {"completion_percentage", std::to_string(order.filled_qty_double() / order.qty_double() * 100)},
        {"filled_size", std::to_string(order.filled_qty_double())},
        {"average_filled_price", std::to_string(order.avg_filled_price_double())},
        {"number_of_fills", std::to_string(order.num_fills)},
        {"fee", std::to_string(order.fee)},
        {"filled_value", std::to_string(order.filled_value)},
        {"pending_cancel", order.pending_cancel},
        {"size_in_quote", false},
        {"total_fee", std::to_string(order.fee)},
        {"size_inclusive_of_fees", false},
        {"total_value_after_fees", std::to_string(order.filled_value - order.fee)},
        {"trigger_status", "UNKNOWN_TRIGGER_STATUS"},
        {"order_type", to_string(order.type)},
        {"reject_reason", ""},
        {"settled", false},
        {"product_type", to_string(order.product_type)},
        {"reject_message", order.reject_message},
        {"cancel_message", order.cancel_message},
        {"order_placement_source", "UNKNOWN_PLACEMENT_SOURCE"},
        {"outstanding_hold_amount", ""},
        {"is_liquidation", false},
        {"last_fill_time", (order.last_fill_time.has_value() ? utils::format_timestamp_iso8601(order.last_fill_time.value()) : "")},
        {"edit_history", order.edit_history},
        {"leverage", ""},
        {"margin_type", "CROSS"},
        {"attached_order_configuration", {{}}},
        {"current_pending_replace", {{}}},
        {"commission_detail_total", {{}}},
        {"workable_size", std::to_string(order.leaves_qty_double())},
        {"workable_size_completion_pct", std::to_string(order.leaves_qty_double() / order.qty_double() * 100)},
    };
}

}   // end namespace slick::sim::order_gateway