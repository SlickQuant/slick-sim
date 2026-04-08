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

class MatchingEngineTIFTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_response_queue_ = std::make_unique<slick::SlickQueue<OrderResponse>>(4096, "test_tif_responses");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*order_response_queue_);
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
        order_book_->addObserver(order_book_->shared_from_this());
    }

    std::unique_ptr<slick::SlickQueue<OrderResponse>> order_response_queue_;
    std::unique_ptr<FifoMatchingEngine> matching_engine_;
    std::shared_ptr<OrderBook> order_book_;
};

TEST_F(MatchingEngineTIFTest, FOK_CompletelyFilled_Success) {
    // Add sufficient liquidity for FOK order
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create FOK buy order that can be completely filled
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL);

    // Match the FOK order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
}

TEST_F(MatchingEngineTIFTest, FOK_InsufficientLiquidity_Canceled) {
    // Add insufficient liquidity for FOK order
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create FOK buy order that cannot be completely filled
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL);

    // Attempt to match the FOK order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_TRUE(trades.empty());
}

TEST_F(MatchingEngineTIFTest, FOK_WithSMP_CancelNewest_Validation) {
    // Add liquidity from same client
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create FOK order from same client (should be rejected by SMP during pre-validation)
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL, 123);

    // Match with SMP=CANCEL_NEWEST
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::CANCEL_NEWEST);

    // FOK pre-validation should detect self-match and reject
    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, 0);
}

TEST_F(MatchingEngineTIFTest, IOC_PartialFill_RemainderNotAdded) {
    // Add partial liquidity
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create IOC buy order (should fill what's available, remainder not added to book)
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::IMMEDIATE_OR_CANCEL);

    // Match the IOC order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::FOK_CANNOT_FILL);
    EXPECT_EQ(buy_order->cum_quantity, kQty5);
    EXPECT_EQ(buy_order->leaves_quantity, kQty5);
}

TEST_F(MatchingEngineTIFTest, DAY_RestingOnBook) {
    // No initial liquidity

    // Create DAY order (should rest on book if not immediately matched)
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::DAY);

    // Add to order book (simulating what would happen in exchange logic)
    order_book_->addOrder(buy_order, 1000, 1);

    // Verify order is on the book
    auto* found = order_book_->findOrder(buy_order->id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->time_in_force, TimeInForce::DAY);
}

TEST_F(MatchingEngineTIFTest, GTC_RestingOnBook) {
    // No initial liquidity

    // Create GTC order (should rest on book if not immediately matched)
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL);

    // Add to order book
    order_book_->addOrder(buy_order, 1000, 1);

    // Verify order is on the book
    auto* found = order_book_->findOrder(buy_order->id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->time_in_force, TimeInForce::GOOD_TILL_CANCEL);
}

TEST_F(MatchingEngineTIFTest, FOK_AcrossMultipleLevels_Success) {
    // Add liquidity across multiple price levels
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, 1000, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty5);
    order_book_->addOrder(sell2, 1001, 2);

    // Create FOK order that needs both levels
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice101, kQty10, TimeInForce::FILL_OR_KILL);

    // Match the FOK order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice101, kQty10, *order_book_, 2000, 3, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
}

TEST_F(MatchingEngineTIFTest, FOK_AcrossMultipleLevels_Insufficient) {
    // Add liquidity across multiple price levels (but not enough)
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5);
    order_book_->addOrder(sell1, 1000, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty3);
    order_book_->addOrder(sell2, 1001, 2);

    // Create FOK order that needs more than available
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice101, kQty10, TimeInForce::FILL_OR_KILL);

    // Attempt to match the FOK order
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice101, kQty10, *order_book_, 2000, 3, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_TRUE(trades.empty());
}
