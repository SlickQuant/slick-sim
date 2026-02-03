#include "client_manager.hpp"
#include <uwebsockets/App.h>
// #include <slick/net/websocket.h>
#include <nlohmann/json.hpp>
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>
#include <format>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "coinbase_common.hpp"

struct us_listen_socket_t;
using json = nlohmann::json;

namespace slick::sim::order_gateway::coinbase {

// Per-socket data structure for uWebSockets
struct PerSocketData {
    std::string sub;  // JWT sub claim
    std::string user_id;
    uint64_t seq_num;
    std::chrono::steady_clock::time_point last_pong;  // Track last pong received
    bool authenticated = false;
    bool subscribed_heartbeat = false;
    static uint_fast64_t heartbeat_count;
};

class CoinbaseWebsocketClientManager : public ClientManager {
    std::thread ws_thread_;
    us_listen_socket_t* listen_socket_ = nullptr;
    uWS::Loop* loop_ = nullptr;
    us_timer_t* heartbeat_timer_ = nullptr;
    uint32_t port_ = 3000;
    std::atomic<bool> running_{false};

    // Response queue read index
    uint64_t response_read_index_ = 0;

    // Client connection mapping: user_id -> WebSocket connection
    // No mutex needed - all access is from the single event loop thread
    std::unordered_map<std::string, uWS::WebSocket<false, true, PerSocketData>*> clients_;

    // Heartbeat configuration
    int ping_interval_ms_{15000};  // Send ping every 30 seconds (in milliseconds)
    std::chrono::seconds pong_timeout_{60};   // Disconnect if no pong for 60 seconds

    std::string products_;
    std::unordered_map<std::string, std::string> product_by_id_;
    std::unordered_map<std::string, Order> orders_;
    std::unordered_multimap<std::string, Order> orders_by_client_id_;
    std::unordered_multimap<std::string, Fill> order_fills_;

public:
    CoinbaseWebsocketClientManager(const json& config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<OrderResponse> &response_queue)
        : ClientManager(config, request_queue, response_queue)
        , port_(config_.value("port", 3001))
        , ping_interval_ms_(config_.value("ping_interval", 30) * 1000)
        , pong_timeout_(config_.value("pong_timeout", 60))
        , response_read_index_(response_queue_.initial_reading_index())
    {
    }
    ~CoinbaseWebsocketClientManager() override {
        stop();
    }

    void start() override {
        running_.store(true, std::memory_order_release);

        // Start WebSocket server thread (handles everything in event loop)
        ws_thread_ = std::thread([this]() {
            auto app = uWS::App();

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

                    .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
                        handleMessage(ws, message);
                    },

                    .drain = [](auto *ws) {
                        // Check backpressure
                    },

                    .ping = [](auto *ws, std::string_view message) {
                        // Automatically respond to client ping with pong
                        // uWebSockets handles this automatically, but we log it
                        LOG_TRACE("Received ping from client");
                    },

                    .pong = [this](auto *ws, std::string_view message) {
                        // Client responded to our ping
                        ws->getUserData()->last_pong = std::chrono::steady_clock::now();
                        LOG_DEBUG("Received pong from client");
                    },

                    .close = [this](auto *ws, int code, std::string_view message) {
                        handleClose(ws, code, message);
                    }
                })
                .listen(port_, [this, &app](auto *listen_socket) {
                    listen_socket_ = listen_socket;
                    if (listen_socket) {
                        LOG_INFO("Coinbase WebSocket OrderGateway started on port {}", port_);

                        // Get the event loop
                        loop_ = uWS::Loop::get();

                        // Create heartbeat timer in the event loop
                        // Use defer() to create timer with proper lambda capture
                        loop_->defer([this]() {
                            heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0, 0);

                            // Store 'this' pointer in timer extension data
                            *((CoinbaseWebsocketClientManager**)us_timer_ext(heartbeat_timer_)) = this;

                            // Set timer callback that retrieves 'this' from extension data
                            us_timer_set(heartbeat_timer_, [](struct us_timer_t *timer) {
                                // Dereference the pointer stored in extension data
                                auto* self = *((CoinbaseWebsocketClientManager**)us_timer_ext(timer));
                                self->checkHeartbeats();
                            }, ping_interval_ms_, ping_interval_ms_);
                        });

                        LOG_INFO("Event loop configured (heartbeat: {}ms)", ping_interval_ms_);

                        // Start continuous response processing
                        scheduleResponseProcessing();

                    } else {
                        LOG_ERROR("Failed to listen on port {}", port_);
                    }
                })
                .run();
        });
    }

    void stop() override {
        running_.store(false, std::memory_order_release);

        // Close heartbeat timer
        if (heartbeat_timer_) {
            us_timer_close(heartbeat_timer_);
            heartbeat_timer_ = nullptr;
        }

        // Close WebSocket server
        if (listen_socket_) {
            us_listen_socket_close(0, listen_socket_);
            listen_socket_ = nullptr;
        }

        // Wait for WebSocket thread to finish
        if (ws_thread_.joinable()) {
            ws_thread_.join();
        }

        // Clear client connections (no lock needed - thread already joined)
        clients_.clear();
    }

