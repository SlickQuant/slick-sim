#include <gtest/gtest.h>
#include <exchange/exch_hyperliquid.hpp>
#include <common/symbol_manager.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <common/messages.hpp>
#include <common/market_data.hpp>
#include <slick/queue.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::exch;
using namespace slick::sim::engine;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// TestableHyperliquidExchange — exposes private/protected methods for testing
// ---------------------------------------------------------------------------
class TestableHyperliquidExchange : public HyperliquidExchange {
public:
    using HyperliquidExchange::HyperliquidExchange;
    void drainEvents() { drainEventQueue(); }
    void subscribeToMD(const Request& req) { handleMdSubscription(req); }
};

// ---------------------------------------------------------------------------
// JSON message helpers
// ---------------------------------------------------------------------------
// Full l2 snapshot shape (documented l2Book, and Hyperliquid's undocumented
// l2 channel's first message after subscribing) — unwrapped, matching what
// HyperliquidLiveWSFeed::on_l2 hands to onL2BookUpdate.
static json makeL2BookMsg(
    const std::string& coin,
    uint64_t time_ms,
    const std::vector<std::pair<double, double>>& bids,
    const std::vector<std::pair<double, double>>& asks)
{
    auto make_side = [](const std::vector<std::pair<double, double>>& lvls) {
        auto arr = json::array();
        for (auto [px, sz] : lvls) {
            arr.push_back({{"px", std::to_string(px)}, {"sz", std::to_string(sz)}, {"n", 1}});
        }
        return arr;
    };
    return {
        {"coin", coin},
        {"time", time_ms},
        {"levels", {make_side(bids), make_side(asks)}}
    };
}

// Incremental l2 diff shape (Hyperliquid's undocumented l2 channel, decoded
// from the compressed "c" field). r_bid_indices/r_ask_indices are indices
// into whatever ordered mirror the exchange has already tracked for that
// side (i.e. the order levels were most recently seeded in).
static json makeL2DiffMsg(
    const std::string& coin,
    uint64_t time_ms,
    const std::vector<std::pair<double, double>>& l_bids,
    const std::vector<std::pair<double, double>>& l_asks,
    const std::vector<size_t>& r_bid_indices = {},
    const std::vector<size_t>& r_ask_indices = {})
{
    auto make_l_side = [](const std::vector<std::pair<double, double>>& lvls) {
        auto arr = json::array();
        for (auto [px, sz] : lvls) {
            arr.push_back({{"p", std::to_string(px)}, {"s", std::to_string(sz)}});
        }
        return arr;
    };
    auto make_r_side = [](const std::vector<size_t>& indices) {
        auto arr = json::array();
        for (auto idx : indices) arr.push_back(idx);
        return arr;
    };
    return {
        {"c", coin},
        {"l", {make_l_side(l_bids), make_l_side(l_asks)}},
        {"r", {make_r_side(r_bid_indices), make_r_side(r_ask_indices)}},
        {"t", time_ms}
    };
}

static json makeTradesMsg(
    const std::string& coin,
    const std::vector<std::tuple<std::string, double, double, uint64_t>>& trades)
{
    auto arr = json::array();
    for (const auto& [side, px, sz, t] : trades) {
        arr.push_back({{"coin", coin}, {"side", side},
                       {"px", std::to_string(px)}, {"sz", std::to_string(sz)}, {"time", t}});
    }
    return {{"data", arr}};
}

static Request makeMDSubRequest(const std::string& coin, HyperliquidChannel ch) {
    Request req{};
    std::memset(req.symbol, 0, sizeof(req.symbol));
    std::memcpy(req.symbol, coin.c_str(), std::min(sizeof(req.symbol) - 1, coin.size()));
    req.msg_type = MessageType::MD_SUBSCRIPTION;
    req.md_subscription.channel = static_cast<uint8_t>(ch);
    return req;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class HyperliquidExchangeTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto config = R"({
            "request_queue_size": 4096,
            "response_queue_size": 4096,
            "md_queue_size": 1048576,
            "request_queue_shm_name": "hl_test_req",
            "response_queue_shm_name": "hl_test_resp",
            "md_queue_shm_name": "hl_test_md",
            "order_gateway": {
                "port": 4000
            },
            "md_publisher": {
                "port": 5000
            }
        })"_json;
        exchange_ = std::make_unique<TestableHyperliquidExchange>(config);
        collector_       = std::make_unique<OrderResponseCollector>(exchange_->response_queue());
        md_collector_    = std::make_unique<MarketDataCollector>(exchange_->md_queue());
        matching_engine_ = std::make_unique<FifoMatchingEngine>(exchange_->response_queue());

    }

    Symbol* registerCoin(const std::string& coin) {
        auto& sym_mgr = SymbolManager::instance();
        auto* sym = sym_mgr.createSymbol(coin, Venue::HYPERLIQUID);
        sym->matching_engine_ = matching_engine_.get();
        sym->createOrderBook<OrderBookType::L2>();
        return sym;
    }

    Order* addSimOrder(Symbol* sym, Side side, price_t p, qty_t q) {
        auto* order = sym->order_book_->allocateOrder();
        order->symbol = sym->symbol_;
        order->user_id = "test-user";
        order->side            = side;
        order->type            = OrderType::LIMIT;
        order->price           = p;
        order->quantity        = q;
        order->leaves_quantity = q;
        order->cum_quantity    = 0;
        order->time_in_force   = TimeInForce::GOOD_TILL_CANCEL;
        order->status          = OrderStatus::NEW;
        sym->order_book_->addOrder(order, 1000, 1);
        return order;
    }

    std::unique_ptr<OrderResponseCollector>           collector_;
    std::unique_ptr<MarketDataCollector>              md_collector_;
    std::unique_ptr<FifoMatchingEngine>               matching_engine_;
    std::unique_ptr<TestableHyperliquidExchange>      exchange_;
};

