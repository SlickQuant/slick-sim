#include <gtest/gtest.h>
#include <exchange/exch_coinbase.hpp>
#include <common/symbol_manager.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <common/messages.hpp>
#include <slick/queue.h>
#include <coinbase/market_data.hpp>
#include <coinbase/websocket.hpp>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::exch;
using namespace slick::sim::engine;
using namespace nlohmann::literals;

// ---------------------------------------------------------------------------
// TestableCoinbaseExchange
//   Exposes processSequencedEvents() as public so tests can drain the queue
//   synchronously without running the exchange thread.
// ---------------------------------------------------------------------------
class TestableCoinbaseExchange : public CoinbaseExchange {
public:
    using CoinbaseExchange::CoinbaseExchange;
    void drainSequencedEvents() { processSequencedEvents(true); }
};

// ---------------------------------------------------------------------------
// Helpers for constructing Coinbase feed types
// ---------------------------------------------------------------------------
static coinbase::Level2UpdateBatch makeBatch(
    const std::string& product_id,
    std::vector<coinbase::Level2Update> updates)
{
    coinbase::Level2UpdateBatch batch;
    batch.product_id = product_id;
    batch.updates    = std::move(updates);
    return batch;
}

static coinbase::Level2Update makeL2Update(
    coinbase::Side side,
    double price_level,
    double new_quantity,
    uint64_t event_time = 1)   // default: far in past so timeout fires immediately
{
    coinbase::Level2Update u;
    u.side         = side;
    u.price_level  = price_level;
    u.new_quantity = new_quantity;
    u.event_time   = event_time;
    return u;
}

static coinbase::MarketTrade makeMarketTrade(
    const std::string& product_id,
    coinbase::Side side,
    double price,
    double size,
    uint64_t time = 1)
{
    coinbase::MarketTrade t;
    t.product_id = product_id;
    t.trade_id   = "trade-1";
    t.side       = side;
    t.price      = price;
    t.size       = size;
    t.time       = time;
    return t;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class CoinbaseExchangeTest : public ::testing::Test {
protected:
    void SetUp() override {

        auto config = R"({
            "request_queue_size": 4096,
            "response_queue_size": 4096,
            "md_queue_size": 1048576,
            "request_queue_shm_name": "test_exch_requests",
            "response_queue_shm_name": "test_exch_responses",
            "md_queue_shm_name": "test_exch_md",
            "order_gateway": {
                "rest": {
                    "port": 4000
                }
            },
            "md_publisher": {
                "port": 5000
            }
        })"_json;

        exchange_ = std::make_unique<TestableCoinbaseExchange>(config);
        collector_      = std::make_unique<OrderResponseCollector>(exchange_->response_queue());
        matching_engine_ = std::make_unique<FifoMatchingEngine>(exchange_->response_queue());
    }

    // Register a symbol in SymbolManager and wire it up with an order book and
    // matching engine, mirroring what CoinbaseExchange::addSymbol() does.
    Symbol* registerSymbol(const std::string& name) {
        auto& sym_mgr = SymbolManager::instance();
        auto* sym = sym_mgr.createSymbol(name, Venue::COINBASE);
        sym->matching_engine_ = matching_engine_.get();
        sym->createOrderBook<OrderBookType::L2>();
        return sym;
    }

    // Add a resting simulator order directly to the symbol's order book.
    Order* addSimOrder(Symbol* sym, Side side, price_t price, qty_t qty) {
        auto* order = sym->order_book_->allocateOrder();
        order->symbol          = sym->symbol_;
        order->user_id         = "test-user";
        order->side            = side;
        order->type            = OrderType::LIMIT;
        order->price           = price;
        order->quantity        = qty;
        order->leaves_quantity = qty;
        order->cum_quantity    = 0;
        order->time_in_force   = TimeInForce::GOOD_TILL_CANCEL;
        order->status          = OrderStatus::NEW;
        sym->order_book_->addOrder(order, 1000, 1);
        return order;
    }

    void drainEvents() {
        exchange_->drainSequencedEvents();
    }

    std::unique_ptr<OrderResponseCollector> collector_;

    // Shared matching engine across all symbols in one test; initialized in
    // SetUp() so it uses the same response_queue_ the collector reads from.
    std::unique_ptr<FifoMatchingEngine> matching_engine_;

    std::unique_ptr<TestableCoinbaseExchange> exchange_;
};

// ===========================================================================
// onLevel2Updates — new level, no matching simulator order
// ===========================================================================