private:
    // Handle incoming WebSocket messages
    void handleMessage(uWS::WebSocket<false, true, PerSocketData> *ws, std::string_view message) {
        try {
            json msg = json::parse(message);

            // Check if this is a subscription message
            if (msg.contains("type") && msg["type"] == "subscribe") {
                // Extract JWT token from message
                if (!msg.contains("jwt")) {
                    sendError(ws, "Missing JWT token");
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
                            auto range = orders_by_client_id_.equal_range(user_data->user_id);
                            for (auto it = range.first; it != range.second; ++it) {
                                orders.push_back(to_json(it->second));
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
                    sendError(ws, std::string("JWT verification failed: ") + e.what());
                    ws->close();
                } catch (const std::exception& e) {
                    sendError(ws, std::string("JWT decode error: ") + e.what());
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
            sendError(ws, std::string("Invalid JSON: ") + e.what());
        }
    }

    // Handle WebSocket close event
    void handleClose(uWS::WebSocket<false, true, PerSocketData> *ws, int code, std::string_view message) {
        auto &user_id = ws->getUserData()->user_id;

        if (!user_id.empty()) {
            // Remove client from map (no lock needed - single threaded)
            clients_.erase(user_id);
            LOG_INFO("Client disconnected: user_id={}, code={}", user_id, code);
        } else {
            LOG_INFO("Client disconnected before authentication, code={}", code);
        }
    }

    // Send error message to client
    void sendError(uWS::WebSocket<false, true, PerSocketData> *ws, const std::string& error_msg) {
        json error = {
            {"type", "error"},
            {"message", error_msg}
        };
        ws->send(error.dump(), uWS::OpCode::TEXT);
        LOG_ERROR("WebSocket error: {}", error_msg);
    }

    // Schedule response processing to run in the event loop
    void scheduleResponseProcessing() {
        if (!running_.load(std::memory_order_acquire) || !loop_) {
            return;
        }

        loop_->defer([this]() {
            processAllResponses();
        });
    }

    // Process all available responses from the queue
    // Called continuously via defer() or after WebSocket events
    void processAllResponses() {
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
                json response_json; // = to_json(response);

                // Send to client via WebSocket
                client_ws->send(response_json.dump(), uWS::OpCode::TEXT);

                LOG_INFO("Sent order response to user_id={}, client_order_id={}",
                         user_id, response.client_order_id);
            } else {
                LOG_WARN("No WebSocket connection found for user_id={}", user_id);
            }

            processed++;
        }

        // Reschedule ourselves to run again on next event loop iteration
        // schedule_response_processing() checks running_ flag
        scheduleResponseProcessing();
    }

    // Heartbeat check - called by timer in event loop
    void checkHeartbeats() {
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
                ws->close();  // Close the connection
                it = clients_.erase(it);
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
                LOG_DEBUG("Sent heartbeat to client {}", user_data->user_id);
                ++it;
            }
        }

        if (!disconnected_clients.empty()) {
            LOG_INFO("Disconnected {} stale clients", disconnected_clients.size());
        }
    }
};

inline uint_fast64_t PerSocketData::heartbeat_count = 0;

}