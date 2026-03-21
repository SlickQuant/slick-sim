#include <gtest/gtest.h>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <slick/object_pool.h>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;

class OrderBookLookupTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_book_ = std::make_unique<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
    }

    std::unique_ptr<OrderBook> order_book_;
    slick::ObjectPool<Order> order_pool_{1024};
};

TEST_F(OrderBookLookupTest, FindOrderById) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    auto* found = order_book_->findOrder(order->id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, order->id);
}

TEST_F(OrderBookLookupTest, FindOrderByClientOrderId) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    auto* found = order_book_->findOrderByClientOrderId(order->client_order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->client_order_id, order->client_order_id);
}

TEST_F(OrderBookLookupTest, FindOrderByOrderId) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    auto* found = order_book_->findOrderByOrderId(order->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->order_id, order->order_id);
}

TEST_F(OrderBookLookupTest, FindOrder_NonExistent_ReturnsNull) {
    auto* found_by_id = order_book_->findOrder(999999);
    EXPECT_EQ(found_by_id, nullptr);

    auto* found_by_client_order_id = order_book_->findOrderByClientOrderId("NONEXISTENT");
    EXPECT_EQ(found_by_client_order_id, nullptr);

    auto* found_by_order_id = order_book_->findOrderByOrderId("NONEXISTENT");
    EXPECT_EQ(found_by_order_id, nullptr);
}

TEST_F(OrderBookLookupTest, MultipleLookupMethods_SameOrder) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    auto* found_by_id = order_book_->findOrder(order->id);
    auto* found_by_client_order_id = order_book_->findOrderByClientOrderId(order->client_order_id);
    auto* found_by_order_id = order_book_->findOrderByOrderId(order->order_id);

    ASSERT_NE(found_by_id, nullptr);
    ASSERT_NE(found_by_client_order_id, nullptr);
    ASSERT_NE(found_by_order_id, nullptr);

    // All three methods should return the same order
    EXPECT_EQ(found_by_id, found_by_client_order_id);
    EXPECT_EQ(found_by_id, found_by_order_id);
}

TEST_F(OrderBookLookupTest, MultipleOrders_CorrectLookup) {
    auto* order1 = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order1, 1000, 1);

    auto* order2 = createTestOrder(order_pool_, Side::BUY, kPrice101, kQty10);
    order_book_->addOrder(order2, 1001, 2);

    auto* found1 = order_book_->findOrder(order1->id);
    auto* found2 = order_book_->findOrder(order2->id);

    ASSERT_NE(found1, nullptr);
    ASSERT_NE(found2, nullptr);
    EXPECT_EQ(found1->id, order1->id);
    EXPECT_EQ(found2->id, order2->id);
}