// ===========================================================================
// Group A — L2 book parsing (no simulator orders)
// ===========================================================================

TEST_F(HyperliquidExchangeTest, L2Book_BidAndAskLevels_BothSidesPopulated) {
    auto msg = makeL2BookMsg("HL-A1", 1000,
        {{100.0, 10.0}, {99.0, 5.0}},
        {{101.0, 8.0}});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-A1");
    ASSERT_NE(sym, nullptr);

    auto [bid_level, bi] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(bid_level, nullptr);
    EXPECT_EQ(bid_level->total_quantity, kQty10);

    auto [ask_level, ai] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice101);
    ASSERT_NE(ask_level, nullptr);
    EXPECT_EQ(ask_level->total_quantity, qty(8.0));
}

TEST_F(HyperliquidExchangeTest, L2Book_SecondUpdate_ClearsAndRebuilds) {
    // First snapshot: 3 bid levels
    auto msg1 = makeL2BookMsg("HL-A2", 1000,
        {{100.0, 10.0}, {99.0, 5.0}, {98.0, 3.0}}, {});
    exchange_->onL2BookUpdate(msg1);
    exchange_->drainEvents();

    // Second snapshot: only 1 bid level
    auto msg2 = makeL2BookMsg("HL-A2", 2000, {{99.0, 7.0}}, {});
    exchange_->onL2BookUpdate(msg2);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-A2");
    ASSERT_NE(sym, nullptr);

    // Only the second snapshot's level remains
    auto [level99, i] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(level99, nullptr);
    EXPECT_EQ(level99->total_quantity, qty(7.0));

    // Old levels from first snapshot must be gone
    auto [level100, j] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    EXPECT_EQ(level100, nullptr);
    auto [level98, k]  = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice98);
    EXPECT_EQ(level98, nullptr);
}

TEST_F(HyperliquidExchangeTest, L2Book_ZeroSizeLevel_Skipped) {
    auto msg = makeL2BookMsg("HL-A3", 1000,
        {{100.0, 0.0}, {99.0, 5.0}}, {});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-A3");
    ASSERT_NE(sym, nullptr);

    auto [level100, i] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    EXPECT_EQ(level100, nullptr);

    auto [level99, j] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(level99, nullptr);
    EXPECT_EQ(level99->total_quantity, kQty5);
}

TEST_F(HyperliquidExchangeTest, L2Book_UnrecognizedShape_MessageIgnored) {
    json bad_msg = {{"other_key", "value"}};  // neither "levels" nor "l"+"r"
    exchange_->onL2BookUpdate(bad_msg);
    exchange_->drainEvents();  // must not crash
    // No symbol created
    EXPECT_EQ(SymbolManager::instance().getSymbol("HL-A4"), nullptr);
}

TEST_F(HyperliquidExchangeTest, L2Book_EmptyLevels_BookClearedOnly) {
    // Seed some levels first
    auto msg1 = makeL2BookMsg("HL-A5", 1000, {{100.0, 10.0}}, {{101.0, 5.0}});
    exchange_->onL2BookUpdate(msg1);
    exchange_->drainEvents();

    // Second update with empty levels clears the book
    auto msg2 = makeL2BookMsg("HL-A5", 2000, {}, {});
    exchange_->onL2BookUpdate(msg2);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-A5");
    ASSERT_NE(sym, nullptr);
    EXPECT_TRUE(sym->order_book_->isEmpty());
}

// ===========================================================================
// Group B — Order matching via L2 book snapshot
// ===========================================================================

TEST_F(HyperliquidExchangeTest, Matching_BidBelowSimSell_NoMatch) {
    auto* sym = registerCoin("HL-B1");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice101, kQty10);

    auto msg = makeL2BookMsg("HL-B1", 1000, {{kPrice99 / 1e8, 10.0}}, {});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, 0);
    EXPECT_EQ(collector_->collect().size(), 0u);
}

TEST_F(HyperliquidExchangeTest, Matching_BidAtSimSell_FullFill) {
    auto* sym = registerCoin("HL-B2");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto msg = makeL2BookMsg("HL-B2", 1000, {{100.0, 10.0}}, {});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty10);
    EXPECT_EQ(sim_sell->leaves_quantity, 0);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty10);
}

TEST_F(HyperliquidExchangeTest, Matching_AskAtSimBid_FullFill) {
    auto* sym = registerCoin("HL-B3");
    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice100, kQty10);

    auto msg = makeL2BookMsg("HL-B3", 1000, {}, {{100.0, 10.0}});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, kQty10);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
}

