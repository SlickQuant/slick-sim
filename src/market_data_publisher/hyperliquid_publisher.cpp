#include <slick/logger.hpp>
#include "hyperliquid_publisher.hpp"
#include "hyperliquid_trade_encoder.hpp"
#include <common/hyperliquid_info_proxy.hpp>
#include <utils/fixed_string.hpp>
#include <utils/timestamp.hpp>
#include <cstring>
#include <format>

using namespace slick::sim::md_publisher;
using namespace slick::sim;
using namespace slick::sim::utils;

uint_fast64_t HyperliquidPerSocketData::heartbeat_count = 0;

HyperliquidPublisher::HyperliquidPublisher(
    const json& config,
    slick::queue<Request>& request_queue,
    slick::queue<uint8_t>& market_data_queue
)
    : WebsocketMarketDataPublisher(Venue::HYPERLIQUID, request_queue, market_data_queue, config.value("port", 5001))
    , upstream_base_url_(config.value("upstream_base_url", "https://api.hyperliquid.xyz"))
{}

// void HyperliquidPublisher::start() {
//     running_.store(true, std::memory_order_release);
//     ws_thread_ = std::thread([this]() {
//         auto app = uWS::App();

//         app.ws<HyperliquidPerSocketData>("/*", {
//             .compression     = uWS::SHARED_COMPRESSOR,
//             .maxPayloadLength = 2 << 24,
//             .idleTimeout     = 120,
//             .maxBackpressure = 2 << 25,
//             .upgrade         = nullptr,

//             .open = [this](auto* ws) {
//                 ws->getUserData()->last_pong = std::chrono::steady_clock::now();
//                 clients_.emplace(ws);
//                 LOG_INFO("HyperliquidPublisher client connected {:p}", (void*)ws);
//             },

//             .message = [this](auto* ws, std::string_view message, uWS::OpCode) {
//                 handleMessage(ws, message);
//             },

//             .drain = [](auto*) {},

//             .ping = [](auto* ws, std::string_view) {
//                 LOG_TRACE("HyperliquidPublisher ping from {:p}", (void*)ws);
//             },

//             .pong = [this](auto* ws, std::string_view) {
//                 ws->getUserData()->last_pong = std::chrono::steady_clock::now();
//             },

//             .close = [this](auto* ws, int code, std::string_view msg) {
//                 handleClose(ws, code, msg);
//             }
//         })
//         .listen(port_, [this](auto* listen_socket) {
//             listen_socket_ = listen_socket;
//             if (listen_socket) {
//                 LOG_INFO("HyperliquidPublisher started on port {}", port_);
//                 loop_ = uWS::Loop::get();
//                 loop_->defer([this]() {
//                     heartbeat_timer_ = us_create_timer((struct us_loop_t*)loop_, 0, 0);
//                     *((HyperliquidPublisher**)us_timer_ext(heartbeat_timer_)) = this;
//                     us_timer_set(heartbeat_timer_, [](struct us_timer_t* t) {
//                         (*((HyperliquidPublisher**)us_timer_ext(t)))->checkHeartbeats();
//                     }, ping_interval_ms_, ping_interval_ms_);
//                 });
//                 schedulePublishProcessing();
//             } else {
//                 LOG_ERROR("HyperliquidPublisher failed to listen on port {}", port_);
//             }
//         })
//         .run();
//     });
// }

// void HyperliquidPublisher::stop() {
//     running_.store(false, std::memory_order_release);
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

// void HyperliquidPublisher::schedulePublishProcessing() {
//     if (!running_.load(std::memory_order_acquire) || !loop_) return;
//     loop_->defer([this]() { publishMarketDataUpdate(); });
// }

void HyperliquidPublisher::setup_routes(uWS::App &app) {
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
            handle_message(ws, message);
        },

        .drain = [](auto*) {},

        .ping = [](auto* ws, std::string_view) {
            LOG_TRACE("HyperliquidPublisher ping from {:p}", (void*)ws);
        },

        .pong = [this](auto* ws, std::string_view) {
            ws->getUserData()->last_pong = std::chrono::steady_clock::now();
        },

        .close = [this](auto* ws, int code, std::string_view msg) {
            handle_close(ws, code, msg);
        }
    });

    app.post("/info", [this](auto* res, auto* req) {
        handle_info(res, req);
    });
}

