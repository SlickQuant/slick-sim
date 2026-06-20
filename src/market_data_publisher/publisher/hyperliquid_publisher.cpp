#include "hyperliquid_publisher.hpp"
#include <slick/logger.hpp>
#include <utils/timestamp.hpp>
#include <cstring>
#include <format>

using namespace slick::sim::md_publisher;
using namespace slick::sim;
using namespace slick::sim::utils;

uint_fast64_t HyperliquidPerSocketData::heartbeat_count = 0;

HyperliquidPublisher::HyperliquidPublisher(
    const json& config,
    slick::SlickQueue<Request>& request_queue,
    slick::SlickQueue<uint8_t>& market_data_queue)
    : Publisher(request_queue, market_data_queue)
    , port_(config.value("port", 5001))
{}

void HyperliquidPublisher::start() {
    running_.store(true, std::memory_order_release);
    ws_thread_ = std::thread([this]() {
        auto app = uWS::App();

        app.ws<HyperliquidPerSocketData>("/*", {
            .compression     = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 2 << 24,
            .idleTimeout     = 120,
            .maxBackpressure = 2 << 25,
            .upgrade         = nullptr,

            .open = [this](auto* ws) {
                ws->getUserData()->last_pong = std::chrono::steady_clock::now();
                clients_.emplace(ws);
                LOG_INFO("HyperliquidPublisher client connected {:p}", (void*)ws);
            },

            .message = [this](auto* ws, std::string_view message, uWS::OpCode) {
                handleMessage(ws, message);
            },

            .drain = [](auto*) {},

            .ping = [](auto* ws, std::string_view) {
                LOG_TRACE("HyperliquidPublisher ping from {:p}", (void*)ws);
            },

            .pong = [this](auto* ws, std::string_view) {
                ws->getUserData()->last_pong = std::chrono::steady_clock::now();
            },

            .close = [this](auto* ws, int code, std::string_view msg) {
                handleClose(ws, code, msg);
            }
        })
        .listen(port_, [this](auto* listen_socket) {
            listen_socket_ = listen_socket;
            if (listen_socket) {
                LOG_INFO("HyperliquidPublisher started on port {}", port_);
                loop_ = uWS::Loop::get();
                loop_->defer([this]() {
                    heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0, 0);
                    *((HyperliquidPublisher**)us_timer_ext(heartbeat_timer_)) = this;
                    us_timer_set(heartbeat_timer_, [](struct us_timer_t* t) {
                        (*((HyperliquidPublisher**)us_timer_ext(t)))->checkHeartbeats();
                    }, ping_interval_ms_, ping_interval_ms_);
                });
                schedulePublishProcessing();
            } else {
                LOG_ERROR("HyperliquidPublisher failed to listen on port {}", port_);
            }
        })
        .run();
    });
}

