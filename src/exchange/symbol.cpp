#include "symbol.hpp"

using namespace slick::sim::exch;

void OrderBookObserver::onPriceLevelUpdate(const PriceLevelUpdate& update) override {
    symbol_->onPriceLevelUpdate(update);
}

void OrderBookObserver::onOrderUpdate(const OrderUpdate& update) override {
    symbol_->onOrderUpdate(update);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::addOrder(Order* order)
{
    LOG_INFO("{} Adding order {}: oid={}, client_oid={}, price={}, qty={}, tif={}",
        symbol_, order->id, order->order_id, order->client_order_id,
        order->price, order->quantity, static_cast<char>(order->time_in_force));

    // Pass Symbol's SMP mode to matching engine
    auto [reject_reason, trade_summaries] = matching_engine_->match(
        order, order->price, order->quantity, *order_book_.get(),
        order->created_time, 0, smp_mode_
    );

    // Only add to book if no rejection and remaining quantity exists
    if (reject_reason == OrdRejectReason::NONE && order->leaves_quantity > 0) {
        // Check if TimeInForce allows resting on book
        bool should_rest = (
            order->time_in_force == TimeInForce::DAY ||
            order->time_in_force == TimeInForce::GOOD_TILL_CANCEL ||
            order->time_in_force == TimeInForce::GOOD_TILL_DATE
        );

        if (should_rest) {
            order_book_->addOrder(order, order->created_time);
        } else {
            // IOC: Remainder is implicitly cancelled (not added to book)
            // FOK: Will never reach here (would have been rejected in match)
            LOG_INFO("{} Order {} has TIF={}, not adding to book (leaves_qty={})",
                symbol_, order->id, static_cast<char>(order->time_in_force), order->leaves_quantity);
        }
    }

    return std::make_tuple(reject_reason, trade_summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::modifyOrder(Order* order, price_t new_price, qty_t new_qty)
{
    LOG_INFO("{} Modifying order {}: oid={}, client_oid={}, new_price={}, new_qty={}", symbol_, order->id, order->order_id, order->client_order_id, new_price, new_qty);
    
    auto [reject_reason, trade_summaries] = matching_engine_->match(order, new_price, new_qty, *order_book_.get(), order->last_update_time);
    
    if (reject_reason == OrdRejectReason::NONE) {
        order_book_->modifyOrder(order, new_price, new_qty, order->last_update_time, 0, true);
    }
    
    order->last_update_time = std::chrono::system_clock::now().time_since_epoch().count();
    return std::make_tuple(reject_reason, trade_summaries);
}

void Symbol::cancelOrder(Order *order)
{
    LOG_INFO("{} Canceling order: oid={}, client_oid={}", symbol_, order->order_id, order->client_order_id);
    order->last_update_time = std::chrono::system_clock::now().time_since_epoch().count();
    order_book_->deleteOrder(order, order->last_update_time);
}

void Symbol::onPriceLevelUpdate(const PriceLevelUpdate& update) {
    // md_level_update_cache_.push_back({
    //     .event_time = update.timestamp,
    //     .seq_num = update.seq_num,
    //     .price = update.price,
    //     .quantity = update.quantity,
    //     .num_orders = update.num_orders,
    //     .flags = static_cast<>(update.flags),
    //     .side = static_cast<Side>(update.side)
    // });
}

void Symbol::onOrderUpdate(const OrderUpdate& update) {
    // md_order_update_cache_.push_back(update.order);
}