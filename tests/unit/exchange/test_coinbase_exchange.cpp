#include <gtest/gtest.h>
#include <slick/logger.hpp>
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
    // processRequest() takes one request per call, the same way the exchange
    // thread's spin loop drives it.
    void drainOneRequest() { processRequest(); }
    // Nothing calls rejectMdSubscription yet - exposed so the frame it produces can
    // be checked before something does.
    using CoinbaseExchange::rejectMdSubscription;
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
        md_collector_   = std::make_unique<MarketDataCollector>(exchange_->md_queue());
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
        order->symbol = sym->symbol_;
        order->user_id = "test-user";
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
    std::unique_ptr<MarketDataCollector> md_collector_;

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
// onMarketTrades — enqueued as TRADE events, dispatched by processSequencedEvents.
// A venue print is replayed as an incoming aggressor order: it takes liquidity
// from the book exactly as the real taker did, and only the resulting fills are
// published (as TRADE_SUMMARY). The print itself is never relayed.
// ===========================================================================

// A BUY market trade fills the resting simulator SELL it crosses.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_BuyTrade_FillsRestingSimSell) {
    auto* sym = registerSymbol("TEST-MT-B");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-B", coinbase::Side::BUY, 100.0, 10.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty10);
    EXPECT_EQ(sim_sell->leaves_quantity, 0);

    auto summaries = md_collector_->collect(MDUpdateType::TRADE_SUMMARY);
    ASSERT_EQ(summaries.size(), 1u);
    auto* summary = reinterpret_cast<TradeSummary*>(summaries[0]->data);
    EXPECT_EQ(summary->aggressor_side, Side::BUY);
    EXPECT_EQ(summary->price, kPrice100);
    EXPECT_EQ(summary->qty, kQty10);
}

// Coinbase sends market_trades newest-first, so a batch sharing one timestamp
// must replay from the back of the array forwards to come out chronological. The
// sequencer gets this from the pairing of its sequence_id tie-break with the
// forward enqueue loop in onMarketTrades; inverting either replays the batch
// backwards, matching against the book and stamping trade ids in reverse.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_SameTimestamp_ReplayedOldestFirst) {
    auto* sym = registerSymbol("TEST-MT-ORDER");

    addSimOrder(sym, Side::SELL, kPrice100, kQty10);

    // One instant, newest-first as the venue sends them: the 3.0 print is newer
    // than the 2.0 print, so 2.0 must be applied first.
    exchange_->onMarketTrades(nullptr, 1, {
        makeMarketTrade("TEST-MT-ORDER", coinbase::Side::BUY, 100.0, 3.0, 7),
        makeMarketTrade("TEST-MT-ORDER", coinbase::Side::BUY, 100.0, 2.0, 7)
    });
    drainEvents();

    auto summaries = md_collector_->collect(MDUpdateType::TRADE_SUMMARY);
    ASSERT_EQ(summaries.size(), 2u);
    auto* first  = reinterpret_cast<TradeSummary*>(summaries[0]->data);
    auto* second = reinterpret_cast<TradeSummary*>(summaries[1]->data);

    EXPECT_EQ(first->qty, kQty3 - to_qty_t(1.0));   // 2.0, the older print
    EXPECT_EQ(second->qty, kQty3);                  // 3.0, the newer one

    // Public trade ids are stamped at publish time, so they follow the same order.
    EXPECT_LT(first->trade_id, second->trade_id);

    // The tape history the subscribe snapshot replays must agree.
    ASSERT_EQ(sym->trade_history_.size(), 2u);
    EXPECT_EQ(sym->trade_history_[0]->qty, to_qty_t(2.0));
    EXPECT_EQ(sym->trade_history_[1]->qty, kQty3);
}

