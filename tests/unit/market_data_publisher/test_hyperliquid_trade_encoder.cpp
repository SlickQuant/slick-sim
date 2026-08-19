#include <gtest/gtest.h>
#include <market_data_publisher/hyperliquid_trade_encoder.hpp>
#include <common/market_data.hpp>
#include <common/messages.hpp>
#include <common/types.hpp>
#include <utils/timestamp.hpp>
#include <vector>

using namespace slick::sim;
using namespace slick::sim::md_publisher;
using json = nlohmann::json;

namespace {

// Expected values are written out in full rather than recomputed with the same
// formatter the code under test uses - a helper mirroring the implementation agrees
// with it however wrong both are, which is how "0.00011628" went out as "0.000116"
// unnoticed.

// TradeSummary ends in a flexible array member, so back it with real storage.
struct SummaryBuffer {
    std::vector<uint8_t> bytes;

    SummaryBuffer(uint64_t trade_id, double price, double qty, Side aggressor_side,
                  uint64_t event_time = 1'700'000'000'000'000'000ULL)
        : bytes(sizeof(TradeSummary) + 2 * sizeof(Trade), 0) {
        auto& summary = get();
        summary.timestamp = static_cast<slick::sim::time_t>(event_time);
        summary.trade_id = trade_id;
        summary.security_id = 1;
        summary.aggressor_side = aggressor_side;
        summary.price = to_price_t(price);
        summary.qty = to_qty_t(qty);
        summary.num_orders = 2;
    }

    TradeSummary& get() { return *reinterpret_cast<TradeSummary*>(bytes.data()); }
};

MDTrade makeTrade(uint64_t trade_id, double price, double qty, Side side, uint64_t event_time) {
    return MDTrade{
        .trade_id = trade_id,
        .event_time = event_time,
        .seq_num = 0,
        .price = to_price_t(price),
        .qty = to_qty_t(qty),
        .flags = UpdateFlags::F_NONE,
        .side = side,
    };
}

}   // namespace

TEST(HyperliquidTradeEncoder, HasVenueEnvelope) {
    SummaryBuffer summary(42, 3000.5, 1.25, Side::BUY);

    auto msg = encode_trade_message("ETH", summary.get());

    EXPECT_EQ(msg["channel"], "trades");
    ASSERT_TRUE(msg["data"].is_array());
    ASSERT_EQ(msg["data"].size(), 1u);
    EXPECT_EQ(msg["data"][0]["coin"], "ETH");
}

TEST(HyperliquidTradeEncoder, PriceAndSizeAreStrings_TidIsNumeric) {
    SummaryBuffer summary(42, 3000.5, 1.25, Side::BUY);

    auto trade = encode_trade_message("ETH", summary.get())["data"][0];

    EXPECT_TRUE(trade["px"].is_string());
    EXPECT_TRUE(trade["sz"].is_string());
    EXPECT_TRUE(trade["tid"].is_number());
    EXPECT_EQ(trade["px"], "3000.5");
    EXPECT_EQ(trade["sz"], "1.25");
    EXPECT_EQ(trade["tid"], 42u);
    // The simulator has no on-chain transaction to reference.
    EXPECT_EQ(trade["hash"], "0x0");
}

TEST(HyperliquidTradeEncoder, SideIsAggressorAsSingleLetter) {
    SummaryBuffer buy(1, 100.0, 1.0, Side::BUY);
    SummaryBuffer sell(2, 100.0, 1.0, Side::SELL);

    EXPECT_EQ(encode_trade_message("ETH", buy.get())["data"][0]["side"], "B");
    EXPECT_EQ(encode_trade_message("ETH", sell.get())["data"][0]["side"], "A");
}

TEST(HyperliquidTradeEncoder, TimeIsMilliseconds) {
    constexpr uint64_t event_time_ns = 1'700'000'000'123'456'000ULL;
    SummaryBuffer summary(1, 100.0, 1.0, Side::BUY, event_time_ns);

    auto time = encode_trade_message("ETH", summary.get())["data"][0]["time"].get<uint64_t>();

    EXPECT_EQ(time, event_time_ns / utils::ONE_MILLISECOND_NS);
}

TEST(HyperliquidTradeEncoder, TradeRun_KeepsGivenOrder) {
    std::vector<MDTrade> trades = {
        makeTrade(7, 100.0, 1.0, Side::BUY, 1'700'000'000'000'000'000ULL),
        makeTrade(8, 101.0, 2.0, Side::SELL, 1'700'000'001'000'000'000ULL),
    };

    auto data = encode_trade_message("ETH", trades.data(), 2)["data"];

    ASSERT_EQ(data.size(), 2u);
    EXPECT_EQ(data[0]["tid"], 7u);
    EXPECT_EQ(data[1]["tid"], 8u);
    EXPECT_EQ(data[1]["side"], "A");
}

TEST(HyperliquidTradeEncoder, EmptyHistory_EmitsEmptyDataArray) {
    auto msg = encode_trade_message("ETH", nullptr, 0);

    EXPECT_EQ(msg["channel"], "trades");
    ASSERT_TRUE(msg["data"].is_array());
    EXPECT_TRUE(msg["data"].empty());
}
