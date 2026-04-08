#include <gtest/gtest.h>
#include <matching_engine/fifo_matching_engine.hpp>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <common/messages.hpp>
#include <slick/queue.h>
#include <slick/object_pool.h>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::engine;

// Synthetic order ID used as the "market data" aggressor (mirrors utils::nextOrderId() in dispatchEvent)
constexpr uint64_t kMarketOrderId = 999;

// Timestamps and seq nums for test events
constexpr uint64_t kT1 = 1000;
constexpr uint64_t kT2 = 2000;
constexpr uint64_t kT3 = 3000;

class CoinbaseMarketDataMatchingTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_response_queue_ = std::make_unique<slick::SlickQueue<OrderResponse>>(4096, "test_coinbase_md_responses");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*order_response_queue_);
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
        order_book_->addObserver(order_book_->shared_from_this());
        collector_ = std::make_unique<OrderResponseCollector>(*order_response_queue_);
    }

    void TearDown() override {
        order_book_.reset();
        matching_engine_.reset();
        order_response_queue_.reset();
        collector_.reset();
    }

    std::unique_ptr<slick::SlickQueue<OrderResponse>> order_response_queue_;
    std::unique_ptr<FifoMatchingEngine> matching_engine_;
    std::shared_ptr<OrderBook> order_book_;
    std::unique_ptr<OrderResponseCollector> collector_;
};

// =============================================================================
// Level2 Update — New Level Fully Matches
// =============================================================================

// Simulates: Coinbase BUY level2 arrives at price 100, matches resting SELL simulator order fully.
// dispatchEvent sees new level -> calls match(Side::BUY, ...) -> full consumption -> no phantom added.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_FullyMatchesRestingSellOrder) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_EQ(qty, 0);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, kPrice100);
    EXPECT_EQ(trades[0].qty, kQty10);
    EXPECT_EQ(trades[0].aggressor_side, Side::BUY);
    // cum_quantity tracks fills and is not affected by book updates
    EXPECT_EQ(sell_order->cum_quantity, kQty10);
}

// Simulates: Coinbase SELL level2 arrives at price 100, matches resting BUY simulator order fully.
TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_FullyMatchesRestingBuyOrder) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_EQ(qty, 0);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].aggressor_side, Side::SELL);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
}

// =============================================================================
// Level2 Update — Partial Matches
// =============================================================================

// Market qty (5) < resting qty (10): market is fully consumed, resting partially filled.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_MarketQtyLessThanRestingSell_PartialFill) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty5;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_EQ(qty, 0);                         // market fully consumed
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(sell_order->cum_quantity, kQty5);
    // Execution report captures the correct partial fill state at publish time
    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].order_status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty5);
    EXPECT_EQ(responses[0].leaves_qty, kQty5);
}

// Market qty (10) > resting qty (5): resting fully consumed, market has remainder.
// dispatchEvent would then call addBookOrder for the remainder.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_MarketQtyExceedsRestingSell_RemainderIsPositive) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_EQ(qty, kQty5);                     // 5 remaining -> dispatchEvent adds phantom
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(sell_order->cum_quantity, kQty5);
}

TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_MarketQtyLessThanRestingBuy_PartialFill) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty5;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_EQ(qty, 0);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(buy_order->cum_quantity, kQty5);
    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].order_status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(responses[0].leaves_qty, kQty5);
}

TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_MarketQtyExceedsRestingBuy_RemainderIsPositive) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty5);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_EQ(qty, kQty5);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(buy_order->cum_quantity, kQty5);
}

// =============================================================================
// Level2 Update — No Match (Price Does Not Cross)
// =============================================================================

// BUY level2 at 99, resting SELL at 100: price doesn't cross, no match.
// dispatchEvent would add a phantom BUY at 99 for the full qty.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_PriceDoesNotCross_NoMatch) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice99, qty, *order_book_, kT2, 2);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(qty, kQty10);                    // unchanged, full phantom would be created
    EXPECT_EQ(sell_order->cum_quantity, 0);    // resting order untouched
}

