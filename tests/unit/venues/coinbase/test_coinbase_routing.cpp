#include <gtest/gtest.h>
#include <venues/coinbase/coinbase_common.hpp>
#include <common/order.hpp>

using namespace slick::sim;

// Check how each report type affects order-id -> symbol routing. Treating REJECTED as terminal in
// the drain loop removed routing for orders with rejected amendments, leaving them resting but
// returning UNKNOWN_ORDER on cancel.
namespace {

using slick::sim::order_gateway::coinbase::RoutingEffect;
using slick::sim::order_gateway::coinbase::routing_effect;

TEST(CoinbaseRoutingTest, AWorkingOrderLearnsItsSymbol) {
    EXPECT_EQ(routing_effect(OrderStatus::PENDING_NEW), RoutingEffect::Learn);
    EXPECT_EQ(routing_effect(OrderStatus::NEW), RoutingEffect::Learn);
    EXPECT_EQ(routing_effect(OrderStatus::PARTIALLY_FILLED), RoutingEffect::Learn);
    // Both are transient states of an order that is still on the book.
    EXPECT_EQ(routing_effect(OrderStatus::PENDING_CANCEL), RoutingEffect::Learn);
    EXPECT_EQ(routing_effect(OrderStatus::PENDING_REPLACE), RoutingEffect::Learn);
    EXPECT_EQ(routing_effect(OrderStatus::REPLACED), RoutingEffect::Learn);
    EXPECT_EQ(routing_effect(OrderStatus::STOPPED), RoutingEffect::Learn);
}

TEST(CoinbaseRoutingTest, OnlyTerminalReportsForgetTheSymbol) {
    EXPECT_EQ(routing_effect(OrderStatus::FILLED), RoutingEffect::Forget);
    EXPECT_EQ(routing_effect(OrderStatus::CANCELED), RoutingEffect::Forget);
    EXPECT_EQ(routing_effect(OrderStatus::EXPIRED), RoutingEffect::Forget);
    EXPECT_EQ(routing_effect(OrderStatus::DONE_FOR_DAY), RoutingEffect::Forget);
}

// A rejected amendment or cancel names a working order, so preserve its routing. A rejected new
// order has no order id and no routing to remove.
TEST(CoinbaseRoutingTest, ARejectNeitherLearnsNorForgets) {
    EXPECT_EQ(routing_effect(OrderStatus::REJECTED), RoutingEffect::Ignore);
}

}  // namespace
