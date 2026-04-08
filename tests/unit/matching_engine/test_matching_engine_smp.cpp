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

class MatchingEngineSMPTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_response_queue_ = std::make_unique<slick::SlickQueue<OrderResponse>>(4096, "test_smp_responses");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*order_response_queue_);
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
        order_book_->addObserver(order_book_->shared_from_this());
    }

    std::unique_ptr<slick::SlickQueue<OrderResponse>> order_response_queue_;
    std::unique_ptr<FifoMatchingEngine> matching_engine_;
    std::shared_ptr<OrderBook> order_book_;
};

TEST_F(MatchingEngineSMPTest, SMP_None_AllowsSelfMatch) {
    // Add resting sell order from client 123
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order from same client
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);

    // Match with SMP=NONE (allows self-matching)
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::NONE);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_FALSE(trades.empty());
}

TEST_F(MatchingEngineSMPTest, SMP_CancelResting_RemovesBookOrder) {
    // Add resting sell order from client 123
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order from same client
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);

    // Match with SMP=CANCEL_RESTING (cancels resting order)
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    // Buy order should not be filled since resting order was canceled
    EXPECT_EQ(buy_order->cum_quantity, 0);
}

TEST_F(MatchingEngineSMPTest, SMP_CancelNewest_RejectsIncomingOrder) {
    // Add resting sell order from client 123
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order from same client
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);

    // Match with SMP=CANCEL_NEWEST (rejects incoming order)
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::CANCEL_NEWEST);

    EXPECT_EQ(reject_reason, OrdRejectReason::SMP);
    EXPECT_EQ(buy_order->cum_quantity, 0);
    EXPECT_TRUE(trades.empty());
}

TEST_F(MatchingEngineSMPTest, SMP_IgnoresWhenClientIdNegativeOne) {
    // Add resting sell order with client_id = -1
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, -1);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order also with client_id = -1
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, -1);

    // Match with SMP=CANCEL_NEWEST (should not trigger SMP since client_id = -1)
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::CANCEL_NEWEST);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
}

TEST_F(MatchingEngineSMPTest, SMP_CancelResting_ContinuesMatching) {
    // Add two resting sell orders: one from same client, one from different client
    auto* sell1 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell1, 1000, 1);

    auto* sell2 = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 456);
    order_book_->addOrder(sell2, 1001, 2);

    // Create buy order from client 123 with enough quantity to match both
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty20, TimeInForce::GOOD_TILL_CANCEL, 123);

    // Match with SMP=CANCEL_RESTING
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty20, *order_book_, 2000, 3, SelfMatchPreventionMode::CANCEL_RESTING);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    // Should have matched with the second order (from client 456)
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
}

TEST_F(MatchingEngineSMPTest, SMP_DifferentClients_NoSelfMatch) {
    // Add resting sell order from client 123
    auto* sell_order = createTestOrder(order_book_, Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    order_book_->addOrder(sell_order, 1000, 1);

    // Create buy order from different client (456)
    auto* buy_order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 456);

    // Match with any SMP mode - should not trigger SMP since clients are different
    auto [reject_reason, trades] = matching_engine_->match(
        buy_order, kPrice100, kQty10, *order_book_, 2000, 2, SelfMatchPreventionMode::CANCEL_NEWEST);

    EXPECT_EQ(reject_reason, OrdRejectReason::NONE);
    EXPECT_EQ(buy_order->cum_quantity, kQty10);
    EXPECT_FALSE(trades.empty());
}
