#include <gtest/gtest.h>
#include <matching_engine/fifo_matching_engine.hpp>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <common/messages.hpp>
#include <slick/queue.h>
#include <slick/object_pool.h>
#include <set>
#include <string>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::engine;

class FifoMatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        response_queue_ = std::make_unique<slick::queue<OrderResponse>>(4096, "test_order_responses");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*response_queue_);
        order_book_ = std::shared_ptr<OrderBook>(new OrderBookImpl<OrderBookType::L2>(kSymbolId, "BTC-USD", Venue::COINBASE));
        order_book_->addObserver(order_book_->shared_from_this());
        collector_ = std::make_unique<OrderResponseCollector>(*response_queue_);
    }

    void TearDown() override {
        order_book_.reset();
        matching_engine_.reset();
        response_queue_.reset();
        collector_.reset();
    }

    std::unique_ptr<slick::queue<OrderResponse>> response_queue_;
    std::unique_ptr<FifoMatchingEngine> matching_engine_;
    std::shared_ptr<OrderBook> order_book_;
    std::unique_ptr<OrderResponseCollector> collector_;
};

TEST_F(FifoMatchingEngineTest, MatchBuyAgainstSingleSellOrder) {
    // Add resting sell order at price 100
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order at price 100
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_FALSE(trades.empty());
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_EQ(buy_order->avg_fill_price, kPrice100);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
}

TEST_F(FifoMatchingEngineTest, ReusedOrderResetsFillAccounting) {
    auto reused_book = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE, 1);
    reused_book->addObserver(reused_book->shared_from_this());

    auto* order = reused_book->allocateOrder();
    order->side = Side::BUY;
    order->price = kPrice100;
    order->quantity = kQty10;
    order->leaves_quantity = kQty10;
    order->cum_quantity = kQty5;
    order->cum_value = static_cast<cum_value_t>(kPrice100) * static_cast<cum_value_t>(kQty5);
    order->avg_fill_price = kPrice100;
    order->num_fills = 1;
    order->last_fill_qty = kQty5;
    order->last_fill_time = 42;

    reused_book->freeOrder(order);
    auto* fresh_order = reused_book->allocateOrder();

    EXPECT_EQ(fresh_order, order);
    EXPECT_EQ(fresh_order->quantity, 0);
    EXPECT_EQ(fresh_order->leaves_quantity, 0);
    EXPECT_EQ(fresh_order->cum_quantity, 0);
    EXPECT_EQ(fresh_order->cum_value, cum_value_t{0});
    EXPECT_EQ(fresh_order->avg_fill_price, 0);
    EXPECT_EQ(fresh_order->num_fills, 0);
    EXPECT_FALSE(fresh_order->last_fill_time.has_value());
}

TEST_F(FifoMatchingEngineTest, MatchSellAgainstSingleBuyOrder) {
    // Add resting buy order at price 100
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, 1000, 1);

    // Create sell order at price 100
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);

    // Match the sell order
    auto [reject_reason, trades] = matching_engine_->match(
        sell_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_FALSE(trades.empty());
    EXPECT_EQ(sell_order->cum_quantity, kQty10);
    EXPECT_EQ(sell_order->leaves_quantity, 0);
}

TEST_F(FifoMatchingEngineTest, PartialFillBuyOrder) {
    // Add resting sell order with smaller quantity at price 100
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order with larger quantity
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty5);
    EXPECT_EQ(buy_order->leaves_quantity, kQty5);
}

TEST_F(FifoMatchingEngineTest, PartialFillSellOrder) {
    // Add resting buy order with smaller quantity at price 100
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty5);
    order_book_->addOrder(buy_order, 1000, 1);

    // Create sell order with larger quantity
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);

    // Match the sell order
    auto [reject_reason, trades] = matching_engine_->match(
        sell_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(sell_order->cum_quantity, kQty5);
    EXPECT_EQ(sell_order->leaves_quantity, kQty5);
}

