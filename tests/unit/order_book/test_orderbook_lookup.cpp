#include <gtest/gtest.h>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <slick/object_pool.h>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;

class OrderBookLookupTest : public ::testing::Test {
protected:
    void SetUp() override {
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "BTC-USD", Venue::COINBASE);
        order_book_->addObserver(order_book_->shared_from_this());
    }

    std::shared_ptr<OrderBook> order_book_;
};

TEST_F(OrderBookLookupTest, FindOrderById) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    auto* found = order_book_->findOrder(order->id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, order->id);
}

TEST_F(OrderBookLookupTest, FindOrderByClientOrderId) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    auto* found = order_book_->findOrderByClientOrderId(order->client_order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->client_order_id, order->client_order_id);
}

TEST_F(OrderBookLookupTest, FindOrderByOrderId) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
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
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
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
    auto* order1 = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order1, 1000, 1);

    auto* order2 = createTestOrder(order_book_, Side::BUY, kPrice101, kQty10);
    order_book_->addOrder(order2, 1001, 2);

    auto* found1 = order_book_->findOrder(order1->id);
    auto* found2 = order_book_->findOrder(order2->id);

    ASSERT_NE(found1, nullptr);
    ASSERT_NE(found2, nullptr);
    EXPECT_EQ(found1->id, order1->id);
    EXPECT_EQ(found2->id, order2->id);
}

// ---------------------------------------------------------------------------
// The id lookup maps are keyed by the order's own fixed_string type rather than
// std::string, so an insert no longer allocates a copy of the key. The transparent
// functors still have to make a string_view lookup find a fixed_string key, which
// is what the existing tests above exercise; these pin the parts that would break
// silently if the hash and the key ever disagreed about where a value ends.
// ---------------------------------------------------------------------------

TEST_F(OrderBookLookupTest, LookupMatchesOnContentNotOnBufferWidth) {
    auto* order = createTestOrder(order_book_, Side::BUY, kPrice100, kQty10);
    order_book_->addOrder(order, 1000, 1);

    // The key buffer is 37 bytes wide whatever the id's length; a lookup built
    // from a plain string_view of the same characters must still land on it.
    const std::string id(order->client_order_id.c_str());
    ASSERT_LT(id.size(), decltype(Order::client_order_id)::capacity());

    EXPECT_EQ(order_book_->findOrderByClientOrderId(std::string_view(id)), order);
    EXPECT_EQ(order_book_->findOrderByOrderId(std::string_view(order->order_id.c_str())), order);

    // A prefix must not match: the key compares over its length, not its buffer.
    EXPECT_EQ(order_book_->findOrderByClientOrderId(std::string_view(id).substr(0, id.size() - 1)), nullptr);
}

TEST_F(OrderBookLookupTest, AllocateOrderGeneratesDistinctRenderedUuids) {
    auto* first = order_book_->allocateOrder();
    auto* second = order_book_->allocateOrder();

    // allocateOrder renders the UUID straight into the order's own buffer, so a
    // capacity mismatch or a missing terminator shows up as a short or unterminated
    // id rather than as a build failure.
    EXPECT_EQ(first->order_id.size(), 36u);
    EXPECT_EQ(second->order_id.size(), 36u);
    EXPECT_EQ(std::strlen(first->order_id.c_str()), 36u);
    EXPECT_NE(first->order_id, second->order_id);

    // 8-4-4-4-12 grouping.
    const std::string_view id = first->order_id.view();
    EXPECT_EQ(id[8], '-');
    EXPECT_EQ(id[13], '-');
    EXPECT_EQ(id[18], '-');
    EXPECT_EQ(id[23], '-');
}

TEST_F(OrderBookLookupTest, SymbolNameIsWireWidth) {
    // The publish path copies sizeof(update->symbol) bytes out of this, so the
    // buffer has to be at least that wide.
    EXPECT_EQ(std::remove_cvref_t<decltype(order_book_->symbolName())>::buffer_size(), 32u);
    EXPECT_EQ(order_book_->symbolName().view(), "BTC-USD");
}
