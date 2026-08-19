#include <gtest/gtest.h>
#include <order_gateway/order_store.hpp>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <memory>
#include <string>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::order_gateway;

namespace {

OrderResponse makeResponse(const std::string &order_id,
                           const std::string &user_id,
                           const std::string &symbol,
                           ExecType exec_type,
                           OrderStatus status) {
    OrderResponse r{};
    utils::copy_wire_field(r.order_id, order_id);
    utils::copy_wire_field(r.user_id, user_id);
    utils::copy_wire_field(r.symbol, symbol);
    utils::copy_wire_field(r.client_order_id, "coid-" + order_id);
    r.response_type = MessageType::EXECUTION_REPORT;
    r.exec_type = exec_type;
    r.order_status = status;
    r.order_type = OrderType::LIMIT;
    r.time_in_force = TimeInForce::GOOD_TILL_CANCEL;
    r.side = Side::BUY;
    r.price = kPrice100;
    r.qty = kQty10;
    r.leaves_qty = kQty10;
    r.cum_qty = 0;
    return r;
}

// The queue is only needed to construct the store; most of these drive apply()
// directly, which is the unit under test.
class OrderStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue_ = std::make_unique<slick::queue<OrderResponse>>(1024, "test_order_store");
        store_ = std::make_unique<OrderStore>(*queue_);
    }
    std::unique_ptr<slick::queue<OrderResponse>> queue_;
    std::unique_ptr<OrderStore> store_;
};

}  // namespace

TEST_F(OrderStoreTest, FirstReportCreatesTheOrderWithItsIdentity) {
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::PENDING_NEW));

    ASSERT_EQ(store_->size(), 1u);
    const Order *order = store_->find("ord-1");
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->user_id.view(), "alice");
    EXPECT_EQ(order->symbol.view(), "BTC-USD");
    EXPECT_EQ(order->client_order_id.view(), "coid-ord-1");
    EXPECT_EQ(order->status, OrderStatus::PENDING_NEW);
}

// Later reports update the same order rather than accumulating duplicates. That
// collapsing is the whole point of the projection.
TEST_F(OrderStoreTest, SubsequentReportsUpdateInPlace) {
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::PENDING_NEW));
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::NEW));

    EXPECT_EQ(store_->size(), 1u) << "an update created a second order";
    EXPECT_EQ(store_->find("ord-1")->status, OrderStatus::NEW);
}

TEST_F(OrderStoreTest, FillsAccumulateCountAndLastFillDetails) {
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::PENDING_NEW));

    auto partial = makeResponse("ord-1", "alice", "BTC-USD",
                                ExecType::TRADE, OrderStatus::PARTIALLY_FILLED);
    partial.last_fill_price = kPrice100;
    partial.last_qty = kQty3;
    partial.cum_qty = kQty3;
    partial.leaves_qty = kQty10 - kQty3;
    partial.avg_fill_price = kPrice100;
    partial.timestamp = 5000;
    store_->apply(partial);

    auto rest = partial;
    rest.order_status = OrderStatus::FILLED;
    rest.last_qty = kQty10 - kQty3;
    rest.cum_qty = kQty10;
    rest.leaves_qty = 0;
    rest.timestamp = 6000;
    store_->apply(rest);

    const Order *order = store_->find("ord-1");
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->num_fills, 2u) << "the fill count is derived from the stream";
    EXPECT_EQ(order->cum_quantity, kQty10);
    EXPECT_EQ(order->leaves_quantity, 0);
    EXPECT_EQ(order->status, OrderStatus::FILLED);
    ASSERT_TRUE(order->last_fill_time.has_value());
    EXPECT_EQ(order->last_fill_time.value(), 6000u);
}

// Acks and cancels are not fills and must not inflate the count.
TEST_F(OrderStoreTest, NonTradeReportsDoNotCountAsFills) {
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::PENDING_NEW));
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::NEW));
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::CANCELED, OrderStatus::CANCELED));

    EXPECT_EQ(store_->find("ord-1")->num_fills, 0u);
    EXPECT_EQ(store_->find("ord-1")->status, OrderStatus::CANCELED);
}

