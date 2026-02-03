#pragma once

#include <cstdint>
#include <string>
#include <set>
#include <sstream>
#include <tuple>
#include <slick/object_pool.h>
#include <slick/queue.h>
#include <common/order.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <common/market_data.hpp>
#include <common/types.hpp>

#include <slick/logger.hpp> // TODO: remove this include after debugging

namespace slick::sim {

struct OrderBookLevel {
    price_t price_ = NULL_PRICE;   // price in 1e-8 units
    qty_t size_ = 0;            // size in 1e-8 units
    uint32_t num_orders_ = 0;

    OrderBookLevel() = default;
    OrderBookLevel(int_fast64_t price, uint_fast64_t size, uint32_t num_orders)
        : price_(price), size_(size), num_orders_(num_orders) {}
    
    OrderBookLevel(double price, double size, uint32_t num_orders)
        : price_(static_cast<int_fast64_t>(price * DOUBLE_MULTIPLIER))
        , size_(static_cast<uint_fast64_t>(size * DOUBLE_MULTIPLIER))
        , num_orders_(num_orders) {}

    OrderBookLevel(const OrderBookLevel& other) = default;
    OrderBookLevel& operator=(const OrderBookLevel& other) = default;
    OrderBookLevel(OrderBookLevel&& other) noexcept = default;
    OrderBookLevel& operator=(OrderBookLevel&& other) noexcept = default;

    virtual ~OrderBookLevel() = default;

    bool operator==(const OrderBookLevel& other) const noexcept {
        return price_ == other.price_
            && size_ == other.size_
            && num_orders_ == other.num_orders_;
    }

    bool operator!=(const OrderBookLevel& other) const noexcept {
        return !(*this == other);
    }

    static bool is_null_price(int_fast64_t price) {
        return price == NULL_PRICE;
    }  
    
    double price_double() const {
        if (price_ == NULL_PRICE) {
            return NAN;
        }
        return static_cast<double>(price_) / DOUBLE_MULTIPLIER;
    }
    
    double size_double() const {
        return static_cast<double>(size_) / DOUBLE_MULTIPLIER;
    }

    bool empty() const noexcept{
        return price_ == NULL_PRICE
            || size_ == 0;
    }

    virtual void addOrder(const Order* order) = 0;
    virtual void removeOrder(const Order* order) = 0;
    virtual void modifyOrder(const Order* order, qty_t old_size) = 0;
    virtual void onLevelUpdate(qty_t size, uint32_t num_orders) = 0;
    virtual void onTrade(price_t trade_price, qty_t trade_size) = 0;
};

struct L2OrderBookLevel : public OrderBookLevel {
    std::vector<Order*> orders_;
    qty_t total_order_size_ = 0;
    
    L2OrderBookLevel() = default;
    L2OrderBookLevel(int_fast64_t price, uint_fast64_t size, uint32_t num_orders)
        : OrderBookLevel(price, size, num_orders) {}
    L2OrderBookLevel(double price, double size, uint32_t num_orders)
        : OrderBookLevel(price, size, num_orders) {}

    void addOrder(const Order* order) override {
        orders_.push_back(const_cast<Order*>(order));
        size_ += order->quantity;
        total_order_size_ += order->quantity;
    }

    void removeOrder(const Order* order) override {
        auto it = std::find(orders_.begin(), orders_.end(), order);
        if (it != orders_.end()) {
            size_ -= order->quantity;
            total_order_size_ -= order->quantity;
            orders_.erase(it);
        }
    }

    void modifyOrder(const Order* order, qty_t old_size) override {
        auto it = std::find(orders_.begin(), orders_.end(), order);
        if (it != orders_.end()) {
            size_ += order->quantity - old_size;
            total_order_size_ += order->quantity - old_size;
        }
    }

    void onLevelUpdate(qty_t size, uint32_t num_orders) override {
        if (size == 0) {
            size_ = total_order_size_;
        }
        else {
            size_ = size + total_order_size_;
        }
        num_orders_ = num_orders + static_cast<uint32_t>(orders_.size());
    }

    void onTrade(price_t trade_price, qty_t trade_size) override {
        // this->size_ -= trade_size;
        // if (size_ < 0) {
        //     size_ = 0;
        // }
    }
};

struct L3OrderBookLevel : public OrderBookLevel {
    using order_container = boost::multi_index_container<
        Order*,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::member<Order, uint64_t, &Order::id>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::member<Order, uint64_t, &Order::priority>
            >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::member<Order, uint64_t, &Order::leaves_quantity>
            >
        >
    >;

