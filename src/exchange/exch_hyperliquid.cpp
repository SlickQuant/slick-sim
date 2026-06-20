#include "exch_hyperliquid.hpp"
#include <md_feed/hyperliquid_live_ws_feed.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <common/symbol_manager.hpp>
#include <utils/timestamp.hpp>
#include <utils/order.hpp>
#include <order_book/order_book.hpp>
#include <slick/logger.hpp>
#include <chrono>
#include <cstring>

using namespace slick::sim::exch;
using namespace slick::sim;

namespace {
    auto &sym_mgr = SymbolManager::instance();
}

HyperliquidExchange::HyperliquidExchange(
    const nlohmann::json &config,
    slick::SlickQueue<Request> &request_queue,
    slick::SlickQueue<OrderResponse> &order_response_queue,
    slick::SlickQueue<uint8_t> &md_update_queue
)
    : Exchange(Venue::HYPERLIQUID, config, request_queue, order_response_queue, md_update_queue)
{
    if (!config_.contains("md_feeds")) {
        return;
    }

    for (const auto &feed_cfg : config_["md_feeds"]) {
        std::string type = feed_cfg.value("type", "");
        if (type == "hyperliquid_live_ws") {
            use_live_feed_ = true;
            live_ws_base_url_ = feed_cfg.value("base_url", live_ws_base_url_);
            std::vector<std::string> coins;
            if (feed_cfg.contains("coins")) {
                coins = feed_cfg["coins"].get<std::vector<std::string>>();
            }
            auto feed = std::make_shared<md_feed::HyperliquidLiveWSFeed>(this, live_ws_base_url_, coins);
            md_feeds_.emplace_back(feed);
            for (const auto &coin : coins) {
                map_coin_feed_.emplace(coin, feed);
            }
        }
    }
}

void HyperliquidExchange::run() {
    run_.store(true, std::memory_order_release);

    for (auto &feed : md_feeds_) {
        feed->start();
    }

    thread_ = std::thread([this]() {
        while (run_.load(std::memory_order_relaxed)) {
            drainEventQueue();
            processRequest();
        }
    });
}

Symbol* HyperliquidExchange::addSymbol(std::string_view coin) {
    auto *symbol = sym_mgr.createSymbol(coin, Venue::HYPERLIQUID);
    assert(symbol);
    if (!matching_engines_[engine::MatchingEngine::Type::FIFO]) {
        matching_engines_[engine::MatchingEngine::Type::FIFO] =
            std::make_unique<engine::FifoMatchingEngine>(order_response_queue_);
    }
    symbol->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
    symbol->createOrderBook<OrderBookType::L2>();
    return symbol;
}

// Called from hyperliquid WS thread — push into event queue
void HyperliquidExchange::onL2BookUpdate(const nlohmann::json& msg) {
    if (!msg.contains("data")) return;
    std::lock_guard lock(event_queue_mutex_);
    event_queue_.push_back({FeedEvent::Type::L2_BOOK, msg["data"]});
}

void HyperliquidExchange::onTradesUpdate(const nlohmann::json& msg) {
    if (!msg.contains("data")) return;
    std::lock_guard lock(event_queue_mutex_);
    event_queue_.push_back({FeedEvent::Type::TRADES, msg["data"]});
}

void HyperliquidExchange::onHyperliquidDisconnected() {
    LOG_WARN("Hyperliquid WebSocket feed disconnected");
    // Mark all tracked symbols as pending so snapshots are re-sent on reconnect
    for (auto &[coin, feed] : map_coin_feed_) {
        auto *symbol = sym_mgr.getSymbol(coin);
        if (symbol) {
            pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::L2_BOOK)].emplace(symbol);
            pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::TRADES)].emplace(symbol);
        }
    }
}

// Drain WS events onto the exchange thread
void HyperliquidExchange::drainEventQueue() {
    {
        std::lock_guard lock(event_queue_mutex_);
        if (event_queue_.empty()) return;
        event_drain_buf_.swap(event_queue_);
    }
    for (auto &evt : event_drain_buf_) {
        if (evt.type == FeedEvent::Type::L2_BOOK) {
            processL2BookEvent(evt.data);
        } else {
            processTradesEvent(evt.data);
        }
    }
    event_drain_buf_.clear();
}

