#include <slick/logger.hpp>
#include "hyperliquid_live_ws_feed.hpp"

using namespace slick::sim::md_feed;

void HyperliquidLiveWSFeed::on_l2(const nlohmann::json& msg) {
    const auto data_ptr = msg.find("data");
    if (data_ptr == msg.end()) return;

    try {
        const auto& data = *data_ptr;

        // First message after subscribing is an uncompressed full snapshot
        // under "s" — no decode_l2_diff needed, it's already plain JSON.
        if (const auto s_ptr = data.find("s"); s_ptr != data.end()) {
            callbacks_->onL2BookUpdate(*s_ptr);
            return;
        }

        const auto c_ptr = data.find("c");
        if (c_ptr == data.end() || !c_ptr->is_string()) return;

        const auto diff = hyperliquid::decode_l2_diff(c_ptr->get<std::string>());
        callbacks_->onL2BookUpdate(diff);
    } catch (const std::exception& e) {
        LOG_ERROR("Hyperliquid MD: Failed to parse l2 diff: {}", e.what());
    }
}
