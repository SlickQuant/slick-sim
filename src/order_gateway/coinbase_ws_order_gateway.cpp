#include <slick/logger.hpp>
#include "coinbase_ws_order_gateway.hpp"
#include <utils/timestamp.hpp>
#include <coinbase/rest.hpp>
#include <coinbase/order.hpp>
#include "coinbase_common.hpp"

using CoinbaseRestClient = coinbase::CoinbaseRestClient;
using namespace slick::sim::order_gateway;
using namespace slick::sim::order_gateway::coinbase;
using namespace slick::sim::utils;

namespace {
    ::coinbase::OrderStatus to_coinbase_order_status(slick::sim::OrderStatus status) {
        switch (status) {
            case slick::sim::OrderStatus::NEW: return ::coinbase::OrderStatus::OPEN;
            case slick::sim::OrderStatus::PENDING_NEW: return ::coinbase::OrderStatus::PENDING;
            case slick::sim::OrderStatus::PENDING_REPLACE: return ::coinbase::OrderStatus::EDIT_QUEUED;
            case slick::sim::OrderStatus::PENDING_CANCEL: return ::coinbase::OrderStatus::CANCEL_QUEUED;
            case slick::sim::OrderStatus::REPLACED: return ::coinbase::OrderStatus::OPEN;
            case slick::sim::OrderStatus::CANCELED: return ::coinbase::OrderStatus::CANCELLED;
            case slick::sim::OrderStatus::FILLED: return ::coinbase::OrderStatus::FILLED;
            case slick::sim::OrderStatus::PARTIALLY_FILLED: return ::coinbase::OrderStatus::FILLED;
            case slick::sim::OrderStatus::EXPIRED: return ::coinbase::OrderStatus::EXPIRED;
            case slick::sim::OrderStatus::REJECTED: return ::coinbase::OrderStatus::FAILED;
            default: return ::coinbase::OrderStatus::UNKNOWN_ORDER_STATUS;
        }
    }
}


uint_fast64_t PerSocketData::heartbeat_count = 0;

CoinbaseWebsocketOrderGateway::CoinbaseWebsocketOrderGateway(const json& config, slick::queue<Request> &request_queue, slick::queue<OrderResponse> &response_queue)
    : RestWsOrderGateway(Venue::COINBASE, request_queue, response_queue, config.value("port", 3001))
    , response_read_index_(response_queue_.initial_reading_index())
    , ping_interval_ms_(config.value("ping_interval", 30) * 1000)
    , pong_timeout_(config.value("pong_timeout", 60))
{
    auto rest_api = CoinbaseRestClient();
    fee_rate_ = rest_api.get_taker_fee_rate();
}

void CoinbaseWebsocketOrderGateway::setup_routes(uWS::App &app) {
    app.ws<PerSocketData>("/*", {
        /* WebSocket settings */
        .compression = uWS::SHARED_COMPRESSOR,
        .maxPayloadLength = 16 * 1024,
        .idleTimeout = 120,
        .maxBackpressure = 1 * 1024 * 1024,

        /* WebSocket handlers */
        .upgrade = nullptr,

        .open = [this](auto *ws) {
            // Initialize connection timestamp
            ws->getUserData()->last_pong = std::chrono::steady_clock::now();
            ws->getUserData()->authenticated = false;
            LOG_INFO("WebSocket client connected");
        },

        .message = [this](auto *ws, std::string_view message, uWS::OpCode /* opCode */) {
            handle_message(ws, message);
        },

        .drain = [](auto */* ws */) {
            // Check backpressure
        },

        .ping = [](auto *ws, std::string_view /* message */) {
            // Automatically respond to client ping with pong
            // uWebSockets handles this automatically, but we log it
            LOG_TRACE("Received ping from client {:p}", (void*)ws);
        },

        .pong = [this](auto *ws, std::string_view /* message */) {
            // Client responded to our ping
            ws->getUserData()->last_pong = std::chrono::steady_clock::now();
            LOG_TRACE("Received pong from client {:p}", (void*)ws);
        },

        .close = [this](auto *ws, int code, std::string_view message) {
            handle_close(ws, code, message);
        }
    });
}

void CoinbaseWebsocketOrderGateway::on_listen_socket([[maybe_unused]] us_listen_socket_t *listen_socket) {
    loop_->defer([this]() {
        // The third argument is ext_size, not a flag: it is the number of bytes
        // us_timer_ext() hands back. Passing 0 and then writing a pointer there
        // overruns the timer allocation.
        heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0,
                                           sizeof(CoinbaseWebsocketOrderGateway*));

        // Store 'this' pointer in timer extension data
        *((CoinbaseWebsocketOrderGateway**)us_timer_ext(heartbeat_timer_)) = this;

        // Set timer callback that retrieves 'this' from extension data
        us_timer_set(heartbeat_timer_, [](struct us_timer_t *timer) {
            // Dereference the pointer stored in extension data
            auto* self = *((CoinbaseWebsocketOrderGateway**)us_timer_ext(timer));
            self->check_heartbeats();
        }, ping_interval_ms_, ping_interval_ms_);
    });

    LOG_INFO("COINBASE Websocket OrderGateway Event loop configured (heartbeat: {}ms)", ping_interval_ms_);

    // Start continuous response processing
    schedule_response_processing();
}