// SELL level2 at 101, resting BUY at 100: price doesn't cross, no match.
TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_PriceDoesNotCross_NoMatch) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice101, qty, *order_book_, kT2, 2);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(qty, kQty10);
    EXPECT_EQ(buy_order->cum_quantity, 0);     // resting order untouched
}

// Empty book: no resting orders, no match possible.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_EmptyBook_NoMatch) {
    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT1, 1);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(qty, kQty10);
}

// Zero qty level update (level deletion signal): no match even if a resting order exists.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_ZeroQty_NoMatch) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = 0;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(qty, 0);
    EXPECT_EQ(sell_order->cum_quantity, 0);    // resting order untouched
}

// =============================================================================
// Level2 Update — Multi-Level Sweep
// =============================================================================

// BUY level2 at 102 sweeps through SELL levels at 100 and 101.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_SweepsMultipleSellLevels) {
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, kT1, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty5);
    order_book_->addOrder(sell2, kT1, 2);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice102, qty, *order_book_, kT2, 3);

    EXPECT_EQ(qty, 0);
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, kPrice100);     // best ask matched first
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(trades[1].price, kPrice101);
    EXPECT_EQ(trades[1].qty, kQty5);
    EXPECT_EQ(sell1->cum_quantity, kQty5);
    EXPECT_EQ(sell2->cum_quantity, kQty5);
}

// SELL level2 at 98 sweeps through BUY levels at 100 and 99.
TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_SweepsMultipleBuyLevels) {
    auto* buy1 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty5);
    order_book_->addOrder(buy1, kT1, 1);

    auto* buy2 = createTestOrder(order_book_, Side::BUY, kPrice99, kQty5);
    order_book_->addOrder(buy2, kT1, 2);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice98, qty, *order_book_, kT2, 3);

    EXPECT_EQ(qty, 0);
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, kPrice100);     // best bid matched first
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(trades[1].price, kPrice99);
    EXPECT_EQ(trades[1].qty, kQty5);
    EXPECT_EQ(buy1->cum_quantity, kQty5);
    EXPECT_EQ(buy2->cum_quantity, kQty5);
}

// BUY level2 at 101 partially sweeps two SELL levels (15 wanted, 10 available), leaving remainder.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_SweepPartialAcrossTwoLevels) {
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, kT1, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty5);
    order_book_->addOrder(sell2, kT1, 2);

    qty_t qty = kQty15;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice101, qty, *order_book_, kT2, 3);

    EXPECT_EQ(qty, kQty5);                     // 15 - 10 matched = 5 remaining
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(sell1->cum_quantity, kQty5);
    EXPECT_EQ(sell2->cum_quantity, kQty5);
}

// =============================================================================
// FIFO Ordering
// =============================================================================

// Two SELL orders at the same price: first-added should be matched first (FIFO).
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_FifoOrdering_FirstOrderFilledFirst) {
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, kT1, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell2, kT1, 2);

    qty_t qty = kQty5;  // only enough to fill one order
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 3);

    EXPECT_EQ(qty, 0);
    EXPECT_EQ(sell1->cum_quantity, kQty5);     // first order filled first (FIFO)
    EXPECT_EQ(sell2->cum_quantity, 0);         // second order untouched
}

// =============================================================================
// Trade Summary Validation
// =============================================================================

// BUY market data: aggressor_side must be BUY, aggressor's order_id listed first.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_TradeSummary_CorrectAggressorSide) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    ASSERT_FALSE(trades.empty());
    EXPECT_EQ(trades[0].aggressor_side, Side::BUY);
    ASSERT_GE(trades[0].Trades.size(), 2u);
    EXPECT_EQ(trades[0].Trades[0].order_id, kMarketOrderId);   // aggressor first
    EXPECT_EQ(trades[0].Trades[1].order_id, sell_order->id);   // resting second
}

// SELL market data: aggressor_side must be SELL.
TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_TradeSummary_CorrectAggressorSide) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    ASSERT_FALSE(trades.empty());
    EXPECT_EQ(trades[0].aggressor_side, Side::SELL);
    ASSERT_GE(trades[0].Trades.size(), 2u);
    EXPECT_EQ(trades[0].Trades[0].order_id, kMarketOrderId);
    EXPECT_EQ(trades[0].Trades[1].order_id, buy_order->id);
}