TEST_F(HyperliquidExchangeTest, Matching_BidLargerThanSimSell_SimFilled_PhantomRemainder) {
    auto* sym = registerCoin("HL-B4");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // BID of 15 at 100: sim SELL (10) fully filled, phantom BID (5) added
    auto msg = makeL2BookMsg("HL-B4", 1000, {{100.0, 15.0}}, {});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty10);

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty5);
}

TEST_F(HyperliquidExchangeTest, Matching_BidSmallerThanSimSell_PartialFill) {
    auto* sym = registerCoin("HL-B5");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // BID of 5 at 100: sim SELL partially filled, no phantom added
    auto msg = makeL2BookMsg("HL-B5", 1000, {{100.0, 5.0}}, {});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty5);
    EXPECT_EQ(sim_sell->leaves_quantity, kQty5);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].order_status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty5);
    EXPECT_EQ(responses[0].leaves_qty, kQty5);
}

TEST_F(HyperliquidExchangeTest, Matching_AskAboveSimBid_NoMatch_PhantomAdded) {
    auto* sym = registerCoin("HL-B6");
    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice99, kQty10);

    // ASK at 101: no crossing, phantom SELL added
    auto msg = makeL2BookMsg("HL-B6", 1000, {}, {{101.0, 5.0}});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, 0);

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice101);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty5);
}

TEST_F(HyperliquidExchangeTest, Matching_FullFill_ExecReportCorrect) {
    auto* sym = registerCoin("HL-B7");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto msg = makeL2BookMsg("HL-B7", 1000, {{100.0, 10.0}}, {});
    exchange_->onL2BookUpdate(msg);
    exchange_->drainEvents();

    auto responses = collector_->collect();
    ASSERT_GE(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty10);
    EXPECT_EQ(responses[0].leaves_qty, 0);
    EXPECT_STREQ(responses[0].order_id, sim_sell->order_id.c_str());
}

TEST_F(HyperliquidExchangeTest, Matching_SimOrderSurvivesAcrossNonCrossingUpdates) {
    auto* sym = registerCoin("HL-B8");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // First update: no crossing (bid at 99)
    auto msg1 = makeL2BookMsg("HL-B8", 1000, {{99.0, 5.0}}, {});
    exchange_->onL2BookUpdate(msg1);
    exchange_->drainEvents();
    EXPECT_EQ(sim_sell->cum_quantity, 0);

    // Second update: crossing bid at 100
    auto msg2 = makeL2BookMsg("HL-B8", 2000, {{100.0, 10.0}}, {});
    exchange_->onL2BookUpdate(msg2);
    exchange_->drainEvents();
    EXPECT_EQ(sim_sell->cum_quantity, kQty10);
}

// ===========================================================================
// Group F — Incremental l2 diff application
// ===========================================================================

TEST_F(HyperliquidExchangeTest, Diff_UntouchedLevelsSurvive_RegressionForClearBug) {
    // Seed 3 bid levels via a snapshot.
    auto snap = makeL2BookMsg("HL-F1", 1000, {{100.0, 10.0}, {99.0, 5.0}, {98.0, 3.0}}, {});
    exchange_->onL2BookUpdate(snap);
    exchange_->drainEvents();

    // A diff that only changes the 99.0 level must not disturb the others.
    auto diff = makeL2DiffMsg("HL-F1", 2000, {{99.0, 7.0}}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-F1");
    ASSERT_NE(sym, nullptr);

    auto [lv100, i1] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(lv100, nullptr);
    EXPECT_EQ(lv100->total_quantity, kQty10);

    auto [lv99, i2] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(lv99, nullptr);
    EXPECT_EQ(lv99->total_quantity, qty(7.0));

    auto [lv98, i3] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice98);
    ASSERT_NE(lv98, nullptr);
    EXPECT_EQ(lv98->total_quantity, kQty3);
}

TEST_F(HyperliquidExchangeTest, Diff_NewPriceCrossesSimOrder_FullFill) {
    auto* sym = registerCoin("HL-F2");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto diff = makeL2DiffMsg("HL-F2", 1000, {{100.0, 10.0}}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty10);
    EXPECT_EQ(sim_sell->leaves_quantity, 0);
}

