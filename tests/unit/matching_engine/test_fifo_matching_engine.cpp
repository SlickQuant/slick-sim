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

class FifoMatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_response_queue_ = std::make_unique<slick::SlickQueue<OrderResponse>>(4096, "test_order_responses");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*order_response_queue_);
        order_book_ = std::make_unique<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
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
    std::unique_ptr<OrderBook> order_book_;
    std::unique_ptr<OrderResponseCollector> collector_;
    slick::ObjectPool<Order> order_pool_{1024};
};

TEST_F(FifoMatchingEngineTest, MatchBuyAgainstSingleSellOrder) {
    // Add resting sell order at price 100
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order at price 100
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_FALSE(trades.empty());
    EXPECT_EQ(buy_order->filled_quantity, kQty10);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
}

TEST_F(FifoMatchingEngineTest, MatchSellAgainstSingleBuyOrder) {
    // Add resting buy order at price 100
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(buy_order, 1000, 1);

    // Create sell order at price 100
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty10);

    // Match the sell order
    auto [reject_reason, trades] = matching_engine_->match(
        sell_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_FALSE(trades.empty());
    EXPECT_EQ(sell_order->filled_quantity, kQty10);
    EXPECT_EQ(sell_order->leaves_quantity, 0);
}

TEST_F(FifoMatchingEngineTest, PartialFillBuyOrder) {
    // Add resting sell order with smaller quantity at price 100
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order with larger quantity
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->filled_quantity, kQty5);
    EXPECT_EQ(buy_order->leaves_quantity, kQty5);
}

TEST_F(FifoMatchingEngineTest, PartialFillSellOrder) {
    // Add resting buy order with smaller quantity at price 100
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty5);
    order_book_->addOrder(buy_order, 1000, 1);

    // Create sell order with larger quantity
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty10);

    // Match the sell order
    auto [reject_reason, trades] = matching_engine_->match(
        sell_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(sell_order->filled_quantity, kQty5);
    EXPECT_EQ(sell_order->leaves_quantity, kQty5);
}

TEST_F(FifoMatchingEngineTest, MatchAcrossMultiplePriceLevels) {
    // Add multiple sell orders at different prices
    auto* sell1 = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, 1000, 1);

    auto* sell2 = createTestOrder(order_pool_, Side::SELL, kPrice101, kQty5);
    order_book_->addOrder(sell2, 1001, 2);

    // Create buy order that should match across both levels
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice101, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice101, kQty10, *order_book_, 2000, 3, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->filled_quantity, kQty10);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
    EXPECT_GE(trades.size(), 1); // Should have at least one trade summary
}

TEST_F(FifoMatchingEngineTest, NoMatchWhenPricesDontCross) {
    // Add resting sell order at price 101
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice101, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order at lower price (99)
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice99, kQty10);

    // Attempt to match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice99, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->filled_quantity, 0);
    EXPECT_EQ(buy_order->leaves_quantity, kQty10);
    EXPECT_TRUE(trades.empty());
}

TEST_F(FifoMatchingEngineTest, TradeSummaryGeneration) {
    // Add resting sell order
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

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
    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    // Match the buy order
    matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(buy_order->last_filled_price, kPrice100);
    EXPECT_EQ(buy_order->last_filled_qty, kQty10);
}

TEST_F(FifoMatchingEngineTest, EngineType) {
    EXPECT_EQ(matching_engine_->type(), MatchingEngine::Type::FIFO);
}