// Trade executes at the resting order's price, not the event price.
// Market BUY at 101 matches resting SELL at 100 -> trade price = 100.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_TradeSummary_PriceIsRestingPrice) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice101, qty, *order_book_, kT2, 2);

    ASSERT_FALSE(trades.empty());
    EXPECT_EQ(trades[0].price, kPrice100);     // executes at resting price, not event price
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(qty, kQty5);                     // 10 - 5 = 5 remaining
}

// Two SELL orders at the same price filled by one BUY event: trade summaries are collapsed.
// The matching engine creates one TradeSummaryInfo per price level, incrementing num_orders for each fill.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_TradeSummary_CollapsedAtSamePrice) {
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, kT1, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell2, kT1, 2);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 3);

    EXPECT_EQ(qty, 0);
    ASSERT_EQ(trades.size(), 1u);              // collapsed: same price level = one summary
    EXPECT_EQ(trades[0].qty, kQty10);
    EXPECT_EQ(trades[0].num_orders, 3);        // 1 aggressor + 2 resting fills
    EXPECT_EQ(trades[0].Trades.size(), 3u);    // aggressor (updated qty), resting1, resting2
}

// Verify trade summary correctly identifies both sides' order IDs.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_TradeSummary_OrderIdsCorrect) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    ASSERT_FALSE(trades.empty());
    ASSERT_EQ(trades[0].Trades.size(), 2u);
    EXPECT_EQ(trades[0].Trades[0].order_id, kMarketOrderId);
    EXPECT_EQ(trades[0].Trades[1].order_id, sell_order->id);
}

// =============================================================================
// Execution Reports
// =============================================================================

// Full fill: verify TRADE exec type and FILLED status are published to the response queue.
// The execution report is published before book.executeOrder, capturing correct state.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_ExecutionReport_FullFill) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty10);
    EXPECT_EQ(responses[0].cum_qty, kQty10);
    EXPECT_EQ(responses[0].leaves_qty, 0);
}

// Partial fill: verify PARTIALLY_FILLED status and correct qty fields.
TEST_F(CoinbaseMarketDataMatchingTest, Level2BuyUpdate_ExecutionReport_PartialFill) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty5;  // only partially matches the resting order
    matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty5);
    EXPECT_EQ(responses[0].cum_qty, kQty5);
    EXPECT_EQ(responses[0].leaves_qty, kQty5);
}

// SELL market data fills a resting BUY: verify execution report for the buy order.
TEST_F(CoinbaseMarketDataMatchingTest, Level2SellUpdate_ExecutionReport_FullFill) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty10;
    matching_engine_->match(Side::SELL, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    auto responses = collector_->collect();
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(responses[0].order_status, OrderStatus::FILLED);
    EXPECT_EQ(responses[0].last_qty, kQty10);
    EXPECT_EQ(responses[0].leaves_qty, 0);
}

// =============================================================================
// Level2 Qty Change Simulation (existing price levels)
// =============================================================================

// Simulates dispatchEvent's "existing level, qty increased" path:
// An existing phantom SELL level grows from 5 to 10 -> delta phantom (5) is added.
TEST_F(CoinbaseMarketDataMatchingTest, Level2QtyIncrease_PhantomOrderAddedForDelta) {
    // Establish initial phantom level at kPrice100 with kQty5
    order_book_->addBookOrder(100, to_book_side(Side::SELL), kPrice100, kQty5, kT1, 1);
    EXPECT_EQ(order_book_->getMDLevelQty(kPrice100), kQty5);

    // Level qty increased: add delta phantom for the additional qty
    order_book_->addBookOrder(101, to_book_side(Side::SELL), kPrice100, kQty5, kT2, 2);

    EXPECT_EQ(order_book_->getMDLevelQty(kPrice100), kQty10);
    auto [level, index] = order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);
    EXPECT_EQ(level->orders.size(), 2u);
}