TEST_F(HyperliquidExchangeTest, Diff_ExistingLevelQtyIncrease_GrowsByDelta) {
    auto snap = makeL2BookMsg("HL-F3", 1000, {{100.0, 10.0}}, {});
    exchange_->onL2BookUpdate(snap);
    exchange_->drainEvents();

    // Delta is +5 (10 -> 15) — must add exactly the delta, not the full new qty again.
    auto diff = makeL2DiffMsg("HL-F3", 2000, {{100.0, 15.0}}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-F3");
    ASSERT_NE(sym, nullptr);
    auto [lv, i] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(lv, nullptr);
    EXPECT_EQ(lv->total_quantity, qty(15.0));
}

TEST_F(HyperliquidExchangeTest, Diff_ExistingLevelQtyDecrease_UserOrderSurvives) {
    auto* sym = registerCoin("HL-F4");
    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice99, kQty5);

    // Snapshot adds phantom liquidity at the SAME price as the user's own
    // resting order: total = user 5 + phantom 12 = 17.
    auto snap = makeL2BookMsg("HL-F4", 1000, {{99.0, 12.0}}, {});
    exchange_->onL2BookUpdate(snap);
    exchange_->drainEvents();

    // Diff shrinks the real-market (phantom) size to 6 -> only phantom shrinks.
    auto diff = makeL2DiffMsg("HL-F4", 2000, {{99.0, 6.0}}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    auto [lv, i] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(lv, nullptr);
    EXPECT_EQ(lv->total_quantity, qty(11.0));  // user 5 + phantom 6

    EXPECT_EQ(sim_buy->leaves_quantity, kQty5);
    EXPECT_EQ(sim_buy->cum_quantity, 0);
}

TEST_F(HyperliquidExchangeTest, Diff_RemovedLevel_ClearsPhantomKeepsUserOrder) {
    auto* sym = registerCoin("HL-F5");
    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice99, kQty5);

    // Snapshot: bid index0=100 (phantom only), index1=99 (user 5 + phantom 12 = 17).
    auto snap = makeL2BookMsg("HL-F5", 1000, {{100.0, 8.0}, {99.0, 12.0}}, {});
    exchange_->onL2BookUpdate(snap);
    exchange_->drainEvents();

    // Remove index 1 (price 99) entirely via diff.
    auto diff = makeL2DiffMsg("HL-F5", 2000, {}, {}, {1}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    // Phantom at 99 is fully gone, but the user's own order keeps the level alive.
    auto [lv99, i99] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(lv99, nullptr);
    EXPECT_EQ(lv99->total_quantity, kQty5);
    EXPECT_EQ(sim_buy->leaves_quantity, kQty5);

    // Untouched level survives.
    auto [lv100, i100] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(lv100, nullptr);
    EXPECT_EQ(lv100->total_quantity, qty(8.0));
}

TEST_F(HyperliquidExchangeTest, Diff_RemovalIndex_ResolvesCorrectPrice) {
    // Snapshot: bid index0=100, index1=99, index2=98 (best-first).
    auto snap = makeL2BookMsg("HL-F6", 1000, {{100.0, 10.0}, {99.0, 5.0}, {98.0, 3.0}}, {});
    exchange_->onL2BookUpdate(snap);
    exchange_->drainEvents();

    // Remove indices 0 and 2 (100 and 98) — 99 (index 1) must survive.
    auto diff = makeL2DiffMsg("HL-F6", 2000, {}, {}, {0, 2}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-F6");
    ASSERT_NE(sym, nullptr);

    auto [lv100, i1] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    EXPECT_EQ(lv100, nullptr);

    auto [lv99, i2] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(lv99, nullptr);
    EXPECT_EQ(lv99->total_quantity, kQty5);

    auto [lv98, i3] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice98);
    EXPECT_EQ(lv98, nullptr);
}

TEST_F(HyperliquidExchangeTest, Diff_OutOfRangeRemovalIndex_SkippedGracefully) {
    auto snap = makeL2BookMsg("HL-F7", 1000, {{100.0, 10.0}}, {});
    exchange_->onL2BookUpdate(snap);
    exchange_->drainEvents();

    // Index 5 is out of range (only 1 bid level tracked) — must not crash,
    // and the "l" entry in the same diff must still apply.
    auto diff = makeL2DiffMsg("HL-F7", 2000, {{99.0, 3.0}}, {}, {5}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-F7");
    ASSERT_NE(sym, nullptr);

    auto [lv100, i1] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(lv100, nullptr);
    EXPECT_EQ(lv100->total_quantity, kQty10);

    auto [lv99, i2] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(lv99, nullptr);
    EXPECT_EQ(lv99->total_quantity, qty(3.0));
}

TEST_F(HyperliquidExchangeTest, Diff_ThenNewSnapshot_FullyReplacesState) {
    auto snap1 = makeL2BookMsg("HL-F8", 1000, {{100.0, 10.0}, {99.0, 5.0}}, {});
    exchange_->onL2BookUpdate(snap1);
    exchange_->drainEvents();

    auto diff = makeL2DiffMsg("HL-F8", 1500, {{98.0, 2.0}}, {});
    exchange_->onL2BookUpdate(diff);
    exchange_->drainEvents();

    // A fresh snapshot (e.g. after a reconnect) replaces everything, including
    // whatever the diff added, and resets the removal-index mirror too.
    auto snap2 = makeL2BookMsg("HL-F8", 2000, {{100.0, 4.0}}, {});
    exchange_->onL2BookUpdate(snap2);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-F8");
    ASSERT_NE(sym, nullptr);

    auto [lv100, i1] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(lv100, nullptr);
    EXPECT_EQ(lv100->total_quantity, qty(4.0));

    auto [lv99, i2] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    EXPECT_EQ(lv99, nullptr);

    auto [lv98, i3] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice98);
    EXPECT_EQ(lv98, nullptr);
}

// ===========================================================================
// Group C — Trades update
// ===========================================================================

TEST_F(HyperliquidExchangeTest, Trades_BuySide_ParsedAndPublished) {
    // Symbol must exist before trades arrive
    registerCoin("HL-C1");

    auto msg = makeTradesMsg("HL-C1", {{"B", 100.0, 5.0, 1000000}});
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();  // must not crash, publishes MDTrade to md_queue
    // No crashes = pass
}

TEST_F(HyperliquidExchangeTest, Trades_SellSide_ParsedCorrectly) {
    registerCoin("HL-C2");

    auto msg = makeTradesMsg("HL-C2", {{"A", 101.0, 3.0, 2000000}});
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();
}

TEST_F(HyperliquidExchangeTest, Trades_MultipleTradesInOneMessage) {
    registerCoin("HL-C3");

    auto msg = makeTradesMsg("HL-C3", {
        {"B", 100.0, 5.0, 1000},
        {"A", 100.0, 3.0, 1001},
        {"B", 99.0,  2.0, 1002}
    });
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();
}

TEST_F(HyperliquidExchangeTest, Trades_EmptyArray_NoCrash) {
    registerCoin("HL-C4");
    json msg = {{"data", json::array()}};
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();
}

TEST_F(HyperliquidExchangeTest, Trades_MissingDataKey_Ignored) {
    json msg = {{"other", "value"}};
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();
}

// A venue print is replayed as an incoming aggressor order, so it fills the
// resting simulator order it crosses and the fill is published as TRADE_SUMMARY.
TEST_F(HyperliquidExchangeTest, Trades_FillRestingSimulatorOrders) {
    auto* sym = registerCoin("HL-C5");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto msg = makeTradesMsg("HL-C5", {{"B", 100.0, 10.0, 1000}});
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty10);
    EXPECT_EQ(sim_sell->leaves_quantity, 0);
    EXPECT_GT(collector_->collect().size(), 0u);

    auto summaries = md_collector_->collect(MDUpdateType::TRADE_SUMMARY);
    ASSERT_EQ(summaries.size(), 1u);
    auto* summary = reinterpret_cast<TradeSummary*>(summaries[0]->data);
    EXPECT_EQ(summary->aggressor_side, Side::BUY);
    EXPECT_EQ(summary->price, kPrice100);
    EXPECT_EQ(summary->qty, kQty10);
}