// A SELL market trade fills the resting simulator BUY it crosses.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_SellTrade_FillsRestingSimBuy) {
    auto* sym = registerSymbol("TEST-MT-S");

    auto* sim_buy = addSimOrder(sym, Side::BUY, kPrice100, kQty10);

    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-S", coinbase::Side::SELL, 100.0, 10.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();

    EXPECT_EQ(sim_buy->cum_quantity, kQty10);
    EXPECT_EQ(sim_buy->leaves_quantity, 0);

    auto summaries = md_collector_->collect(MDUpdateType::TRADE_SUMMARY);
    ASSERT_EQ(summaries.size(), 1u);
    auto* summary = reinterpret_cast<TradeSummary*>(summaries[0]->data);
    EXPECT_EQ(summary->aggressor_side, Side::SELL);
    EXPECT_EQ(summary->price, kPrice100);
}

// With nothing crossable in the book, the aggressor rests and nothing prints:
// the tape is fill-driven, so a print with no liquidity to take produces no output.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_NoCrossableLiquidity_RestsAndPublishesNothing) {
    auto* sym = registerSymbol("TEST-MT-MULTI");

    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-MULTI", coinbase::Side::BUY, 100.0, 5.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();

    EXPECT_TRUE(md_collector_->collect(MDUpdateType::TRADE_SUMMARY).empty());

    // The unfilled remainder rests on the aggressor's own side, and cannot cross:
    // it only exists because nothing was still crossable at this limit.
    auto [buy_level,  bi] = sym->order_book_->getLevel(to_book_side(Side::BUY),  kPrice100);
    auto [sell_level, si] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    ASSERT_NE(buy_level, nullptr);
    EXPECT_EQ(buy_level->total_quantity, kQty5);
    EXPECT_EQ(sell_level, nullptr);
}

// A print bigger than the crossable liquidity fills what it can and rests the rest.
TEST_F(CoinbaseExchangeTest, OnMarketTrades_PartialFill_RemainderRestsOnAggressorSide) {
    auto* sym = registerSymbol("TEST-MT-RESIDUAL");

    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice100, kQty3);

    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-MT-RESIDUAL", coinbase::Side::BUY, 100.0, 10.0)
    };
    exchange_->onMarketTrades(nullptr, 1, trades);
    drainEvents();

    EXPECT_EQ(sim_sell->cum_quantity, kQty3);

    auto summaries = md_collector_->collect(MDUpdateType::TRADE_SUMMARY);
    ASSERT_EQ(summaries.size(), 1u);
    EXPECT_EQ(reinterpret_cast<TradeSummary*>(summaries[0]->data)->qty, kQty3);

    // kQty10 - kQty3 left over, resting as a BUY at the print price
    auto [buy_level, bi] = sym->order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(buy_level, nullptr);
    EXPECT_EQ(buy_level->total_quantity, kQty10 - kQty3);
}

// ===========================================================================
// Interleaved level2 + market trade events
// ===========================================================================

// Level2 event builds a phantom SELL, then a market trade consumes it.
TEST_F(CoinbaseExchangeTest, InterleavedLevel2AndTrade_TradeConsumesPhantom) {
    auto* sym = registerSymbol("TEST-IL-1");

    // Level2 SELL at 101 (no sim orders on book → phantom added)
    auto l2batch = makeBatch("TEST-IL-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 5.0)
    });
    exchange_->onLevel2Updates(nullptr, 1, l2batch);
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty5);

    // BUY trade at 101 arrives and takes that phantom liquidity
    std::vector<coinbase::MarketTrade> trades = {
        makeMarketTrade("TEST-IL-1", coinbase::Side::BUY, 101.0, 5.0, 2)
    };
    exchange_->onMarketTrades(nullptr, 2, trades);
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), 0);
}