    order_container orders_;

    L3OrderBookLevel() = default;
    L3OrderBookLevel(int_fast64_t price, uint_fast64_t size, uint32_t num_orders)
        : OrderBookLevel(price, size, num_orders) {}
};

struct BookLevelComparator {
    Side side_;
    BookLevelComparator(Side side) : side_(side) {}
    bool operator()(price_t a, price_t b) const noexcept {
        return side_ == Side::BUY ? a > b : a < b;
    }
};

template<typename LevelT>
    requires std::derived_from<LevelT, OrderBookLevel>
class OrderBookSide {
public:
    using Levels = std::map<price_t, LevelT, BookLevelComparator>;
    
    OrderBookSide(Side side)
        : levels_(BookLevelComparator(side))
    {};
    ~OrderBookSide() = default;

    std::tuple<int, qty_t, uint32_t> updateLevel(price_t price, qty_t size, uint32_t num_orders = 0) {
        auto it = levels_.find(price);
        int index = -1;
        qty_t qty = 0;
        uint32_t order_count = 0;
        if (it != levels_.end()) {
            it->second.onLevelUpdate(size, num_orders);
            qty = it->second.size_;
            order_count = it->second.num_orders_;
            index = std::distance(levels_.begin(), it);
            if (it->second.size_ == 0) {
                levels_.erase(it);
            }
        } else if (size > 0) {
            LevelT level(price, size, num_orders);
            it = levels_.emplace(price, std::move(level)).first;
            qty = it->second.size_;
            order_count = it->second.num_orders_;
            index = std::distance(levels_.begin(), it);
        }
        return std::make_tuple(index, qty, order_count);
    }

    void removeLevel(price_t price) {
        levels_.erase(price);
    }

    Levels& levels() {
        return levels_;
    }

    const Levels& levels() const {
        return levels_;
    }

    void addOrder(Order* order) {
        auto it = levels_.find(order->price);
        if (it == levels_.end()) {
            LevelT level(order->price, order->quantity, 1);
            auto [new_it, inserted] = levels_.emplace(order->price, std::move(level));
            it = new_it;
        }
        it->second.addOrder(order);
    }

    void removeOrder(Order* order) {
        auto it = levels_.find(order->price);
        if (it != levels_.end()) {
            it->second.removeOrder(order);
            if (it->second.size_ == 0) {
                levels_.erase(it);
            }
        }
    }

    void modifyOrder(Order* order, qty_t old_qty) {
        auto it = levels_.find(order->price);
        if (it != levels_.end()) {
            it->second.modifyOrder(order, old_qty);
        }
        else {
            it->second.addOrder(order);
        }
    }

private:
    Levels levels_;
};

class OrderBook {
public:
    OrderBook(std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
        : symbol_(std::move(symbol))
        , venue_(venue)
        , order_buffer_(buffer_size)
    {}
    virtual ~OrderBook() = default;

    Order* allocateOrder();
    Order* getOrderByClientOrderId(std::string_view client_order_id);

    const std::string& symbol() const noexcept {
        return symbol_;
    }

    void setSid(symid_t sid) noexcept {
        sid_ = sid;
    }

    symid_t sid() const noexcept {
        return sid_;
    }

    virtual void clear() = 0;
    virtual uint32_t bidLevelsCount() const = 0;
    virtual uint32_t askLevelsCount() const = 0;
    virtual OrderBookLevel* bidLevel(uint32_t index) = 0;
    virtual OrderBookLevel* askLevel(uint32_t index) = 0;

    virtual void populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) = 0;
    virtual void populateMDBookUpdate(MDBookUpdate &book_update) = 0;
    virtual std::tuple<int, qty_t, uint32_t> onLevelUpdate(
        Side side, price_t price, qty_t size, uint32_t num_orders,
        std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
    ) = 0;