// Trade ids are a per-venue sequence, independent of the matching engine's own
// static counter, and monotonic across prints.
TEST_F(HyperliquidExchangeTest, Trades_TradeIdsAreMonotonicPerVenue) {
    auto* sym = registerCoin("HL-C6");
    addSimOrder(sym, Side::SELL, kPrice100, kQty10);
    addSimOrder(sym, Side::SELL, kPrice101, kQty10);

    exchange_->onTradesUpdate(makeTradesMsg("HL-C6", {{"B", 100.0, 5.0, 1000}}));
    exchange_->drainEvents();
    exchange_->onTradesUpdate(makeTradesMsg("HL-C6", {{"B", 101.0, 5.0, 1001}}));
    exchange_->drainEvents();

    auto summaries = md_collector_->collect(MDUpdateType::TRADE_SUMMARY);
    ASSERT_EQ(summaries.size(), 2u);
    auto first  = reinterpret_cast<TradeSummary*>(summaries[0]->data)->trade_id;
    auto second = reinterpret_cast<TradeSummary*>(summaries[1]->data)->trade_id;
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);
}

// The tape is fill-driven: a print with no crossable liquidity produces no output,
// and its unfilled remainder rests on the aggressor's own side.
TEST_F(HyperliquidExchangeTest, Trades_NoCrossableLiquidity_RestsAndPublishesNothing) {
    auto* sym = registerCoin("HL-C7");

    exchange_->onTradesUpdate(makeTradesMsg("HL-C7", {{"B", 100.0, 5.0, 1000}}));
    exchange_->drainEvents();

    EXPECT_TRUE(md_collector_->collect(MDUpdateType::TRADE_SUMMARY).empty());

    auto [buy_level, bi] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(buy_level, nullptr);
    EXPECT_EQ(buy_level->total_quantity, kQty5);
}

// ===========================================================================
// Group D — MD subscription (use_live_feed_ = false)
// ===========================================================================

TEST_F(HyperliquidExchangeTest, Subscription_NewCoin_CreatesSymbol) {
    auto req = makeMDSubRequest("HL-D1", HyperliquidChannel::L2_BOOK);
    exchange_->subscribeToMD(req);

    EXPECT_NE(SymbolManager::instance().getSymbol("HL-D1"), nullptr);
}

TEST_F(HyperliquidExchangeTest, Subscription_NewCoin_WritesMdQueueEntry) {
    auto req = makeMDSubRequest("HL-D2", HyperliquidChannel::L2_BOOK);
    exchange_->subscribeToMD(req);

    uint64_t cursor = 0;
    auto [data, sz] = exchange_->md_queue().read(cursor);
    ASSERT_NE(data, nullptr);
    auto* update = reinterpret_cast<MarketDataUpdate*>(data);
    EXPECT_EQ(update->type, MDUpdateType::SUB_RESPONSE);
}

// ---------------------------------------------------------------------------
// trades channel subscription — served from the tape history, split against the
// book snapshot the subscriber holds.
// ---------------------------------------------------------------------------