// A trade takes phantom liquidity, then the venue's own level update reports the
// post-trade quantity. The level must settle at what the venue reports, not be
// reduced a second time by the same quantity.
TEST_F(CoinbaseExchangeTest, TradeThenLevelUpdate_DoesNotDoubleReduceLevel) {
    auto* sym = registerSymbol("TEST-DBL-1");

    // Phantom SELL of 10 at 101
    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-DBL-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 1)
    }));
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10);

    // BUY trade of 3 at 101 consumes 3 of it, leaving 7.
    exchange_->onMarketTrades(nullptr, 2, {
        makeMarketTrade("TEST-DBL-1", coinbase::Side::BUY, 101.0, 3.0, 5)
    });
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);

    // The venue's level update for that same trade carries the post-trade
    // quantity. The book already absorbed the trade, so the two agree and the
    // level must stay at 7 rather than being reduced by the same 3 again.
    exchange_->onLevel2Updates(nullptr, 3, makeBatch("TEST-DBL-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 7.0, 5)
    }));
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);
}

// A level update stamped strictly before the trade still reports the pre-trade
// quantity. Taking it at face value would add the traded quantity back, only for
// the next update to remove it again.
TEST_F(CoinbaseExchangeTest, StaleLevelUpdateAfterTrade_DoesNotRestoreTradedQty) {
    auto* sym = registerSymbol("TEST-DBL-4");

    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-DBL-4", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 1)
    }));
    drainEvents();

    // Trade at t=10 takes 3, leaving 7.
    exchange_->onMarketTrades(nullptr, 2, {
        makeMarketTrade("TEST-DBL-4", coinbase::Side::BUY, 101.0, 3.0, 10)
    });
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);

    // An update stamped t=5 predates the trade and reports the pre-trade 10.
    exchange_->onLevel2Updates(nullptr, 3, makeBatch("TEST-DBL-4", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 5)
    }));
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);
}

// A level update landing between two trades at the same price reflects the
// earlier one but not the later one. Offsetting by both would over-reduce the
// level; only the trade the update is too old to have seen may be offset.
TEST_F(CoinbaseExchangeTest, LevelUpdateBetweenTwoTrades_OffsetsOnlyTheLaterTrade) {
    auto* sym = registerSymbol("TEST-DBL-5");

    // Phantom SELL of 20 at 101.
    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-DBL-5", {
        makeL2Update(coinbase::Side::SELL, 101.0, 20.0, 1)
    }));
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty20);

    // Two BUY prints at the same price: 3 at t=10, then 5 at t=20. Drained
    // separately so both land before the level update below.
    exchange_->onMarketTrades(nullptr, 2, {
        makeMarketTrade("TEST-DBL-5", coinbase::Side::BUY, 101.0, 3.0, 10)
    });
    drainEvents();
    exchange_->onMarketTrades(nullptr, 3, {
        makeMarketTrade("TEST-DBL-5", coinbase::Side::BUY, 101.0, 5.0, 20)
    });
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty20 - kQty3 - kQty5);

    // An update stamped t=15 saw the first trade (20 - 3 = 17) but not the
    // second. Offsetting by both credits would target 20 - 3 - 5 - 3 = 9.
    exchange_->onLevel2Updates(nullptr, 4, makeBatch("TEST-DBL-5", {
        makeL2Update(coinbase::Side::SELL, 101.0, 17.0, 15)
    }));
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty20 - kQty3 - kQty5);

    // The later trade's credit was kept, not consumed, so an update stamped
    // after it still reconciles cleanly.
    exchange_->onLevel2Updates(nullptr, 5, makeBatch("TEST-DBL-5", {
        makeL2Update(coinbase::Side::SELL, 101.0, 12.0, 25)
    }));
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty20 - kQty3 - kQty5);
}

