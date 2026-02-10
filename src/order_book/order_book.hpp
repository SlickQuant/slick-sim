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
        , order_buffer_(buffer_size)
    {}
    ~OrderBook() override = default;

    Order* allocateOrder();
    Order* getOrderByClientOrderId(std::string_view client_order_id);
    Order* findOrder(uint64_t order_id);

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
        addOrder(order->id, to_book_side(order->side), order->price, order->quantity, timestamp, order->priority,
            seq_num, is_last_in_batch);
    }

    void removeOrder(Order* order, uint64_t timestamp, uint64_t seq_num = 0, bool is_last_in_batch = true) {
        auto it = orders_.find(order->id);
        if (it != orders_.end()) {
            if (!order->client_order_id.empty()) {
                orders_by_client_order_id_.erase(order->client_order_id);
            }
            orders_.erase(it);
        }
        deleteOrder(order->id, seq_num, is_last_in_batch);
        order_buffer_.free(order);
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
            }
        }
        else if (new_price != order->price) {
            order->price = new_price;
            order->priority = nextOrderPriority();
        }
        order->quantity = new_qty;
        modifyOrder(order->id, new_price, new_qty, timestamp, order->priority, seq_num, is_last_in_batch);
        
        if (order->quantity == 0 && it != orders_.end()) {
            if (!order->client_order_id.empty()) {
                orders_by_client_order_id_.erase(order->client_order_id);
            }
            orders_.erase(it);
        }
    }

    time_t lastUpdateTime() const {
        return last_update_time_;
    }
    void setLastUpdateTime(time_t timestamp) {
        last_update_time_ = timestamp;
    }

    virtual std::string to_string() const = 0;

protected:
    static uint64_t nextOrderId() noexcept {
        return ++next_order_id_;
    }

    static uint64_t nextOrderPriority() noexcept {
        return ++next_priority_;
    }

protected:
    static uint64_t next_order_id_;
    static uint64_t next_priority_;
    time_t last_update_time_ = 0;
    symid_t sid_;
    Venue venue_;
    std::string symbol_;
    slick::ObjectPool<Order> order_buffer_;
    std::unordered_map<uint64_t, Order*> orders_;
    std::unordered_map<std::string, Order*> orders_by_client_order_id_;
    OrderUpdate last_order_update_;
};

struct Empty {};

