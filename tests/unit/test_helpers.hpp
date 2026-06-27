#pragma once

#include <gtest/gtest.h>
#include <common/order.hpp>
#include <common/types.hpp>
#include <common/messages.hpp>
#include <slick/queue.h>
#include <slick/object_pool.h>
#include <utils/order.hpp>
#include <order_book/order_book.hpp>

namespace slick::sim::test {

using price_t = slick::sim::price_t;
using qty_t = slick::sim::qty_t;
using symid_t = slick::sim::symid_t;
using OrderBook = slick::sim::OrderBook;

// Helper: Price conversion shortcut
inline constexpr price_t price(double p) { return to_price_t(p); }

// Helper: Quantity conversion shortcut
inline constexpr qty_t qty(double q) { return to_qty_t(q); }

// Helper: Create test order with defaults
inline Order* createTestOrder(
    slick::ObjectPool<Order>& pool,
    Side side,
    price_t price_val,
    qty_t qty_val,
    TimeInForce tif = TimeInForce::GOOD_TILL_CANCEL,
    int client_id = 1,
    const std::string& symbol = "TEST-SYMBOL"
) {
    auto* order = pool.allocate();
    order->id = utils::nextOrderId();
    order->order_id = "ORD-" + std::to_string(order->id);
    order->client_order_id = "CLIENT-" + std::to_string(order->id);
    order->user_id = "user1";
    order->symbol = symbol;
    order->side = side;
    order->type = OrderType::LIMIT;
    order->price = price_val;
    order->quantity = qty_val;
    order->leaves_quantity = qty_val;
    order->cum_quantity = 0;
    order->time_in_force = tif;
    order->client_id = client_id;
    order->status = OrderStatus::NEW;
    order->product_type = ProductType::SPOT;
    return order;
}

inline Order* createTestOrder(
    std::shared_ptr<OrderBook>& order_book,
    Side side,
    price_t price_val,
    qty_t qty_val,
    TimeInForce tif = TimeInForce::GOOD_TILL_CANCEL,
    int client_id = 1,
    const std::string& symbol = "TEST-SYMBOL"
) {
    auto* order = order_book->allocateOrder();
    order->id = utils::nextOrderId();
    order->order_id = "ORD-" + std::to_string(order->id);
    order->client_order_id = "CLIENT-" + std::to_string(order->id);
    order->user_id = "user1";
    order->symbol = symbol;
    order->side = side;
    order->type = OrderType::LIMIT;
    order->price = price_val;
    order->quantity = qty_val;
    order->leaves_quantity = qty_val;
    order->cum_quantity = 0;
    order->time_in_force = tif;
    order->client_id = client_id;
    order->status = OrderStatus::NEW;
    order->product_type = ProductType::SPOT;
    return order;
}

// Helper: Verify order response fields
inline void verifyOrderResponse(
    const OrderResponse& response,
    const Order* order,
    ExecType expected_exec_type,
    OrderStatus expected_status
) {
    EXPECT_STREQ(response.order_id, order->order_id.c_str());
    EXPECT_EQ(response.exec_type, expected_exec_type);
    EXPECT_EQ(response.order_status, expected_status);
}

// Mock: Simple OrderResponse collector
class OrderResponseCollector {
public:
    explicit OrderResponseCollector(slick::queue<OrderResponse>& queue)
        : queue_(queue), cursor_(0) {}

    std::vector<OrderResponse> collect() {
        std::vector<OrderResponse> responses;
        while (true) {
            auto [response, size] = queue_.read(cursor_);
            if (!response) break;
            responses.push_back(*response);
        }
        return responses;
    }

    void reset() {
        cursor_ = 0;
    }

    size_t count() {
        size_t cnt = 0;
        uint64_t temp_cursor = cursor_;
        while (true) {
            auto [response, size] = queue_.read(temp_cursor);
            if (!response) break;
            cnt++;
        }
        return cnt;
    }

private:
    slick::queue<OrderResponse>& queue_;
    uint64_t cursor_;
};

// Test constants
constexpr symid_t kSymbolId = 1;
constexpr price_t kPrice100 = 10000000000;  // 100.00 * 1e8
constexpr price_t kPrice101 = 10100000000;  // 101.00 * 1e8
constexpr price_t kPrice99  = 9900000000;   // 99.00 * 1e8
constexpr price_t kPrice102 = 10200000000;  // 102.00 * 1e8
constexpr price_t kPrice98  = 9800000000;   // 98.00 * 1e8
constexpr qty_t kQty3  = 300000000;         // 3.0 * 1e8
constexpr qty_t kQty5  = 500000000;         // 5.0 * 1e8
constexpr qty_t kQty10 = 1000000000;        // 10.0 * 1e8
constexpr qty_t kQty15 = 1500000000;        // 15.0 * 1e8
constexpr qty_t kQty20 = 2000000000;        // 20.0 * 1e8

} // namespace slick::sim::test