// What gets published on l2_data must be what the book actually holds. When
// reconcilePhantomQty lowers a stale update's target, emitting the venue's raw
// event.qty would leave the internal book corrected while downstream clients
// rebuilt the pre-adjustment quantity.
TEST_F(CoinbaseExchangeTest, StaleLevelUpdate_PublishesReconciledQtyNotRawVenueQty) {
    auto* sym = registerSymbol("TEST-PUB-1");

    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-PUB-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 1)
    }));
    drainEvents();

    // Trade at t=10 takes 3, leaving 7.
    exchange_->onMarketTrades(nullptr, 2, {
        makeMarketTrade("TEST-PUB-1", coinbase::Side::BUY, 101.0, 3.0, 10)
    });
    drainEvents();

    // Update stamped t=5 predates the trade and still reports the pre-trade 10.
    exchange_->onLevel2Updates(nullptr, 3, makeBatch("TEST-PUB-1", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 5)
    }));
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);

    // The buffer flushes when the sequence number changes, so drive one more
    // update at an unrelated price to push the stale one downstream.
    exchange_->onLevel2Updates(nullptr, 4, makeBatch("TEST-PUB-1", {
        makeL2Update(coinbase::Side::BUY, 99.0, 1.0, 20)
    }));
    drainEvents();

    // Find the published delta for the stale update.
    bool found = false;
    for (auto* update : md_collector_->collect(MDUpdateType::LEVEL)) {
        auto* level_update = reinterpret_cast<MDLevelUpdate*>(update->data);
        for (uint32_t i = 0; i < level_update->num_level_update; ++i) {
            const auto& level = level_update->levels[i];
            if (level.price == kPrice101 && level.event_time == 5) {
                EXPECT_EQ(level.qty, kQty10 - kQty3);   // not the raw 10.0
                found = true;
            }
        }
    }
    EXPECT_TRUE(found) << "no l2 delta published for the stale update";
}

// A real venue's L2 shows your own resting orders, and the snapshot path
// publishes total_quantity, so a feed-driven delta must include the simulator's
// orders at that price too — not just the phantom liquidity mirroring the venue.
TEST_F(CoinbaseExchangeTest, LevelUpdate_PublishedQtyIncludesSimulatorOrders) {
    auto* sym = registerSymbol("TEST-PUB-2");

    // Simulator SELL of 5 resting at 101, then the venue quotes 10 at that price.
    addSimOrder(sym, Side::SELL, kPrice101, kQty5);
    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-PUB-2", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 2000)
    }));
    drainEvents();

    // Phantom mirrors the venue's 10; the level as a whole holds 15.
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10);
    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice101);
    ASSERT_NE(level, nullptr);
    ASSERT_EQ(level->total_quantity, kQty15);

    // Flush the buffer with an update at an unrelated price.
    exchange_->onLevel2Updates(nullptr, 2, makeBatch("TEST-PUB-2", {
        makeL2Update(coinbase::Side::BUY, 99.0, 1.0, 2001)
    }));
    drainEvents();

    // Two producers emit level deltas — the order book observer and the feed's
    // own level_update_buffer_ — so gather every published quantity at this price
    // rather than assuming which frame comes first.
    std::vector<qty_t> published;
    for (auto* update : md_collector_->collect(MDUpdateType::LEVEL)) {
        auto* level_update = reinterpret_cast<MDLevelUpdate*>(update->data);
        for (uint32_t i = 0; i < level_update->num_level_update; ++i) {
            const auto& lvl = level_update->levels[i];
            if (lvl.price == kPrice101) {
                published.push_back(lvl.qty);
            }
        }
    }
    ASSERT_FALSE(published.empty()) << "no l2 delta published for the quoted level";

    size_t full_level = 0;
    size_t phantom_only = 0;
    for (auto q : published) {
        full_level   += (q == kQty15);
        phantom_only += (q == kQty10);
    }
    EXPECT_GT(full_level, 0u) << "no delta carried the whole level (phantom + simulator)";
    EXPECT_EQ(phantom_only, 0u) << "a delta published the venue quantity, omitting the simulator's order";
}

