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
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <common/market_data.hpp>
#include <common/types.hpp>
#include <slick/orderbook/orderbook_l3.hpp>
#include <utils/order.hpp>

#include <slick/logger.hpp> // TODO: remove this include after debugging

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

class OrderBook : public OrderBookL3 {
public:
    OrderBook(symid_t sid, std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
        : OrderBookL3(sid, buffer_size, 500)
        , symbol_(std::move(symbol))
        , venue_(venue)
        // , order_buffer_(buffer_size)
    {}
    ~OrderBook() override = default;

    // Order* allocateOrder();
    Order* findOrderByClientOrderId(std::string_view client_order_id);
    Order* findOrderByOrderId(std::string_view order_id);
    Order* findOrder(uint64_t id);

    const std::string& symbolName() const noexcept {
        return symbol_;
    }

    symid_t sid() const noexcept {
        return symbol();
    }

    virtual std::tuple<int, qty_t, uint32_t> onLevelUpdate(
        uint64_t event_time, uint64_t seq_num, Side side, price_t price, qty_t size, uint32_t num_orders,
        std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates, bool is_last_in_batch, bool is_snapshot = false
    ) = 0;

    virtual void populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) = 0;
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
        // order_buffer_.free(order);
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

    bool executeOrder(slick::orderbook::OrderId order_id, slick::orderbook::Quantity executed_quantity, uint64_t timestamp, uint64_t seq_num = 0, bool is_last_in_batch = true) override {
        auto rt = OrderBookL3::executeOrder(order_id, executed_quantity, timestamp, seq_num, is_last_in_batch);
        auto *order = findOrder(order_id);
        if (order) {
            order->leaves_quantity -= executed_quantity;
            order->last_update_time = timestamp;
            order->avg_filled_price = ((order->avg_filled_price * order->filled_quantity) + (order->price * executed_quantity)) / (order->filled_quantity + executed_quantity);
            order->filled_quantity += executed_quantity;
            order->last_filled_price = order->price;
            order->last_filled_qty = executed_quantity;
            order->last_fill_time = timestamp;
            order->num_fills += 1;
            if (order->leaves_quantity == 0) {
                if (!order->client_order_id.empty()) {
                    orders_by_client_order_id_.erase(order->client_order_id);
                }
                if (!order->order_id.empty()) {
                    orders_by_order_id_.erase(order->order_id);
                }
                orders_.erase(order_id);
            }
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
    static uint64_t next_priority_;
    time_t last_update_time_ = 0;
    uint64_t last_seq_num_ = 0;
    symid_t sid_;
    Venue venue_;
    std::string symbol_;
    // slick::ObjectPool<Order> order_buffer_;
    std::unordered_map<uint64_t, Order*> orders_;
    std::unordered_map<std::string, Order*> orders_by_client_order_id_;
    std::unordered_map<std::string, Order*> orders_by_order_id_;
    OrderUpdate last_order_update_;
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
    {}
    ~OrderBookImpl() override = default;

    // template<Side SIDE>
    // uint32_t getLevelCount() const {
    //     return sides_[SIDE].levels().size();
    // } 

    // uint32_t bidLevelsCount() const override {
    //     return getLevelCount<Side::BUY>();
    // }

    // uint32_t askLevelsCount() const override {
    //     return getLevelCount<Side::SELL>();
    // }

    // template<Side SIDE>
    // OrderBookLevel* getLevel(uint32_t index) {
    //     auto &levels = sides_[SIDE].levels();
    //     if (index <  levels.size()) {
    //         auto it = levels.begin();
    //         std::advance(it, index);
    //         return &it->second;
    //     }
    //     return nullptr;
    // }

    // OrderBookLevel* bidLevel(uint32_t index) override {
    //     return getLevel<Side::BUY>(index);
    // }

    // OrderBookLevel* askLevel(uint32_t index) override {
    //     return getLevel<Side::SELL>(index);
    // }

    // OrderBookSide<LevelT>& getSide(Side side) const {
    //     return sides_[side];
    // }

    std::tuple<int, qty_t, uint32_t> onLevelUpdate(
        uint64_t event_time, uint64_t seq_num, Side side, price_t price, qty_t size, uint32_t num_orders,
        std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates, bool is_last_in_batch, bool is_snapshot = false
    ) override;

    void populateMDBookUpdate(MDBookUpdate &book_update) override;
    void populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) override;

    // void modifyOrder(Order* order, price_t new_price, qty_t new_qty) override {}


    std::string to_string() const override;

protected:
    // void addOrderInternal(Order* order) override {
    //     auto& book_side = getSide(order->side);
    //     book_side.addOrder(order);
    // }

    // void removeOrderInternal(Order* order) override {
    //     auto& book_side = getSide(order->side);
    //     book_side.removeOrder(order);
    // }

    // void modifyOrderInternal(Order* order, qty_t old_qty) override {
    //     auto& book_side = getSide(order->side);
    //     book_side.modifyOrder(order, old_qty);
    // }

protected:
    [[no_unique_address]] std::conditional_t<
        BookType == OrderBookType::L2,
        std::unordered_map<price_t, qty_t>,
        Empty> feed_md_level_quantity_;
};



inline uint64_t OrderBook::next_priority_ = 0;

// inline Order* OrderBook::allocateOrder() {
//     auto *order = order_buffer_.allocate();
//     order->id = nextOrderId();
//     return order;
// }

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

// inline void OrderBook::clear() {
//     book_.clear();
// }

// inline size_t OrderBook::levelCount(Side side) {
//     return book_.levelCount(to_book_side(side));
// }

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
inline std::tuple<int, qty_t, uint32_t> OrderBookImpl<OrderBookType::L2>::onLevelUpdate(
    uint64_t event_time, uint64_t seq_num, Side side, price_t price, qty_t size, uint32_t /* num_orders */,
    std::vector<MDLevel>& /* level_updates */, std::vector<MDTrade>& /* trade_updates */, bool is_last_in_batch, bool is_snapshot
) {
    auto book_side = to_book_side(side);
    if (is_snapshot) {
        feed_md_level_quantity_[price] = size;
        // Create a phantom order to represent the new L2 level
        addOrder(utils::nextOrderId(), book_side, price, size, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
        auto [level, index] = getLevel(book_side, price);
        return std::make_tuple(index, level->total_quantity, static_cast<uint32_t>(level->orderCount()));
    }

    const auto *top_bid = getBestBid();
    const auto *top_ask = getBestAsk();
    if (side == Side::BUY && top_ask && price >= top_ask->price) {
        // Match against sell levels
        LOG_WARN("Matching against opposite side levels. {} price={}, best_sell={}", symbol_, price, top_ask->price);
    }
    else if (side == Side::SELL && top_bid && price <= top_bid->price) {
        // Match against buy levels
        LOG_WARN("Matching against opposite side levels. {} price={}, best_buy={}", symbol_, price, top_bid->price);
    }

    auto [level, index] = getLevel(book_side, price);
    if (!level) {
        // this is a new level. Create a phantom order to represent the level
        addOrder(utils::nextOrderId(), book_side, price, book_side, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
    }
    else {
        SLICK_ASSERT(feed_md_level_quantity_.contains(price));
        if (size > feed_md_level_quantity_[price]) {
            // Level qty increased, assume new orders added at the back. Create new phantom order to represent then increment
            auto sz_diff = size - feed_md_level_quantity_[price];
            addOrder(utils::nextOrderId(), book_side, price, sz_diff, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
        }
        else if (size < feed_md_level_quantity_[price]) {
            // Level qty decreased. Alwayse assume the worst case, that the quantity decreased at the back
            auto diff = feed_md_level_quantity_[price] - size;
            for (auto it = level->orders.rbegin(), it_last = std::prev(level->orders.rend()); it != level->orders.rend(); ++it) {
                auto &order = *it;
                if (orders_.contains(order.order_id)) {
                    // This is an exchange order, skip it
                    continue;
                }

                auto to_reduce = std::min<qty_t>(diff, order.quantity);
                diff -= to_reduce;
                this->modifyOrder(order.order_id, price, order.quantity - to_reduce, event_time, order.priority, seq_num, is_last_in_batch && (diff == 0));
                if (diff == 0) {
                    break;
                }
            }
            SLICK_ASSERT(diff == 0);
        }
    }
    feed_md_level_quantity_[price] = size;
    auto [updated_level, level_index] = getLevel(book_side, price);
    return std::make_tuple(level_index, updated_level->total_quantity, static_cast<uint32_t>(updated_level->orderCount()));
}

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
inline void OrderBookImpl<OrderBookType::L2>::populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) {
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



// L3 Book Implementation
template<>
inline std::tuple<int, qty_t, uint32_t> OrderBookImpl<OrderBookType::L3>::onLevelUpdate(
    uint64_t event_time, uint64_t seq_num, Side side, price_t price, qty_t size, uint32_t num_orders,
    std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates,
    bool is_last_in_batch, bool is_snapshot)
{
    // Stub implementation for testing
    // In production, this would update market data structures
    return std::make_tuple(0, size, num_orders);
}

template<>
inline void OrderBookImpl<OrderBookType::L3>::populateL2SubscriptionResponse(slick::SlickQueue<uint8_t>& md_update_queue, uint8_t channel) {
    // Stub implementation for testing
    // In production, this would populate subscription response
}

template<>
inline void OrderBookImpl<OrderBookType::L3>::populateMDBookUpdate(MDBookUpdate& book_update) {
    // Stub implementation for testing
    // In production, this would populate MD book update
}


}   // end namespace slick::sim::order_book