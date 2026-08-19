#include <slick/logger.hpp>
#include "coinbase_publisher.hpp"
#include "coinbase_trade_encoder.hpp"
#include <common/symbol_manager.hpp>
#include <utils/fixed_string.hpp>
#include <utils/timestamp.hpp>

using namespace slick::sim::md_publisher;
using namespace slick::sim;
using namespace slick::sim::utils;

namespace {
    auto &symbol_mgr = SymbolManager::instance();
}

uint_fast64_t PerSocketData::heartbeat_count = 0;

CoinbasePublisher::CoinbasePublisher(const nlohmann::json &config, slick::queue<Request> &request_queue, slick::queue<uint8_t> &market_data_queue)
    : WebsocketMarketDataPublisher(Venue::COINBASE, request_queue, market_data_queue, config.value("port", 5000))
{
}

// void CoinbasePublisher::start() {
//     running_.store(true, std::memory_order_release);
//     // Start WebSocket server thread (handles everything in event loop)
//     ws_thread_ = std::thread([this]() {
//         auto app = uWS::App();

//         app.ws<PerSocketData>("/*", {
//                 /* WebSocket settings */
//                 .compression = uWS::SHARED_COMPRESSOR,
//                 .maxPayloadLength = 2 << 24,
//                 .idleTimeout = 120,
//                 .maxBackpressure = 2 << 25,

//                 /* WebSocket handlers */
//                 .upgrade = nullptr,

//                 .open = [this](auto *ws) {
//                     // Initialize connection timestamp
//                     ws->getUserData()->last_pong = std::chrono::steady_clock::now();
//                     clients_.emplace(ws);
//                     LOG_INFO("CoinbasePublisher WebSocket client connected");
//                 },

//                 .message = [this](auto *ws, std::string_view message, uWS::OpCode /* opCode */) {
//                     handleMessage(ws, message);
//                 },

//                 .drain = [](auto */* ws */) {
//                     // Check backpressure
//                 },

//                 .ping = [](auto *ws, std::string_view /* message */) {
//                     // Automatically respond to client ping with pong
//                     // uWebSockets handles this automatically, but we log it
//                     LOG_TRACE("CoinbasePublisher Received ping from client {:p}", (void*)ws);
//                 },

//                 .pong = [this](auto *ws, std::string_view /* message */) {
//                     // Client responded to our ping
//                     ws->getUserData()->last_pong = std::chrono::steady_clock::now();
//                     LOG_TRACE("CoinbasePublisher Received pong from client {:p}", (void*)ws);
//                 },

//                 .close = [this](auto *ws, int code, std::string_view message) {
//                     handleClose(ws, code, message);
//                 }
//             })
//             .listen(port_, [this, &app](auto *listen_socket) {
//                 listen_socket_ = listen_socket;
//                 if (listen_socket) {
//                     LOG_INFO("CoinbasePublisher started on port {}", port_);

//                     // Get the event loop
//                     loop_ = uWS::Loop::get();

//                     // Create heartbeat timer in the event loop
//                     // Use defer() to create timer with proper lambda capture
//                     loop_->defer([this]() {
//                         heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0, 0);

//                         // Store 'this' pointer in timer extension data
//                         *((CoinbasePublisher**)us_timer_ext(heartbeat_timer_)) = this;

//                         // Set timer callback that retrieves 'this' from extension data
//                         us_timer_set(heartbeat_timer_, [](struct us_timer_t *timer) {
//                             // Dereference the pointer stored in extension data
//                             auto* self = *((CoinbasePublisher**)us_timer_ext(timer));
//                             self->checkHeartbeats();
//                         }, ping_interval_ms_, ping_interval_ms_);
//                     });

//                     LOG_INFO("CoinbasePublisher Event loop configured (heartbeat: {}ms)", ping_interval_ms_);

//                     // Start continuous response processing
//                     schedulePublishProcessing();

//                 } else {
//                     LOG_ERROR("CoinbasePublisher Failed to listen on port {}", port_);
//                 }
//             })
//             .run();
//     });
// }