// Simulates dispatchEvent's "existing level, qty decreased" path:
// Two phantom BUY orders at kPrice100; the back one (last added, worst priority) is reduced to 0.
TEST_F(CoinbaseMarketDataMatchingTest, Level2QtyDecrease_PhantomOrderReducedFromBack) {
    // Establish two phantom orders at kPrice100
    order_book_->addBookOrder(100, to_book_side(Side::BUY), kPrice100, kQty5, kT1, 1);
    order_book_->addBookOrder(101, to_book_side(Side::BUY), kPrice100, kQty5, kT2, 2);
    EXPECT_EQ(order_book_->getMDLevelQty(kPrice100), kQty10);

    // Reduce from back: order 101 (last added = back of queue) reduced to 0 -> deleted
    order_book_->modifyOrder(101, kPrice100, 0, kT3, 0, 3, true);

    EXPECT_EQ(order_book_->getMDLevelQty(kPrice100), kQty5);
    auto [level, index] = order_book_->getLevel(to_book_side(Side::BUY), kPrice100);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty5);
    EXPECT_EQ(level->orders.size(), 1u);
}

// =============================================================================
// Sequential Level2 Updates — Book State Accumulation
// =============================================================================

// Simulates snapshot-style loading: non-crossing level2 updates on both sides build book state.
// A simulator SELL at 100 coexists with phantom BUY at 99 and phantom SELL at 102.
TEST_F(CoinbaseMarketDataMatchingTest, SequentialLevel2Updates_BuildBookState) {
    // Simulator order: SELL at kPrice100
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    // BUY level2 at kPrice99 — no match (99 < best ask 100), add phantom BUY at 99
    qty_t qty1 = kQty5;
    auto trades1 = matching_engine_->match(Side::BUY, 1001, kPrice99, qty1, *order_book_, kT2, 2);
    EXPECT_TRUE(trades1.empty());
    EXPECT_EQ(qty1, kQty5);
    order_book_->addBookOrder(1001, to_book_side(Side::BUY), kPrice99, qty1, kT2, 2);

    // SELL level2 at kPrice102 — no match (best bid 99 < 102), add phantom SELL at 102
    qty_t qty2 = kQty5;
    auto trades2 = matching_engine_->match(Side::SELL, 1002, kPrice102, qty2, *order_book_, kT3, 3);
    EXPECT_TRUE(trades2.empty());
    EXPECT_EQ(qty2, kQty5);
    order_book_->addBookOrder(1002, to_book_side(Side::SELL), kPrice102, qty2, kT3, 3);

    // Verify book state: phantom BUY at 99, simulator SELL at 100, phantom SELL at 102
    EXPECT_EQ(order_book_->getMDLevelQty(kPrice99), kQty5);    // phantom tracked
    EXPECT_EQ(order_book_->getMDLevelQty(kPrice100), 0);        // simulator order not in MD qty
    EXPECT_EQ(order_book_->getMDLevelQty(kPrice102), kQty5);    // phantom tracked
    EXPECT_EQ(sell_order->cum_quantity, 0);                     // simulator order untouched
}

// =============================================================================
// Trade Feed Matching (forward-looking — mirrors the TODO in dispatchEvent)
// =============================================================================

// Simulates what dispatchEvent would do for TRADE events once implemented:
// A BUY trade at price 100 matches against resting SELL simulator orders.
TEST_F(CoinbaseMarketDataMatchingTest, TradeUpdate_BuyTrade_MatchesRestingSellOrders) {
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::BUY, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].aggressor_side, Side::BUY);
    EXPECT_EQ(qty, 0);
    EXPECT_EQ(sell_order->cum_quantity, kQty10);
}

// A SELL trade at price 100 matches against resting BUY simulator orders.
TEST_F(CoinbaseMarketDataMatchingTest, TradeUpdate_SellTrade_MatchesRestingBuyOrders) {
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, kT1, 1);

    qty_t qty = kQty10;
    auto trades = matching_engine_->match(Side::SELL, kMarketOrderId, kPrice100, qty, *order_book_, kT2, 2);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].aggressor_side, Side::SELL);
    EXPECT_EQ(qty, 0);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
}
