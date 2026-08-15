#include <gtest/gtest.h>
#include <exchange/symbol.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <slick/queue.h>
#include <set>
#include <string>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::exch;
using namespace slick::sim::engine;

// ---------------------------------------------------------------------------
// The order book's pool is the only owner of an Order that is not resting, so
// Symbol::addOrder has to hand back anything that does not make it onto the
// book. Freeing only when leaves_quantity reached zero leaked one Order per
// order that ended with quantity outstanding — every reject and every IOC
// remainder — and slick::ObjectPool degrades silently, falling back to heap
// allocation once exhausted, so a leak shows up as unbounded growth rather
// than a failure.
//
// The pool is small here and the orders are allocated by the test, so reuse is
// directly observable: a pool that recycles hands the same few slots back.
// ---------------------------------------------------------------------------
class OrderPoolRecyclingTest : public ::testing::Test {
protected:
    static constexpr uint_fast32_t kPoolSize = 8;
    static constexpr int kIterations = 64;   // many times the pool capacity

    void SetUp() override {
        response_queue_ = std::make_unique<slick::queue<OrderResponse>>(65536, "pool_recycle_resp");
        matching_engine_ = std::make_unique<FifoMatchingEngine>(*response_queue_);

        symbol_.id_ = kSymbolId;
        symbol_.venue_ = Venue::COINBASE;
        symbol_.symbol_ = "POOL-TEST";
        symbol_.matching_engine_ = matching_engine_.get();
        symbol_.order_book_ =
            std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "POOL-TEST", Venue::COINBASE, kPoolSize);
        symbol_.order_book_->addObserver(symbol_.order_book_->shared_from_this());
        symbol_.order_book_->addObserver(symbol_.book_observer_);
    }

    Order *newOrder(Side side, price_t price, qty_t qty, TimeInForce tif, int client_id) {
        auto *order = symbol_.order_book_->allocateOrder();
        order->symbol = symbol_.symbol_;
        order->user_id = "user-" + std::to_string(client_id);
        order->client_order_id = "cl-" + std::to_string(order->id);
        order->side = side;
        order->type = OrderType::LIMIT;
        order->price = price;
        order->quantity = qty;
        order->leaves_quantity = qty;
        order->cum_quantity = 0;
        order->time_in_force = tif;
        order->client_id = client_id;
        order->status = OrderStatus::PENDING_NEW;
        return order;
    }

    std::unique_ptr<slick::queue<OrderResponse>> response_queue_;
    std::unique_ptr<FifoMatchingEngine> matching_engine_;
    Symbol symbol_;
};

TEST_F(OrderPoolRecyclingTest, SmpRejectedOrdersAreRecycled) {
    symbol_.smp_mode_ = SelfMatchPreventionMode::CANCEL_NEWEST;

    // One resting sell from client 1 for the incoming buys to self-match against.
    auto *resting = newOrder(Side::SELL, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 1);
    symbol_.addOrder(resting, 1000);

    std::set<const Order *> seen;
    for (int i = 0; i < kIterations; ++i) {
        auto *order = newOrder(Side::BUY, kPrice100, kQty10, TimeInForce::GOOD_TILL_CANCEL, 1);
        seen.insert(order);
        auto [reject_reason, trades] = symbol_.addOrder(order, 2000 + i);
        ASSERT_EQ(reject_reason, OrdRejectReason::SMP);
    }

    EXPECT_LE(seen.size(), kPoolSize)
        << "rejected orders were not returned to the pool: " << seen.size()
        << " distinct orders drawn from a pool of " << kPoolSize;
}

TEST_F(OrderPoolRecyclingTest, IocRemaindersAreRecycled) {
    // No liquidity at all, so every IOC is cancelled outright with its full
    // quantity outstanding and never rests.
    std::set<const Order *> seen;
    for (int i = 0; i < kIterations; ++i) {
        auto *order = newOrder(Side::BUY, kPrice100, kQty10, TimeInForce::IMMEDIATE_OR_CANCEL, 1);
        seen.insert(order);
        symbol_.addOrder(order, 2000 + i);
    }

    EXPECT_LE(seen.size(), kPoolSize)
        << "IOC remainders were not returned to the pool: " << seen.size()
        << " distinct orders drawn from a pool of " << kPoolSize;
}

TEST_F(OrderPoolRecyclingTest, FokUnfillableOrdersAreRecycled) {
    std::set<const Order *> seen;
    for (int i = 0; i < kIterations; ++i) {
        auto *order = newOrder(Side::BUY, kPrice100, kQty10, TimeInForce::FILL_OR_KILL, 1);
        seen.insert(order);
        auto [reject_reason, trades] = symbol_.addOrder(order, 2000 + i);
        ASSERT_EQ(reject_reason, OrdRejectReason::FOK_CANNOT_FILL);
    }

    EXPECT_LE(seen.size(), kPoolSize)
        << "unfillable FOK orders were not returned to the pool: " << seen.size()
        << " distinct orders drawn from a pool of " << kPoolSize;
}

// The complement: an order that does rest must NOT be recycled while it is live.
TEST_F(OrderPoolRecyclingTest, RestingOrdersAreRetained) {
    std::set<const Order *> seen;
    for (uint_fast32_t i = 0; i < kPoolSize / 2; ++i) {
        auto *order = newOrder(Side::BUY, kPrice98 - static_cast<price_t>(i), kQty5,
                               TimeInForce::GOOD_TILL_CANCEL, 1);
        seen.insert(order);
        symbol_.addOrder(order, 2000 + i);
    }

    // Each rested, so each had to come from a distinct slot.
    EXPECT_EQ(seen.size(), kPoolSize / 2);
    for (const auto *order : seen) {
        EXPECT_NE(symbol_.order_book_->findOrder(order->id), nullptr) << "a resting order went missing";
    }
}
