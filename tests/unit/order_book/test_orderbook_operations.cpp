#include <gtest/gtest.h>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <slick/object_pool.h>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;

class OrderBookOperationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
        order_book_->addObserver(order_book_->shared_from_this());
    }

    std::shared_ptr<OrderBook> order_book_;
};

TEST_F(OrderBookOperationsTest, AddOrder_NewBidLevel) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);

    order_book_->addOrder(order, 1000, 1);

    auto* found = order_book_->findOrder(order->id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, order->id);
}

TEST_F(OrderBookOperationsTest, AddOrder_ExistingBidLevel) {
    // Add first order at price 100
    auto* order1 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order1, 1000, 1);

    // Add second order at same price
    auto* order2 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order2, 1001, 2);

    // Both orders should be found
    ASSERT_NE(order_book_->findOrder(order1->id), nullptr);
    ASSERT_NE(order_book_->findOrder(order2->id), nullptr);
}

TEST_F(OrderBookOperationsTest, DeleteOrder_RemovesFromBook) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    ASSERT_NE(order_book_->findOrder(order->id), nullptr);

    order_book_->deleteOrder(order, 2000, 2);

    // Order should no longer be found
    EXPECT_EQ(order_book_->findOrder(order->id), nullptr);
}

TEST_F(OrderBookOperationsTest, ModifyOrder_PriceChange_LosesPriority) {
    // Add two orders at same price
    auto* order1 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order1, 1000, 1);

    auto* order2 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order2, 1001, 2);

    uint64_t original_priority1 = order1->priority;

    // Modify order1's price (should lose priority)
    order_book_->modifyOrder(order1, kPrice101, kQty10, 2000, 3);

    EXPECT_GT(order1->priority, original_priority1);
}

TEST_F(OrderBookOperationsTest, ModifyOrder_QtyChange_KeepsPriority) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    uint64_t original_priority = order->priority;

    // Modify order's quantity only (should keep priority)
    order_book_->modifyOrder(order, kPrice100, kQty5, 2000, 2);

    EXPECT_EQ(order->priority, original_priority);
}

TEST_F(OrderBookOperationsTest, ExecuteOrder_FullFill) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    // Execute full quantity
    order_book_->executeOrder(order->id, kQty10, 2000, 2, true);

    // Order should be removed after full execution
    auto* found = order_book_->findOrder(order->id);
    EXPECT_EQ(found, nullptr);
}

TEST_F(OrderBookOperationsTest, ExecuteOrder_PartialFill) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    // Execute partial quantity
    order_book_->executeOrder(order->id, kQty5, 2000, 2, false);

    // Order should still exist with reduced quantity
    auto* found = order_book_->findOrder(order->id);
    ASSERT_NE(found, nullptr);
}

TEST_F(OrderBookOperationsTest, PriorityAssignment_Monotonic) {
    auto* order1 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order1, 1000, 1);

    auto* order2 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order2, 1001, 2);

    auto* order3 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order3, 1002, 3);

    // Priorities should be increasing
    EXPECT_LT(order1->priority, order2->priority);
    EXPECT_LT(order2->priority, order3->priority);
}
