#pragma once

#include <gtest/gtest.h>
#include <common/order.hpp>
#include <common/types.hpp>
#include <common/messages.hpp>
#include <common/market_data.hpp>
#include <slick/queue.hpp>
#include <slick/shm/shared_memory.hpp>
#include <slick/object_pool.hpp>
#include <utils/order.hpp>
#include <order_book/order_book.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>

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

// Mock: MarketDataUpdate collector.
//
// md_queue_ carries variable-length frames, so the raw bytes are copied out and
// kept alive by the collector - the returned pointers stay valid until the next
// collect()/reset().
class MarketDataCollector {
public:
    explicit MarketDataCollector(slick::queue<uint8_t>& queue)
        : queue_(queue), cursor_(0) {}

    std::vector<MarketDataUpdate*> collect() {
        frames_.clear();
        std::vector<MarketDataUpdate*> updates;
        while (true) {
            auto [data, size] = queue_.read(cursor_);
            if (!data) break;
            frames_.emplace_back(data, data + size);
        }
        updates.reserve(frames_.size());
        for (auto& frame : frames_) {
            updates.push_back(reinterpret_cast<MarketDataUpdate*>(frame.data()));
        }
        return updates;
    }

    /// Every frame of one type, in order.
    std::vector<MarketDataUpdate*> collect(MDUpdateType type) {
        std::vector<MarketDataUpdate*> matched;
        for (auto* update : collect()) {
            if (update->type == type) {
                matched.push_back(update);
            }
        }
        return matched;
    }

    void reset() {
        cursor_ = 0;
        frames_.clear();
    }

private:
    slick::queue<uint8_t>& queue_;
    uint64_t cursor_;
    std::vector<std::vector<uint8_t>> frames_;
};

// Shared-memory cleanup for the queues a test names.
//
// slick-queue 2.3.0 stopped unlinking a named segment in the queue's destructor,
// because a peer process may still be attached; removing the name became the
// coordinator's job, through remove_shm(). On POSIX a name left behind outlives
// the process, and the next queue built with that name attaches to the old
// segment instead of creating one - so a collector, which reads from cursor 0, is
// handed every response the previous test published. Windows frees a segment
// with its last handle, so this only ever failed on Linux and macOS.
//
// A queue or exchange held through TestQueuePtr / TestExchangePtr unlinks its
// segments when it is destroyed, which a failed ASSERT reaches as surely as a
// passing test does. Build them with makeTestQueue / makeTestExchange rather than
// std::make_unique whenever a shared-memory name is involved.

template <typename T>
concept ShmQueue = requires(const T& queue) { queue.remove_shm(); };

/// Unlinks the segment behind a queue. A no-op for a queue on process memory.
template <ShmQueue QueueT>
void removeShm(QueueT& queue) noexcept {
    queue.remove_shm();
}

/// Unlinks the segments behind an exchange's three queues.
template <typename ExchangeT>
void removeShm(ExchangeT& exchange) noexcept {
    removeShm(exchange.request_queue());
    removeShm(exchange.response_queue());
    removeShm(exchange.md_queue());
}

/// Deleter that unlinks before it destroys. The unlink leaves the owner's own
/// mappings valid, and has to come first: only a live queue can name its segment.
struct ShmRemover {
    template <typename T>
    void operator()(T* owner) const noexcept {
        removeShm(*owner);
        delete owner;
    }
};

template <typename T>
using TestQueuePtr = std::unique_ptr<slick::queue<T>, ShmRemover>;

template <typename ExchangeT>
using TestExchangePtr = std::unique_ptr<ExchangeT, ShmRemover>;

/// Builds a shared-memory queue that is unlinked when the test ends.
///
/// A segment already under `shm_name` is unlinked first. A test that crashed never
/// ran its cleanup, and without this every later run would attach to what it left
/// behind and fail for a reason that is nowhere in its own log.
template <typename T>
TestQueuePtr<T> makeTestQueue(uint32_t size, const char* shm_name) {
    slick::shm::shared_memory::remove(shm_name);
    return TestQueuePtr<T>(new slick::queue<T>(size, shm_name));
}

/// Builds an exchange whose shared-memory queues are unlinked when the test ends,
/// clearing stale segments under its configured names first, as makeTestQueue does.
template <typename ExchangeT>
TestExchangePtr<ExchangeT> makeTestExchange(const nlohmann::json& config) {
    for (const char* key : {"request_queue_shm_name", "response_queue_shm_name", "md_queue_shm_name"}) {
        if (auto it = config.find(key); it != config.end()) {
            slick::shm::shared_memory::remove(it->template get_ref<const std::string&>().c_str());
        }
    }
    return TestExchangePtr<ExchangeT>(new ExchangeT(config));
}

// Test constants
constexpr symid_t kSymbolId = 1;
constexpr price_t kPrice100 = 10000000000;  // 100.00 * 1e8
constexpr price_t kPrice101 = 10100000000;  // 101.00 * 1e8
constexpr price_t kPrice99  = 9900000000;   // 99.00 * 1e8
constexpr price_t kPrice102 = 10200000000;  // 102.00 * 1e8
constexpr price_t kPrice98  = 9800000000;   // 98.00 * 1e8
constexpr qty_t kQty2  = 200000000;         // 2.0 * 1e8
constexpr qty_t kQty3  = 300000000;         // 3.0 * 1e8
constexpr qty_t kQty5  = 500000000;         // 5.0 * 1e8
constexpr qty_t kQty10 = 1000000000;        // 10.0 * 1e8
constexpr qty_t kQty15 = 1500000000;        // 15.0 * 1e8
constexpr qty_t kQty20 = 2000000000;        // 20.0 * 1e8

} // namespace slick::sim::test