// The listing is per user: one client must never be shown another client's orders.
TEST_F(OrderStoreTest, OrdersAreScopedToTheirOwner) {
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD",
                               ExecType::NEW, OrderStatus::NEW));
    store_->apply(makeResponse("ord-2", "bob", "BTC-USD",
                               ExecType::NEW, OrderStatus::NEW));
    store_->apply(makeResponse("ord-3", "alice", "ETH-USD",
                               ExecType::NEW, OrderStatus::NEW));

    auto alice = store_->orders_of("alice");
    ASSERT_EQ(alice.size(), 2u);
    for (const Order *o : alice) {
        EXPECT_EQ(o->user_id.view(), "alice");
    }

    EXPECT_EQ(store_->orders_of("bob").size(), 1u);
    EXPECT_TRUE(store_->orders_of("nobody").empty());
}

TEST_F(OrderStoreTest, ListingIsNewestFirst) {
    store_->apply(makeResponse("ord-1", "alice", "BTC-USD", ExecType::NEW, OrderStatus::NEW));
    store_->apply(makeResponse("ord-2", "alice", "BTC-USD", ExecType::NEW, OrderStatus::NEW));
    store_->apply(makeResponse("ord-3", "alice", "BTC-USD", ExecType::NEW, OrderStatus::NEW));

    auto orders = store_->orders_of("alice");
    ASSERT_EQ(orders.size(), 3u);
    EXPECT_EQ(orders[0]->order_id.view(), "ord-3");
    EXPECT_EQ(orders[1]->order_id.view(), "ord-2");
    EXPECT_EQ(orders[2]->order_id.view(), "ord-1");
}

// A response carrying no order id describes nothing that can be listed.
TEST_F(OrderStoreTest, ResponsesWithoutAnOrderIdAreIgnored) {
    store_->apply(makeResponse("", "alice", "BTC-USD", ExecType::NEW, OrderStatus::NEW));
    EXPECT_EQ(store_->size(), 0u);
}

TEST_F(OrderStoreTest, RejectAndCancelMessagesAreRetained) {
    auto rejected = makeResponse("ord-1", "alice", "BTC-USD",
                                 ExecType::REJECTED, OrderStatus::REJECTED);
    utils::copy_wire_field(rejected.error_message, "insufficient funds");
    store_->apply(rejected);
    EXPECT_EQ(store_->find("ord-1")->reject_message, "insufficient funds");

    auto canceled = makeResponse("ord-2", "alice", "BTC-USD",
                                 ExecType::CANCELED, OrderStatus::CANCELED);
    utils::copy_wire_field(canceled.error_message, "user requested");
    store_->apply(canceled);
    EXPECT_EQ(store_->find("ord-2")->cancel_message, "user requested");
}

// drain() is how the gateway actually feeds the store, and its cursor must advance
// so a second drain does not replay what it already consumed.
TEST_F(OrderStoreTest, DrainConsumesFromTheResponseQueueExactlyOnce) {
    for (int i = 1; i <= 3; ++i) {
        auto index = queue_->reserve();
        *(*queue_)[index] = makeResponse("q-ord-" + std::to_string(i), "carol",
                                         "BTC-USD", ExecType::NEW, OrderStatus::NEW);
        queue_->publish(index);
    }

    store_->drain();
    EXPECT_EQ(store_->orders_of("carol").size(), 3u);

    store_->drain();
    EXPECT_EQ(store_->orders_of("carol").size(), 3u);
    EXPECT_EQ(store_->find("q-ord-1")->num_fills, 0u);
}

// The WS gateway already drains the response stream for its own dispatch, so its
// store is fed through apply() and holds no cursor. drain() must be harmless there
// rather than silently taking a second pass over the queue.
TEST(OrderStoreProjectionOnlyTest, DrainIsANoOpWithoutAQueue) {
    OrderStore store;

    store.apply(makeResponse("ord-1", "alice", "BTC-USD",
                             ExecType::NEW, OrderStatus::NEW));
    ASSERT_EQ(store.size(), 1u);

    store.drain();

    EXPECT_EQ(store.size(), 1u);
    EXPECT_EQ(store.orders_of("alice").size(), 1u);
}