    void addOrder(Order* order) {
        order->priority = ++next_priority_;
        orders_.emplace(order->id, order);
        if (!order->client_order_id.empty()) {
            orders_by_client_order_id_.emplace(order->client_order_id, order);
        }
        addOrderInternal(order);
    }
    void removeOrder(Order* order) {
        removeOrderInternal(order);
        orders_.erase(order->id);
        if (!order->client_order_id.empty()) {
            orders_by_client_order_id_.erase(order->client_order_id);
        }
    }
    virtual void modifyOrder(Order* order, price_t new_price, qty_t new_qty) {
        auto it = orders_.find(order->id);
        if (it == orders_.end()) [[unlikely]] {
            order->price = new_price;
            order->quantity = new_qty;
            order->priority = ++next_priority_;
            addOrderInternal(order);
            return;
        }
        if (new_price != order->price) {
            removeOrderInternal(order);
            order->price = new_price;
            order->quantity = new_qty;
            order->priority = ++next_priority_;
            addOrderInternal(order);
        }
        else {
            qty_t old_size = order->quantity;
            order->quantity = new_qty;
            modifyOrderInternal(order, old_size);
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
    virtual void addOrderInternal(Order* order) = 0;
    virtual void removeOrderInternal(Order* order) = 0;
    virtual void modifyOrderInternal(Order* order, qty_t old_qty) = 0;

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
};

template<typename LevelT = OrderBookLevel>
class OrderBookImpl : public OrderBook {
public:
    OrderBookImpl(std::string symbol, Venue venue, uint_fast32_t buffer_size = 65536)
        : OrderBook(std::move(symbol), venue, buffer_size)
    {}
    ~OrderBookImpl() override = default;

    OrderBookSide<LevelT>& getSide(Side side) {
        return sides_[side];
    }

    void clear() override {
        sides_[0] = OrderBookSide<LevelT>(Side::BUY);
        sides_[1] = OrderBookSide<LevelT>(Side::SELL);
        for (auto& [id, order] : orders_) {
            auto &book_side = getSide(order->side);
            book_side.addOrder(order);
        }
    }

    template<Side SIDE>
    uint32_t getLevelCount() const {
        return sides_[SIDE].levels().size();
    } 

    uint32_t bidLevelsCount() const override {
        return getLevelCount<Side::BUY>();
    }

    uint32_t askLevelsCount() const override {
        return getLevelCount<Side::SELL>();
    }

    template<Side SIDE>
    OrderBookLevel* getLevel(uint32_t index) {
        auto &levels = sides_[SIDE].levels();
        if (index <  levels.size()) {
            auto it = levels.begin();
            std::advance(it, index);
            return &it->second;
        }
        return nullptr;
    }

    OrderBookLevel* bidLevel(uint32_t index) override {
        return getLevel<Side::BUY>(index);
    }

    OrderBookLevel* askLevel(uint32_t index) override {
        return getLevel<Side::SELL>(index);
    }

    OrderBookSide<LevelT>& getSide(Side side) const {
        return sides_[side];
    }

    void populateMDBookUpdate(MDBookUpdate &book_update) override;
    void populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) override;

    void modifyOrder(Order* order, price_t new_price, qty_t new_qty) override {}

    std::tuple<int, qty_t, uint32_t> onLevelUpdate(
        Side side, price_t price, qty_t size, uint32_t num_orders,
        std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
    ) override;

    std::string to_string() const override;

protected:
    void addOrderInternal(Order* order) override {
        auto& book_side = getSide(order->side);
        book_side.addOrder(order);
    }

    void removeOrderInternal(Order* order) override {
        auto& book_side = getSide(order->side);
        book_side.removeOrder(order);
    }

    void modifyOrderInternal(Order* order, qty_t old_qty) override {
        auto& book_side = getSide(order->side);
        book_side.modifyOrder(order, old_qty);
    }

protected:
    std::array<OrderBookSide<LevelT>, 2> sides_ = { OrderBookSide<LevelT>(Side::BUY), OrderBookSide<LevelT>(Side::SELL) };
};



inline uint64_t OrderBook::next_order_id_ = 0;
inline uint64_t OrderBook::next_priority_ = 0;

inline Order* OrderBook::allocateOrder() {
    auto *order = order_buffer_.allocate();
    order->id = ++next_order_id_;
    return order;
}

inline Order* OrderBook::getOrderByClientOrderId(std::string_view client_order_id) {
    auto it = orders_by_client_order_id_.find(std::string(client_order_id));
    if (it == orders_by_client_order_id_.end()) {
        return nullptr;
    }
    return it->second;
}


template<typename LevelT>
inline std::tuple<int, qty_t, uint32_t> OrderBookImpl<LevelT>::onLevelUpdate(
    Side side, price_t price, qty_t size, uint32_t num_orders,
    std::vector<MDLevel>& level_updates, std::vector<MDTrade>& trade_updates
) {
    auto& order_book_side = getSide(side);
    auto& other_side = getSide(opposite_side(side));
    // TODO: match other side levels
    if (side == Side::BUY && !other_side.levels().empty() && price >= other_side.levels().begin()->first) {
        // Match against sell levels
        LOG_WARN("Matching against opposite side levels. {} price={}, best_sell={}", symbol_, price, other_side.levels().begin()->first);
    }
    else if (side == Side::SELL && !other_side.levels().empty() && price <= other_side.levels().begin()->first) {
        // Match against buy levels
        LOG_WARN("Matching against opposite side levels. {} price={}, best_buy={}", symbol_, price, other_side.levels().begin()->first);
    }
    return order_book_side.updateLevel(price, size, num_orders);
}

template<typename LevelT>
inline std::string OrderBookImpl<LevelT>::to_string() const {
    std::stringstream ss;
    ss << "OrderBook: " << symbol_ << "\n";
    auto &sell_levels = sides_[1].levels();
    auto it_sell = sell_levels.rend();
    std::advance(it_sell, -20);
    for (; it_sell !=  sell_levels.rend(); ++it_sell) {
        ss << std::right << std::setw(50) << std::format("{:.2f}", static_cast<double>(it_sell->first) / DOUBLE_MULTIPLIER);
        ss << std::right << std::setw(20) << std::format("{:.8f}", static_cast<double>(it_sell->second.size_) / DOUBLE_MULTIPLIER);
        ss << std::right << std::setw(10) << it_sell->second.num_orders_ << "\n";
    }

    auto &buy_levels = sides_[0].levels();
    uint32_t i = 0;
    for (auto it_buy = buy_levels.begin(); it_buy != buy_levels.end() && i < 20; ++it_buy, ++i) {
        ss << std::right << std::setw(10) << it_buy->second.num_orders_;
        ss << std::right << std::setw(20) << std::format("{:.8f}", static_cast<double>(it_buy->second.size_) / DOUBLE_MULTIPLIER);
        ss << std::right << std::setw(20) << std::format("{:.2f}", static_cast<double>(it_buy->first) / DOUBLE_MULTIPLIER) << "\n";
    }
    return ss.str();
}

template<typename LevelT>
inline void OrderBookImpl<LevelT>::populateMDBookUpdate(MDBookUpdate &book_update) {
    auto &ask_levels = sides_[Side::SELL].levels();
    uint32_t i = 0;
    for (auto it = ask_levels.begin(); it != ask_levels.end() && i < 10; ++it, ++i) {
        auto &levels = book_update.sell;
        levels[i].price = it->first;
        levels[i].qty = it->second.size_;
        levels[i].num_orders = it->second.num_orders_;
    }
    i = 0;
    auto &bid_levels = sides_[Side::BUY].levels();
    for (auto it = bid_levels.begin(); it != bid_levels.end() && i < 10; ++it, ++i) {
        auto &levels = book_update.buy;
        levels[i].price = it->first;
        levels[i].qty = it->second.size_;
        levels[i].num_orders = it->second.num_orders_;
    }
}

template<typename LevelT>
inline void OrderBookImpl<LevelT>::populateL2SubscriptionResponse(slick::SlickQueue<uint8_t> &md_update_queue, uint8_t channel) {
    auto &bids = sides_[Side::BUY].levels();
    auto &asks = sides_[Side::SELL].levels();
    auto sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(BookSnapshot) + (bids.size() + asks.size()) * sizeof(MDLevel);
    auto index = md_update_queue.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue[index]);
    memcpy(update->symbol, symbol_.c_str(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    response->channel = channel;

    auto *snapshot = reinterpret_cast<BookSnapshot*>(response->data);
    snapshot->num_bid = bids.size();
    snapshot->num_ask = asks.size();

    auto *levels = reinterpret_cast<MDLevel*>(snapshot->levels);

    for (auto it = bids.begin(); it != bids.end(); ++it, ++levels) {
        levels->price = it->first;
        levels->qty = it->second.size_;
        levels->num_orders = it->second.num_orders_;
    }
    for (auto it = asks.begin(); it != asks.end(); ++it, ++levels) {
        levels->price = it->first;
        levels->qty = it->second.size_;
        levels->num_orders = it->second.num_orders_;
    }
    md_update_queue.publish(index, sz);
}


}   // end namespace slick::sim::order_book