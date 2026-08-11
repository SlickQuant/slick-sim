#include "exch_hyperliquid.hpp"
#include <md_feed/hyperliquid_live_ws_feed.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <common/symbol_manager.hpp>
#include <utils/timestamp.hpp>
#include <utils/order.hpp>
#include <order_book/order_book.hpp>
#include <slick/logger.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <market_data_publisher/hyperliquid_publisher.hpp>
#include <order_gateway/hyperliquid_rest_order_gateway.hpp>

using namespace slick::sim;
using namespace slick::sim::exch;
using namespace slick::sim::order_gateway;
using namespace slick::sim::md_publisher;

namespace {
    auto &sym_mgr = SymbolManager::instance();
}

HyperliquidExchange::HyperliquidExchange(const nlohmann::json &config)
    : Exchange(Venue::HYPERLIQUID, config)
{
    md_publisher_ = std::make_unique<HyperliquidPublisher>(config["md_publisher"], request_queue_, md_queue_);

    order_gateways_.emplace_back(
        std::make_unique<HyperliquidRestOrderGateway>(config["order_gateway"], request_queue_, response_queue_)
    );

    if (!config.contains("md_feeds")) {
        return;
    }

    for (const auto &feed_cfg : config["md_feeds"]) {
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

void HyperliquidExchange::start() {
    if (!enabled_) {
        LOG_WARN("Exchange {} is disabled, skipping start", to_string(venue_));
        return;
    }
    
    run_.store(true, std::memory_order_release);

    for (auto &og : order_gateways_) {
        og->start();
    }
    md_publisher_->start();

    if (md_feed_) {
        md_feed_->start();
    }

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
            std::make_unique<engine::FifoMatchingEngine>(response_queue_);
    }
    symbol->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
    symbol->createOrderBook<OrderBookType::L2>();
    return symbol;
}

// Called from hyperliquid WS thread — push into event queue.
// `msg` is already the unwrapped payload from HyperliquidLiveWSFeed::on_l2:
// either a full snapshot ({"coin","time","levels"}) or a decoded diff
// ({"c","l","r","t"}) — processL2BookEvent sniffs which shape it got.
void HyperliquidExchange::onL2BookUpdate(const nlohmann::json& msg) {
    event_queue_.push_back({FeedEvent::Type::L2_BOOK, msg});
}

void HyperliquidExchange::onTradesUpdate(const nlohmann::json& msg) {
    if (!msg.contains("data")) return;
    event_queue_.push_back({FeedEvent::Type::TRADES, msg["data"]});
}

void HyperliquidExchange::onHyperliquidDisconnected() {
    LOG_WARN("Hyperliquid WebSocket feed disconnected");
    // Mark all tracked symbols as pending so snapshots are re-sent on reconnect
    for (auto &[coin, feed] : map_coin_feed_) {
        auto *symbol = sym_mgr.getSymbol(coin);
        if (symbol) {
            pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::L2_BOOK)].emplace(symbol);
            pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::L2)].emplace(symbol);
            pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::TRADES)].emplace(symbol);
        }
    }
}

// Drain WS events onto the exchange thread
void HyperliquidExchange::drainEventQueue() {
    for (auto &feed : md_feeds_) {
        reinterpret_pointer_cast<md_feed::HyperliquidLiveWSFeed>(feed)->getInfo()->dispatch(100);
    }
    for (auto &evt : event_queue_) {
        if (evt.type == FeedEvent::Type::L2_BOOK) {
            processL2BookEvent(evt.data);
        } else {
            processTradesEvent(evt.data);
        }
    }
    event_queue_.clear();
}

// Dispatches to the snapshot or incremental-diff handler based on which
// shape the (already-unwrapped, see onL2BookUpdate) payload has — the two
// never share a top-level key.
void HyperliquidExchange::processL2BookEvent(const nlohmann::json& data) {
    if (data.contains("levels")) {
        processL2Snapshot(data);
    } else if (data.contains("l") && data.contains("r")) {
        processL2Diff(data);
    }
}