TEST_F(FifoMatchingEngineTest, MatchAcrossMultiplePriceLevels) {
    // Add multiple sell orders at different prices
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, 1000, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty5);
    order_book_->addOrder(sell2, 1001, 2);

    // Create buy order that should match across both levels
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice101, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice101, kQty10, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
    EXPECT_GE(trades.size(), 1); // Should have at least one trade summary
}

TEST_F(FifoMatchingEngineTest, NoMatchWhenPricesDontCross) {
    // Add resting sell order at price 101
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice101, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order at lower price (99)
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice99, kQty10);

    // Attempt to match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice99, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_EQ(buy_order->leaves_quantity, kQty10);
    EXPECT_TRUE(trades.empty());
}

TEST_F(FifoMatchingEngineTest, TradeSummaryGeneration) {
    // Add resting sell order
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    ASSERT_FALSE(trades.empty());
    const auto& trade = trades[0];

    EXPECT_EQ(trade.price, kPrice100);
    EXPECT_EQ(trade.qty, kQty10);
    EXPECT_EQ(trade.aggressor_side, Side::BUY);
    EXPECT_EQ(trade.num_orders, 2);
    EXPECT_FALSE(trade.Trades.empty());
}

TEST_F(FifoMatchingEngineTest, LastFilledPriceAndQuantityTracking) {
    // Add resting sell order
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(buy_order->last_fill_price, kPrice100);
    EXPECT_EQ(buy_order->last_fill_qty, kQty10);
}

TEST_F(FifoMatchingEngineTest, AverageFillPrice_UsesFixedPointWeightedAverage) {
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty3);
    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty2);
    order_book_->addOrder(sell1, 1000, 1);
    order_book_->addOrder(sell2, 1001, 2);

    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice101, kQty5);
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice101, kQty5, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty5);
    EXPECT_EQ(buy_order->avg_fill_price, to_price_t(100.4));
    EXPECT_EQ(buy_order->leaves_quantity, 0);
    EXPECT_FALSE(trades.empty());
}

TEST_F(FifoMatchingEngineTest, EngineType) {
    EXPECT_EQ(matching_engine_->type(), MatchingEngine::Type::FIFO);
}

// ---------------------------------------------------------------------------
// A trade has two sides, and both belong to a client when both orders are the
// simulator's own. The aggressor's owner has always been told; the resting
// order's owner was not.
//
// `match()` publishes an execution report for the aggressor and then calls
// `book.executeOrder(book_order_id, …)`, which books the fill onto the resting
// Order but cannot publish anything — `OrderBook` holds no reference to the
// matching engine — and, when the resting order is fully filled, only removes it
// from the lookup maps without returning it to the pool. So the resting client
// saw its order vanish with no fill report, and the pooled Order leaked.
//
// The market-data replay path in the second `match()` overload always did this
// correctly, which is what makes the omission visible as an inconsistency.
// ---------------------------------------------------------------------------

TEST_F(FifoMatchingEngineTest, RestingOrderOwnerGetsAnExecutionReport) {
    auto* resting = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10,
                                    TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/1);
    order_book_->addOrder(resting, 1000, 1);
    const std::string resting_order_id(resting->order_id.c_str());

    // A different client crosses it.
    auto* aggressor = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10,
                                      TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/2);
    collector_->reset();

    auto [reject_reason, trades] = matching_engine_->match(
        aggressor, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);
    ASSERT_EQ(reject_reason, OrdRejectReason::NONE);

    int resting_fills = 0;
    int aggressor_fills = 0;
    for (const auto& response : collector_->collect()) {
        if (response.exec_type != ExecType::TRADE) {
            continue;
        }
        if (resting_order_id == response.order_id) {
            ++resting_fills;
            EXPECT_EQ(response.last_fill_price, kPrice100);
            EXPECT_EQ(response.cum_qty, kQty10);
            EXPECT_EQ(response.leaves_qty, 0);
            EXPECT_EQ(response.order_status, OrderStatus::FILLED);
            EXPECT_EQ(response.side, Side::SELL);
        } else {
            ++aggressor_fills;
        }
    }

    EXPECT_EQ(aggressor_fills, 1);
    EXPECT_EQ(resting_fills, 1) << "the resting order's owner was never told its order filled";
}