void HyperliquidPublisher::handle_info(uWS::HttpResponse<false>* res, uWS::HttpRequest*) {
    proxy_hyperliquid_info_request(res, upstream_base_url_);
}

void HyperliquidPublisher::publish_market_data_update(MarketDataUpdate* update) {
    switch (update->type) {
        case MDUpdateType::SUB_RESPONSE:
            publish_subscription_response(update);
            break;
        case MDUpdateType::BOOK_SNAPSHOT:
            publish_book_snapshot(update);
            break;
        case MDUpdateType::TRADE_SUMMARY:
            publish_trade_summary(update);
            break;
        default:
            break;
    }
}

void HyperliquidPublisher::handle_message(wsT* ws, std::string_view message) {
    try {
        json msg = json::parse(message);

        std::string method = msg.value("method", "");
        if (method == "ping") {
            json pong = {{"channel", "pong"}};
            ws->send(pong.dump(), uWS::OpCode::TEXT);
            return;
        }

        if (method != "subscribe" && method != "unsubscribe") {
            send_error(ws, std::format("method `{}` is not supported", method));
            return;
        }

        if (!msg.contains("subscription")) {
            send_error(ws, "missing `subscription` field");
            return;
        }

        const auto& sub = msg["subscription"];
        std::string type = sub.value("type", "");

        if (type == "l2Book") {
            std::string coin = sub.value("coin", "");
            if (coin.empty()) { send_error(ws, "missing `coin` in subscription"); return; }
            if (method == "subscribe") {
                subscribe_channel(ws, HyperliquidChannel::L2_BOOK, coin);
            }
        } else if (type == "trades") {
            std::string coin = sub.value("coin", "");
            if (coin.empty()) { send_error(ws, "missing `coin` in subscription"); return; }
            if (method == "subscribe") {
                subscribe_channel(ws, HyperliquidChannel::TRADES, coin);
            }
        } else if (type == "l2") {
            // Undocumented channel — coin is field "c", not "coin".
            std::string coin = sub.value("c", "");
            if (coin.empty()) { send_error(ws, "missing `c` in subscription"); return; }
            if (method == "subscribe") {
                // Real Hyperliquid acks "l2" subscriptions immediately by
                // echoing the subscription back, ahead of any snapshot data.
                json ack = {
                    {"channel", "subscriptionResponse"},
                    {"data", {{"method", "subscribe"}, {"subscription", sub}}}
                };
                ws->send(ack.dump(), uWS::OpCode::TEXT);
                subscribe_channel(ws, HyperliquidChannel::L2, coin);
            }
        } else if (type == "heartbeat") {
            ws->getUserData()->subscribed_heartbeat = true;
            json rsp = {{"channel", "subscriptions"}, {"data", {{"type", "heartbeat"}}}};
            ws->send(rsp.dump(), uWS::OpCode::TEXT);
        } else {
            send_error(ws, std::format("subscription type `{}` is not supported", type));
        }
    } catch (const json::parse_error& e) {
        send_error(ws, std::string("invalid JSON: ") + e.what());
    }
}