// Full order-book snapshot (documented l2Book shape, and also what
// Hyperliquid's undocumented l2 channel sends as the very first message
// after subscribing). The whole book is authoritative here, so it's safe —
// and necessary, to re-validate crossing — to clear all phantom liquidity
// and rebuild every level from scratch. Also resets the "r" index-resolution
// mirror (l2_ref_levels_) from this snapshot's own (already-ordered) levels.
void HyperliquidExchange::processL2Snapshot(const nlohmann::json& data) {
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

    auto &ref = l2_ref_levels_[symbol];
    ref[0].clear();
    ref[1].clear();

    auto process_side = [&](const nlohmann::json& side_levels, Side side, std::vector<price_t>& ref_side) {
        auto book_side = to_book_side(side);
        for (const auto& level : side_levels) {
            double px = std::stod(level["px"].get<std::string>());
            double sz = std::stod(level["sz"].get<std::string>());
            auto price = to_price_t(px);
            auto qty   = to_qty_t(sz);
            if (qty <= 0) [[unlikely]] continue;
            auto order_id = utils::nextOrderId();
            if (symbol->matching_engine_) {
                symbol->matching_engine_->match(side, order_id, price, qty, *symbol->order_book_, event_time_ns, 0);
            }
            if (qty > 0) {
                symbol->order_book_->addOrder(order_id, book_side, price, qty, event_time_ns, 0, 0, true);
            }
            // Snapshot levels already arrive best-first, so appending
            // preserves the correct order.
            ref_side.push_back(price);
        }
    };

    process_side(levels_arr[0], Side::BUY, ref[0]);
    process_side(levels_arr[1], Side::SELL, ref[1]);

    symbol->order_book_->setLastUpdate(event_time_ns, 0);

    finalizeL2Event(symbol);

    symbol->md_level_update_cache_.clear();
    symbol->md_order_update_cache_.clear();
    LOG_TRACE("Hyperliquid l2 snapshot applied: {}", coin);
}

// Incremental diff from Hyperliquid's undocumented l2 channel: "l" gives the
// new absolute quantity at changed/added price levels; "r" gives indices
// into the *previous* per-side ordered price array (l2_ref_levels_) for
// levels that were removed entirely. Only the touched levels are mutated —
// everything else in the book is left exactly as it was.
void HyperliquidExchange::processL2Diff(const nlohmann::json& data) {
    if (!data.contains("c")) return;

    const auto coin = data["c"].get<std::string_view>();

    auto *symbol = sym_mgr.getSymbol(coin);
    if (!symbol) {
        symbol = addSymbol(coin);
    }

    // levels[0] = bids, levels[1] = asks
    const auto& levels_arr = data["l"];
    if (levels_arr.size() < 2) return;

    uint64_t event_time_ms = data.value("t", uint64_t{0});
    uint64_t event_time_ns = event_time_ms * utils::ONE_MILLISECOND_NS;

    auto &ref = l2_ref_levels_[symbol];

    auto apply_removals = [&](const nlohmann::json& indices, Side side, std::vector<price_t>& ref_side) {
        // Resolve every index against the array as it stood *before* this
        // event's mutations (removing while iterating would shift later
        // indices), then apply.
        std::vector<price_t> removed_prices;
        removed_prices.reserve(indices.size());
        for (const auto& idx_json : indices) {
            auto idx = idx_json.get<size_t>();
            if (idx < ref_side.size()) {
                removed_prices.push_back(ref_side[idx]);
            } else {
                LOG_WARN("Hyperliquid l2 diff: removed-level index {} out of range (have {}) for {}",
                         idx, ref_side.size(), coin);
            }
        }
        for (price_t price : removed_prices) {
            applyPhantomLevelUpdate(symbol, side, price, 0, event_time_ns);
            if (auto it = std::find(ref_side.begin(), ref_side.end(), price); it != ref_side.end()) {
                ref_side.erase(it);
            }
        }
    };

    if (const auto& removed = data.value("r", nlohmann::json::array()); removed.is_array() && removed.size() == 2) {
        apply_removals(removed[0], Side::BUY, ref[0]);
        apply_removals(removed[1], Side::SELL, ref[1]);
    }

    auto apply_changes = [&](const nlohmann::json& side_levels, Side side, std::vector<price_t>& ref_side) {
        for (const auto& level : side_levels) {
            double px = std::stod(level["p"].get<std::string>());
            double sz = std::stod(level["s"].get<std::string>());
            auto price = to_price_t(px);
            auto qty   = to_qty_t(sz);

            applyPhantomLevelUpdate(symbol, side, price, qty, event_time_ns);

            auto it = std::lower_bound(ref_side.begin(), ref_side.end(), price,
                [side](price_t a, price_t b) { return utils::isPriceBetter(side, a, b); });
            if (it == ref_side.end() || *it != price) {
                ref_side.insert(it, price);
            }
        }
    };

    apply_changes(levels_arr[0], Side::BUY, ref[0]);
    apply_changes(levels_arr[1], Side::SELL, ref[1]);

    symbol->order_book_->setLastUpdate(event_time_ns, 0);

    finalizeL2Event(symbol);

    symbol->md_level_update_cache_.clear();
    symbol->md_order_update_cache_.clear();
    LOG_TRACE("Hyperliquid l2 diff applied: {}", coin);
}