void HyperliquidPublisher::stop() {
    running_.store(false, std::memory_order_release);
    if (heartbeat_timer_) {
        us_timer_close(heartbeat_timer_);
        heartbeat_timer_ = nullptr;
    }
    if (listen_socket_) {
        us_listen_socket_close(0, listen_socket_);
        listen_socket_ = nullptr;
    }
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void HyperliquidPublisher::schedulePublishProcessing() {
    if (!running_.load(std::memory_order_acquire) || !loop_) return;
    loop_->defer([this]() { publishMarketDataUpdate(); });
}

void HyperliquidPublisher::publishMarketDataUpdate() {
    constexpr int max_batch = 100;
    int processed = 0;

    while (processed < max_batch) {
        auto [data, size] = market_data_queue_.read(data_cursor_);
        if (!data) break;

        auto* update = reinterpret_cast<MarketDataUpdate*>(data);
        // Only handle updates from HYPERLIQUID venue
        if (update->venue != Venue::HYPERLIQUID) {
            processed++;
            continue;
        }

        switch (update->type) {
            case MDUpdateType::SUB_RESPONSE:
                publishSubscriptionResponse(update);
                processed = max_batch;
                break;
            case MDUpdateType::BOOK_SNAPSHOT:
                publishBookSnapshot(update);
                processed = max_batch;
                break;
            case MDUpdateType::TRADE:
                publishTradeUpdate(update);
                break;
            default:
                break;
        }
        processed++;
    }

    schedulePublishProcessing();
}

void HyperliquidPublisher::handleMessage(wsT* ws, std::string_view message) {
    try {
        json msg = json::parse(message);

        std::string method = msg.value("method", "");
        if (method != "subscribe" && method != "unsubscribe") {
            sendError(ws, std::format("method `{}` is not supported", method));
            return;
        }

        if (!msg.contains("subscription")) {
            sendError(ws, "missing `subscription` field");
            return;
        }

        const auto& sub = msg["subscription"];
        std::string type = sub.value("type", "");

        if (type == "l2Book") {
            std::string coin = sub.value("coin", "");
            if (coin.empty()) { sendError(ws, "missing `coin` in subscription"); return; }
            if (method == "subscribe") {
                subscribeChannel(ws, HyperliquidChannel::L2_BOOK, coin);
            }
        } else if (type == "trades") {
            std::string coin = sub.value("coin", "");
            if (coin.empty()) { sendError(ws, "missing `coin` in subscription"); return; }
            if (method == "subscribe") {
                subscribeChannel(ws, HyperliquidChannel::TRADES, coin);
            }
        } else if (type == "heartbeat") {
            ws->getUserData()->subscribed_heartbeat = true;
            json rsp = {{"channel", "subscriptions"}, {"data", {{"type", "heartbeat"}}}};
            ws->send(rsp.dump(), uWS::OpCode::TEXT);
        } else {
            sendError(ws, std::format("subscription type `{}` is not supported", type));
        }
    } catch (const json::parse_error& e) {
        sendError(ws, std::string("invalid JSON: ") + e.what());
    }
}

void HyperliquidPublisher::subscribeChannel(wsT* ws, HyperliquidChannel channel, const std::string& coin) {
    auto ch = static_cast<uint8_t>(channel);
    LOG_INFO("HyperliquidPublisher {:p} subscribe {}: {}", (void*)ws,
             channel == HyperliquidChannel::L2_BOOK ? "l2Book" : "trades", coin);

    auto index = request_queue_.reserve();
    auto* request = request_queue_[index];
    memcpy(request->symbol, coin.c_str(), sizeof(request->symbol));
    request->msg_type = MessageType::MD_SUBSCRIPTION;
    request->md_subscription.channel = ch;
    request->md_subscription.client  = (void*)ws;
    request_queue_.publish(index);

    subscription_info_[ch][coin].emplace(ws);
    symbol_channel_client_[coin].emplace(ch, ws);

    auto* ud = ws->getUserData();
    ud->subs[ch].active.emplace(coin);
    ud->subs[ch].pending.emplace(coin);
}

void HyperliquidPublisher::publishSubscriptionResponse(MarketDataUpdate* update) {
    auto* response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    uint8_t ch = response->channel;
    auto& channel_sub = subscription_info_[ch];
    auto it = channel_sub.find(update->symbol);
    if (it == channel_sub.end() || it->second.empty()) return;

    auto channel = static_cast<HyperliquidChannel>(ch);
    json snapshot_msg;

    if (channel == HyperliquidChannel::L2_BOOK) {
        auto* snapshot = reinterpret_cast<BookSnapshot*>(response->data);
        auto* levels   = reinterpret_cast<MDLevel*>(snapshot->levels);

        json::array_t bids, asks;
        for (uint32_t i = 0; i < snapshot->num_bid; ++i) {
            bids.push_back({
                {"px", std::to_string(to_price_double(levels[i].price))},
                {"sz", std::to_string(to_qty_double(levels[i].qty))},
                {"n",  levels[i].num_orders}
            });
        }
        for (uint32_t i = snapshot->num_bid; i < snapshot->num_bid + snapshot->num_ask; ++i) {
            asks.push_back({
                {"px", std::to_string(to_price_double(levels[i].price))},
                {"sz", std::to_string(to_qty_double(levels[i].qty))},
                {"n",  levels[i].num_orders}
            });
        }
        snapshot_msg = {
            {"channel", "l2Book"},
            {"data", {
                {"coin",   update->symbol},
                {"time",   get_current_time_ns() / ONE_MILLISECOND_NS},
                {"levels", {bids, asks}}
            }}
        };
    } else if (channel == HyperliquidChannel::TRADES) {
        snapshot_msg = {
            {"channel", "trades"},
            {"data", json::array()}
        };
    } else {
        return;
    }

    for (auto* ws : it->second) {
        auto* ud = ws->getUserData();
        auto& sub = ud->subs[ch];
        auto it_sym = sub.pending.find(update->symbol);
        if (it_sym != sub.pending.end()) {
            sub.pending.erase(it_sym);
            sub.received.emplace_back(update->symbol);
            ws->send(snapshot_msg.dump(), uWS::OpCode::TEXT);
            LOG_DEBUG("HyperliquidPublisher sent subscription snapshot for {}", update->symbol);

            if (sub.pending.empty()) {
                json confirm = {
                    {"channel", "subscriptions"},
                    {"data", {{"subscriptions", sub.received}}}
                };
                ws->send(confirm.dump(), uWS::OpCode::TEXT);
            }
        }
    }
}

void HyperliquidPublisher::publishBookSnapshot(MarketDataUpdate* update) {
    auto ch = static_cast<uint8_t>(HyperliquidChannel::L2_BOOK);
    auto& channel_sub = subscription_info_[ch];
    auto it = channel_sub.find(update->symbol);
    if (it == channel_sub.end() || it->second.empty()) return;

    auto* snapshot = reinterpret_cast<BookSnapshot*>(update->data);
    auto* levels   = reinterpret_cast<MDLevel*>(snapshot->levels);

    json::array_t bids, asks;
    for (uint32_t i = 0; i < snapshot->num_bid; ++i) {
        bids.push_back({
            {"px", std::to_string(to_price_double(levels[i].price))},
            {"sz", std::to_string(to_qty_double(levels[i].qty))},
            {"n",  levels[i].num_orders}
        });
    }
    for (uint32_t i = snapshot->num_bid; i < snapshot->num_bid + snapshot->num_ask; ++i) {
        asks.push_back({
            {"px", std::to_string(to_price_double(levels[i].price))},
            {"sz", std::to_string(to_qty_double(levels[i].qty))},
            {"n",  levels[i].num_orders}
        });
    }
    json msg = {
        {"channel", "l2Book"},
        {"data", {
            {"coin",   update->symbol},
            {"time",   get_current_time_ns() / ONE_MILLISECOND_NS},
            {"levels", {bids, asks}}
        }}
    };
    std::string payload = msg.dump();
    for (auto* ws : it->second) {
        ws->send(payload, uWS::OpCode::TEXT);
    }
}

void HyperliquidPublisher::publishTradeUpdate(MarketDataUpdate* update) {
    auto ch = static_cast<uint8_t>(HyperliquidChannel::TRADES);
    auto& channel_sub = subscription_info_[ch];
    auto it = channel_sub.find(update->symbol);
    if (it == channel_sub.end() || it->second.empty()) return;

    auto* trade_update = reinterpret_cast<MDTradeUpdate*>(update->data);
    json::array_t trades;
    for (uint32_t i = 0; i < trade_update->num_trades; ++i) {
        const auto& t = trade_update->trades[i];
        trades.push_back({
            {"coin",  update->symbol},
            {"side",  t.side == Side::BUY ? "B" : "A"},
            {"px",    std::to_string(to_price_double(t.price))},
            {"sz",    std::to_string(to_qty_double(t.qty))},
            {"time",  t.event_time / ONE_MILLISECOND_NS},
            {"hash",  "0x0"}
        });
    }
    json msg = {{"channel", "trades"}, {"data", trades}};
    std::string payload = msg.dump();
    for (auto* ws : it->second) {
        ws->send(payload, uWS::OpCode::TEXT);
    }
}

void HyperliquidPublisher::checkHeartbeats() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = clients_.begin(); it != clients_.end();) {
        auto* ws   = *it;
        auto* ud   = ws->getUserData();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ud->last_pong);
        if (elapsed > pong_timeout_) {
            LOG_WARN("HyperliquidPublisher: client {:p} timed out, disconnecting", (void*)ws);
            it = clients_.erase(it);
            ws->close();
        } else {
            if (ud->subscribed_heartbeat) {
                json hb = {
                    {"channel", "heartbeats"},
                    {"data", {{"time", format_timestamp_iso8601()}, {"counter", ud->heartbeat_count++}}}
                };
                ws->send(hb.dump(), uWS::OpCode::TEXT);
            }
            ws->send("", uWS::OpCode::PING);
            ++it;
        }
    }
}