// A BUY level2 update for a price below the SELL simulator order creates a
// phantom BUY entry in the book. No match occurs, book levels are correct.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewBuyLevel_BelowSimSell_NoMatch_PhantomAdded) {
    auto* sym = registerSymbol("TEST-NB-1");

    // Resting simulator SELL at 101
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice101, kQty10);

    // BUY level2 arrives at 99 (below ask) — should not match
    auto batch = makeBatch("TEST-NB-1", {
        makeL2Update(coinbase::Side::BUY, 99.0, 10.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    // Simulator SELL untouched
    EXPECT_EQ(sim_sell->cum_quantity, 0);

    // BUY side of book now has the phantom at kPrice99
    auto [level, index] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice99), kQty10);
}

// A SELL level2 update for a price above the BUY simulator order creates a
// phantom SELL entry. No match.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewSellLevel_AboveSimBuy_NoMatch_PhantomAdded) {
    auto* sym = registerSymbol("TEST-NS-1");

    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice99, kQty10);

    auto batch = makeBatch("TEST-NS-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, 0);

    auto [level, index] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice101);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10);
}

// ===========================================================================
// onLevel2Updates — new level, fully matches simulator order
// ===========================================================================

// BUY level2 at 100 crosses resting SELL simulator order at 100.
// Full match: resting SELL completely filled, no phantom added.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewBuyLevel_FullyMatchesSimSell) {
    auto* sym = registerSymbol("TEST-FM-B");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto batch = makeBatch("TEST-FM-B", {
        makeL2Update(coinbase::Side::BUY, 100.0, 10.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    // Resting SELL fully consumed
    EXPECT_EQ(sim_sell->cum_quantity, kQty10);

    // SELL level at 100 should be gone (fully matched)
    auto [sell_level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    EXPECT_EQ(sell_level, nullptr);

    // No BUY phantom: qty was fully consumed during match
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice100), 0);

    // Execution report published for the filled simulator order
    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty10);
    EXPECT_EQ(responses[0].leaves_qty, 0);
}

// SELL level2 at 100 crosses resting BUY simulator order at 100.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewSellLevel_FullyMatchesSimBuy) {
    auto* sym = registerSymbol("TEST-FM-S");

    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice100, kQty10);

    auto batch = makeBatch("TEST-FM-S", {
        makeL2Update(coinbase::Side::SELL, 100.0, 10.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, kQty10);

    auto [buy_level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    EXPECT_EQ(buy_level, nullptr);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
}

// ===========================================================================
// onLevel2Updates — new level, partial match + phantom remainder
// ===========================================================================

// BUY level2 qty (5) < resting SELL qty (10): SELL partially filled, phantom
// BUY added for the 5 that remained after matching.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewBuyLevel_PartialMatchSimSell_PhantomForRemainder) {
    auto* sym = registerSymbol("TEST-PM-B");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto batch = makeBatch("TEST-PM-B", {
        makeL2Update(coinbase::Side::BUY, 100.0, 5.0)   // only 5
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    // Simulator SELL partially filled (5 matched)
    EXPECT_EQ(sim_sell->cum_quantity, kQty5);

    // No remainder phantom on BUY side (qty was fully consumed matching the SELL)
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice100), 0);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].order_status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty5);
    EXPECT_EQ(responses[0].leaves_qty, kQty5);
}

// BUY level2 qty (15) > resting SELL qty (10): SELL fully filled, remainder
// (5) added as phantom BUY at the same price.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewBuyLevel_ExceedsSimSell_PhantomForRemainder) {
    auto* sym = registerSymbol("TEST-EX-B");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    auto batch = makeBatch("TEST-EX-B", {
        makeL2Update(coinbase::Side::BUY, 100.0, 15.0)   // 15 requested, 10 available
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    // Simulator SELL fully filled
    EXPECT_EQ(sim_sell->cum_quantity, kQty10);

    // Phantom BUY for remaining 5 added at kPrice100
    auto [buy_level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(buy_level, nullptr);
    EXPECT_EQ(buy_level->total_quantity, kQty5);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice100), kQty5);
}

// SELL level2 qty (15) > resting BUY qty (10): BUY fully filled, remainder added as phantom SELL.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_NewSellLevel_ExceedsSimBuy_PhantomForRemainder) {
    auto* sym = registerSymbol("TEST-EX-S");

    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice100, kQty10);

    auto batch = makeBatch("TEST-EX-S", {
        makeL2Update(coinbase::Side::SELL, 100.0, 15.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, kQty10);

    auto [sell_level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    ASSERT_NE(sell_level, nullptr);
    EXPECT_EQ(sell_level->total_quantity, kQty5);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice100), kQty5);
}

// ===========================================================================
// onLevel2Updates — existing level qty increase with simulator order present
// ===========================================================================