// Sets the phantom (real-market-mirroring) liquidity at (side, price) to
// exactly target_qty, without disturbing any other price level or any order
// belonging to the simulator's own user (tracked separately via
// Symbol::findOrder — phantom orders are never registered there). Mirrors
// the established incremental-update pattern in
// CoinbaseExchange::dispatchEvent (exch_coinbase.cpp).
void HyperliquidExchange::applyPhantomLevelUpdate(
    Symbol* symbol, Side side, price_t price, qty_t target_qty, uint64_t event_time_ns)
{
    auto book_side = to_book_side(side);
    auto &book = *symbol->order_book_;
    auto [level, index] = book.getLevel(book_side, price);

    if (!level) {
        // Brand-new price level — the full target quantity is the delta
        // from an implicit zero, and this price hasn't been checked for
        // crossing the opposing side yet.
        if (target_qty <= 0) return;
        qty_t qty = target_qty;
        auto order_id = utils::nextOrderId();
        if (symbol->matching_engine_) {
            symbol->matching_engine_->match(side, order_id, price, qty, book, event_time_ns, 0);
        }
        if (qty > 0) {
            book.addOrder(order_id, book_side, price, qty, event_time_ns, 0, 0, true);
        }
        return;
    }

    // Existing level: already known non-crossing, so no need to re-match —
    // just adjust the phantom-only quantity (getMDLevelQty excludes the
    // user's own resting quantity at this price) toward the target.
    auto md_level_qty = book.getMDLevelQty(price);
    if (target_qty > md_level_qty) {
        auto size = target_qty - md_level_qty;
        book.addBookOrder(utils::nextOrderId(), book_side, price, size, event_time_ns, 0);
    } else if (target_qty < md_level_qty) {
        auto diff = md_level_qty - target_qty;
        for (auto it = level->orders.rbegin(); it != level->orders.rend(); ++it) {
            auto &order = *it;
            if (symbol->findOrder(order.order_id)) {
                // Belongs to the simulator's own user — never touch it.
                continue;
            }
            auto to_reduce = std::min<qty_t>(diff, order.quantity);
            book.modifyOrder(order.order_id, price, order.quantity - to_reduce,
                              event_time_ns, order.priority, 0, diff == to_reduce);
            diff -= to_reduce;
            if (diff == 0 || book.getMDLevelQty(price) == 0) {
                break;
            }
        }
    }
}

// Shared tail for both processL2Snapshot and processL2Diff: dispatches the
// first (SUB_RESPONSE) or routine (BOOK_SNAPSHOT) downstream update for the
// L2_BOOK/L2 channels. See the design note above pending_md_subscription_.
void HyperliquidExchange::finalizeL2Event(Symbol* symbol) {
    // L2_BOOK and L2 (undocumented, compressed-diff variant) are independent
    // downstream channels over the same book data — a symbol can have a
    // pending subscriber on one while already having an active subscriber on
    // the other, so each channel's first-snapshot delivery is tracked
    // separately.
    bool l2book_was_pending = false;
    bool l2_was_pending = false;

    auto &pending_l2book = pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::L2_BOOK)];
    if (auto it = pending_l2book.find(symbol); it != pending_l2book.end()) {
        symbol->order_book_->populateL2SubscriptionResponse(
            md_queue_, static_cast<uint8_t>(HyperliquidChannel::L2_BOOK));
        pending_l2book.erase(it);
        l2book_was_pending = true;
    }

    auto &pending_l2 = pending_md_subscription_[static_cast<size_t>(HyperliquidChannel::L2)];
    if (auto it = pending_l2.find(symbol); it != pending_l2.end()) {
        symbol->order_book_->populateL2SubscriptionResponse(
            md_queue_, static_cast<uint8_t>(HyperliquidChannel::L2));
        pending_l2.erase(it);
        l2_was_pending = true;
    }

    if (!l2book_was_pending || !l2_was_pending) {
        // Routine update — the publisher filters/formats per actual subscribers.
        symbol->order_book_->populateL2Snapshot(md_queue_);
    }
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
                if (channel == HyperliquidChannel::L2_BOOK || channel == HyperliquidChannel::L2) {
                    symbol->order_book_->populateL2SubscriptionResponse(
                        md_queue_, static_cast<uint8_t>(channel));
                }
            }
        }
    } else {
        // No live feed — send snapshot from whatever is in the book
        if (channel == HyperliquidChannel::L2_BOOK || channel == HyperliquidChannel::L2) {
            symbol->order_book_->populateL2SubscriptionResponse(
                md_queue_, static_cast<uint8_t>(channel));
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