// Unapplied prints at or before the book's last update are covered by the book
// snapshot the subscriber receives, so the whole history is snapshot material.
TEST_F(HyperliquidExchangeTest, TradesSubscription_HistoryOlderThanBook_AllInSnapshot) {
    auto* sym = registerCoin("HL-TS1");
    sym->recordTrade(MDTrade{.trade_id = 1, .event_time = 100, .seq_num = 0,
                             .price = kPrice100, .qty = kQty5,
                             .flags = UpdateFlags::F_NOT_IN_BOOK, .side = Side::BUY});
    sym->recordTrade(MDTrade{.trade_id = 2, .event_time = 200, .seq_num = 0,
                             .price = kPrice101, .qty = kQty3,
                             .flags = UpdateFlags::F_NOT_IN_BOOK, .side = Side::SELL});
    sym->order_book_->setLastUpdate(300, 0);
    md_collector_->collect();   // drop frames from setup

    exchange_->subscribeToMD(makeMDSubRequest("HL-TS1", HyperliquidChannel::TRADES));

    auto responses = md_collector_->collect(MDUpdateType::SUB_RESPONSE);
    ASSERT_EQ(responses.size(), 1u);
    auto* response = reinterpret_cast<MDSubscriptionResponse*>(responses[0]->data);
    EXPECT_EQ(response->channel, static_cast<uint8_t>(HyperliquidChannel::TRADES));

    auto* snapshot = reinterpret_cast<MDTradeSnapshotResponse*>(response->data);
    EXPECT_EQ(snapshot->num_snapshot, 2u);
    EXPECT_EQ(snapshot->num_update, 0u);
    // Chronological, oldest first.
    EXPECT_EQ(snapshot->trades[0].trade_id, 1u);
    EXPECT_EQ(snapshot->trades[1].trade_id, 2u);
}

// An unapplied print stamped after the book's last update is not reflected in
// the book snapshot the subscriber receives, so it belongs in the update block.
TEST_F(HyperliquidExchangeTest, TradesSubscription_UnappliedTradeNewerThanBook_GoesInUpdateBlock) {
    auto* sym = registerCoin("HL-TS2");
    sym->recordTrade(MDTrade{.trade_id = 1, .event_time = 100, .seq_num = 0,
                             .price = kPrice100, .qty = kQty5,
                             .flags = UpdateFlags::F_NOT_IN_BOOK, .side = Side::BUY});
    sym->recordTrade(MDTrade{.trade_id = 2, .event_time = 500, .seq_num = 0,
                             .price = kPrice101, .qty = kQty3,
                             .flags = UpdateFlags::F_NOT_IN_BOOK, .side = Side::SELL});
    sym->order_book_->setLastUpdate(300, 0);
    md_collector_->collect();

    exchange_->subscribeToMD(makeMDSubRequest("HL-TS2", HyperliquidChannel::TRADES));

    auto responses = md_collector_->collect(MDUpdateType::SUB_RESPONSE);
    ASSERT_EQ(responses.size(), 1u);
    auto* snapshot = reinterpret_cast<MDTradeSnapshotResponse*>(
        reinterpret_cast<MDSubscriptionResponse*>(responses[0]->data)->data);

    EXPECT_EQ(snapshot->num_snapshot, 1u);
    EXPECT_EQ(snapshot->num_update, 1u);
    EXPECT_EQ(snapshot->trades[0].trade_id, 1u);
    // The update block follows the snapshot block in the same array.
    EXPECT_EQ(snapshot->trades[1].trade_id, 2u);
}

// A trade the matching engine produced is in the book by construction. The trade
// handlers do not advance `lastUpdateTime()` — only level updates do — so
// classifying by timestamp alone would call an already-applied trade "not yet
// reflected" and hand the subscriber a book snapshot that already contains it.
TEST_F(HyperliquidExchangeTest, TradesSubscription_AppliedTradeNewerThanBook_StaysInSnapshot) {
    auto* sym = registerCoin("HL-TS5");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // Book's last update predates the trade that follows.
    sym->order_book_->setLastUpdate(300, 0);

    // A print at t=1000ms matches, so the book now reflects it while
    // lastUpdateTime() still reads 300.
    exchange_->onTradesUpdate(makeTradesMsg("HL-TS5", {{"B", 100.0, 10.0, 1000}}));
    exchange_->drainEvents();
    ASSERT_EQ(sim_sell->cum_quantity, kQty10);
    ASSERT_GT(sym->trade_history_.size(), 0u);
    md_collector_->collect();

    exchange_->subscribeToMD(makeMDSubRequest("HL-TS5", HyperliquidChannel::TRADES));

    auto responses = md_collector_->collect(MDUpdateType::SUB_RESPONSE);
    ASSERT_EQ(responses.size(), 1u);
    auto* snapshot = reinterpret_cast<MDTradeSnapshotResponse*>(
        reinterpret_cast<MDSubscriptionResponse*>(responses[0]->data)->data);

    EXPECT_EQ(snapshot->num_snapshot, 1u);
    EXPECT_EQ(snapshot->num_update, 0u);
}