void HyperliquidPublisher::handleClose(wsT* ws, int code, std::string_view message) {
    LOG_INFO("HyperliquidPublisher: client {:p} disconnected code={} msg={}", (void*)ws, code, message);
    unsubscribeMD(ws);
    clients_.erase(ws);
}

void HyperliquidPublisher::unsubscribeMD(wsT* ws) {
    auto* ud = ws->getUserData();
    for (uint8_t ch = 0; ch < static_cast<uint8_t>(HyperliquidChannel::__COUNT__); ++ch) {
        for (const auto& coin : ud->subs[ch].active) {
            auto index = request_queue_.reserve();
            auto* req  = request_queue_[index];
            memcpy(req->symbol, coin.data(), sizeof(req->symbol));
            req->msg_type = MessageType::MD_UNSUBSCRIPTION;
            req->md_subscription.channel = ch;
            req->md_subscription.client  = (void*)ws;
            request_queue_.publish(index);

            auto it_sym = symbol_channel_client_.find(coin);
            if (it_sym != symbol_channel_client_.end()) {
                auto range = it_sym->second.equal_range(ch);
                for (auto itr = range.first; itr != range.second; ) {
                    if (itr->second == ws) { itr = it_sym->second.erase(itr); break; }
                    else ++itr;
                }
                if (it_sym->second.empty()) symbol_channel_client_.erase(it_sym);
            }
            subscription_info_[ch][coin].erase(ws);
        }
        ud->subs[ch].active.clear();
        ud->subs[ch].pending.clear();
        ud->subs[ch].received.clear();
    }
}

void HyperliquidPublisher::sendError(wsT* ws, const std::string& error_msg) {
    json err = {{"channel", "error"}, {"data", error_msg}};
    ws->send(err.dump(), uWS::OpCode::TEXT);
    LOG_ERROR("HyperliquidPublisher error: {}", error_msg);
}
