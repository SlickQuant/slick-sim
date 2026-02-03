#pragma once

#include "exchange.hpp"
#include <slick/logger.hpp>
#include <coinbase/websocket.hpp>
#include <queue>
#include <array>
#include <unordered_map>
#include <utils/ring_buffer.hpp>

namespace slick::sim::exch {

enum class EventType : uint8_t { LEVEL_UPDATE, TRADE };

class CoinbaseExchange : public Exchange, public coinbase::UserThreadWebsocketCallbacks {
public:
    CoinbaseExchange(
        const nlohmann::json &config,
        slick::SlickQueue<Request> &request_queue,
        slick::SlickQueue<OrderResponse> &order_response_queue,
        slick::SlickQueue<uint8_t> &md_update_queue
    );

    ~CoinbaseExchange() override = default;

    void run() override;

    void onLevel2Snapshot(const coinbase::Level2UpdateBatch& snapshot) override;
    void onLevel2Updates(const coinbase::Level2UpdateBatch& updates) override;
    void onMarketTradesSnapshot(const std::vector<coinbase::MarketTrade>& snapshots) override;
    void onMarketTrades(const std::vector<coinbase::MarketTrade>& trades) override;
    void onTickerSnapshot(uint64_t seq_num, uint64_t timestamp, const std::vector<coinbase::Ticker>& tickers) override {}
    void onTickers(uint64_t seq_num, uint64_t timestamp, const std::vector<coinbase::Ticker>& tickers) override {}
    void onCandlesSnapshot(uint64_t seq_num, uint64_t timestamp, const std::vector<coinbase::Candle>& candles) override {}
    void onCandles(uint64_t seq_num, uint64_t timestamp, const std::vector<coinbase::Candle>& candles) override {}
    void onStatusSnapshot(uint64_t seq_num, uint64_t timestamp, const std::vector<coinbase::Status>& status) override {}
    void onStatus(uint64_t seq_num, uint64_t timestamp, const std::vector<coinbase::Status>& status) override {}
    void onMarketDataGap() override;
    void onUserDataGap() override;
    void onUserDataSnapshot(uint64_t seq_num, const std::vector<coinbase::Order>& orders, const std::vector<coinbase::PerpetualFuturePosition>& perpetual_future_positions, const std::vector<coinbase::ExpiringFuturePosition>& expiring_future_positions) override {}
    void onOrderUpdates(uint64_t seq_num, const std::vector<coinbase::Order>& orders) override {}
    void onMarketDataError(std::string err) override;
    void onUserDataError(std::string err) override;

protected:
    void handleMdSubscription(const Request &request) override;
    void handleMdUnsubscription(const Request &request) override;

private:
    Symbol* addSymbol(std::string_view product_id);
    void processSequencedEvents();
    
    struct Event {
        EventType type;
        uint64_t received_time;     // When we received it (system time)
        uint64_t event_time;        // Coinbase's exchange timestamp
        uint64_t sequence_id;       // Internal counter for tiebreaking
        price_t price;
        qty_t qty;
        Side side;
    };

    void dispatchEvent(Symbol* symbol, const Event& event);
    void populateMDTradesResponse(Symbol* symbol);

private:
    std::vector<std::shared_ptr<md_feed::MDFeed>> md_feeds_;
    std::unordered_map<std::string, std::shared_ptr<md_feed::MDFeed>> map_symbol_feed_;
    std::array<std::unordered_set<Symbol*>, static_cast<size_t>(coinbase::WebSocketChannel::__COUNT__)> pending_md_subscription_;

    struct EventCompare {
        bool operator()(const Event& a, const Event& b) const {
            if (a.event_time != b.event_time) {
                return a.event_time > b.event_time;
            }
            return a.sequence_id > b.sequence_id;
        }
    };
    bool use_live_feed_ = false;

    // Per-symbol event sequencing state
    struct SymbolEventState {
        std::priority_queue<Event, std::vector<Event>, EventCompare> pending_events;
        uint64_t last_event_time = 0;
        uint64_t next_sequence_id = 0;
    };
    std::unordered_map<Symbol*, SymbolEventState> symbol_event_state_;
    std::unordered_map<Symbol*, utils::RingBuffer<Event>> trade_snapshots_;
    std::vector<MDLevel> level_update_buffer_;
    std::vector<MDTrade> trade_update_buffer_;
};

}   // end namespace slick::sim::exch