void HyperliquidPublisher::subscribe_channel(wsT* ws, HyperliquidChannel channel, const std::string& coin) {
    auto ch = static_cast<uint8_t>(channel);
    const char* channel_name = channel == HyperliquidChannel::L2_BOOK ? "l2Book"
                              : channel == HyperliquidChannel::L2      ? "l2"
                                                                        : "trades";
    LOG_INFO("HyperliquidPublisher {:p} subscribe {}: {}", (void*)ws, channel_name, coin);

    auto index = request_queue_.reserve();
    auto* request = request_queue_[index];
    utils::copy_wire_field(request->symbol, coin);
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

void HyperliquidPublisher::publish_subscription_response(MarketDataUpdate* update) {
    auto* response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    uint8_t ch = response->channel;
    auto& channel_sub = subscription_info_[ch];
    auto it = channel_sub.find(update->symbol);
    if (it == channel_sub.end() || it->second.empty()) return;

    auto channel = static_cast<HyperliquidChannel>(ch);
    json snapshot_msg;
    json update_msg;   // trades newer than the book snapshot, if any

    if (channel == HyperliquidChannel::L2_BOOK || channel == HyperliquidChannel::L2) {
        auto* snapshot = reinterpret_cast<BookSnapshot*>(response->data);
        auto* levels   = reinterpret_cast<MDLevel*>(snapshot->levels);

        json::array_t bids, asks;
        L2Levels curr_bid, curr_ask;
        curr_bid.reserve(snapshot->num_bid);
        curr_ask.reserve(snapshot->num_ask);
        for (uint32_t i = 0; i < snapshot->num_bid; ++i) {
            bids.push_back({
                {"px", std::to_string(to_price_double(levels[i].price))},
                {"sz", std::to_string(to_qty_double(levels[i].qty))},
                {"n",  levels[i].num_orders}
            });
            curr_bid.emplace_back(levels[i].price, levels[i].qty);
        }
        for (uint32_t i = snapshot->num_bid; i < snapshot->num_bid + snapshot->num_ask; ++i) {
            asks.push_back({
                {"px", std::to_string(to_price_double(levels[i].price))},
                {"sz", std::to_string(to_qty_double(levels[i].qty))},
                {"n",  levels[i].num_orders}
            });
            curr_ask.emplace_back(levels[i].price, levels[i].qty);
        }

        if (channel == HyperliquidChannel::L2_BOOK) {
            snapshot_msg = {
                {"channel", "l2Book"},
                {"data", {
                    {"coin",   update->symbol},
                    {"time",   get_current_time_ns() / ONE_MILLISECOND_NS},
                    {"levels", {bids, asks}}
                }}
            };
        } else {
            // Undocumented "l2" channel: first delivery after subscribing is
            // an uncompressed full snapshot under key "s" — there is nothing
            // to diff against yet. Seed the diff baseline from it so the
            // next routine book update can be sent as a compressed diff.
            l2_diff_baseline_[update->symbol] = {std::move(curr_bid), std::move(curr_ask)};
            snapshot_msg = {
                {"channel", "l2"},
                {"data", {{"s", {
                    {"coin",   update->symbol},
                    {"time",   get_current_time_ns() / ONE_MILLISECOND_NS},
                    {"levels", {bids, asks}}
                }}}}
            };
        }
    } else if (channel == HyperliquidChannel::TRADES) {
        auto* trade_response = reinterpret_cast<MDTradeSnapshotResponse*>(response->data);
        snapshot_msg = encode_trade_message(update->symbol, trade_response->trades,
                                            trade_response->num_snapshot);
        if (trade_response->num_update) {
            // These are newer than the book snapshot this subscriber gets, so they
            // are not settled history — deliver them as a normal update right
            // after the snapshot.
            update_msg = encode_trade_message(update->symbol,
                                              trade_response->trades + trade_response->num_snapshot,
                                              trade_response->num_update);
        }
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

            if (!update_msg.is_null()) {
                ws->send(update_msg.dump(), uWS::OpCode::TEXT);
            }

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

void HyperliquidPublisher::publish_book_snapshot(MarketDataUpdate* update) {
    auto l2book_ch = static_cast<uint8_t>(HyperliquidChannel::L2_BOOK);
    auto l2_ch     = static_cast<uint8_t>(HyperliquidChannel::L2);
    auto it_l2book = subscription_info_[l2book_ch].find(update->symbol);
    auto it_l2     = subscription_info_[l2_ch].find(update->symbol);
    bool has_l2book = it_l2book != subscription_info_[l2book_ch].end() && !it_l2book->second.empty();
    bool has_l2     = it_l2     != subscription_info_[l2_ch].end()     && !it_l2->second.empty();
    if (!has_l2book && !has_l2) return;

    auto* snapshot = reinterpret_cast<BookSnapshot*>(update->data);
    auto* levels   = reinterpret_cast<MDLevel*>(snapshot->levels);

    json::array_t bids, asks;
    L2Levels curr_bid, curr_ask;
    curr_bid.reserve(snapshot->num_bid);
    curr_ask.reserve(snapshot->num_ask);
    for (uint32_t i = 0; i < snapshot->num_bid; ++i) {
        bids.push_back({
            {"px", std::to_string(to_price_double(levels[i].price))},
            {"sz", std::to_string(to_qty_double(levels[i].qty))},
            {"n",  levels[i].num_orders}
        });
        curr_bid.emplace_back(levels[i].price, levels[i].qty);
    }
    for (uint32_t i = snapshot->num_bid; i < snapshot->num_bid + snapshot->num_ask; ++i) {
        asks.push_back({
            {"px", std::to_string(to_price_double(levels[i].price))},
            {"sz", std::to_string(to_qty_double(levels[i].qty))},
            {"n",  levels[i].num_orders}
        });
        curr_ask.emplace_back(levels[i].price, levels[i].qty);
    }
    uint64_t time_ms = get_current_time_ns() / ONE_MILLISECOND_NS;

    if (has_l2book) {
        json msg = {
            {"channel", "l2Book"},
            {"data", {
                {"coin",   update->symbol},
                {"time",   time_ms},
                {"levels", {bids, asks}}
            }}
        };
        std::string payload = msg.dump();
        for (auto* ws : it_l2book->second) {
            ws->send(payload, uWS::OpCode::TEXT);
        }
    }

    if (has_l2) {
        auto& baseline = l2_diff_baseline_[update->symbol];
        L2Diff diff = compute_l2_diff(baseline.first, baseline.second, curr_bid, curr_ask);
        baseline.first  = curr_bid;
        baseline.second = curr_ask;

        json diff_payload = {
            {"c", update->symbol},
            {"l", diff.l},
            {"r", diff.r},
            {"t", time_ms}
        };
        json msg = {
            {"channel", "l2"},
            {"data", {{"c", encode_l2_diff(diff_payload)}}}
        };
        std::string payload = msg.dump();
        for (auto* ws : it_l2->second) {
            ws->send(payload, uWS::OpCode::TEXT);
        }
    }
}

void HyperliquidPublisher::publish_trade_summary(MarketDataUpdate* update) {
    auto ch = static_cast<uint8_t>(HyperliquidChannel::TRADES);
    auto& channel_sub = subscription_info_[ch];
    auto it = channel_sub.find(update->symbol);
    if (it == channel_sub.end() || it->second.empty()) return;

    auto* summary = reinterpret_cast<TradeSummary*>(update->data);
    // No per-socket sequence number on this venue, so one payload serves everyone.
    std::string payload = encode_trade_message(update->symbol, *summary).dump();
    for (auto* ws : it->second) {
        ws->send(payload, uWS::OpCode::TEXT);
    }
}

void HyperliquidPublisher::check_heartbeats() {
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

void HyperliquidPublisher::handle_close(wsT* ws, int code, std::string_view message) {
    LOG_INFO("HyperliquidPublisher: client {:p} disconnected code={} msg={}", (void*)ws, code, message);
    unsubscribe_md(ws);
    clients_.erase(ws);
}

void HyperliquidPublisher::unsubscribe_md(wsT* ws) {
    auto* ud = ws->getUserData();
    for (uint8_t ch = 0; ch < static_cast<uint8_t>(HyperliquidChannel::__COUNT__); ++ch) {
        for (const auto& coin : ud->subs[ch].active) {
            auto index = request_queue_.reserve();
            auto* req  = request_queue_[index];
            utils::copy_wire_field(req->symbol, coin);
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
            auto& coin_subs = subscription_info_[ch][coin];
            coin_subs.erase(ws);
            if (ch == static_cast<uint8_t>(HyperliquidChannel::L2) && coin_subs.empty()) {
                l2_diff_baseline_.erase(coin);
            }
        }
        ud->subs[ch].active.clear();
        ud->subs[ch].pending.clear();
        ud->subs[ch].received.clear();
    }
}

void HyperliquidPublisher::send_error(wsT* ws, const std::string& error_msg) {
    json err = {{"channel", "error"}, {"data", error_msg}};
    ws->send(err.dump(), uWS::OpCode::TEXT);
    LOG_ERROR("HyperliquidPublisher error: {}", error_msg);
}
