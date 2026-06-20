#pragma once

#include "md_feed.hpp"
#include <hyperliquid/info.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

namespace slick::sim::md_feed {

struct HyperliquidFeedCallbacks {
    virtual void onL2BookUpdate(const nlohmann::json& msg) = 0;
    virtual void onTradesUpdate(const nlohmann::json& msg) = 0;
    virtual void onHyperliquidDisconnected() = 0;
    virtual ~HyperliquidFeedCallbacks() = default;
};

class HyperliquidLiveWSFeed : public MDFeed {
    std::shared_ptr<hyperliquid::Info> info_;
    std::vector<std::string> coins_;
    // coin -> {l2Book sub_id, trades sub_id}
    std::unordered_map<std::string, std::pair<int, int>> sub_ids_;
    HyperliquidFeedCallbacks* callbacks_;
    bool started_ = false;

public:
    HyperliquidLiveWSFeed(HyperliquidFeedCallbacks* callbacks,
                          std::string_view base_url,
                          const std::vector<std::string>& coins = {})
        : info_(std::make_shared<hyperliquid::Info>(base_url, /*skip_ws=*/false))
        , coins_(coins)
        , callbacks_(callbacks)
    {}

    ~HyperliquidLiveWSFeed() override = default;

    void start() override {
        started_ = true;
        for (const auto& coin : coins_) {
            subscribeCoin(coin);
        }
    }

    void stop() override {
        for (auto& [coin, ids] : sub_ids_) {
            info_->unsubscribe({{"type", "l2Book"}, {"coin", coin}}, ids.first);
            info_->unsubscribe({{"type", "trades"}, {"coin", coin}}, ids.second);
        }
        sub_ids_.clear();
        started_ = false;
    }

    // Add a coin subscription at runtime (called by the exchange when a new symbol is subscribed)
    void addCoin(const std::string& coin) {
        if (std::find(coins_.begin(), coins_.end(), coin) == coins_.end()) {
            coins_.push_back(coin);
        }
        if (started_ && sub_ids_.find(coin) == sub_ids_.end()) {
            subscribeCoin(coin);
        }
    }

    hyperliquid::Info* getInfo() const { return info_.get(); }

private:
    void subscribeCoin(const std::string& coin) {
        int l2_id = info_->subscribe(
            {{"type", "l2Book"}, {"coin", coin}},
            [this](const nlohmann::json& msg) { callbacks_->onL2BookUpdate(msg); }
        );
        int trades_id = info_->subscribe(
            {{"type", "trades"}, {"coin", coin}},
            [this](const nlohmann::json& msg) { callbacks_->onTradesUpdate(msg); }
        );
        sub_ids_[coin] = {l2_id, trades_id};
    }
};

}   // end namespace slick::sim::md_feed
