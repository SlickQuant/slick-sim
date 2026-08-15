#pragma once

#include "exchange.hpp"
#include <slick/stream_buffer_multiplexer.hpp>
#include <coinbase/websocket.hpp>
#include <queue>
#include <array>
#include <unordered_map>
#include <utils/ring_buffer.hpp>

namespace slick::sim::exch {

enum class EventType : uint8_t { LEVEL_UPDATE, TRADE };

using WebSocketClient = coinbase::WebSocketClient;

class CoinbaseExchange : public Exchange, public coinbase::UserThreadWebsocketCallbacks {
public:
    CoinbaseExchange(const nlohmann::json &config);

    ~CoinbaseExchange() override = default;

    void start() override;

    void onMarketDataConnected(WebSocketClient* client) override;
    void onMarketDataDisconnected(WebSocketClient* client) override;
    void onUserDataConnected(WebSocketClient* client) override;
    void onUserDataDisconnected(WebSocketClient* client) override;
    void onLevel2Snapshot(WebSocketClient* client, uint64_t seq_num, const coinbase::Level2UpdateBatch& snapshot) override;
    void onLevel2Updates(WebSocketClient* client, uint64_t seq_num, const coinbase::Level2UpdateBatch& updates) override;
    void onMarketTradesSnapshot(WebSocketClient* client, uint64_t seq_num, const std::vector<coinbase::MarketTrade>& snapshots) override;
    void onMarketTrades(WebSocketClient* client, uint64_t seq_num, const std::vector<coinbase::MarketTrade>& trades) override;
    void onTickerSnapshot(WebSocketClient* /* client */, uint64_t /* seq_num */, uint64_t /* timestamp */, const std::vector<coinbase::Ticker>& /* tickers */) override {}
    void onTickers(WebSocketClient* /* client */, uint64_t /* seq_num */, uint64_t /* timestamp */, const std::vector<coinbase::Ticker>& /* tickers */) override {}
    void onCandlesSnapshot(WebSocketClient* /* client */, uint64_t /* seq_num */, uint64_t /* timestamp */, const std::vector<coinbase::Candle>& /* candles */) override {}
    void onCandles(WebSocketClient* /* client */, uint64_t /* seq_num */, uint64_t /* timestamp */, const std::vector<coinbase::Candle>& /* candles */) override {}
    void onStatusSnapshot(WebSocketClient* /* client */, uint64_t /* seq_num */, uint64_t /* timestamp */, const std::vector<coinbase::Status>& /* status */) override {}
    void onStatus(WebSocketClient* /* client */, uint64_t /* seq_num */, uint64_t /* timestamp */, const std::vector<coinbase::Status>& /* status */) override {}
    void onMarketDataGap(WebSocketClient* client) override;
    void onUserDataGap(WebSocketClient* client) override;
    void onUserDataSnapshot(WebSocketClient* /* client */, uint64_t /* seq_num */, const std::vector<coinbase::Order>& /* orders */, const std::vector<coinbase::PerpetualFuturePosition>& /* perpetual_future_positions */, const std::vector<coinbase::ExpiringFuturePosition>& /* expiring_future_positions */) override {}
    void onOrderUpdates(WebSocketClient* /* client */, uint64_t /* seq_num */, const std::vector<coinbase::Order>& /* orders */) override {}
    void onMarketDataError(WebSocketClient* client, std::string &&err) override;
    void onUserDataError(WebSocketClient* client, std::string &&err) override;

protected:
    void handleMdSubscription(const Request &request) override;
    void handleMdUnsubscription(const Request &request) override;
    void processSequencedEvents(bool force = false);

private:
    Symbol* addSymbol(std::string_view product_id);
    
    struct Event {
        EventType type;
        uint64_t received_time;     // When we received it (system time)
        uint64_t event_time;        // Coinbase's exchange timestamp
        uint64_t sequence_id;       // Internal counter for tiebreaking
        uint64_t seq_num;
        price_t price;
        qty_t qty;
        Side side;
        UpdateFlags flags;
    };

    // std::priority_queue pops the *greatest* element, so every comparison here is
    // inverted: `a < b` must mean "a is processed after b".
    struct EventCompare {
        bool operator()(const Event& a, const Event& b) const {
            if (a.event_time != b.event_time) {
                // Earliest event time first.
                return a.event_time > b.event_time;
            }
            if (a.type != b.type) {
                // A trade and the level update reflecting it share a timestamp but
                // arrive on independent channels, so sequence_id (arrival order)
                // is arbitrary between them. Always apply the trade first: the
                // level update is then measured against a book that has already
                // absorbed it, instead of reducing the same quantity twice.
                return a.type != EventType::TRADE;
            }
            // Same instant, same kind: pop the *highest* sequence_id first. That
            // reads backwards next to the inverted event_time comparison above, but
            // it is load-bearing — do not "fix" it to match. sequence_id is assigned
            // as each event is enqueued, and the two enqueue loops deliberately walk
            // their batch in opposite directions so that this ordering replays both
            // chronologically:
            //
            //   onLevel2Updates  iterates the batch in REVERSE, so array index 0
            //                    gets the highest id and replays first — Coinbase
            //                    sends level updates oldest-first.
            //   onMarketTrades   iterates FORWARD, so the last array element gets
            //                    the highest id and replays first — Coinbase sends
            //                    market_trades newest-first.
            //
            // Invert it and both batches replay backwards: levels settle on a stale
            // quantity, and trades match against the book and take their public
            // trade ids in reverse.
            return a.sequence_id < b.sequence_id;
        }
    };

    // Per-symbol event sequencing state
    struct SymbolEventState {
        std::priority_queue<Event, std::vector<Event>, EventCompare> pending_events;
        uint64_t last_event_time = 0;
        uint64_t last_sequence_id = 0;
        uint64_t last_seq_num = 0;
        uint64_t next_sequence_id = 0;
        uint64_t last_published_level_seq_num = 0;
    };

    void dispatchEvent(Symbol* symbol, const Event& event, SymbolEventState& state);
    void populateMDTradesResponse(Symbol* symbol);

private:
    slick::stream_buffer_multiplexer ws_mux_;
    std::vector<std::shared_ptr<md_feed::MDFeed>> md_feeds_;
    std::unordered_map<md_feed::MDFeed*, std::unordered_set<std::string>> feed_symbols_;
    std::unordered_map<std::string, std::shared_ptr<md_feed::MDFeed>> map_symbol_feed_;
    std::array<std::unordered_set<Symbol*>, static_cast<size_t>(coinbase::WebSocketChannel::_CHANNEL_COUNT_)> pending_md_subscription_;

    bool use_live_feed_ = false;

    std::unordered_map<Symbol*, SymbolEventState> symbol_event_state_;
    std::vector<MDLevel> level_update_buffer_;
};

}   // end namespace slick::sim::exch