void CoinbaseWebsocketOrderGateway::handle_message(wsT *ws, std::string_view message) {
    try {
        json msg = json::parse(message);

        // Check if this is a subscription message
        if (msg.contains("type") && msg["type"] == "subscribe") {
            // Extract JWT token from message
            if (!msg.contains("jwt")) {
                send_error(ws, "Missing JWT token");
                return;
            }

            auto &token = msg["jwt"].get_ref<std::string&>();

            // Verify and decode JWT
            try {
                auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
                // auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
                //     .allow_algorithm(jwt::algorithm::hs256{jwt_secret_})
                //     .with_issuer("coinbase");
                // verifier.verify(decoded);

                // Extract 'sub' claim
                std::string sub = decoded.get_subject();

                // Store user_id in per-socket data
                auto *user_data = ws->getUserData();
                user_data->sub = sub;
                user_data->user_id = sub.substr(sub.rfind('/') + 1);
                user_data->authenticated = true;

                if (!clients_.contains(user_data->user_id)) {
                    user_data->seq_num = 0;
                    clients_[user_data->user_id] = ws;
                }

                auto &channel = msg["channel"].get_ref<std::string&>();

                LOG_INFO("Client {} subscribe channel {}", sub, channel);

                if (channel == "user") {
                    // Send subscription confirmation
                    {
                        json orders = json::array();
                        for (const Order *order : orders_.orders_of(user_data->user_id)) {
                            orders.push_back(to_json(*order));
                        }
                        json response = {
                            {"channel", "user"},
                            {"client_id", ""},
                            {"timestamp", utils::format_timestamp_iso8601(9)},
                            {"sequence_num", user_data->seq_num++},
                            {"events", {{
                                {"type", "snapshot"},
                                {"orders", orders},
                                {"positions",  {
                                    {"perpetual_futures_positions", json::array()},
                                    {"expiring_futures_positions", json::array()}
                                }}
                            }}}
                        };
                        ws->send(response.dump(), uWS::OpCode::TEXT);
                    }
                    {
                        json response = {
                            {"channel", "subscriptions"},
                            {"client_id", ""},
                            {"timestamp", utils::format_timestamp_iso8601(9)},
                            {"sequence_num", user_data->seq_num++},
                            {"events", {{
                                {"subscriptions",  {
                                    {"user", {user_data->user_id}}
                                }}
                            }}}
                        };

                        ws->send(response.dump(), uWS::OpCode::TEXT);
                    }
                }
                else if (channel == "heartbeats") {
                    user_data->subscribed_heartbeat = true;
                    json response = {
                        {"channel", "subscriptions"},
                        {"client_id", ""},
                        {"timestamp", utils::format_timestamp_iso8601(9)},
                        {"sequence_num", user_data->seq_num++},
                        {"events", {{
                            {"subscriptions", {
                                {"heartbeats", {"heartbeats"}}
                            }}
                        }}}
                    };
                    ws->send(response.dump(), uWS::OpCode::TEXT);
                }
                else if (channel == "futures_balance_summary") {
                    json response = {
                        {"channel", "futures_balance_summary"},
                        {"client_id", ""},
                        {"timestamp", utils::format_timestamp_iso8601(9)},
                        {"sequence_num", user_data->seq_num++},
                        {"events", {{
                            {"type", "snapshot"},
                            {"fcm_balance_summary", {
                                {"futures_buying_power", "10000.00"},
                                {"total_usd_balance", "3611.86"},
                                {"cbi_usd_balance", "2051.75"},
                                {"cfm_usd_balance", "1560.11"},
                                {"total_open_orders_hold_amount", "0"},
                                {"unrealized_pnl", "0"},
                                {"daily_realized_pnl", "0"},
                                {"initial_margin","460.14"},
                                {"available_margin", "5060.76"},
                                {"liquidation_threshold", "345.11"},
                                {"liquidation_buffer_amount", "4715.66"},
                                {"liquidation_buffer_percentage", "1000"},
                                {"intraday_margin_window_measure", {
                                    {"margin_window_type", "FCM_MARGIN_WINDOW_TYPE_INTRADAY"},
                                    {"margin_level","MARGIN_LEVEL_TYPE_BASE"},
                                    {"initial_margin","460.14"},
                                    {"maintenance_margin","345.11"},
                                    {"liquidation_buffer_percentage", "1000"},
                                    {"total_hold","0"},
                                    {"futures_buying_power","10000.00"}
                                }},
                                {"overnight_margin_window_measure", {
                                    {"margin_window_type","FCM_MARGIN_WINDOW_TYPE_OVERNIGHT"},
                                    {"margin_level","MARGIN_LEVEL_TYPE_BASE"},
                                    {"initial_margin","1497.85"},
                                    {"maintenance_margin","1318.11"},
                                    {"liquidation_buffer_percentage","1000"},
                                    {"total_hold","0"},
                                    {"futures_buying_power","2293.45"}
                                }}
                            }}
                        }}}
                    };
                    ws->send(response.dump(), uWS::OpCode::TEXT);
                }
                else {
                    json response = {
                        {"type", "error"},
                        {"message", std::format("channel `{}` is not supported", channel)},
                    };
                    ws->send(response.dump(), uWS::OpCode::TEXT);
                }
            } catch (const jwt::error::token_verification_exception& e) {
                send_error(ws, std::string("JWT verification failed: ") + e.what());
                ws->close();
            } catch (const std::exception& e) {
                send_error(ws, std::string("JWT decode error: ") + e.what());
                ws->close();
            }
        }
        else {
            json response = {
                {"type", "error"},
                {"message", std::format("type `{}` is not supported", msg.value<std::string>("type", ""))},
            };
            ws->send(response.dump(), uWS::OpCode::TEXT);
        }
    } catch (const json::parse_error& e) {
        send_error(ws, std::string("Invalid JSON: ") + e.what());
    }
}

