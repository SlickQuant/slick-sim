#include <gtest/gtest.h>
#include <common/order.hpp>
#include <common/types.hpp>
#include <slick/object_pool.h>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;

class OrderTest : public ::testing::Test {
protected:
    slick::ObjectPool<Order> order_pool_{1024};
};

TEST_F(OrderTest, InitialState) {
    Order order;

    EXPECT_EQ(order.side, Side::BUY);
    EXPECT_EQ(order.type, OrderType::MARKET);
    EXPECT_EQ(order.status, OrderStatus::NEW);
    EXPECT_EQ(order.time_in_force, TimeInForce::DAY);
    EXPECT_EQ(order.product_type, ProductType::UNKNOWN);
    EXPECT_EQ(order.price, 0);
    EXPECT_EQ(order.quantity, 0);
    EXPECT_EQ(order.leaves_quantity, 0);
    EXPECT_EQ(order.filled_quantity, 0);
    EXPECT_EQ(order.avg_filled_price, 0);
    EXPECT_EQ(order.last_filled_price, 0);
    EXPECT_EQ(order.last_filled_qty, 0);
    EXPECT_EQ(order.num_fills, 0);
    EXPECT_EQ(order.client_id, -1);
    EXPECT_FALSE(order.post_only);
    EXPECT_FALSE(order.pending_cancel);
}

TEST_F(OrderTest, FillTracking_SingleFill) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    // Simulate a single fill
    qty_t fill_qty = kQty10;
    order->filled_quantity = fill_qty;
    order->leaves_quantity = order->quantity - fill_qty;
    order->last_filled_price = kPrice100;
    order->last_filled_qty = fill_qty;

    EXPECT_EQ(order->filled_quantity, kQty10);
    EXPECT_EQ(order->leaves_quantity, 0);
    EXPECT_EQ(order->last_filled_price, kPrice100);
    EXPECT_EQ(order->last_filled_qty, kQty10);
}

TEST_F(OrderTest, FillTracking_MultipleFills) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    // First fill
    order->filled_quantity += kQty5;
    order->leaves_quantity = order->quantity - order->filled_quantity;
    order->last_filled_price = kPrice100;
    order->last_filled_qty = kQty5;

    EXPECT_EQ(order->filled_quantity, kQty5);
    EXPECT_EQ(order->leaves_quantity, kQty5);

    // Second fill
    order->filled_quantity += kQty5;
    order->leaves_quantity = order->quantity - order->filled_quantity;
    order->last_filled_price = kPrice100;
    order->last_filled_qty = kQty5;

    EXPECT_EQ(order->filled_quantity, kQty10);
    EXPECT_EQ(order->leaves_quantity, 0);
}

TEST_F(OrderTest, LeavesQuantityCalculation) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    EXPECT_EQ(order->leaves_quantity, kQty10);

    // Partial fill
    order->filled_quantity = kQty5;
    order->leaves_quantity = order->quantity - order->filled_quantity;

    EXPECT_EQ(order->leaves_quantity, kQty5);
}

TEST_F(OrderTest, PriceConversions) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    double price_double = order->price_double();
    EXPECT_NEAR(price_double, 100.0, 0.01);
}

TEST_F(OrderTest, QuantityConversions) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);

    double qty_double = order->qty_double();
    EXPECT_NEAR(qty_double, 10.0, 0.01);
}

TEST_F(OrderTest, FilledQtyDouble) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order->filled_quantity = kQty5;

    double filled_qty_double = order->filled_qty_double();
    EXPECT_NEAR(filled_qty_double, 5.0, 0.01);
}

TEST_F(OrderTest, LeavesQtyDouble) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order->filled_quantity = kQty5;
    order->leaves_quantity = kQty5;

    double leaves_qty_double = order->leaves_qty_double();
    EXPECT_NEAR(leaves_qty_double, 5.0, 0.01);
}

TEST_F(OrderTest, LastFilledPriceDouble) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order->last_filled_price = kPrice100;

    double last_filled_price_double = order->last_filled_price_double();
    EXPECT_NEAR(last_filled_price_double, 100.0, 0.01);
}

TEST_F(OrderTest, AvgFilledPriceDouble) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    order->avg_filled_price = kPrice100;

    double avg_filled_price_double = order->avg_filled_price_double();
    EXPECT_NEAR(avg_filled_price_double, 100.0, 0.01);
}

TEST_F(OrderTest, SideEnum) {
    auto* buy_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    EXPECT_EQ(buy_order->side, Side::BUY);

    auto* sell_order = createTestOrder(order_pool_, Side::SELL, kPrice100, kQty10);
    EXPECT_EQ(sell_order->side, Side::SELL);
}

TEST_F(OrderTest, OrderTypeEnum) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10);
    EXPECT_EQ(order->type, OrderType::LIMIT);
}

TEST_F(OrderTest, TimeInForceEnum) {
    auto* gtc_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL);
    EXPECT_EQ(gtc_order->time_in_force, TimeInForce::GOOD_TILL_CANCEL);

    auto* ioc_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10, TimeInForce::IMMEDIATE_OR_CANCEL);
    EXPECT_EQ(ioc_order->time_in_force, TimeInForce::IMMEDIATE_OR_CANCEL);

    auto* fok_order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL);
    EXPECT_EQ(fok_order->time_in_force, TimeInForce::FILL_OR_KILL);
}

TEST_F(OrderTest, ClientIdTracking) {
    auto* order = createTestOrder(order_pool_, Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 123);
    EXPECT_EQ(order->client_id, 123);
}

TEST_F(OrderTest, OppositeSideFunction) {
    EXPECT_EQ(opposite_side(Side::BUY), Side::SELL);
    EXPECT_EQ(opposite_side(Side::SELL), Side::BUY);
    EXPECT_EQ(opposite_side(Side::UNKNOWN_SIDE), Side::UNKNOWN_SIDE);
}

TEST_F(OrderTest, SideToString) {
    EXPECT_STREQ(to_string(Side::BUY).data(), "BUY");
    EXPECT_STREQ(to_string(Side::SELL).data(), "SELL");
    EXPECT_STREQ(to_string(Side::UNKNOWN_SIDE).data(), "UNKNOWN");
}

TEST_F(OrderTest, OrderTypeToString) {
    // EXPECT_STREQ(std::string(to_string(OrderType::MARKET)), "MARKET");
    // EXPECT_STREQ(std::string(to_string(OrderType::LIMIT)), "LIMIT");
    // EXPECT_STREQ(std::string(to_string(OrderType::STOP)), "STOP");
    // EXPECT_STREQ(std::string(to_string(OrderType::STOP_LIMIT)), "STOP_LIMIT");
}