// Classification is not monotonic over the history: an applied trade can follow
// an unapplied print that is newer than the book. The two blocks must still be
// written contiguously, or the consumer reads one block's trades as the other's.
TEST_F(HyperliquidExchangeTest, TradesSubscription_InterleavedHistory_BlocksStayContiguous) {
    auto* sym = registerCoin("HL-TS6");
    sym->order_book_->setLastUpdate(300, 0);

    // Alternating in history order: unapplied (newer than the book) and applied.
    sym->recordTrade(MDTrade{.trade_id = 1, .event_time = 400, .seq_num = 0,
                             .price = kPrice100, .qty = kQty3,
                             .flags = UpdateFlags::F_NOT_IN_BOOK, .side = Side::BUY});
    sym->recordTrade(MDTrade{.trade_id = 2, .event_time = 500, .seq_num = 0,
                             .price = kPrice100, .qty = kQty3,
                             .flags = UpdateFlags::F_NONE, .side = Side::BUY});
    sym->recordTrade(MDTrade{.trade_id = 3, .event_time = 600, .seq_num = 0,
                             .price = kPrice100, .qty = kQty3,
                             .flags = UpdateFlags::F_NOT_IN_BOOK, .side = Side::BUY});
    sym->recordTrade(MDTrade{.trade_id = 4, .event_time = 700, .seq_num = 0,
                             .price = kPrice100, .qty = kQty3,
                             .flags = UpdateFlags::F_NONE, .side = Side::BUY});
    md_collector_->collect();

    exchange_->subscribeToMD(makeMDSubRequest("HL-TS6", HyperliquidChannel::TRADES));

    auto responses = md_collector_->collect(MDUpdateType::SUB_RESPONSE);
    ASSERT_EQ(responses.size(), 1u);
    auto* snapshot = reinterpret_cast<MDTradeSnapshotResponse*>(
        reinterpret_cast<MDSubscriptionResponse*>(responses[0]->data)->data);

    ASSERT_EQ(snapshot->num_snapshot, 2u);
    ASSERT_EQ(snapshot->num_update, 2u);

    // Applied trades first, chronological within the block.
    EXPECT_EQ(snapshot->trades[0].trade_id, 2u);
    EXPECT_EQ(snapshot->trades[1].trade_id, 4u);
    // Then the unapplied ones, also chronological.
    EXPECT_EQ(snapshot->trades[2].trade_id, 1u);
    EXPECT_EQ(snapshot->trades[3].trade_id, 3u);
}

TEST_F(HyperliquidExchangeTest, TradesSubscription_NoHistory_EmptyBlocks) {
    registerCoin("HL-TS3");
    md_collector_->collect();

    exchange_->subscribeToMD(makeMDSubRequest("HL-TS3", HyperliquidChannel::TRADES));

    auto responses = md_collector_->collect(MDUpdateType::SUB_RESPONSE);
    ASSERT_EQ(responses.size(), 1u);
    auto* snapshot = reinterpret_cast<MDTradeSnapshotResponse*>(
        reinterpret_cast<MDSubscriptionResponse*>(responses[0]->data)->data);

    EXPECT_EQ(snapshot->num_snapshot, 0u);
    EXPECT_EQ(snapshot->num_update, 0u);
}

// The history is bounded, so a long-running instrument cannot grow the frame
// without limit or overrun its own reservation.
TEST_F(HyperliquidExchangeTest, TradesSubscription_HistoryCappedAtCapacity) {
    auto* sym = registerCoin("HL-TS4");
    constexpr size_t kOverflow = Symbol::TRADE_HISTORY_CAPACITY + 10;
    for (size_t i = 0; i < kOverflow; ++i) {
        sym->recordTrade(MDTrade{.trade_id = i + 1, .event_time = 100 + i, .seq_num = 0,
                                 .price = kPrice100, .qty = kQty3,
                                 .flags = UpdateFlags::F_NONE, .side = Side::BUY});
    }
    sym->order_book_->setLastUpdate(100 + kOverflow, 0);
    md_collector_->collect();

    exchange_->subscribeToMD(makeMDSubRequest("HL-TS4", HyperliquidChannel::TRADES));

    auto responses = md_collector_->collect(MDUpdateType::SUB_RESPONSE);
    ASSERT_EQ(responses.size(), 1u);
    auto* snapshot = reinterpret_cast<MDTradeSnapshotResponse*>(
        reinterpret_cast<MDSubscriptionResponse*>(responses[0]->data)->data);

    EXPECT_GT(snapshot->num_snapshot, 0u);
    EXPECT_LE(snapshot->num_snapshot, Symbol::TRADE_HISTORY_CAPACITY);
    EXPECT_EQ(snapshot->num_update, 0u);
    // Oldest entries were evicted; the newest survives as the last entry.
    EXPECT_EQ(snapshot->trades[snapshot->num_snapshot - 1].trade_id, kOverflow);
    EXPECT_GT(snapshot->trades[0].trade_id, 1u);
}

TEST_F(HyperliquidExchangeTest, Subscription_ExistingCoin_ResendsSnapshot) {
    // Pre-populate book, then subscribe twice
    auto* sym = registerCoin("HL-D3");
    addSimOrder(sym, Side::BUY, kPrice99, kQty5);

    auto req = makeMDSubRequest("HL-D3", HyperliquidChannel::L2_BOOK);
    exchange_->subscribeToMD(req);

    auto &md_queue = exchange_->md_queue();

    // Read first entry
    uint64_t cursor = 0;
    auto [data1, sz1] = md_queue.read(cursor);
    ASSERT_NE(data1, nullptr);

    // Second subscription should also write a snapshot
    exchange_->subscribeToMD(req);
    auto [data2, sz2] = md_queue.read(cursor);
    ASSERT_NE(data2, nullptr);

    auto* update2 = reinterpret_cast<MarketDataUpdate*>(data2);
    EXPECT_EQ(update2->type, MDUpdateType::SUB_RESPONSE);
}

