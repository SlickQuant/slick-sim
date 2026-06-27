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
        response_queue_ = std::make_unique<slick::queue<OrderResponse>>(4096, "test_tif_responses");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*response_queue_);
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
        order_book_->addObserver(order_book_->shared_from_this());
    }

    std::unique_ptr<slick::queue<OrderResponse>> response_queue_;
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
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

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
        buy_order, kPrice100, kQty10, *order_book_, 2000,2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::FOK_CANNOT_FILL);
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
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::CANCEL_NEWEST);

    // FOK pre-validation should detect self-match and reject
    EXPECT_EQ(reject_reason, OrdRejectReason::FOK_CANNOT_FILL);
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
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
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
        buy_order, kPrice101, kQty10, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::NONE);

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
        buy_order, kPrice101, kQty10, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::FOK_CANNOT_FILL);
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_TRUE(trades.empty());
}

TEST_F(MatchingEngineTIFTest, GTC_WithSMP_CancelResting_RestingCanceledNoFill) {
    // Add resting sell order from client 123 (the only liquidity)
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell_order, 1000, 1);
    auto sell_id = sell_order->id;

    // Create GTC buy order from same client
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);

    // Match with SMP=CANCEL_RESTING
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 2, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    // No fill: the only liquidity was the self order, which got canceled
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_TRUE(trades.empty());
    // Resting order is removed from the book
    EXPECT_EQ(order_book_->findOrder(sell_id), nullptr);
    EXPECT_EQ(sell_order->leaves_quantity, 0);
}

TEST_F(MatchingEngineTIFTest, FOK_WithSMP_CancelResting_FillsFromOtherLiquidity) {
    // Add resting sell from same client (first in priority) and one from another client
    auto* self_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(self_sell, 1000, 1);
    auto self_sell_id = self_sell->id;

    auto* other_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 456);
    order_book_->addOrder(other_sell, 1001, 2);

    // FOK buy from client 123: pre-check skips the self order but finds enough from client 456
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL, 123);

    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
    // Self resting order was canceled during the matching loop
    EXPECT_EQ(order_book_->findOrder(self_sell_id), nullptr);
    EXPECT_EQ(self_sell->leaves_quantity, 0);
}

TEST_F(MatchingEngineTIFTest, FOK_WithSMP_CancelResting_InsufficientLiquidity) {
    // Self order has enough qty, but it doesn't count; other client only has kQty5
    auto* self_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(self_sell, 1000, 1);
    auto self_sell_id = self_sell->id;

    auto* other_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5, TimeInForce::GOOD_TILL_CANCEL, 456);
    order_book_->addOrder(other_sell, 1001, 2);

    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL, 123);

    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::FOK_CANNOT_FILL);
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_TRUE(trades.empty());
    // FOK pre-validation only skips the self order; it must not cancel it
    auto* found = order_book_->findOrder(self_sell_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->leaves_quantity, kQty10);
}

TEST_F(MatchingEngineTIFTest, IOC_WithSMP_CancelResting_PartialFill) {
    // Self order first in priority, partial liquidity from another client behind it
    auto* self_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(self_sell, 1000, 1);
    auto self_sell_id = self_sell->id;

    auto* other_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5, TimeInForce::GOOD_TILL_CANCEL, 456);
    order_book_->addOrder(other_sell, 1001, 2);

    // IOC buy: self order canceled, fills kQty5 from client 456, remainder canceled
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::IMMEDIATE_OR_CANCEL, 123);

    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2000, 3, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty5);
    EXPECT_EQ(buy_order->leaves_quantity, kQty5);
    // Self resting order was canceled, not traded against
    EXPECT_EQ(order_book_->findOrder(self_sell_id), nullptr);
    EXPECT_EQ(self_sell->leaves_quantity, 0);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, kQty5);
}

TEST_F(MatchingEngineTIFTest, FOK_WithSMP_CancelResting_AcrossMultipleLevels) {
    // Self order at best level, other-client liquidity split across two levels
    auto* self_sell = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(self_sell, 1000, 1);
    auto self_sell_id = self_sell->id;

    auto* other_sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty5, TimeInForce::GOOD_TILL_CANCEL, 456);
    order_book_->addOrder(other_sell1, 1001, 2);

    auto* other_sell2 = createTestOrder(order_book_, Side::SELL, kPrice101, kQty5, TimeInForce::GOOD_TILL_CANCEL, 456);
    order_book_->addOrder(other_sell2, 1002, 3);

    // FOK buy needs both other-client levels, skipping the self order at the best level
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice101, kQty10, TimeInForce::FILL_OR_KILL, 123);

    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice101, kQty10, *order_book_, 2000, 2000, 4, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_EQ(buy_order->leaves_quantity, 0);
    // Self resting order was canceled during matching
    EXPECT_EQ(order_book_->findOrder(self_sell_id), nullptr);
    EXPECT_EQ(self_sell->leaves_quantity, 0);
    // One trade summary per price level
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, kPrice100);
    EXPECT_EQ(trades[0].qty, kQty5);
    EXPECT_EQ(trades[1].price, kPrice101);
    EXPECT_EQ(trades[1].qty, kQty5);
}