void CoinbasePublisher::setup_routes(uWS::App &app) {
    app.ws<PerSocketData>("/*", {
        /* WebSocket settings */
        .compression = uWS::SHARED_COMPRESSOR,
        .maxPayloadLength = 1 << 24,
        .idleTimeout = 120,
        .maxBackpressure = 1 << 25,

        /* WebSocket handlers */
        .upgrade = nullptr,

        .open = [this](auto *ws) {
            // Initialize connection timestamp
            ws->getUserData()->last_pong = std::chrono::steady_clock::now();
            clients_.emplace(ws);
            LOG_INFO("CoinbasePublisher WebSocket client connected");
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
            LOG_TRACE("CoinbasePublisher Received ping from client {:p}", (void*)ws);
        },

        .pong = [this](auto *ws, std::string_view /* message */) {
            // Client responded to our ping
            ws->getUserData()->last_pong = std::chrono::steady_clock::now();
            LOG_TRACE("CoinbasePublisher Received pong from client {:p}", (void*)ws);
        },

        .close = [this](auto *ws, int code, std::string_view message) {
            handle_close(ws, code, message);
        }
    });
}

// void CoinbasePublisher::stop() {
//     running_.store(false, std::memory_order_release);

//     // Close heartbeat timer
//     if (heartbeat_timer_) {
//         us_timer_close(heartbeat_timer_);
//         heartbeat_timer_ = nullptr;
//     }

//     if (listen_socket_ && loop_) {
//         // Closing the listen socket must happen on the loop's own thread;
//         // doing it cross-thread races with the loop's poll handles.
//         loop_->defer([this]() {
//             if (listen_socket_) {
//                 us_listen_socket_close(0, listen_socket_);
//                 listen_socket_ = nullptr;
//             }
//         });
//     }

//     if (ws_thread_.joinable()) {
//         ws_thread_.join();
//     }
// }

// void CoinbasePublisher::schedulePublishProcessing() {
//     if (!running_.load(std::memory_order_acquire) || !loop_) {
//         return;
//     }

//     loop_->defer([this]() {
//         publishMarketDataUpdate();
//     });
// }

void CoinbasePublisher::publish_market_data_update(MarketDataUpdate *update) {
    switch(update->type) {
        case MDUpdateType::BOOK:
            break;
        case MDUpdateType::LEVEL:
            publish_level_update(update);
            break;
        case MDUpdateType::ORDER:
            publish_order_update(update);
            break;
        case MDUpdateType::SUB_RESPONSE:
            publish_subscription_response(update);
            break;
        case MDUpdateType::BOOK_SNAPSHOT:
            publish_level_snapshot(update);
            break;
        case MDUpdateType::TRADE_SUMMARY:
            publish_trade_summary(update);
            break;
        default:
            LOG_WARN("Received unknown or invalid market data update type: {}", static_cast<int>(update->type));
            break;
    }
}