// Level already has a simulator SELL + some phantoms. A new level2 event says
// the qty grew, so a delta phantom is appended. Only getMDLevelQty (phantom
// tracking) grows; the simulator order quantity is unchanged.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_ExistingLevel_QtyIncreased_SimOrderPresent) {
    auto* sym = registerSymbol("TEST-QI-1");

    // Resting simulator SELL at 101 (not on the price we'll update)
    addSimOrder(sym, Side::SELL, kPrice101, kQty10);

    // Seed an existing phantom SELL at 102 with qty=5
    sym->order_book_->addBookOrder(9001, to_book_side(Side::SELL), kPrice102, kQty5, 500, 1);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice102), kQty5);

    // Level2 event says qty at 102 is now 10 (delta = 5 → new phantom appended)
    auto batch = makeBatch("TEST-QI-1", {
        makeL2Update(coinbase::Side::SELL, 102.0, 10.0)
    });
    exchange_->onLevel2Updates(nullptr, 2, batch);
    drainEvents();

    // getMDLevelQty now reflects both phantoms
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice102), kQty10);

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice102);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);
    EXPECT_EQ(level->orders.size(), 2u);
}

// ===========================================================================
// onLevel2Updates — existing level qty decrease with simulator order present
// ===========================================================================

// Level has a simulator BUY + a phantom BUY. Level2 says qty dropped (only
// the phantom portion). dispatchEvent reduces/removes the phantom from the
// back, leaving the simulator order intact.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_ExistingLevel_QtyDecreased_PhantomReducedSimOrderUntouched) {
    auto* sym = registerSymbol("TEST-QD-1");

    // Simulator BUY at 99
    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice99, kQty10);

    // Also seed a phantom BUY at 99 (qty=10), so getMDLevelQty=10, total=20
    sym->order_book_->addBookOrder(9002, to_book_side(Side::BUY), kPrice99, kQty10, 500, 1);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice99), kQty10);
    {
        auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(level->total_quantity, kQty20);
    }

    // Level2 says qty at 99 dropped to 5 (md_level_qty was 10 → diff=5)
    // dispatchEvent reduces the back phantom from 10→5
    auto batch = makeBatch("TEST-QD-1", {
        makeL2Update(coinbase::Side::BUY, 99.0, 5.0)
    });
    exchange_->onLevel2Updates(nullptr, 2, batch);
    drainEvents();

    // getMDLevelQty reduced accordingly
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice99), kQty5);

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(level, nullptr);
    // total_quantity = sim (10) + reduced phantom (5)
    EXPECT_EQ(level->total_quantity, kQty10 + kQty5);

    // Simulator order untouched
    EXPECT_EQ(sim_buy->quantity, kQty10);
    EXPECT_EQ(sim_buy->cum_quantity, 0);
}

// Level2 says qty dropped to zero for the phantom portion. Phantom is deleted;
// simulator order remains the only entry on the level.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_ExistingLevel_QtyToZero_PhantomDeletedSimOrderSurvives) {
    auto* sym = registerSymbol("TEST-QZ-1");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // Phantom SELL at 100 qty=5
    sym->order_book_->addBookOrder(9003, to_book_side(Side::SELL), kPrice100, kQty5, 500, 1);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice100), kQty5);

    // Level2 says qty at 100 dropped to 0 (phantom fully removed)
    auto batch = makeBatch("TEST-QZ-1", {
        makeL2Update(coinbase::Side::SELL, 100.0, 0.0)
    });
    exchange_->onLevel2Updates(nullptr, 2, batch);
    drainEvents();

    // Phantom gone, MD tracking = 0
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice100), 0);

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);   // simulator order only
    EXPECT_EQ(level->orders.size(), 1u);

    EXPECT_EQ(sim_sell->quantity, kQty10);
    EXPECT_EQ(sim_sell->cum_quantity, 0);
}

// ===========================================================================
// onLevel2Updates — batch with multiple updates in one call
// ===========================================================================

// A batch containing two non-crossing updates (BUY at 99, SELL at 102) each
// builds up the book without triggering any matches.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_BatchTwoNonCrossingUpdates_BothPhantomAdded) {
    auto* sym = registerSymbol("TEST-BATCH-1");

    // No simulator orders — clean book
    auto batch = makeBatch("TEST-BATCH-1", {
        makeL2Update(coinbase::Side::BUY,  99.0,  5.0),
        makeL2Update(coinbase::Side::SELL, 102.0, 5.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice99),  kQty5);
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice102), kQty5);

    auto [buy_level,  bi] = sym->order_book_->getLevel(to_book_side(Side::BUY),  kPrice99);
    auto [sell_level, si] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice102);
    ASSERT_NE(buy_level,  nullptr);
    ASSERT_NE(sell_level, nullptr);
    EXPECT_EQ(buy_level->total_quantity,  kQty5);
    EXPECT_EQ(sell_level->total_quantity, kQty5);
}

