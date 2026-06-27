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
    return {{"data", {
        {"coin", coin},
        {"time", time_ms},
        {"levels", {make_side(bids), make_side(asks)}}
    }}};
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
        order->symbol          = sym->symbol_;
        order->user_id         = "test-user";
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

TEST_F(HyperliquidExchangeTest, L2Book_MissingDataKey_MessageIgnored) {
    json bad_msg = {{"other_key", "value"}};  // no "data" key
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

TEST_F(HyperliquidExchangeTest, Trades_DoNotAffectSimulatorOrders) {
    auto* sym = registerCoin("HL-C5");
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto msg = makeTradesMsg("HL-C5", {{"B", 100.0, 10.0, 1000}});
    exchange_->onTradesUpdate(msg);
    exchange_->drainEvents();

    // Trades do NOT trigger matching — sim order untouched
    EXPECT_EQ(sim_sell->cum_quantity, 0);
    EXPECT_EQ(sim_sell->leaves_quantity, kQty10);
    EXPECT_EQ(collector_->collect().size(), 0u);
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
