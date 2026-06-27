#pragma once

#include "exchange.hpp"
#include <md_feed/hyperliquid_live_ws_feed.hpp>
#include <slick/logger.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>

namespace slick::sim::exch {

enum class HyperliquidChannel : uint8_t {
    L2_BOOK = 0,
    TRADES  = 1,
    __COUNT__ = 2,
};

class HyperliquidExchange
    : public Exchange
    , public md_feed::HyperliquidFeedCallbacks
{
public:
    HyperliquidExchange(const nlohmann::json &config);
    ~HyperliquidExchange() override = default;

    void start() override;

    // HyperliquidFeedCallbacks — called from the hyperliquid WS thread
    void onL2BookUpdate(const nlohmann::json& msg) override;
    void onTradesUpdate(const nlohmann::json& msg) override;
    void onHyperliquidDisconnected() override;

protected:
    void handleMdSubscription(const Request &request) override;
    void handleMdUnsubscription(const Request &request) override;
    void drainEventQueue();

private:
    Symbol* addSymbol(std::string_view coin);
    void processL2BookEvent(const nlohmann::json& data);
    void processTradesEvent(const nlohmann::json& data);

    // Feed management
    std::vector<std::shared_ptr<md_feed::MDFeed>> md_feeds_;
    std::unordered_map<std::string, std::shared_ptr<md_feed::MDFeed>> map_coin_feed_;

    // Per-channel pending subscriptions (symbols waiting for first snapshot)
    std::array<std::unordered_set<Symbol*>,
               static_cast<size_t>(HyperliquidChannel::__COUNT__)> pending_md_subscription_;

    bool use_live_feed_ = false;
    std::string live_ws_base_url_{"https://api.hyperliquid.xyz"};

    // Cross-thread event queue: WS callbacks push here, exchange thread drains
    struct FeedEvent {
        enum class Type : uint8_t { L2_BOOK, TRADES };
        Type type;
        nlohmann::json data;
    };
    std::mutex event_queue_mutex_;
    std::vector<FeedEvent> event_queue_;
    std::vector<FeedEvent> event_drain_buf_;

    std::vector<MDLevel> level_update_buffer_;
    std::vector<MDTrade> trade_update_buffer_;
};

}   // end namespace slick::sim::exch