// A batch where the first update crosses a resting simulator order (full fill)
// and the second is a non-crossing update that builds a phantom level.
TEST_F(CoinbaseExchangeTest, OnLevel2Updates_BatchMatchThenBuildLevel) {
    auto* sym = registerSymbol("TEST-BATCH-2");

    // Simulator SELL at 101
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice101, kQty5);

    // BUY at 101 (matches sim SELL) + BUY at 99 (no match)
    auto batch = makeBatch("TEST-BATCH-2", {
        makeL2Update(coinbase::Side::BUY, 101.0, 5.0),
        makeL2Update(coinbase::Side::BUY,  99.0, 5.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch);
    drainEvents();

    // First update fully filled the simulator SELL
    EXPECT_EQ(sim_sell->cum_quantity, kQty5);

    // Second update created a phantom BUY at 99
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice99), kQty5);
    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice99);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty5);
}

// ===========================================================================
// onMarketTrades — enqueued as TRADE events, dispatched by processSequencedEvents
// (trade matching against simulator orders is a TODO in dispatchEvent; these
//  tests verify that trade events are accepted and queued without panic, and
//  that the resting simulator orders are NOT disturbed until the TODO is done)
// ===========================================================================

// A BUY market trade arrives. The simulator SELL order at the same price is
// currently unaffected (dispatchEvent TODO). Trade is accepted gracefully.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_BuyTrade_SimSellUntouched_AcceptedGracefully) {
    auto* sym = registerSymbol("TEST-MT-B");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-B", coinbase::Side::BUY, 100.0, 10.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();

    // dispatchEvent for TRADE is a TODO — sim order should be untouched
    EXPECT_EQ(sim_sell->cum_quantity, 0);
    EXPECT_EQ(sim_sell->quantity, kQty10);
}

// A SELL market trade arrives. Simulator BUY order unaffected (TODO).
TEST_F(CoinbaseExchangeTest, OnMarketTrades_SellTrade_SimBuyUntouched_AcceptedGracefully) {
    auto* sym = registerSymbol("TEST-MT-S");

    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice100, kQty10);

    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-S", coinbase::Side::SELL, 100.0, 10.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, 0);
    EXPECT_EQ(sim_buy->quantity, kQty10);
}

// Multiple market trades arrive in one call; all are enqueued.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_MultipleTrades_AllAccepted) {
    auto* sym = registerSymbol("TEST-MT-MULTI");

    // No simulator orders — just verify no crash
    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-MULTI", coinbase::Side::BUY,  100.0, 5.0),
        makeMarketTrade("TEST-MT-MULTI", coinbase::Side::SELL, 100.0, 3.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();   // should complete without assert/crash

    // Book is empty — no matches expected
    auto [buy_level,  bi] = sym->order_book_->getLevel(to_book_side(Side::BUY),  kPrice100);
    auto [sell_level, si] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    EXPECT_EQ(buy_level,  nullptr);
    EXPECT_EQ(sell_level, nullptr);
}

// ===========================================================================
// Interleaved level2 + market trade events
// ===========================================================================

// Level2 event builds a phantom SELL, then a market trade arrives.
// Book state after both: phantom is in place, trade queued (TODO: no match yet).
TEST_F(CoinbaseExchangeTest, InterleavedLevel2AndTrade_PhantomBuildsThenTradeArrives) {
    auto* sym = registerSymbol("TEST-IL-1");

    // Level2 SELL at 101 (no sim orders on book → phantom added)
    auto l2batch = makeBatch("TEST-IL-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 5.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, l2batch);
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty5);

    // BUY trade at 101 arrives
    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-IL-1", coinbase::Side::BUY, 101.0, 5.0)
    };
    exchange_->onMarketTrades(nullptr, 2, trades);
    drainEvents();

    // Phantom level intact (trade matching is TODO)
    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty5);
}

// Level2 BUY at 100 matches resting SELL sim order, then a subsequent level2
// increases the phantom count at a different price — both interactions correct.
TEST_F(CoinbaseExchangeTest, InterleavedLevel2_MatchThenBuild_CorrectFinalState) {
    auto* sym = registerSymbol("TEST-IL-2");

    // Resting SELL at 100
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // First batch: BUY at 100 fully matches SELL
    auto batch1 = makeBatch("TEST-IL-2", {
        makeL2Update(coinbase::Side::BUY, 100.0, 10.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, batch1);
    drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty10);

    // Second batch: SELL level at 102 added (new phantom)
    auto batch2 = makeBatch("TEST-IL-2", {
        makeL2Update(coinbase::Side::SELL, 102.0, 5.0)
    });
    exchange_->onLevel2Updates(nullptr, 2, batch2);
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice102), kQty5);
    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice102);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty5);
}