void CoinbasePublisher::handle_message(wsT *ws, std::string_view message) {
    try {
        json msg = json::parse(message);

        // Check if this is a subscription message
        if (msg.contains("type") && msg["type"] == "subscribe") {

            auto *user_data = ws->getUserData();

            if (!clients_.contains(ws)) {
                user_data->seq_num = 0;
                clients_.emplace(ws);
            }

            auto &channel = msg["channel"].get_ref<std::string&>();

            if (channel == "level2") {
                for (auto &prod : msg["product_ids"]) {
                    LOG_INFO("Client {:p} subscribe level2: {}", ws, prod.get<std::string_view>());
                    auto index = request_queue_.reserve();
                    auto *request = request_queue_[index];
                    std::string symbol = prod.get<std::string>();
                    utils::copy_wire_field(request->symbol, symbol);
                    request->msg_type = MessageType::MD_SUBSCRIPTION;
                    auto channel_index = static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2);
                    request->md_subscription.channel = channel_index;
                    request->md_subscription.client = (void*)ws;
                    request_queue_.publish(index);
                    auto& channel_map = subscription_info_[channel_index];
                    auto &subscriptions = channel_map[symbol];
                    subscriptions.emplace(ws);
                    user_data->subscription_info[channel_index].active_subscriptions.emplace(symbol);
                    user_data->subscription_info[channel_index].pending_subscriptions.emplace(symbol);
                    symbol_channel_client_[symbol].emplace(channel_index, ws);
                }
            }
            else if (channel == "market_trades") {
                for (auto &prod : msg["product_ids"]) {
                    LOG_INFO("Client {:p} subscribe market_trades: {}", ws, prod.get<std::string_view>());
                    auto index = request_queue_.reserve();
                    auto *request = request_queue_[index];
                    std::string symbol = prod.get<std::string>();
                    utils::copy_wire_field(request->symbol, symbol);
                    request->msg_type = MessageType::MD_SUBSCRIPTION;
                    auto channel_index = static_cast<uint8_t>(coinbase::WebSocketChannel::MARKET_TRADES);
                    request->md_subscription.channel = channel_index;
                    request->md_subscription.client = (void*)ws;
                    request_queue_.publish(index);
                    auto& channel_map = subscription_info_[channel_index];
                    auto &subscriptions = channel_map[symbol];
                    subscriptions.emplace(ws);
                    user_data->subscription_info[channel_index].active_subscriptions.emplace(symbol);
                    user_data->subscription_info[channel_index].pending_subscriptions.emplace(symbol);
                    symbol_channel_client_[symbol].emplace(channel_index, ws);
                }
            }
            else if (channel == "ticker") {
                for (auto &prod : msg["product_ids"]) {
                    LOG_INFO("Client {:p} subscribe ticker: {}", ws, prod.get<std::string_view>());
                    auto index = request_queue_.reserve();
                    auto *request = request_queue_[index];
                    std::string symbol = prod.get<std::string>();
                    utils::copy_wire_field(request->symbol, symbol);
                    request->msg_type = MessageType::MD_SUBSCRIPTION;
                    auto channel_index = static_cast<uint8_t>(coinbase::WebSocketChannel::TICKER);
                    request->md_subscription.channel = channel_index;
                    request->md_subscription.client = (void*)ws;
                    request_queue_.publish(index);
                    auto& channel_map = subscription_info_[channel_index];
                    auto &subscriptions = channel_map[symbol];
                    subscriptions.emplace(ws);
                    user_data->subscription_info[channel_index].active_subscriptions.emplace(symbol);
                    user_data->subscription_info[channel_index].pending_subscriptions.emplace(symbol);
                    symbol_channel_client_[symbol].emplace(channel_index, ws);
                }
            }
            else if (channel == "heartbeats") {
                user_data->subscribed_heartbeat = true;
                json response = {
                    {"channel", "subscriptions"},
                    {"client_id", ""},
                    {"timestamp", format_timestamp_iso8601(9)},
                    {"sequence_num", user_data->seq_num++},
                    {"events", {{
                        {"subscriptions", {
                            {"heartbeats", {"heartbeats"}}
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

void CoinbasePublisher::check_heartbeats() {
    auto now = std::chrono::steady_clock::now();
    std::vector<wsT*> disconnected_clients;

    // No lock needed - single threaded event loop
    for (auto it = clients_.begin(); it != clients_.end();) {
        auto* ws = *it;
        auto* user_data = ws->getUserData();

        // Check if client has responded to pings
        auto time_since_pong = std::chrono::duration_cast<std::chrono::seconds>(
            now - user_data->last_pong);

        if (time_since_pong > pong_timeout_) {
            // Client hasn't responded to pings, close connection
            LOG_WARN("Client {:p} failed heartbeat check (last_pong: {}s ago), disconnecting",
                        ws, time_since_pong.count());

            disconnected_clients.push_back(ws);
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
            LOG_TRACE("Sent heartbeat to client {:p}", ws);
            ++it;
        }
    }

    if (!disconnected_clients.empty()) {
        LOG_INFO("Disconnected {} stale clients", disconnected_clients.size());
    }
}

void CoinbasePublisher::handle_close(wsT *ws, int code, std::string_view message) {
    LOG_INFO("Client {:p} disconnected: code={}, msg={}", ws, code, message);
    unsubscribe_md(ws);
    clients_.erase(ws);
}

void CoinbasePublisher::unsubscribe_md(wsT *ws) {
    for (auto channel = 0; channel < static_cast<int>(coinbase::WebSocketChannel::_CHANNEL_COUNT_); ++channel) {
        auto *user_data = ws->getUserData();
        for (const auto &symbol : user_data->subscription_info[channel].active_subscriptions) {
            LOG_INFO("Client {:p} unsubscribe channel {}: {}", ws, coinbase::to_string(static_cast<coinbase::WebSocketChannel>(channel)), symbol);
            auto index = request_queue_.reserve();
            auto *request = request_queue_[index];
            utils::copy_wire_field(request->symbol, symbol);
            request->msg_type = MessageType::MD_UNSUBSCRIPTION;
            request->md_subscription.channel = static_cast<uint8_t>(channel);
            request->md_subscription.client = (void*)ws;
            request_queue_.publish(index);

            auto it = symbol_channel_client_.find(symbol);
            if (it != symbol_channel_client_.end()) {
                auto range = it->second.equal_range(static_cast<uint8_t>(channel));
                for (auto itr = range.first; itr != range.second; ) {
                    if (itr->second == ws) {
                        itr = it->second.erase(itr);
                        break;
                    } else {
                        ++itr;
                    }
                }
                if (it->second.empty()) {
                    symbol_channel_client_.erase(it);
                }
            }
            subscription_info_[channel][symbol].erase(ws);
        }
        user_data->subscription_info[channel].active_subscriptions.clear();
        user_data->subscription_info[channel].pending_subscriptions.clear();
        user_data->subscription_info[channel].received_subscriptions.clear();
    }
}

void CoinbasePublisher::send_error(wsT *ws, const std::string& error_msg) {
    json error = {
        {"type", "error"},
        {"message", error_msg}
    };
    ws->send(error.dump(), uWS::OpCode::TEXT);
    LOG_ERROR("WebSocket error: {}", error_msg);
}

void CoinbasePublisher::publish_subscription_response(MarketDataUpdate *update) {
    auto *response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    auto &channel_sub = subscription_info_[response->channel];
    auto it = channel_sub.find(update->symbol);
    if (it != channel_sub.end()) {
        if (it->second.empty()) {
            return;
        }

        // A reject reserves room for the response header and nothing else, so the
        // BookSnapshot read below would run off the end of the frame and then
        // iterate however many levels the leftover bytes happened to describe.
        // Tell the subscribers why instead.
        if (response->reject_reason != MDSubscriptionRejectReason::NONE) {
            auto message = std::format("Subscription to {} rejected: {}",
                                       update->symbol, to_string(response->reject_reason));
            LOG_WARN(message);
            for (auto *ws : it->second) {
                send_error(ws, message);
            }
            return;
        }
        json snapshot_msg;
        json update_msg;   // trades newer than the book snapshot, if any
        auto channel = static_cast<coinbase::WebSocketChannel>(response->channel);
        // Send coinbase snapshot based on channel type
        if (channel == coinbase::WebSocketChannel::LEVEL2) {
            // Extract book snapshot data
            auto *snapshot = reinterpret_cast<BookSnapshot*>(response->data);
            auto *levels = reinterpret_cast<MDLevel*>(snapshot->levels);

            // Build bids and asks arrays
            json::array_t updates;

            auto total_levels = snapshot->num_bid + snapshot->num_ask;
            uint32_t i = 0;

            for (; i < snapshot->num_bid; ++i) {
                updates.push_back({
                    {"side", "bid"},
                    {"price_level", std::to_string(to_price_double(levels[i].price))},
                    {"new_quantity", std::to_string(to_qty_double(levels[i].qty))}
                });
            }

            for (; i < total_levels; ++i) {
                updates.push_back({
                    {"side", "offer"},
                    {"price_level", std::to_string(to_price_double(levels[i].price))},
                    {"new_quantity", std::to_string(to_qty_double(levels[i].qty))}
                });
            }

            // Create level2 snapshot message
            snapshot_msg = {
                {"channel", "l2_data"},
                {"client_id", ""},
                {"timestamp", format_timestamp_iso8601(9)},
                {"events", {{
                    {"type", "snapshot"},
                    {"product_id", update->symbol},
                    {"updates", updates}
                }}}
            };
        }
        else if (channel == coinbase::WebSocketChannel::MARKET_TRADES) {
            auto *trade_response = reinterpret_cast<MDTradeSnapshotResponse*>(response->data);
            // Coinbase orders snapshot trades newest first.
            snapshot_msg = encode_market_trades(update->symbol, "snapshot", trade_response->trades,
                                                trade_response->num_snapshot, true);
            if (trade_response->num_update) {
                // These are newer than the book snapshot this subscriber gets, so
                // they are not settled history — deliver them as a normal update,
                // chronologically, right after the snapshot.
                update_msg = encode_market_trades(update->symbol, "update",
                                                  trade_response->trades + trade_response->num_snapshot,
                                                  trade_response->num_update, false);
            }
        }
        else if (channel == coinbase::WebSocketChannel::TICKER) {
            // Send ticker snapshot (would need actual ticker data)
            snapshot_msg = {
                {"channel", "ticker"},
                {"client_id", ""},
                {"timestamp", format_timestamp_iso8601(9)},
                {"events", {{
                    {"type", "snapshot"},
                    {"tickers", json::array()}
                }}}
            };
        }
        else {
            LOG_WARN("Subscription response for unsupported channel: {}", coinbase::to_string(channel));
            return;
        }

        for (const auto &ws : it->second) {
            auto *user_data = ws->getUserData();
            auto &subscription_info = user_data->subscription_info[response->channel];
            auto it_sym = subscription_info.pending_subscriptions.find(update->symbol);
            if (it_sym != subscription_info.pending_subscriptions.end()) {
                subscription_info.pending_subscriptions.erase(it_sym);
                subscription_info.received_subscriptions.emplace_back(update->symbol);

                snapshot_msg["sequence_num"] = user_data->seq_num++;
                ws->send(snapshot_msg.dump(), uWS::OpCode::TEXT);

                LOG_DEBUG("CoinbasePublisher send {} snapshot", update->symbol);

                if (!update_msg.is_null()) {
                    update_msg["sequence_num"] = user_data->seq_num++;
                    ws->send(update_msg.dump(), uWS::OpCode::TEXT);
                }

                if (subscription_info.pending_subscriptions.empty()) {
                    // Send subscription confirmation
                    json subscription_msg = {
                        {"channel", "subscriptions"},
                        {"client_id", ""},
                        {"timestamp", format_timestamp_iso8601(9)},
                        {"sequence_num", user_data->seq_num++},
                        {"events", {{
                            {"subscriptions", {
                                {coinbase::to_string(channel), subscription_info.received_subscriptions}
                            }}
                        }}}
                    };

                    for (auto i = 0u; i < coinbase::WebSocketChannel::_CHANNEL_COUNT_; ++i) {
                        if (i == response->channel) {
                            continue;
                        }
                        auto &other_sub_info = user_data->subscription_info[i];
                        if (!other_sub_info.received_subscriptions.empty()) {
                            subscription_msg["events"][0]["subscriptions"][coinbase::to_string(static_cast<coinbase::WebSocketChannel>(i))] = other_sub_info.received_subscriptions;
                        }
                    }
                    ws->send(subscription_msg.dump(), uWS::OpCode::TEXT);
                }
            }
        }
    }
}

void CoinbasePublisher::publish_level_snapshot(MarketDataUpdate *update) {
    auto *snapshot = reinterpret_cast<BookSnapshot*>(update->data);
    auto it_range = symbol_channel_client_.find(update->symbol);
    if (it_range != symbol_channel_client_.end()) {
        auto range = it_range->second.equal_range(static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2));
        if (range.first == range.second) {
            return;
        }
        json::array_t updates;

        auto total_levels = snapshot->num_bid + snapshot->num_ask;
        uint32_t i = 0;

        for (; i < snapshot->num_bid; ++i) {
            updates.push_back({
                {"side", "bid"},
                {"price_level", std::to_string(to_price_double(snapshot->levels[i].price))},
                {"new_quantity", std::to_string(to_qty_double(snapshot->levels[i].qty))}
            });
        }

        for (; i < total_levels; ++i) {
            updates.push_back({
                {"side", "offer"},
                {"price_level", std::to_string(to_price_double(snapshot->levels[i].price))},
                {"new_quantity", std::to_string(to_qty_double(snapshot->levels[i].qty))}
            });
        }

        // Create level2 snapshot message
        json snapshot_msg = {
            {"channel", "l2_data"},
            {"client_id", ""},
            {"timestamp", format_timestamp_iso8601(9)},
            {"events", {{
                {"type", "snapshot"},
                {"product_id", update->symbol},
                {"updates", updates}
            }}}
        };

        for (auto it = range.first; it != range.second; ++it) {
            auto *ws = it->second;
            auto *user_data = ws->getUserData();
            snapshot_msg["sequence_num"] = user_data->seq_num++;
            ws->send(snapshot_msg.dump(), uWS::OpCode::TEXT);
        }
    }
}

void CoinbasePublisher::publish_level_update(MarketDataUpdate *update) {
    auto *level_update = reinterpret_cast<MDLevelUpdate*>(update->data);
    auto it_range = symbol_channel_client_.find(update->symbol);
    if (it_range != symbol_channel_client_.end()) {
        auto range = it_range->second.equal_range(static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2));
        if (range.first == range.second) {
            return;
        }
        // Build level update message
        json::array_t updates;
        for (auto i = 0u; i < level_update->num_level_update; ++i) {
            auto &level = level_update->levels[i];
            // LOG_DEBUG("Level Update: {} {} @ {} size={} seq_num={} time={}({})",
            //           update->symbol,
            //           level.side ? "SELL" : "BUY",
            //           to_price_double(level.price),
            //           to_qty_double(level.qty),
            //           level.seq_num,
            //           format_timestamp_iso8601(level.event_time, 9),
            //           level.event_time);
            updates.push_back({
                {"side", level.side ? "offer" : "bid"},
                {"price_level", std::to_string(to_price_double(level.price))},
                {"new_quantity", std::to_string(to_qty_double(level.qty))},
                {"event_time", format_timestamp_iso8601(level.event_time, 9)}
            });
        }

        json update_msg = {
            {"channel", "l2_data"},
            {"client_id", ""},
            {"timestamp", format_timestamp_iso8601(9)},
            {"events", {{
                {"type", "update"},
                {"product_id", update->symbol},
                {"updates", updates}
            }}}
        };

        for (auto it = range.first; it != range.second; ++it) {
            auto *ws = it->second;
            auto *user_data = ws->getUserData();
            update_msg["sequence_num"] = user_data->seq_num++;
            ws->send(update_msg.dump(), uWS::OpCode::TEXT);
        }
    }
}

void CoinbasePublisher::publish_trade_summary(MarketDataUpdate *update) {
    auto it_range = symbol_channel_client_.find(update->symbol);
    if (it_range == symbol_channel_client_.end()) {
        return;
    }
    auto range = it_range->second.equal_range(static_cast<uint8_t>(coinbase::WebSocketChannel::MARKET_TRADES));
    if (range.first == range.second) {
        return;
    }

    auto *summary = reinterpret_cast<TradeSummary*>(update->data);
    auto trade_msg = encode_market_trade(update->symbol, *summary);

    for (auto it = range.first; it != range.second; ++it) {
        auto *ws = it->second;
        auto *user_data = ws->getUserData();
        trade_msg["sequence_num"] = user_data->seq_num++;
        ws->send(trade_msg.dump(), uWS::OpCode::TEXT);
    }
}

void CoinbasePublisher::publish_order_update(MarketDataUpdate *update) {
    auto it_range = symbol_channel_client_.find(update->symbol);
    if (it_range != symbol_channel_client_.end()) {
        auto range = it_range->second.equal_range(static_cast<uint8_t>(coinbase::WebSocketChannel::USER));
        if (range.first == range.second) {
            return;
        }
        SLICK_ASSERT(false && "User data shouldn't subscribed in market data publisher");
    }
}
