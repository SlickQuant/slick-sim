#include "coinbase_ws_client_manager.hpp"
#include <utils/timestamp.hpp>
#include <unordered_map>
#include <coinbase/rest.hpp>
#include <coinbase/order.hpp>

using CoinbaseRestClient = coinbase::CoinbaseRestClient;
using namespace slick::sim::order_gateway::coinbase;
using namespace slick::sim::utils;

namespace {
    coinbase::OrderStatus to_coinbase_order_status(slick::sim::OrderStatus status) {
        switch (status) {
            case slick::sim::OrderStatus::NEW: return coinbase::OrderStatus::OPEN;
            case slick::sim::OrderStatus::PENDING_NEW: return coinbase::OrderStatus::PENDING;
            case slick::sim::OrderStatus::PENDING_REPLACE: return coinbase::OrderStatus::EDIT_QUEUED;
            case slick::sim::OrderStatus::PENDING_CANCEL: return coinbase::OrderStatus::CANCEL_QUEUED;
            case slick::sim::OrderStatus::REPLACED: return coinbase::OrderStatus::OPEN;
            case slick::sim::OrderStatus::CANCELED: return coinbase::OrderStatus::CANCELLED;
            case slick::sim::OrderStatus::FILLED: return coinbase::OrderStatus::FILLED;
            case slick::sim::OrderStatus::PARTIALLY_FILLED: return coinbase::OrderStatus::FILLED;
            case slick::sim::OrderStatus::EXPIRED: return coinbase::OrderStatus::EXPIRED;
            case slick::sim::OrderStatus::REJECTED: return coinbase::OrderStatus::FAILED;
            default: return coinbase::OrderStatus::UNKNOWN_ORDER_STATUS;
        }
    }
}

CoinbaseWebsocketClientManager::CoinbaseWebsocketClientManager(const json& config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<OrderResponse> &response_queue)
    : ClientManager(config, request_queue, response_queue)
    , port_(config_.value("port", 3001))
    , ping_interval_ms_(config_.value("ping_interval", 30) * 1000)
    , pong_timeout_(config_.value("pong_timeout", 60))
    , response_read_index_(response_queue_.initial_reading_index())
{
    auto rest_api = CoinbaseRestClient();
    fee_rate_ = rest_api.get_taker_fee_rate();
}

void CoinbaseWebsocketClientManager::handleOrderResponse(const OrderResponse &response, wsT *ws) {
    // Serialize order response to JSON
    if (response.order_status == OrderStatus::PENDING_NEW) {
        // Don't send pending new updates to client - wait for final status update
        return;
    }
    auto filled_value = to_price_double(response.avg_fill_price) * to_qty_double(response.cum_qty);
    auto fees = filled_value * fee_rate_;
    auto *user_data = ws->getUserData();
    json response_json = {
        {"channel", "user"},
        {"client_id", ""},
        {"timestamp", format_timestamp_iso8601(9)},
        {"sequence_num", user_data->seq_num++},
        {"events", json::array({
            json::object({
                {"type", "update"},
                {"orders", json::array({
                    json::object({
                        {"avg_price", std::to_string(to_price_double(response.avg_fill_price))},
                        {"client_order_id", std::string(response.client_order_id)},
                        {"completion_percentage", std::to_string(response.qty > 0 ? (to_qty_double(response.cum_qty) / response.qty) * 100 : 0.0)},
                        {"contract_expiry_type", "UNKNOWN_CONTRACT_EXPIRY_TYPE"},
                        {"cumulative_quantity", std::to_string(to_qty_double(response.cum_qty))},
                        {"filled_value", std::to_string(filled_value)},
                        {"leaves_quantity", std::to_string(to_qty_double(response.leaves_qty))},
                        {"limit_price", std::to_string(to_price_double(response.price))},
                        {"number_of_fills", std::to_string(response.last_qty > 0 ? response.cum_qty / response.last_qty : 0)},
                        {"order_id", std::string(response.order_id)},
                        {"order_side", response.side == Side::BUY ? "BUY" : "SELL"},
                        {"order_type", to_string(response.order_type)},
                        {"post_only", response.post_only ? "true" : "false"},
                        {"product_id", std::string(response.symbol)},
                        {"reject_reason", to_string(response.reject_reason)},
                        {"status", to_string(to_coinbase_order_status(response.order_status))},
                        {"time_in_force", to_string(response.time_in_force)},
                        {"total_fees", std::to_string(fees)},
                        {"total_value_after_fees", std::to_string(filled_value - fees)},
                        {"trigger_status", "INVALID_ORDER_TYPE"},
                        {"creation_time", format_timestamp_iso8601(response.creation_time, 9)},
                        {"end_time", "0001-01-01T00:00:00Z"},
                        {"start_time", "0001-01-01T00:00:00Z"}
                    })
                })}
            })
        })}
    };

    // Send to client via WebSocket
    ws->send(response_json.dump(), uWS::OpCode::TEXT);
}
