#pragma once

#include <common/market_data.hpp>
#include <common/order.hpp>
#include <common/types.hpp>
#include <cstdint>

namespace slick::sim::exch {

/// One of the venue's own orders entering, changing or leaving the book, from an
/// order-by-order feed, in the simulator's units. Applied with
/// Exchange::applyMarketOrder, which rests it as a phantom order beside the clients'.
struct MarketOrderEvent {
    /// The simulator's id for this order, from utils::nextOrderId(), so it can never
    /// collide with a client order. The venue's own order id is not used in the book.
    uint64_t book_order_id = 0;
    /// The order's place in its level's queue: drawn from
    /// OrderBook::nextOrderPriority() when it joined, and kept until it requeues.
    /// An order a client took entirely rests again under it, back where it was.
    /// Never 0: OrderBookL3 reads a priority of 0 as "use the timestamp" and would
    /// sort the order against the clients' sequence-numbered priorities.
    uint64_t book_priority = 0;
    /// The price it rests at. A CHANGE or NEW must carry a real one: NULL_PRICE would
    /// rest the phantom at the top of the book. A DELETE may omit it.
    price_t price = 0;
    uint64_t transact_time = 0;
    /// Remaining visible quantity, 0 on DELETE. A CHANGE carries the venue's
    /// quantity, and applying it always restores the book to it. It can equal what
    /// the venue reported before while the book holds less, because a client order
    /// took some.
    qty_t qty = 0;
    Side side = Side::BUY;
    MDUpdateAction action = MDUpdateAction::ACTION_NEW;
    /// On CHANGE: the order lost its place, and `book_priority` is a new one.
    bool requeue = false;
};

/// One trade the venue reports, with the side that crossed the spread. Replayed
/// with Exchange::replayMarketTrade as an aggressor order, so a client order
/// queued ahead of the market orders it filled is filled first.
struct MarketTradeEvent {
    price_t price = 0;
    qty_t qty = 0;
    Side aggressor = Side::BUY;
    uint64_t transact_time = 0;
};

}   // namespace slick::sim::exch