template<bool IS_LEVEL2>
class OrderBookImpl : public OrderBook {
public:
    OrderBookImpl(std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
        : OrderBook(std::move(symbol), venue, buffer_size)
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
        IS_LEVEL2,
        std::unordered_map<price_t, qty_t>,
        Empty> feed_md_level_quantity_;

};



inline uint64_t OrderBook::next_order_id_ = 0;
inline uint64_t OrderBook::next_priority_ = 0;

inline Order* OrderBook::allocateOrder() {
    auto *order = order_buffer_.allocate();
    order->id = nextOrderId();
    return order;
}

inline Order* OrderBook::getOrderByClientOrderId(std::string_view client_order_id) {
    auto it = orders_by_client_order_id_.find(std::string(client_order_id));
    if (it == orders_by_client_order_id_.end()) {
        return nullptr;
    }
    return it->second;
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

template<bool IS_LEVEL2>
inline std::string OrderBookImpl<IS_LEVEL2>::to_string() const {
    std::stringstream ss;
    ss << "OrderBook: " << symbol_ << "\n";
    auto &sell_levels = getLevelsL3(orderbook::Side::Sell);
    auto it_sell = sell_levels.rend();
    std::advance(it_sell, -20);
    for (; it_sell !=  sell_levels.rend(); ++it_sell) {
        ss << std::right << std::setw(50) << std::format("{:.2f}", static_cast<double>(it_sell->first) / DOUBLE_MULTIPLIER);
        ss << std::right << std::setw(20) << std::format("{:.8f}", static_cast<double>(it_sell->second.total_quantity) / DOUBLE_MULTIPLIER);
        ss << std::right << std::setw(10) << it_sell->second.orderCount() << "\n";
    }

    auto &buy_levels = getLevelsL3(orderbook::Side::Buy);
    uint32_t i = 0;
    for (auto it_buy = buy_levels.begin(); it_buy != buy_levels.end() && i < 20; ++it_buy, ++i) {
        ss << std::right << std::setw(10) << it_buy->second.orderCount();
        ss << std::right << std::setw(20) << std::format("{:.8f}", static_cast<double>(it_buy->second.total_quantity) / DOUBLE_MULTIPLIER);
        ss << std::right << std::setw(20) << std::format("{:.2f}", static_cast<double>(it_buy->first) / DOUBLE_MULTIPLIER) << "\n";
    }
    return ss.str();
}


// Level2 Book Implementation
template<>
inline std::tuple<int, qty_t, uint32_t> OrderBookImpl<true>::onLevelUpdate(
    uint64_t event_time, uint64_t seq_num, Side side, price_t price, qty_t size, uint32_t num_orders,
    std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates, bool is_last_in_batch, bool is_snapshot
) {
    auto book_side = to_book_side(side);
    if (is_snapshot) {
        feed_md_level_quantity_[price] = size;
        // Create a phantom order to represent the new L2 level
        addOrder(nextOrderId(), book_side, price, size, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
        auto [level, index] = getLevel(book_side, price);
        return std::make_tuple(index, level->total_quantity, level->orderCount());
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
        addOrder(nextOrderId(), book_side, price, book_side, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
    }
    else {
        SLICK_ASSERT(feed_md_level_quantity_.contains(price));
        if (size > feed_md_level_quantity_[price]) {
            // Level qty increased, assume new orders added at the back. Create new phantom order to represent then increment
            auto sz_diff = size - feed_md_level_quantity_[price];
            addOrder(nextOrderId(), book_side, price, sz_diff, event_time, nextOrderPriority(), seq_num, is_last_in_batch);
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
    return std::make_tuple(level_index, updated_level->total_quantity, updated_level->orderCount());
}

template<>
inline void OrderBookImpl<true>::populateMDBookUpdate(MDBookUpdate &book_update) {
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
inline void OrderBookImpl<true>::populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) {
    auto &ask_levels = getLevelsL3(orderbook::Side::Sell);
    auto &bid_levels = getLevelsL3(orderbook::Side::Buy);
    uint32_t sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(BookSnapshot) + (bid_levels.size() + ask_levels.size()) * sizeof(MDLevel);
    auto index = md_update_queue.reserve(sz);
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
    md_update_queue.publish(index, sz);
}


// // OrderBookAdapter - Adapts slick::orderbook::OrderBookL2 to OrderBook interface
// // Maintains Order* tracking for matching engine while using external library for level management
// class OrderBookAdapter : public OrderBook {
// public:
//     OrderBookAdapter(std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
//         : OrderBook(std::move(symbol), venue, buffer_size)
//         , external_book_(0) // SymbolId = 0, we don't use symbol IDs in sim
//     {
//         // Initialize cached levels for conversion
//         cached_bid_levels_.reserve(20);
//         cached_ask_levels_.reserve(20);
//     }

//     ~OrderBookAdapter() override = default;

//     void clear() override {
//         external_book_.clear();
//         level_order_counts_.clear();
//         cached_bid_levels_.clear();
//         cached_ask_levels_.clear();
//     }

//     uint32_t bidLevelsCount() const override {
//         return static_cast<uint32_t>(external_book_.levelCount(slick::orderbook::Side::Buy));
//     }

//     uint32_t askLevelsCount() const override {
//         return static_cast<uint32_t>(external_book_.levelCount(slick::orderbook::Side::Sell));
//     }

//     OrderBookLevel* bidLevel(uint32_t index) override {
//         const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Buy, static_cast<uint16_t>(index));
//         if (!level) return nullptr;

//         // Ensure cache has enough space
//         if (cached_bid_levels_.size() <= index) {
//             cached_bid_levels_.resize(index + 1);
//         }

//         // Convert external level to local format
//         auto& cached = cached_bid_levels_[index];
//         cached.price_ = level->price;
//         cached.size_ = static_cast<qty_t>(level->quantity);
//         cached.num_orders_ = getLevelOrderCount(Side::BUY, level->price);

//         return &cached;
//     }

//     OrderBookLevel* askLevel(uint32_t index) override {
//         const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Sell, static_cast<uint16_t>(index));
//         if (!level) return nullptr;

//         // Ensure cache has enough space
//         if (cached_ask_levels_.size() <= index) {
//             cached_ask_levels_.resize(index + 1);
//         }

//         // Convert external level to local format
//         auto& cached = cached_ask_levels_[index];
//         cached.price_ = level->price;
//         cached.size_ = static_cast<qty_t>(level->quantity);
//         cached.num_orders_ = getLevelOrderCount(Side::SELL, level->price);

//         return &cached;
//     }

//     void populateMDBookUpdate(MDBookUpdate &book_update) override {
//         // Get ask levels (sells)
//         for (uint32_t i = 0; i < 10; ++i) {
//             const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Sell, static_cast<uint16_t>(i));
//             if (!level) break;

//             book_update.sell[i].price = level->price;
//             book_update.sell[i].qty = static_cast<qty_t>(level->quantity);
//             book_update.sell[i].num_orders = getLevelOrderCount(Side::SELL, level->price);
//         }

//         // Get bid levels (buys)
//         for (uint32_t i = 0; i < 10; ++i) {
//             const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Buy, static_cast<uint16_t>(i));
//             if (!level) break;

//             book_update.buy[i].price = level->price;
//             book_update.buy[i].qty = static_cast<qty_t>(level->quantity);
//             book_update.buy[i].num_orders = getLevelOrderCount(Side::BUY, level->price);
//         }
//     }

//     void populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) override {
//         auto bids_count = external_book_.levelCount(slick::orderbook::Side::Buy);
//         auto asks_count = external_book_.levelCount(slick::orderbook::Side::Sell);

//         auto sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(BookSnapshot) + (bids_count + asks_count) * sizeof(MDLevel);
//         auto index = md_update_queue.reserve(sz);
//         auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue[index]);
//         memcpy(update->symbol, symbol_.c_str(), sizeof(update->symbol));
//         update->venue = venue_;
//         update->type = MDUpdateType::SUB_RESPONSE;

//         auto *response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
//         response->channel = channel;

//         auto *snapshot = reinterpret_cast<BookSnapshot*>(response->data);
//         snapshot->num_bid = static_cast<uint32_t>(bids_count);
//         snapshot->num_ask = static_cast<uint32_t>(asks_count);

//         auto *levels = reinterpret_cast<MDLevel*>(snapshot->levels);

//         // Populate bid levels
//         for (size_t i = 0; i < bids_count; ++i) {
//             const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Buy, static_cast<uint16_t>(i));
//             if (level) {
//                 levels->price = level->price;
//                 levels->qty = static_cast<qty_t>(level->quantity);
//                 levels->num_orders = getLevelOrderCount(Side::BUY, level->price);
//                 ++levels;
//             }
//         }

//         // Populate ask levels
//         for (size_t i = 0; i < asks_count; ++i) {
//             const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Sell, static_cast<uint16_t>(i));
//             if (level) {
//                 levels->price = level->price;
//                 levels->qty = static_cast<qty_t>(level->quantity);
//                 levels->num_orders = getLevelOrderCount(Side::SELL, level->price);
//                 ++levels;
//             }
//         }

//         md_update_queue.publish(index, sz);
//     }

//     std::tuple<int, qty_t, uint32_t> onLevelUpdate(
//         uint64_t event_time, uint64_t seq_num, Side side, price_t price, qty_t size, uint32_t num_orders,
//         std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates, bool is_last_in_batch, bool is_snapshot
//     ) override {
//         auto ext_side = convertSide(side);
//         auto timestamp = getCurrentTimestamp();

//         // Update the level in external book
//         external_book_.updateLevel(ext_side, price, static_cast<slick::orderbook::Quantity>(size), timestamp);

//         // Update order count tracking
//         updateLevelOrderCount(side, price, num_orders);

//         // Find the level index
//         int index = -1;
//         size_t level_count = external_book_.levelCount(ext_side);
//         for (uint16_t i = 0; i < level_count; ++i) {
//             const auto* level = external_book_.getLevelByIndex(ext_side, i);
//             if (level && level->price == price) {
//                 index = i;
//                 break;
//             }
//         }

//         return std::make_tuple(index, size, num_orders);
//     }

//     std::string to_string() const override {
//         std::stringstream ss;
//         ss << "OrderBook: " << symbol_ << "\n";

//         // Show top 20 sell levels (reversed)
//         auto sell_count = external_book_.levelCount(slick::orderbook::Side::Sell);
//         int start = std::max(0, static_cast<int>(sell_count) - 20);
//         for (int i = sell_count - 1; i >= start; --i) {
//             const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Sell, static_cast<uint16_t>(i));
//             if (level) {
//                 ss << std::right << std::setw(50) << std::format("{:.2f}", static_cast<double>(level->price) / DOUBLE_MULTIPLIER);
//                 ss << std::right << std::setw(20) << std::format("{:.8f}", static_cast<double>(level->quantity) / DOUBLE_MULTIPLIER);
//                 ss << std::right << std::setw(10) << getLevelOrderCount(Side::SELL, level->price) << "\n";
//             }
//         }

//         // Show top 20 buy levels
//         auto buy_count = std::min(static_cast<size_t>(20), external_book_.levelCount(slick::orderbook::Side::Buy));
//         for (size_t i = 0; i < buy_count; ++i) {
//             const auto* level = external_book_.getLevelByIndex(slick::orderbook::Side::Buy, static_cast<uint16_t>(i));
//             if (level) {
//                 ss << std::right << std::setw(10) << getLevelOrderCount(Side::BUY, level->price);
//                 ss << std::right << std::setw(20) << std::format("{:.8f}", static_cast<double>(level->quantity) / DOUBLE_MULTIPLIER);
//                 ss << std::right << std::setw(20) << std::format("{:.2f}", static_cast<double>(level->price) / DOUBLE_MULTIPLIER) << "\n";
//             }
//         }

//         return ss.str();
//     }

// protected:
//     void addOrderInternal(Order* order) override {
//         auto ext_side = convertSide(order->side);

//         // Get current level quantity
//         const auto* level = external_book_.getLevel(ext_side, order->price);
//         slick::orderbook::Quantity current_qty = level ? level->quantity : 0;

//         // Calculate new quantity
//         slick::orderbook::Quantity new_qty = current_qty + static_cast<slick::orderbook::Quantity>(order->quantity);

//         // Update level
//         auto timestamp = getCurrentTimestamp();
//         external_book_.updateLevel(ext_side, order->price, new_qty, timestamp);

//         // Increment order count for this level
//         incrementLevelOrderCount(order->side, order->price);
//     }

//     void removeOrderInternal(Order* order) override {
//         auto ext_side = convertSide(order->side);

//         // Get current level quantity
//         const auto* level = external_book_.getLevel(ext_side, order->price);
//         if (!level) return; // Level doesn't exist, nothing to remove

//         // Calculate new quantity
//         slick::orderbook::Quantity new_qty = level->quantity - static_cast<slick::orderbook::Quantity>(order->quantity);

//         if (new_qty <= 0) {
//             // Delete level if quantity reaches zero
//             external_book_.deleteLevel(ext_side, order->price);
//             // Remove from order count tracking
//             removeLevelOrderCount(order->side, order->price);
//         } else {
//             // Update level with new quantity
//             auto timestamp = getCurrentTimestamp();
//             external_book_.updateLevel(ext_side, order->price, new_qty, timestamp);

//             // Decrement order count
//             decrementLevelOrderCount(order->side, order->price);
//         }
//     }

//     void modifyOrderInternal(Order* order, qty_t old_qty) override {
//         auto ext_side = convertSide(order->side);

//         // Get current level quantity
//         const auto* level = external_book_.getLevel(ext_side, order->price);
//         if (!level) {
//             // Level doesn't exist, treat as add
//             addOrderInternal(order);
//             return;
//         }

//         // Calculate quantity delta
//         qty_t qty_delta = order->quantity - old_qty;
//         slick::orderbook::Quantity new_qty = level->quantity + static_cast<slick::orderbook::Quantity>(qty_delta);

//         // Update level
//         auto timestamp = getCurrentTimestamp();
//         external_book_.updateLevel(ext_side, order->price, new_qty, timestamp);
//         // Order count stays the same for modify
//     }

// private:
//     // Helper: Convert local Side enum to external Side enum
//     static slick::orderbook::Side convertSide(Side side) noexcept {
//         return (side == Side::BUY) ? slick::orderbook::Side::Buy : slick::orderbook::Side::Sell;
//     }

//     // Helper: Get current timestamp in nanoseconds
//     static slick::orderbook::Timestamp getCurrentTimestamp() noexcept {
//         using namespace std::chrono;
//         return static_cast<slick::orderbook::Timestamp>(
//             duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()
//         );
//     }

//     // Helper: Create level key for order count tracking
//     static uint64_t makeLevelKey(Side side, price_t price) noexcept {
//         return (static_cast<uint64_t>(side) << 56) | (static_cast<uint64_t>(price) & 0x00FFFFFFFFFFFFFF);
//     }

//     // Helper: Get order count for a level
//     uint32_t getLevelOrderCount(Side side, price_t price) const noexcept {
//         auto key = makeLevelKey(side, price);
//         auto it = level_order_counts_.find(key);
//         return (it != level_order_counts_.end()) ? it->second : 0;
//     }

//     // Helper: Increment order count for a level
//     void incrementLevelOrderCount(Side side, price_t price) {
//         auto key = makeLevelKey(side, price);
//         ++level_order_counts_[key];
//     }

//     // Helper: Decrement order count for a level
//     void decrementLevelOrderCount(Side side, price_t price) {
//         auto key = makeLevelKey(side, price);
//         auto it = level_order_counts_.find(key);
//         if (it != level_order_counts_.end()) {
//             if (--it->second == 0) {
//                 level_order_counts_.erase(it);
//             }
//         }
//     }

//     // Helper: Set order count for a level (used by onLevelUpdate)
//     void updateLevelOrderCount(Side side, price_t price, uint32_t count) {
//         auto key = makeLevelKey(side, price);
//         if (count == 0) {
//             level_order_counts_.erase(key);
//         } else {
//             level_order_counts_[key] = count;
//         }
//     }

//     // Helper: Remove level from order count tracking
//     void removeLevelOrderCount(Side side, price_t price) {
//         auto key = makeLevelKey(side, price);
//         level_order_counts_.erase(key);
//     }

// private:
//     slick::orderbook::OrderBookL2 external_book_;               // External library for level management
//     std::unordered_map<uint64_t, uint32_t> level_order_counts_; // Track order counts per level (side+price -> count)
//     mutable std::vector<OrderBookLevel> cached_bid_levels_;     // Cache for bidLevel() conversion
//     mutable std::vector<OrderBookLevel> cached_ask_levels_;     // Cache for askLevel() conversion
// };


}   // end namespace slick::sim::order_book