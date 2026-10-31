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
        symbol_.view(), order->id, order->order_id.view(), order->client_order_id.view(),
        to_price_double(order->price), to_qty_double(order->quantity), to_string(order->time_in_force));

    // Pass Symbol's SMP mode to matching engine
    auto [reject_reason, trade_summaries] = matching_engine_->match(
        order, order->price, order->quantity, *order_book_.get(),
        request_time, order->created_time, 0, smp_mode_
    );

    // Only add to book if no rejection and remaining quantity exists
    bool resting = false;
    if (reject_reason == OrdRejectReason::NONE && order->leaves_quantity > 0) [[likely]] {
        if (order->restsRemainder()) [[likely]] {
            order_book_->addOrder(order, order->created_time);
            resting = true;
        } else {
            // The engine has already cancelled the remainder: an IOC, or a market order.
            LOG_INFO("{} Order {} cannot rest (TIF={}, market={}), not adding to book (leaves_qty={})",
                symbol_.view(), order->id, static_cast<char>(order->time_in_force), order->price == NULL_PRICE,
                order->leaves_quantity);
        }
    }

    if (!resting) [[unlikely]] {
        // Return every order that did not rest to the pool: fully filled orders,
        // SMP/FOK rejects and IOC remainders. The book only owns resting orders;
        // checking leaves_quantity == 0 alone would leak orders with quantity left.
        order_book_->freeOrder(order);
    }

    return std::make_tuple(reject_reason, trade_summaries);
}

std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> Symbol::modifyOrder(Order* order, price_t new_price, qty_t new_qty, time_t request_time)
{
    LOG_INFO("{} Modifying order {}: oid={}, client_oid={}, new_price={}, new_qty={}", symbol_.view(), order->id, order->order_id.view(), order->client_order_id.view(), new_price, new_qty);
    
    // A new price or a larger quantity loses the order its place, as on CME and ICE.
    // Read now: the match writes the amendment onto the order.
    const bool requeue = new_price != order->price || new_qty > order->quantity;

    // An amendment re-enters the matching loop, so it needs the same SMP mode a
    // new order gets — otherwise a symbol configured for SMP would still let an
    // amended order trade against its owner's resting orders.
    auto [reject_reason, trade_summaries] = matching_engine_->match(
        order, new_price, new_qty, *order_book_.get(), request_time, order->last_update_time, 0, smp_mode_);

    order->last_update_time = utils::get_current_time_ns();
    if (reject_reason == OrdRejectReason::NONE) {
        if (requeue) {
            order->priority = OrderBook::nextOrderPriority();
        }
        // The book stores this as the order's total and rests its leaves. Passing the
        // leaves made a partly filled order's total shrink with every amendment.
        order_book_->modifyOrder(order, new_price, order->quantity, order->last_update_time, 0, true);
    }

    if (order->leaves_quantity == 0) {
        order_book_->freeOrder(order);
    }
    
    return std::make_tuple(reject_reason, trade_summaries);
}

void Symbol::cancelOrder(Order *order, time_t request_time)
{
    LOG_INFO("{} Canceling order: oid={}, client_oid={}", symbol_.view(), order->order_id.view(), order->client_order_id.view());
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
        .update_action = MDUpdateAction::ACTION_NEW,  // unread on LEVEL frames, qty 0 means delete
        .flags = (update.change_flags & slick::orderbook::ChangeFlag::LastInBatch) ? UpdateFlags::F_END_EVENT : UpdateFlags::F_NONE,
        .side = static_cast<Side>(update.side)
    });
}

void Symbol::onOrderUpdate(const OrderUpdate& update) {
    // slick-orderbook reports an add with no previous state at all, and a removal
    // with zero quantity; anything else is an existing order changing.
    MDUpdateAction action = MDUpdateAction::ACTION_CHANGE;
    if (update.isDelete()) {
        action = MDUpdateAction::ACTION_DELETE;
    } else if (update.old_qty == 0 && update.old_price == 0) {
        action = MDUpdateAction::ACTION_NEW;
    }

    md_order_update_cache_.push_back({
        .order_id = update.order_id,
        .event_time = update.timestamp,
        .priority = update.priority,
        .price = update.price,
        .qty = update.quantity,
        .side = static_cast<Side>(update.side),
        .update_action = action
    });
}