#include "symbol.hpp"
#include <slick/logger.hpp>

using namespace slick::sim;
using namespace slick::sim::exch;

void OrderBookObserver::onPriceLevelUpdate(const PriceLevelUpdate& update) {
    symbol_->onPriceLevelUpdate(update);
}

void OrderBookObserver::onOrderUpdate(const OrderUpdate& update) {
    symbol_->onOrderUpdate(update);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::addOrder(Order* order, time_t request_time)
{
    LOG_INFO("{} Adding order {}: oid={}, client_oid={}, price={}, qty={}, tif={}",
        symbol_, order->id, order->order_id, order->client_order_id,
        to_price_double(order->price), to_qty_double(order->quantity), to_string(order->time_in_force));

    // Pass Symbol's SMP mode to matching engine
    auto [reject_reason, trade_summaries] = matching_engine_->match(
        order, order->price, order->quantity, *order_book_.get(),
        request_time, order->created_time, 0, smp_mode_
    );

    // Only add to book if no rejection and remaining quantity exists
    bool resting = false;
    if (reject_reason == OrdRejectReason::NONE && order->leaves_quantity > 0) {
        // Check if TimeInForce allows resting on book
        bool should_rest = (
            order->time_in_force == TimeInForce::DAY ||
            order->time_in_force == TimeInForce::GOOD_TILL_CANCEL ||
            order->time_in_force == TimeInForce::GOOD_TILL_DATE
        );

        if (should_rest) {
            order_book_->addOrder(order, order->created_time);
            resting = true;
        } else {
            // IOC: Remainder is implicitly cancelled (not added to book)
            // FOK: Will never reach here (would have been rejected in match)
            LOG_INFO("{} Order {} has TIF={}, not adding to book (leaves_qty={})",
                symbol_, order->id, static_cast<char>(order->time_in_force), order->leaves_quantity);
        }
    }

    if (!resting) {
        // The book is the only thing that takes ownership of an order, so anything
        // that did not rest has to go back to the pool here: fully filled orders,
        // rejects (SMP, FOK), and IOC remainders alike. Freeing only on
        // leaves_quantity == 0 leaked every order that was rejected or cancelled
        // with quantity outstanding, and a client repeating a rejected order would
        // drain the pool.
        order_book_->freeOrder(order);
    }

    return std::make_tuple(reject_reason, trade_summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::modifyOrder(Order* order, price_t new_price, qty_t new_qty, time_t request_time)
{
    LOG_INFO("{} Modifying order {}: oid={}, client_oid={}, new_price={}, new_qty={}", symbol_, order->id, order->order_id, order->client_order_id, new_price, new_qty);
    
    // An amendment re-enters the matching loop, so it needs the same SMP mode a
    // new order gets — otherwise a symbol configured for SMP would still let an
    // amended order trade against its owner's resting orders.
    auto [reject_reason, trade_summaries] = matching_engine_->match(
        order, new_price, new_qty, *order_book_.get(), request_time, order->last_update_time, 0, smp_mode_);
    
    order->last_update_time = utils::get_current_time_ns();
    if (reject_reason == OrdRejectReason::NONE) {
        order_book_->modifyOrder(order, new_price, order->leaves_quantity, order->last_update_time, 0, true);
    }

    if (order->leaves_quantity == 0) {
        order_book_->freeOrder(order);
    }
    
    return std::make_tuple(reject_reason, trade_summaries);
}

void Symbol::cancelOrder(Order *order, time_t request_time)
{
    LOG_INFO("{} Canceling order: oid={}, client_oid={}", symbol_, order->order_id, order->client_order_id);
    order->last_update_time = utils::get_current_time_ns();
    order_book_->deleteOrder(order, order->last_update_time);
    matching_engine_->publishOrderCancel(order, request_time);
    order_book_->freeOrder(order);
}

void Symbol::onPriceLevelUpdate(const PriceLevelUpdate& update) {
    md_level_update_cache_.push_back({
        .event_time = update.timestamp,
        .seq_num = update.seq_num,
        .price = update.price,
        .qty = update.quantity,
        .num_orders = update.num_orders,
        .level_index = update.level_index,
        .flags = (update.change_flags & slick::orderbook::ChangeFlag::LastInBatch) ? UpdateFlags::F_END_EVENT : UpdateFlags::F_NONE,
        .side = static_cast<Side>(update.side)
    });
}

void Symbol::onOrderUpdate(const OrderUpdate& update) {
    md_order_update_cache_.push_back({
        .order_id = update.order_id,
        .event_time = update.timestamp,
        .priority = update.priority,
        .price = update.price,
        .qty = update.quantity,
        .side = static_cast<Side>(update.side)
    });
}