TEST_F(FifoMatchingEngineTest, PartiallyFilledRestingOrderReportsRemainingLeaves) {
    auto* resting = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10,
                                    TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/1);
    order_book_->addOrder(resting, 1000, 1);
    const std::string resting_order_id(resting->order_id.c_str());

    auto* aggressor = createTestOrder(order_book_, Side::BUY, kPrice100, kQty3,
                                      TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/2);
    collector_->reset();

    matching_engine_->match(aggressor, kPrice100, kQty3, *order_book_, 2000, 2000, 2,
                            SelfMatchPreventionMode::NONE);

    bool seen = false;
    for (const auto& response : collector_->collect()) {
        if (response.exec_type == ExecType::TRADE && resting_order_id == response.order_id) {
            seen = true;
            EXPECT_EQ(response.last_qty, kQty3);
            EXPECT_EQ(response.cum_qty, kQty3);
            EXPECT_EQ(response.leaves_qty, kQty10 - kQty3);
            EXPECT_EQ(response.order_status, OrderStatus::PARTIALLY_FILLED);
        }
    }
    EXPECT_TRUE(seen) << "no fill report for the partially filled resting order";

    // Still live, so it must stay in the book and keep its remaining quantity.
    EXPECT_EQ(order_book_->findOrder(resting->id), resting);
    EXPECT_EQ(resting->leaves_quantity, kQty10 - kQty3);
}

TEST_F(FifoMatchingEngineTest, FullyFilledRestingOrdersAreReturnedToThePool) {
    // Rest-and-fill the same shape repeatedly against a deliberately small pool. The
    // pool hands out fresh slots until it wraps, so reuse is only observable once the
    // round count passes its capacity; a leak then shows as a fresh slot every pass,
    // which is how it degrades in production — silently, because slick::ObjectPool
    // falls back to the heap once exhausted rather than failing.
    constexpr uint_fast32_t kPoolSize = 8;
    constexpr int kRounds = 64;
    std::shared_ptr<OrderBook> pooled_book(
        new OrderBookImpl<OrderBookType::L2>(kSymbolId, "BTC-USD", Venue::COINBASE, kPoolSize));
    pooled_book->addObserver(pooled_book->shared_from_this());

    std::set<const Order*> resting_slots;

    for (int i = 0; i < kRounds; ++i) {
        // Sequence numbers must keep climbing across rounds — the L3 book ignores an
        // update older than the last one it applied, so reusing them here would make
        // later rounds silently fail to rest.
        const uint64_t rest_seq = static_cast<uint64_t>(i) * 2 + 1;
        const uint64_t fill_seq = rest_seq + 1;

        auto* resting = createTestOrder(pooled_book, Side::SELL, kPrice100, kQty10,
                                        TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/1);
        resting_slots.insert(resting);
        pooled_book->addOrder(resting, 1000 + i, rest_seq);

        auto* aggressor = createTestOrder(pooled_book, Side::BUY, kPrice100, kQty10,
                                          TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/2);
        matching_engine_->match(aggressor, kPrice100, kQty10, *pooled_book, 2000 + i, 2000 + i,
                                fill_seq, SelfMatchPreventionMode::NONE);
        ASSERT_EQ(aggressor->leaves_quantity, 0) << "round " << i << " did not trade";
        ASSERT_EQ(resting->leaves_quantity, 0);

        // The aggressor's own recycling belongs to Symbol::addOrder, not the engine.
        pooled_book->freeOrder(aggressor);
    }

    EXPECT_LE(resting_slots.size(), kPoolSize)
        << "fully filled resting orders were not returned to the pool: "
        << resting_slots.size() << " distinct slots drawn across " << kRounds
        << " rounds from a pool of " << kPoolSize;
}