TEST_F(HyperliquidExchangeTest, Subscription_NewCoin_L2Channel_CreatesSymbol) {
    auto req = makeMDSubRequest("HL-D4", HyperliquidChannel::L2);
    exchange_->subscribeToMD(req);

    EXPECT_NE(SymbolManager::instance().getSymbol("HL-D4"), nullptr);
}

TEST_F(HyperliquidExchangeTest, Subscription_NewCoin_L2Channel_WritesTaggedSubResponse) {
    auto req = makeMDSubRequest("HL-D5", HyperliquidChannel::L2);
    exchange_->subscribeToMD(req);

    uint64_t cursor = 0;
    auto [data, sz] = exchange_->md_queue().read(cursor);
    ASSERT_NE(data, nullptr);
    auto* update = reinterpret_cast<MarketDataUpdate*>(data);
    EXPECT_EQ(update->type, MDUpdateType::SUB_RESPONSE);

    auto* response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    EXPECT_EQ(response->channel, static_cast<uint8_t>(HyperliquidChannel::L2));
}

TEST_F(HyperliquidExchangeTest, Subscription_L2AndL2Book_AreIndependentChannels) {
    // Subscribing the same coin to both channels yields two separate
    // SUB_RESPONSE entries, each tagged with its own channel.
    auto req_book = makeMDSubRequest("HL-D6", HyperliquidChannel::L2_BOOK);
    auto req_l2   = makeMDSubRequest("HL-D6", HyperliquidChannel::L2);
    exchange_->subscribeToMD(req_book);
    exchange_->subscribeToMD(req_l2);

    auto &md_queue = exchange_->md_queue();
    uint64_t cursor = 0;

    auto [data1, sz1] = md_queue.read(cursor);
    ASSERT_NE(data1, nullptr);
    auto* update1 = reinterpret_cast<MarketDataUpdate*>(data1);
    auto* response1 = reinterpret_cast<MDSubscriptionResponse*>(update1->data);
    EXPECT_EQ(response1->channel, static_cast<uint8_t>(HyperliquidChannel::L2_BOOK));

    auto [data2, sz2] = md_queue.read(cursor);
    ASSERT_NE(data2, nullptr);
    auto* update2 = reinterpret_cast<MarketDataUpdate*>(data2);
    auto* response2 = reinterpret_cast<MDSubscriptionResponse*>(update2->data);
    EXPECT_EQ(response2->channel, static_cast<uint8_t>(HyperliquidChannel::L2));
}

TEST_F(HyperliquidExchangeTest, Subscription_ExistingCoin_L2Channel_ResendsSnapshot) {
    auto* sym = registerCoin("HL-D7");
    addSimOrder(sym, Side::BUY, kPrice99, kQty5);

    auto req = makeMDSubRequest("HL-D7", HyperliquidChannel::L2);
    exchange_->subscribeToMD(req);

    auto &md_queue = exchange_->md_queue();
    uint64_t cursor = 0;
    auto [data1, sz1] = md_queue.read(cursor);
    ASSERT_NE(data1, nullptr);

    exchange_->subscribeToMD(req);
    auto [data2, sz2] = md_queue.read(cursor);
    ASSERT_NE(data2, nullptr);
    auto* update2 = reinterpret_cast<MarketDataUpdate*>(data2);
    EXPECT_EQ(update2->type, MDUpdateType::SUB_RESPONSE);
}

// ===========================================================================
// Group E — Event queue ordering
// ===========================================================================

TEST_F(HyperliquidExchangeTest, EventQueue_L2ThenTrades_ProcessedInOrder) {
    registerCoin("HL-E1");

    // Push L2 update then trades (without draining in between)
    auto l2_msg = makeL2BookMsg("HL-E1", 1000, {{100.0, 5.0}}, {});
    auto tr_msg = makeTradesMsg("HL-E1", {{"B", 100.0, 5.0, 1000}});

    exchange_->onL2BookUpdate(l2_msg);
    exchange_->onTradesUpdate(tr_msg);
    exchange_->drainEvents();  // both processed in order, no crash

    auto* sym = SymbolManager::instance().getSymbol("HL-E1");
    ASSERT_NE(sym, nullptr);
    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(level, nullptr);  // L2 book was applied
}

TEST_F(HyperliquidExchangeTest, EventQueue_MultipleL2Updates_OnlyLastStateVisible) {
    auto msg1 = makeL2BookMsg("HL-E2", 1000, {{100.0, 10.0}, {99.0, 5.0}}, {});
    auto msg2 = makeL2BookMsg("HL-E2", 2000, {{101.0, 3.0}}, {});  // replaces first

    exchange_->onL2BookUpdate(msg1);
    exchange_->onL2BookUpdate(msg2);
    exchange_->drainEvents();

    auto* sym = SymbolManager::instance().getSymbol("HL-E2");
    ASSERT_NE(sym, nullptr);

    // Only levels from msg2 remain
    auto [lv101, i] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice101);
    ASSERT_NE(lv101, nullptr);
    EXPECT_EQ(lv101->total_quantity, qty(3.0));

    auto [lv100, j] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    EXPECT_EQ(lv100, nullptr);
}
