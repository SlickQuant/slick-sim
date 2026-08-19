#pragma once

#include <slick/queue.h>
#include <slick/object_pool.h>
#include <md_feed/md_feed.hpp>
#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <array>
#include <atomic>
#include <nlohmann/json.hpp>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <common/market_data.hpp>
#include <order_book/order_book.hpp>
#include <matching_engine/matching_engine.hpp>
#include <order_gateway/order_gateway.hpp>
#include <market_data_publisher/market_data_publisher.hpp>

namespace slick::sim::exch {

struct Symbol;

class Exchange {
public:
    Exchange(Venue venue, const nlohmann::json &config);

    virtual ~Exchange() {
        stop();
    }

    virtual void start();
    virtual void stop();

    queue<Request>& request_queue() noexcept {
        return request_queue_;
    }
    queue<OrderResponse>& response_queue() noexcept {
        return response_queue_;
    }
    queue<uint8_t>& md_queue() noexcept {
        return md_queue_;
    }
    
protected:
    void processRequest();
    void handleNewOrderRequest(const Request &request);
    void handleModifyOrderRequest(const Request &request);
    void handleCancelOrderRequest(const Request &request);
    void rejectMdSubscription(const Request &request, MDSubscriptionRejectReason reason);
    void rejectNewOrderRequest(const Request &request, OrdRejectReason reason);
    void rejectModifyOrderRequest(const Request &request, OrdRejectReason reason);
    void rejectCancelOrderRequest(const Request &request, OrdRejectReason reason);
    void sendOrderNewPending(const Order *order, time_t request_time);
    void sendOrderReplacePending(const Order *order, time_t request_time);
    void sendOrderCancelPending(const Order *order, time_t request_time);
    void publishMDBookUpdate(symid_t sid, OrderBook &order_book, const std::array<uint8_t, 2> &indices);
    void publishLevelUpdate(const symbol_name_t &symbol, const std::vector<MDLevel> &level_updates);
    void publishMDOrderUpdate(const symbol_name_t &symbol, const std::vector<MDOrder> &order_updates);
    // NOTE: there is deliberately no raw trade relay. A venue trade print is
    // replayed through the matching engine instead, and only the resulting fills
    // are published - see publishTradeSummary and the venues' trade event handlers.

    /// Publishes one trade onto the instrument's public tape and records it in
    /// the symbol's recent-trade history, which serves the subscribe snapshot.
    /// Every trade the simulator prints goes through here.
    void publishTradeSummary(Symbol *symbol, const TradeSummaryInfo &trade_summary);

    /// Frames the SUB_RESPONSE for a venue's trade channel from the symbol's
    /// recent-trade history, split against the book snapshot the subscriber
    /// receives with it. See MDTradeSnapshotResponse.
    void publishTradeSubscriptionResponse(Symbol *symbol, uint8_t channel);

    /// Adjusts the phantom (market-mirroring) quantity at an existing (side,
    /// price) level toward target_qty, leaving the simulator's own orders alone.
    /// Offsets any reduction a trade print already applied at that price so the
    /// same quantity is not removed twice.
    void reconcilePhantomQty(Symbol *symbol, Side side, price_t price, qty_t target_qty,
                             time_t event_time, uint64_t seq_num, bool end_event);

    /// Public trade ids, one independent sequence per venue. Exchange-thread
    /// only, so a plain increment is enough.
    uint64_t nextVenueTradeId() noexcept { return next_trade_id_++; }

    /// Resolves an order's `user_id` to the integer identity self-match
    /// prevention compares. Ids are assigned on first sight and stable for the
    /// life of the process; an empty `user_id` yields -1, which `isSelfMatch`
    /// treats as "no identity" and never matches, so an unauthenticated client
    /// is left out of SMP rather than being lumped in with every other one.
    int clientIdFor(const char *user_id);

    virtual void handleMdSubscription(const Request &/* request */) {}
    virtual void handleMdUnsubscription(const Request &/* request */) {}


protected:
    Venue venue_;
    queue<Request> request_queue_;
    queue<OrderResponse> response_queue_;
    queue<uint8_t> md_queue_;
    std::shared_ptr<md_feed::MDFeed> md_feed_;
    std::thread thread_;
    std::thread md_thread_;
    std::atomic_bool run_{true};
    uint_fast64_t order_request_cursor_{0};
    std::array<std::unique_ptr<engine::MatchingEngine>, engine::MatchingEngine::Type::__count__> matching_engines_;
    std::vector<std::unique_ptr<order_gateway::OrderGateway>> order_gateways_;
    std::unique_ptr<md_publisher::MarketDataPublisher> md_publisher_;
    bool enabled_ = true;
    uint64_t next_trade_id_{1};

    /// Self-match prevention mode applied to every symbol on this venue, from the
    /// `self_match_prevention` config key. Stamped onto each Symbol as it is created.
    SelfMatchPreventionMode smp_mode_ = SelfMatchPreventionMode::NONE;
    /// Keyed by the same fixed_string width as `AddOrderMessage::user_id`, with
    /// transparent functors so `clientIdFor` can look up by string_view. As a
    /// `std::unordered_map<std::string, int>` every new order built a std::string
    /// key just to probe the map, allocating for any user_id past the small-string
    /// buffer even though the entry was almost always already there.
    std::unordered_map<utils::fixed_string<sizeof(AddOrderMessage::user_id)>, int,
                       utils::StringViewHash, utils::StringViewEq> client_ids_;
    int next_client_id_ = 0;
};

}   // end namespace slick::sim::exch