// The credit a trade leaves behind is consumed by the next level update at that
// price and must not survive to swallow a later, genuine cancel.
TEST_F(CoinbaseExchangeTest, TradeCredit_DoesNotSwallowLaterCancel) {
    auto* sym = registerSymbol("TEST-DBL-2");

    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-DBL-2", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 1)
    }));
    drainEvents();

    exchange_->onMarketTrades(nullptr, 2, {
        makeMarketTrade("TEST-DBL-2", coinbase::Side::BUY, 101.0, 3.0, 5)
    });
    drainEvents();

    exchange_->onLevel2Updates(nullptr, 3, makeBatch("TEST-DBL-2", {
        makeL2Update(coinbase::Side::SELL, 101.0, 7.0, 5)
    }));
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);

    // A later reduction is a real cancel: it must be applied in full.
    exchange_->onLevel2Updates(nullptr, 4, makeBatch("TEST-DBL-2", {
        makeL2Update(coinbase::Side::SELL, 101.0, 5.0, 10)
    }));
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty5);
}

// A trade that fills only the simulator's own order credits nothing: the venue's
// level still legitimately drops, because that liquidity really did trade there.
TEST_F(CoinbaseExchangeTest, TradeFillingOnlySimOrder_LeavesLevelUpdateApplied) {
    auto* sym = registerSymbol("TEST-DBL-3");

    // Simulator SELL of 3 at 101, resting ahead of a phantom 10 at the same price.
    auto* sim_sell = addSimOrder(sym, Side::SELL, kPrice101, kQty3);
    exchange_->onLevel2Updates(nullptr, 1, makeBatch("TEST-DBL-3", {
        makeL2Update(coinbase::Side::SELL, 101.0, 10.0, 1)
    }));
    drainEvents();
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10);

    // A BUY of 3 hits the simulator order first (FIFO), leaving phantom untouched.
    exchange_->onMarketTrades(nullptr, 2, {
        makeMarketTrade("TEST-DBL-3", coinbase::Side::BUY, 101.0, 3.0, 5)
    });
    drainEvents();
    EXPECT_EQ(sim_sell->cum_quantity, kQty3);
    ASSERT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10);

    // Nothing was credited, so the venue's reduction applies in full.
    exchange_->onLevel2Updates(nullptr, 3, makeBatch("TEST-DBL-3", {
        makeL2Update(coinbase::Side::SELL, 101.0, 7.0, 5)
    }));
    drainEvents();

    EXPECT_EQ(sym->order_book_->getMDLevelQty(kPrice101), kQty10 - kQty3);
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

// ===========================================================================
// processRequest dispatch — MD_UNSUBSCRIPTION
// ===========================================================================

// Both publishers write an MD_UNSUBSCRIPTION request when a client unsubscribes
// or disconnects, but processRequest()'s switch once handled only NEW_ORDER_SINGLE,
// ORDER_REPLACE_REQUEST, ORDER_CANCEL_REQUEST and MD_SUBSCRIPTION. The request was
// read off the queue and dropped, so handleMdUnsubscription() — implemented on both
// adapters — never ran, num_subscriptions_ only ever climbed, and the upstream feed
// was never torn down when the last client went away.
TEST_F(CoinbaseExchangeTest, MdUnsubscriptionRequestReachesTheHandler) {
    auto* sym = registerSymbol("TEST-UNSUB-1");
    sym->num_subscriptions_.store(2, std::memory_order_relaxed);

    auto enqueueUnsubscribe = [&](std::string_view name) {
        auto& queue = exchange_->request_queue();
        auto index = queue.reserve();
        auto* request = queue[index];
        utils::copy_wire_field(request->symbol, name);
        request->msg_type = MessageType::MD_UNSUBSCRIPTION;
        request->md_subscription.channel =
            static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2);
        request->md_subscription.client = nullptr;
        queue.publish(index);
    };

    // Two subscribers, so this one drops the count without tearing the feed down.
    enqueueUnsubscribe("TEST-UNSUB-1");
    exchange_->drainOneRequest();
    EXPECT_EQ(sym->num_subscriptions_.load(std::memory_order_relaxed), 2u - 1u)
        << "MD_UNSUBSCRIPTION was dropped by processRequest()'s switch";

    // The last subscriber leaving also takes the feed-teardown branch.
    enqueueUnsubscribe("TEST-UNSUB-1");
    exchange_->drainOneRequest();
    EXPECT_EQ(sym->num_subscriptions_.load(std::memory_order_relaxed), 0u);
}