void CoinbaseWebsocketOrderGateway::handle_close(wsT *ws, int code, std::string_view message) {
    auto &user_id = ws->getUserData()->user_id;

    if (!user_id.empty()) {
        // Remove client from map (no lock needed - single threaded)
        clients_.erase(user_id);
        LOG_INFO("Client disconnected: user_id={}, code={}, msg={}", user_id, code, message);
    } else {
        LOG_INFO("Client disconnected before authentication, code={}, msg={}", code, message);
    }
}

void CoinbaseWebsocketOrderGateway::send_error(wsT *ws, const std::string& error_msg) {
    json error = {
        {"type", "error"},
        {"message", error_msg}
    };
    ws->send(error.dump(), uWS::OpCode::TEXT);
    LOG_ERROR("WebSocket error: {}", error_msg);
}

void CoinbaseWebsocketOrderGateway::process_all_responses() {
    // Process up to 100 responses per call to avoid blocking event loop too long
    constexpr int max_batch = 100;
    int processed = 0;

    while (processed < max_batch) {
        // Try to read from the queue (non-blocking)
        auto [data, size] = response_queue_.read(response_read_index_);

        if (data == nullptr) {
            // No more data available
            break;
        }

        // Process the order response
        auto& response = *data;

        // Record it before anything else. This has to happen whether or not the
        // owner is connected: a client that subscribes later still expects its
        // earlier orders in the snapshot, and this is the only pass over the stream.
        orders_.apply(response);

        // Extract user_id from the order response
        std::string user_id = response.user_id;

        if (user_id.empty()) {
            LOG_WARN("Order response has empty user_id, skipping");
            processed++;
            continue;
        }

        // Find the WebSocket connection for this user (no lock needed - single threaded)
        auto it = clients_.find(user_id);
        if (it != clients_.end()) {
            auto* client_ws = it->second;
            // Serialize order response to JSON
            handle_order_response(response, client_ws);

            LOG_INFO("Sent order response {} to user_id={}, client_order_id={}, order_status={}",
                        to_string(response.exec_type), user_id, response.client_order_id, to_string(response.order_status));
        } else {
            LOG_WARN("No WebSocket connection found for user_id={}", user_id);
        }

        processed++;
    }

    // Reschedule ourselves to run again on next event loop iteration
    // schedule_response_processing() checks running_ flag
    schedule_response_processing();
}

void CoinbaseWebsocketOrderGateway::handle_order_response(const OrderResponse &response, wsT *ws) {
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

void CoinbaseWebsocketOrderGateway::check_heartbeats() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> disconnected_clients;

    // No lock needed - single threaded event loop
    for (auto it = clients_.begin(); it != clients_.end();) {
        auto* ws = it->second;
        auto* user_data = ws->getUserData();

        // Check if client has responded to pings
        auto time_since_pong = std::chrono::duration_cast<std::chrono::seconds>(
            now - user_data->last_pong);

        if (time_since_pong > pong_timeout_) {
            // Client hasn't responded to pings, close connection
            LOG_WARN("Client {} failed heartbeat check (last_pong: {}s ago), disconnecting",
                        user_data->user_id, time_since_pong.count());

            disconnected_clients.push_back(it->first);
            it = clients_.erase(it);
            ws->close();  // Close the connection
        } else {
            // Send ping to client (only if authenticated)
            if (user_data->subscribed_heartbeat) {
                // Send a text-based ping message (Coinbase style)
                json event = {
                    {"current_time", utils::format_heatbeat_timestamp()},
                    {"heartbeat_counter", user_data->heartbeat_count++}
                };
                json ping_msg = {
                    {"channel", "heartbeats"},
                    {"client_id", ""},
                    {"timestamp", utils::format_timestamp_iso8601(9)},
                    {"sequence_num", user_data->seq_num++},
                    {"events", {event}}
                };
                ws->send(ping_msg.dump(), uWS::OpCode::TEXT);
            }
            // Also send protocol-level ping
            ws->send("", uWS::OpCode::PING);
            LOG_TRACE("Sent heartbeat to client {}", user_data->user_id);
            ++it;
        }
    }

    if (!disconnected_clients.empty()) {
        LOG_INFO("Disconnected {} stale clients", disconnected_clients.size());
    }
}
