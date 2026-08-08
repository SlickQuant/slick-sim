#pragma once

#include "exchange.hpp"
#include <md_feed/hyperliquid_live_ws_feed.hpp>
#include <utils/price.hpp>
#include <slick/logger.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>

namespace slick::sim::exch {

enum class HyperliquidChannel : uint8_t {
    L2_BOOK = 0,
    TRADES  = 1,
    L2      = 2,
    __COUNT__ = 3,
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
    void processL2Snapshot(const nlohmann::json& data);
    void processL2Diff(const nlohmann::json& data);
    void applyPhantomLevelUpdate(Symbol* symbol, Side side, price_t price,
                                  qty_t target_qty, uint64_t event_time_ns);
    void finalizeL2Event(Symbol* symbol);
    void processTradesEvent(const nlohmann::json& data);

    // Feed management
    std::vector<std::shared_ptr<md_feed::MDFeed>> md_feeds_;
    std::unordered_map<std::string, std::shared_ptr<md_feed::MDFeed>> map_coin_feed_;

    // Per-channel pending subscriptions (symbols waiting for first snapshot)
    std::array<std::unordered_set<Symbol*>,
               static_cast<size_t>(HyperliquidChannel::__COUNT__)> pending_md_subscription_;

    // coin's Symbol* -> (bid, ask) ordered price mirror — reflects the
    // upstream exchange's OWN l2 book ordering (real-market only),
    // independent of user orders resting in our simulated book. Used solely
    // to resolve "r" (removed-by-index) diff entries back to a price.
    std::unordered_map<Symbol*, std::array<std::vector<price_t>, 2>> l2_ref_levels_;

    bool use_live_feed_ = false;
    std::string live_ws_base_url_{"https://api.hyperliquid.xyz"};

    // Cross-thread event queue: WS callbacks push here, exchange thread drains
    struct FeedEvent {
        enum class Type : uint8_t { L2_BOOK, TRADES };
        Type type;
        nlohmann::json data;
    };
    std::vector<FeedEvent> event_queue_;

    std::vector<MDLevel> level_update_buffer_;
    std::vector<MDTrade> trade_update_buffer_;
};

}   // end namespace slick::sim::exch
