#pragma once

#include "md_feed.hpp"
#include <coinbase/websocket.hpp>
#include <memory>
#include <slick/queue.h>

namespace slick::sim::md_feed {

class CoinbaseLiveWSFeed : public MDFeed
{
    std::shared_ptr<coinbase::WebSocketClient> ws_client_;
    std::vector<std::string> symbols_;

public:
    CoinbaseLiveWSFeed(
        slick::stream_buffer_multiplexer& mux,
        coinbase::UserThreadWebsocketCallbacks *callbacks,
        const std::vector<std::string> &symbols = {})
        : ws_client_(std::make_shared<coinbase::WebSocketClient>(callbacks, mux))
        , symbols_(std::move(symbols))
    {
        ws_client_->logData("logs/coinbase_data");
    }
    ~CoinbaseLiveWSFeed() override = default;

    void start() override;
    void stop() override;

    coinbase::WebSocketClient* getClient() const { return ws_client_.get(); }
};


inline void CoinbaseLiveWSFeed::start() {
    if (!symbols_.empty()) {
        ws_client_->subscribe(symbols_, {coinbase::WebSocketChannel::LEVEL2, coinbase::WebSocketChannel::MARKET_TRADES});
    }
}

inline void CoinbaseLiveWSFeed::stop() {
    if (ws_client_) {
        ws_client_->unsubscribe(symbols_, {coinbase::WebSocketChannel::LEVEL2, coinbase::WebSocketChannel::MARKET_TRADES});
    }
}


}   // end nanespace slick::sim::md_feed