void HyperliquidExchange::processL2BookEvent(const nlohmann::json& data) {
    if (!data.contains("coin") || !data.contains("levels")) return;

    const std::string coin = data["coin"].get<std::string>();

    auto *symbol = sym_mgr.getSymbol(coin);
    if (!symbol) {
        symbol = addSymbol(coin);
    }

    // levels[0] = bids, levels[1] = asks
    const auto& levels_arr = data["levels"];
    if (levels_arr.size() < 2) return;

    uint64_t event_time_ms = data.value("time", uint64_t{0});
    uint64_t event_time_ns = event_time_ms * utils::ONE_MILLISECOND_NS;

    symbol->order_book_->clearMDOrders();

    auto process_side = [&](const nlohmann::json& side_levels, Side side) {
        auto book_side = to_book_side(side);
        for (const auto& level : side_levels) {
            double px = std::stod(level["px"].get<std::string>());
            double sz = std::stod(level["sz"].get<std::string>());
            auto price = to_price_t(px);
            auto qty   = to_qty_t(sz);
            if (qty <= 0) continue;
            auto order_id = utils::nextOrderId();
            if (symbol->matching_engine_) {
                symbol->matching_engine_->match(side, order_id, price, qty, *symbol->order_book_, event_time_ns, 0);
            }
            if (qty > 0) {
                symbol->order_book_->addOrder(order_id, book_side, price, qty, event_time_ns, 0, false);
            }
        }
    };

    process_side(levels_arr[0], Side::BUY);
    process_side(levels_arr[1], Side::SELL);

    symbol->order_book_->setLastUpdate(event_time_ns, 0);

    auto &pending_l2 = pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::L2_BOOK)];
    auto it = pending_l2.find(symbol);
    if (it != pending_l2.end()) {
        // First snapshot for a pending subscription — send as SUB_RESPONSE
        symbol->order_book_->populateL2SubscriptionResponse(
            md_update_queue_, static_cast<uint8_t>(HyperliquidChannel::L2_BOOK));
        pending_l2.erase(it);
    } else {
        symbol->order_book_->populateL2Snapshot(md_update_queue_);
    }

    symbol->md_level_update_cache_.clear();
    symbol->md_order_update_cache_.clear();
    LOG_TRACE("Hyperliquid l2Book updated: {}", coin);
}

void HyperliquidExchange::processTradesEvent(const nlohmann::json& data) {
    if (!data.is_array() || data.empty()) return;

    const std::string coin = data[0].value("coin", "");
    if (coin.empty()) return;

    auto *symbol = sym_mgr.getSymbol(coin);
    if (!symbol) return;

    trade_update_buffer_.clear();
    for (const auto& trade : data) {
        std::string side_str = trade.value("side", "B");
        Side side = (side_str == "B") ? Side::BUY : Side::SELL;
        double px = std::stod(trade["px"].get<std::string>());
        double sz = std::stod(trade["sz"].get<std::string>());
        uint64_t time_ms = trade.value("time", uint64_t{0});

        trade_update_buffer_.push_back(MDTrade{
            .event_time = time_ms * utils::ONE_MILLISECOND_NS,
            .seq_num    = 0,
            .price      = to_price_t(px),
            .qty        = to_qty_t(sz),
            .flags      = UpdateFlags::F_NONE,
            .side       = side,
        });
    }

    if (!trade_update_buffer_.empty()) {
        publishMDTrades(coin.c_str(), trade_update_buffer_);
    }
}

void HyperliquidExchange::handleMdSubscription(const Request &request) {
    const auto &msg = request.md_subscription;
    std::string coin = request.symbol;

    auto *symbol = sym_mgr.getSymbol(coin);
    bool is_new = false;
    if (!symbol) {
        symbol = addSymbol(coin);
        is_new = true;
    }

    symbol->num_subscriptions_.fetch_add(1, std::memory_order_relaxed);

    auto channel = static_cast<HyperliquidChannel>(msg.channel);

    if (use_live_feed_) {
        if (is_new) {
            // Subscribe to live feed for this coin
            auto it_feed = map_coin_feed_.find(coin);
            if (it_feed == map_coin_feed_.end()) {
                // No existing feed — use or create one
                std::shared_ptr<md_feed::HyperliquidLiveWSFeed> live_feed;
                if (!md_feeds_.empty()) {
                    live_feed = std::dynamic_pointer_cast<md_feed::HyperliquidLiveWSFeed>(md_feeds_.front());
                }
                if (!live_feed) {
                    auto new_feed = std::make_shared<md_feed::HyperliquidLiveWSFeed>(this, live_ws_base_url_);
                    md_feeds_.emplace_back(new_feed);
                    new_feed->start();
                    live_feed = new_feed;
                }
                live_feed->addCoin(coin);
                map_coin_feed_.emplace(coin, live_feed);
            } else {
                auto live_feed = std::dynamic_pointer_cast<md_feed::HyperliquidLiveWSFeed>(it_feed->second);
                if (live_feed) {
                    live_feed->addCoin(coin);
                }
            }
            // Mark as pending — snapshot will arrive on the next l2Book WS message
            pending_md_subscription_[static_cast<size_t>(channel)].emplace(symbol);
        } else {
            auto &pending = pending_md_subscription_[static_cast<size_t>(channel)];
            if (pending.find(symbol) == pending.end()) {
                // Already have a snapshot, send it immediately
                if (channel == HyperliquidChannel::L2_BOOK) {
                    symbol->order_book_->populateL2SubscriptionResponse(
                        md_update_queue_, static_cast<uint8_t>(channel));
                }
            }
        }
    } else {
        // No live feed — send snapshot from whatever is in the book
        if (channel == HyperliquidChannel::L2_BOOK) {
            symbol->order_book_->populateL2SubscriptionResponse(
                md_update_queue_, static_cast<uint8_t>(channel));
        }
    }
}

void HyperliquidExchange::handleMdUnsubscription(const Request &request) {
    std::string coin = request.symbol;
    auto *symbol = sym_mgr.getSymbol(coin);
    if (!symbol) return;

    if (symbol->num_subscriptions_.fetch_sub(1, std::memory_order_relaxed) == 1) {
        auto it_feed = map_coin_feed_.find(coin);
        if (it_feed != map_coin_feed_.end()) {
            // Don't stop the feed entirely; just stop tracking this coin's subscription
            map_coin_feed_.erase(it_feed);
        }
    }
}
