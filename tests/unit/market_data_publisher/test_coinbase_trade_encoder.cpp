#include <gtest/gtest.h>
#include <market_data_publisher/coinbase_trade_encoder.hpp>
#include <common/market_data.hpp>
#include <common/messages.hpp>
#include <common/types.hpp>
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

// ===========================================================================
// encode_market_trade — one executed trade as an "update" event
// ===========================================================================

TEST(CoinbaseTradeEncoder, Update_HasVenueEnvelope) {
    SummaryBuffer summary(42, 60455.46, 0.00011628, Side::BUY);

    auto msg = encode_market_trade("BTC-USD", summary.get());

    EXPECT_EQ(msg["channel"], "market_trades");
    EXPECT_EQ(msg["client_id"], "");
    EXPECT_TRUE(msg.contains("timestamp"));
    // sequence_num is stamped per socket by the publisher, not by the encoder.
    EXPECT_FALSE(msg.contains("sequence_num"));
    ASSERT_EQ(msg["events"].size(), 1u);
    EXPECT_EQ(msg["events"][0]["type"], "update");
    ASSERT_EQ(msg["events"][0]["trades"].size(), 1u);
}

TEST(CoinbaseTradeEncoder, Update_NumericFieldsAreStrings) {
    SummaryBuffer summary(42, 60455.46, 0.00011628, Side::BUY);

    auto trade = encode_market_trade("BTC-USD", summary.get())["events"][0]["trades"][0];

    EXPECT_TRUE(trade["price"].is_string());
    EXPECT_TRUE(trade["size"].is_string());
    EXPECT_TRUE(trade["trade_id"].is_string());
    EXPECT_EQ(trade["price"], "60455.46");
    EXPECT_EQ(trade["size"], "0.00011628");
    EXPECT_EQ(trade["trade_id"], "42");
    EXPECT_EQ(trade["product_id"], "BTC-USD");
}

TEST(CoinbaseTradeEncoder, Update_SideIsAggressor) {
    SummaryBuffer buy(1, 100.0, 1.0, Side::BUY);
    SummaryBuffer sell(2, 100.0, 1.0, Side::SELL);

    EXPECT_EQ(encode_market_trade("BTC-USD", buy.get())["events"][0]["trades"][0]["side"], "BUY");
    EXPECT_EQ(encode_market_trade("BTC-USD", sell.get())["events"][0]["trades"][0]["side"], "SELL");
}

TEST(CoinbaseTradeEncoder, Update_TimeIsMicrosecondIso8601) {
    SummaryBuffer summary(1, 100.0, 1.0, Side::BUY, 1'700'000'000'123'456'000ULL);

    auto time = encode_market_trade("BTC-USD", summary.get())["events"][0]["trades"][0]["time"]
                    .get<std::string>();

    EXPECT_EQ(time.back(), 'Z');
    EXPECT_NE(time.find('T'), std::string::npos);
    // ".123456Z" — six subsecond digits, matching the venue.
    EXPECT_EQ(time.substr(time.size() - 8), ".123456Z");
}

// ===========================================================================
// encode_market_trades — snapshot and update blocks
// ===========================================================================

TEST(CoinbaseTradeEncoder, Snapshot_IsNewestFirst) {
    // Stored chronologically, as the history keeps them.
    std::vector<MDTrade> trades = {
        makeTrade(1, 100.0, 1.0, Side::BUY, 1'700'000'000'000'000'000ULL),
        makeTrade(2, 101.0, 2.0, Side::SELL, 1'700'000'001'000'000'000ULL),
        makeTrade(3, 102.0, 3.0, Side::BUY, 1'700'000'002'000'000'000ULL),
    };

    auto msg = encode_market_trades("BTC-USD", "snapshot", trades.data(), 3, true);

    EXPECT_EQ(msg["events"][0]["type"], "snapshot");
    auto encoded = msg["events"][0]["trades"];
    ASSERT_EQ(encoded.size(), 3u);
    EXPECT_EQ(encoded[0]["trade_id"], "3");
    EXPECT_EQ(encoded[1]["trade_id"], "2");
    EXPECT_EQ(encoded[2]["trade_id"], "1");
}

TEST(CoinbaseTradeEncoder, UpdateBlock_StaysChronological) {
    std::vector<MDTrade> trades = {
        makeTrade(7, 100.0, 1.0, Side::BUY, 1'700'000'000'000'000'000ULL),
        makeTrade(8, 101.0, 2.0, Side::SELL, 1'700'000'001'000'000'000ULL),
    };

    auto msg = encode_market_trades("BTC-USD", "update", trades.data(), 2, false);

    EXPECT_EQ(msg["events"][0]["type"], "update");
    auto encoded = msg["events"][0]["trades"];
    ASSERT_EQ(encoded.size(), 2u);
    EXPECT_EQ(encoded[0]["trade_id"], "7");
    EXPECT_EQ(encoded[1]["trade_id"], "8");
}

TEST(CoinbaseTradeEncoder, EmptyHistory_StillEmitsSnapshotEvent) {
    auto msg = encode_market_trades("BTC-USD", "snapshot", nullptr, 0, true);

    ASSERT_EQ(msg["events"].size(), 1u);
    EXPECT_EQ(msg["events"][0]["type"], "snapshot");
    EXPECT_TRUE(msg["events"][0]["trades"].is_array());
    EXPECT_TRUE(msg["events"][0]["trades"].empty());
}
