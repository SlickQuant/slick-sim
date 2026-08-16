#pragma once

#include <cstdint>
#include <string>
#include <set>
#include <sstream>
#include <tuple>
#include <chrono>
#include <unordered_map>
#include <type_traits>
#include <slick/object_pool.h>
#include <slick/queue.h>
#include <common/order.hpp>
#include <common/market_data.hpp>
#include <common/types.hpp>
#include <slick/orderbook/orderbook_l3.hpp>
#include <utils/order.hpp>
#include <slick/orderbook/observer.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/lexical_cast.hpp>
#include <utils/order.hpp>

// #include <slick/logger.hpp> // TODO: remove this include after debugging

namespace slick::sim {

using OrderBookL3 = slick::orderbook::OrderBookL3;
using IOrderBookObserver = slick::orderbook::IOrderBookObserver;
using PriceLevelUpdate = slick::orderbook::PriceLevelUpdate;
using OrderUpdate = slick::orderbook::OrderUpdate;
using TopOfBook = slick::orderbook::TopOfBook;
using SymbolId = slick::orderbook::SymbolId;
using Timestamp = slick::orderbook::Timestamp;

inline orderbook::Side to_book_side(Side side) {
    return static_cast<orderbook::Side>(side);
}

class OrderBook;

class OrderBook : public OrderBookL3, public orderbook::IOrderBookObserver, public std::enable_shared_from_this<OrderBook> {
public:
    OrderBook(symid_t sid, std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
        : OrderBookL3(sid, buffer_size, 500)
        , venue_(venue)
        , symbol_(std::move(symbol))
        , order_buffer_(buffer_size)
    {
    }
    ~OrderBook() override = default;

    Order* allocateOrder();
    void freeOrder(Order* order) {
        order_buffer_.free(order);
    }
    Order* findOrderByClientOrderId(std::string_view client_order_id);
    Order* findOrderByOrderId(std::string_view order_id);
    Order* findOrder(uint64_t id);

    const std::string& symbolName() const noexcept {
        return symbol_;
    }

    symid_t sid() const noexcept {
        return symbol();
    }

    void onPriceLevelUpdate(const PriceLevelUpdate& update) override;
    void onOrderUpdate(const OrderUpdate& update) override;

    // The level quantity excludes exchange orders
    qty_t getMDLevelQty(price_t price) const;

    virtual void populateL2SubscriptionResponse(slick::queue<uint8_t> &md_update_queue, uint8_t channel) = 0;
    virtual void populateL2Snapshot(slick::queue<uint8_t> &md_update_queue) = 0;
    virtual void populateMDBookUpdate(MDBookUpdate &book_update) = 0;

    using OrderBookL3::addOrder;
    using OrderBookL3::modifyOrder;
    using OrderBookL3::deleteOrder;

    void addOrder(Order* order, uint64_t timestamp, uint64_t seq_num = 0, bool is_last_in_batch = true) {
        order->priority = nextOrderPriority();
        orders_.emplace(order->id, order);
        if (!order->client_order_id.empty()) {
            orders_by_client_order_id_.emplace(order->client_order_id, order);
        }
        if (!order->order_id.empty()) {
            orders_by_order_id_.emplace(order->order_id, order);
        }
        addOrder(order->id, to_book_side(order->side), order->price, order->leaves_quantity, timestamp, order->priority,
            seq_num, is_last_in_batch);
    }

    virtual void addBookOrder(uint64_t order_id, slick::orderbook::Side book_side, price_t price, qty_t qty, uint64_t timestamp, uint64_t seq_num) = 0;

    void deleteOrder(Order* order, uint64_t timestamp, uint64_t seq_num = 0, bool is_last_in_batch = true) {
        deleteOrder(order->id, timestamp, seq_num, is_last_in_batch);
        auto it = orders_.find(order->id);
        if (it != orders_.end()) {
            if (!order->client_order_id.empty()) {
                orders_by_client_order_id_.erase(order->client_order_id);
            }
            if (!order->order_id.empty()) {
                orders_by_order_id_.erase(order->order_id);
            }
            orders_.erase(it);
        }
    }

    void deleteOrder(Order* order) {
        auto it = orders_.find(order->id);
        if (it != orders_.end()) {
            if (!order->client_order_id.empty()) {
                orders_by_client_order_id_.erase(order->client_order_id);
            }
            if (!order->order_id.empty()) {
                orders_by_order_id_.erase(order->order_id);
            }
            orders_.erase(it);
        }
        freeOrder(order);
    }

    void modifyOrder(Order* order, price_t new_price, qty_t new_qty, uint64_t timestamp,
        uint64_t seq_num = 0, bool is_last_in_batch = true) {
        auto it = orders_.find(order->id);
        if (it == orders_.end()) [[unlikely]] {
            order->price = new_price;
            order->priority = nextOrderPriority();
            if (new_qty) {
                it = orders_.emplace(order->id, order).first;
                if (!order->client_order_id.empty()) {
                    orders_by_client_order_id_.emplace(order->client_order_id, order);
                }
                if (!order->order_id.empty()) {
                    orders_by_order_id_.emplace(order->order_id, order);
                }
            }
        }
        else if (new_price != order->price) {
            order->price = new_price;
            order->priority = nextOrderPriority();
        }
        order->quantity = new_qty;
        modifyOrder(order->id, new_price, order->leaves_quantity, timestamp, order->priority, seq_num, is_last_in_batch);
        
        if (order->leaves_quantity == 0 && it != orders_.end()) {
            if (!order->client_order_id.empty()) {
                orders_by_client_order_id_.erase(order->client_order_id);
            }
            if (!order->order_id.empty()) {
                orders_by_order_id_.erase(order->order_id);
            }
            orders_.erase(it);
        }
    }

    // Clears only phantom (market-data) orders from the L3 book, then re-inserts
    // active simulator orders so they remain available for matching.  Call this
    // instead of clear() when rebuilding from a full book snapshot (e.g. Hyperliquid).
    void clearMDOrders() {
        std::vector<std::tuple<uint64_t, orderbook::Side, price_t, qty_t, time_t, uint64_t>> sim_orders;
        sim_orders.reserve(orders_.size());
        for (auto& [id, order] : orders_) {
            if (order->leaves_quantity > 0) {
                sim_orders.emplace_back(id, to_book_side(order->side), order->price,
                                        order->leaves_quantity, order->last_update_time, order->priority);
            }
        }
        OrderBookL3::clear();
        feed_md_level_quantity_.clear();
        for (auto& [id, side, price, qty, ts, prio] : sim_orders) {
            OrderBookL3::addOrder(id, side, price, qty, ts, prio, 0, false);
        }
    }

    bool executeOrder(slick::orderbook::OrderId order_id, slick::orderbook::Quantity executed_quantity, uint64_t timestamp, uint64_t seq_num = 0, bool is_last_in_batch = true) override {
        auto *order = findOrder(order_id);
        if (order) {
            order->leaves_quantity -= executed_quantity;
            assert(order->leaves_quantity >= 0);
            assert(executed_quantity > 0);
            order->last_update_time = timestamp;
            order->cum_value += static_cast<cum_value_t>(order->price) * static_cast<cum_value_t>(executed_quantity);
            order->cum_quantity += executed_quantity;
            order->avg_fill_price = order->cum_quantity > 0
                ? static_cast<price_t>(order->cum_value / static_cast<cum_value_t>(order->cum_quantity))
                : 0;
            order->last_fill_price = order->price;
            order->last_fill_qty = executed_quantity;
            order->last_fill_time = timestamp;
            order->num_fills += 1;
        }
        auto rt = OrderBookL3::executeOrder(order_id, executed_quantity, timestamp, seq_num, is_last_in_batch);
        if (order && order->leaves_quantity == 0) {
            if (!order->client_order_id.empty()) {
                orders_by_client_order_id_.erase(order->client_order_id);
            }
            if (!order->order_id.empty()) {
                orders_by_order_id_.erase(order->order_id);
            }
            orders_.erase(order_id);
        }
        return rt;
    }

    time_t lastUpdateTime() const noexcept {
        return last_update_time_;
    }

    uint64_t lastSeqNum() const noexcept {
        return last_seq_num_;
    }

    void setLastUpdate(time_t timestamp, uint64_t seq_num) noexcept {
        last_update_time_ = timestamp;
        last_seq_num_ = seq_num;
    }

    virtual std::string to_string() const = 0;

protected:
    static uint64_t nextOrderPriority() noexcept {
        return ++next_priority_;
    }

protected:
    friend struct Observer;
    static uint64_t next_priority_;
    time_t last_update_time_ = 0;
    uint64_t last_seq_num_ = 0;
    symid_t sid_;
    Venue venue_;
    std::string symbol_;
    slick::ObjectPool<Order> order_buffer_;
    std::unordered_map<uint64_t, Order*> orders_;
    std::unordered_map<std::string, Order*> orders_by_client_order_id_;
    std::unordered_map<std::string, Order*> orders_by_order_id_;
    OrderUpdate last_order_update_;
    std::unordered_map<price_t, qty_t> feed_md_level_quantity_;     // tracking the level quantity exclude exchange orders
};

struct Empty {};

enum class OrderBookType {
    L2,
    L3
};

template<OrderBookType BookType>
class OrderBookImpl : public OrderBook {
public:
    OrderBookImpl(symid_t sid, std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
        : OrderBook(sid, std::move(symbol), venue, buffer_size)
    {
    }
    ~OrderBookImpl() override = default;

    void addBookOrder(uint64_t order_id, slick::orderbook::Side book_side, price_t price, qty_t qty, uint64_t timestamp, uint64_t seq_num) override;

    void populateMDBookUpdate(MDBookUpdate &book_update) override;
    void populateL2SubscriptionResponse(slick::queue<uint8_t> &md_update_queue, uint8_t channel) override;
    void populateL2Snapshot(slick::queue<uint8_t> &md_update_queue) override;
    
    // void modifyOrder(Order* order, price_t new_price, qty_t new_qty) override {}


    std::string to_string() const override;
};



inline uint64_t OrderBook::next_priority_ = 0;

inline Order* OrderBook::allocateOrder() {
    auto *order = order_buffer_.allocate();
    boost::uuids::uuid u = boost::uuids::random_generator()();
    order->order_id = boost::lexical_cast<std::string>(u);
    order->id = utils::nextOrderId();
    order->created_time = utils::get_current_time_ns();
    order->last_update_time = order->created_time;
    order->resetFillAccounting();
    return order;
}

inline qty_t OrderBook::getMDLevelQty(price_t price) const {
    auto it = feed_md_level_quantity_.find(price);
    if (it == feed_md_level_quantity_.end()) {
        return 0;
    }

    return it->second;
}

inline void OrderBook::onPriceLevelUpdate(const PriceLevelUpdate& update) {
    if (update.quantity == 0) {
        SLICK_ASSERT(feed_md_level_quantity_.find(update.price) != feed_md_level_quantity_.end()
            || feed_md_level_quantity_[update.price] == 0);
    }
}

inline void OrderBook::onOrderUpdate(const OrderUpdate& update) {
    auto *our_order = findOrder(update.order_id);
    if (!our_order) {
        // This is a MD order, update md_level_qty
        if (update.price != update.old_price) {
            auto it = feed_md_level_quantity_.find(update.old_price);
            if (it != feed_md_level_quantity_.end()) {
                it->second -= update.old_qty;
            }

            it = feed_md_level_quantity_.find(update.price);
            if (it != feed_md_level_quantity_.end()) {
                it->second += update.quantity;
            } else if (update.quantity) {
                feed_md_level_quantity_[update.price] = update.quantity;
            }
        }
        else {
            auto it = feed_md_level_quantity_.find(update.price);
            if (it != feed_md_level_quantity_.end()) {
                it->second += update.quantity - update.old_qty;
            }
            else if (update.quantity) {
                feed_md_level_quantity_[update.price] = update.quantity;
            }
        }
    }
    else if (update.quantity == 0 && our_order->quantity != update.quantity) {
        our_order->leaves_quantity = 0;
    }
}

inline Order* OrderBook::findOrderByClientOrderId(std::string_view client_order_id) {
    auto it = orders_by_client_order_id_.find(std::string(client_order_id));
    return it == orders_by_client_order_id_.end() ? nullptr : it->second;
}

inline Order* OrderBook::findOrderByOrderId(std::string_view client_order_id) {
    auto it = orders_by_order_id_.find(std::string(client_order_id));
    return it == orders_by_order_id_.end() ? nullptr : it->second;
}

inline Order* OrderBook::findOrder(uint64_t order_id) {
    auto it = orders_.find(order_id);
    return it != orders_.end() ? it->second : nullptr;
}


template<OrderBookType BookType>
inline void OrderBookImpl<BookType>::addBookOrder(
    uint64_t order_id,
    slick::orderbook::Side book_side,
    price_t price,
    qty_t qty,
    uint64_t timestamp,
    uint64_t seq_num) {
    addOrder(order_id, book_side, price, qty, timestamp, nextOrderPriority(), seq_num, true);
}

template<OrderBookType BookType>
inline std::string OrderBookImpl<BookType>::to_string() const {
    std::stringstream ss;
    ss << "OrderBook: " << symbol_ << "\n";
    auto &sell_levels = getLevelsL3(orderbook::Side::Sell);
    auto it_sell = sell_levels.rend();
    std::advance(it_sell, -20);
    for (; it_sell !=  sell_levels.rend(); ++it_sell) {
        ss << std::right << std::setw(50) << std::format("{:.2f}", to_price_double(it_sell->first));
        ss << std::right << std::setw(20) << std::format("{:.8f}", to_qty_double(it_sell->second.total_quantity));
        ss << std::right << std::setw(10) << it_sell->second.orderCount() << "\n";
    }

    auto &buy_levels = getLevelsL3(orderbook::Side::Buy);
    uint32_t i = 0;
    for (auto it_buy = buy_levels.begin(); it_buy != buy_levels.end() && i < 20; ++it_buy, ++i) {
        ss << std::right << std::setw(10) << it_buy->second.orderCount();
        ss << std::right << std::setw(20) << std::format("{:.8f}", to_qty_double(it_buy->second.total_quantity));
        ss << std::right << std::setw(20) << std::format("{:.2f}", to_price_double(it_buy->first)) << "\n";
    }
    return ss.str();
}

// L2 Book Implementation
template<>
inline void OrderBookImpl<OrderBookType::L2>::populateMDBookUpdate(MDBookUpdate &book_update) {
    auto &ask_levels = getLevelsL3(orderbook::Side::Sell);
    uint32_t i = 0;
    for (auto it = ask_levels.begin(); it != ask_levels.end() && i < 10; ++it, ++i) {
        auto &levels = book_update.sell;
        levels[i].price = it->first;
        levels[i].qty = it->second.getTotalQuantity();
        levels[i].num_orders = static_cast<uint32_t>(it->second.orderCount());
    }
    i = 0;
    auto &bid_levels = getLevelsL3(orderbook::Side::Buy);
    for (auto it = bid_levels.begin(); it != bid_levels.end() && i < 10; ++it, ++i) {
        auto &levels = book_update.buy;
        levels[i].price = it->first;
        levels[i].qty = it->second.getTotalQuantity();
        levels[i].num_orders = static_cast<uint32_t>(it->second.orderCount());
    }
}

template<>
inline void OrderBookImpl<OrderBookType::L2>::populateL2SubscriptionResponse(slick::queue<uint8_t> &md_update_queue, uint8_t channel) {
    auto &ask_levels = getLevelsL3(orderbook::Side::Sell);
    auto &bid_levels = getLevelsL3(orderbook::Side::Buy);
    auto sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(BookSnapshot) + (bid_levels.size() + ask_levels.size()) * sizeof(MDLevel);
    auto index = md_update_queue.reserve(static_cast<uint32_t>(sz));
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue[index]);
    memcpy(update->symbol, symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    response->channel = channel;

    auto *snapshot = reinterpret_cast<BookSnapshot*>(response->data);
    snapshot->num_bid = static_cast<uint32_t>(bid_levels.size());
    snapshot->num_ask = static_cast<uint32_t>(ask_levels.size());

    auto *levels = reinterpret_cast<MDLevel*>(snapshot->levels);

    for (auto it = bid_levels.begin(); it != bid_levels.end(); ++it, ++levels) {
        levels->price = it->first;
        levels->qty = it->second.total_quantity;
        levels->num_orders = static_cast<uint32_t>(it->second.orderCount());
    }
    for (auto it = ask_levels.begin(); it != ask_levels.end(); ++it, ++levels) {
        levels->price = it->first;
        levels->qty = it->second.total_quantity;
        levels->num_orders = static_cast<uint32_t>(it->second.orderCount());
    }
    md_update_queue.publish(index, static_cast<uint32_t>(sz));
}

template<>
inline void OrderBookImpl<OrderBookType::L2>::populateL2Snapshot(slick::queue<uint8_t> &md_update_queue) {
    auto &ask_levels = getLevelsL3(orderbook::Side::Sell);
    auto &bid_levels = getLevelsL3(orderbook::Side::Buy);
    auto sz = sizeof(MarketDataUpdate) + sizeof(BookSnapshot) + (bid_levels.size() + ask_levels.size()) * sizeof(MDLevel);
    auto index = md_update_queue.reserve(static_cast<uint32_t>(sz));
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue[index]);
    memcpy(update->symbol, symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::BOOK_SNAPSHOT;

    auto *snapshot = reinterpret_cast<BookSnapshot*>(update->data);
    snapshot->num_bid = static_cast<uint32_t>(bid_levels.size());
    snapshot->num_ask = static_cast<uint32_t>(ask_levels.size());

    auto *levels = reinterpret_cast<MDLevel*>(snapshot->levels);

    for (auto it = bid_levels.begin(); it != bid_levels.end(); ++it, ++levels) {
        levels->price = it->first;
        levels->qty = it->second.total_quantity;
        levels->num_orders = static_cast<uint32_t>(it->second.orderCount());
    }
    for (auto it = ask_levels.begin(); it != ask_levels.end(); ++it, ++levels) {
        levels->price = it->first;
        levels->qty = it->second.total_quantity;
        levels->num_orders = static_cast<uint32_t>(it->second.orderCount());
    }
    md_update_queue.publish(index, static_cast<uint32_t>(sz));
}

// L3 Book Implementation


template<>
inline void OrderBookImpl<OrderBookType::L3>::populateL2SubscriptionResponse([[maybe_unused]] slick::queue<uint8_t>& md_update_queue, [[maybe_unused]] uint8_t channel) {
    // Stub implementation for testing
    // In production, this would populate subscription response
}

template<>
inline void OrderBookImpl<OrderBookType::L3>::populateMDBookUpdate([[maybe_unused]] MDBookUpdate& book_update) {
    // Stub implementation for testing
    // In production, this would populate MD book update
}

template<>
inline void OrderBookImpl<OrderBookType::L3>::populateL2Snapshot([[maybe_unused]] slick::queue<uint8_t> &md_update_queue) {
}


}   // end namespace slick::sim::order_book