// An unsubscribe naming a symbol nobody ever subscribed to is dropped by the
// handler, not by the switch above it.
TEST_F(CoinbaseExchangeTest, MdUnsubscriptionForUnknownSymbolIsIgnored) {
    auto& queue = exchange_->request_queue();
    auto index = queue.reserve();
    auto* request = queue[index];
    utils::copy_wire_field(request->symbol, std::string_view("TEST-UNSUB-NEVER-SEEN"));
    request->msg_type = MessageType::MD_UNSUBSCRIPTION;
    request->md_subscription.channel = static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2);
    request->md_subscription.client = nullptr;
    queue.publish(index);

    exchange_->drainOneRequest();

    EXPECT_EQ(SymbolManager::instance().getSymbol("TEST-UNSUB-NEVER-SEEN", Venue::COINBASE), nullptr);
}

// ===========================================================================
// MD subscription responses - reject_reason
// ===========================================================================

// A reject frame reserves room for the response header and nothing more. The
// reason used to be accepted and then dropped, so the field carried whatever the
// recycled queue slot held: a reject could arrive claiming NONE, and a publisher
// that trusts the field goes on to read a BookSnapshot the frame never reserved.
TEST_F(CoinbaseExchangeTest, RejectedMdSubscriptionCarriesItsReason) {
    Request req{};
    utils::copy_wire_field(req.symbol, std::string_view("TEST-REJECT-1"));
    req.msg_type = MessageType::MD_SUBSCRIPTION;
    req.md_subscription.channel = static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2);
    req.md_subscription.client = nullptr;

    md_collector_->reset();
    exchange_->rejectMdSubscription(req, MDSubscriptionRejectReason::UNKNOWN_CONTRACT);

    bool seen = false;
    for (auto* update : md_collector_->collect(MDUpdateType::SUB_RESPONSE)) {
        if (std::string_view(update->symbol) != "TEST-REJECT-1") {
            continue;
        }
        seen = true;
        auto* response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
        EXPECT_EQ(response->channel,
                  static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2));
        EXPECT_EQ(response->reject_reason, MDSubscriptionRejectReason::UNKNOWN_CONTRACT)
            << "the reject was published without the reason it was given";
    }
    EXPECT_TRUE(seen) << "no SUB_RESPONSE was published for the rejected subscription";
}

// The accepted path carries a book snapshot, and says so, rather than leaving the
// discriminant to whatever the slot last held.
TEST_F(CoinbaseExchangeTest, AcceptedL2SubscriptionReportsNoRejectReason) {
    auto* sym = registerSymbol("TEST-ACCEPT-1");
    addSimOrder(sym, Side::BUY, kPrice99, kQty5);

    md_collector_->reset();
    sym->order_book_->populateL2SubscriptionResponse(
        exchange_->md_queue(), static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2));

    bool seen = false;
    for (auto* update : md_collector_->collect(MDUpdateType::SUB_RESPONSE)) {
        if (std::string_view(update->symbol) != "TEST-ACCEPT-1") {
            continue;
        }
        seen = true;
        auto* response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
        EXPECT_EQ(response->reject_reason, MDSubscriptionRejectReason::NONE);

        // And the snapshot it claims to carry is really there.
        auto* snapshot = reinterpret_cast<BookSnapshot*>(response->data);
        EXPECT_EQ(snapshot->num_bid, 1u);
        EXPECT_EQ(snapshot->num_ask, 0u);
    }
    EXPECT_TRUE(seen) << "no SUB_RESPONSE was published for the accepted subscription";
}
