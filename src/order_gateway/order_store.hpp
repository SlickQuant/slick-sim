#pragma once

#include <common/messages.hpp>
#include <common/order.hpp>
#include <slick/queue.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace slick::sim::order_gateway {

/// A gateway-side view of the orders the simulator has reported on.
///
/// A gateway cannot answer "list this user's orders" by looking at the order books:
/// those belong to the exchange thread, and reading them from a gateway's event loop
/// would race it. What every gateway does have is the execution-report stream, and
/// each `OrderResponse` carries the order's whole identity plus its state at that
/// moment - so replaying the stream in order reconstructs the current state of every
/// order without touching anything the exchange owns.
///
/// One instance per gateway. `slick::queue` gives each reader an independent cursor,
/// so draining here does not disturb the request handlers polling the same queue for
/// their own responses.
///
/// Deliberately not thread safe: a gateway drains and reads this only on its own
/// uWebSockets loop thread, which is the same rule the rest of the gateway follows.
class OrderStore {
public:
    /// Projection only. For a gateway that already drains the response stream for
    /// its own purposes and can feed apply() from that same read - taking a second
    /// cursor would mean reading every response twice to no benefit.
    OrderStore() = default;

    /// Self-draining, with its own cursor. For a gateway that has no continuous
    /// reader of its own and needs to catch up on demand.
    explicit OrderStore(slick::queue<OrderResponse> &response_queue)
        : response_queue_(&response_queue)
        , cursor_(response_queue.initial_reading_index()) {}

    /// Applies every response published since the last call. A no-op on a store
    /// constructed without a queue, whose owner feeds it through apply() instead.
    ///
    /// Bounded per call so a caller on the event loop cannot be stalled indefinitely
    /// by a backlog; whatever is left is picked up by the next drain.
    void drain(int max_batch = 4096) {
        if (response_queue_ == nullptr) {
            return;
        }
        for (int i = 0; i < max_batch; ++i) {
            auto [data, size] = response_queue_->read(cursor_);
            if (data == nullptr) {
                break;
            }
            apply(*data);
        }
    }

    /// The orders belonging to one user, most recently created first.
    std::vector<const Order*> orders_of(std::string_view user_id) const {
        std::vector<const Order*> result;
        auto it = ids_by_user_.find(std::string(user_id));
        if (it == ids_by_user_.end()) {
            return result;
        }
        result.reserve(it->second.size());
        // ids_by_user_ is append-ordered, so walking it backwards yields newest
        // first without having to sort on a timestamp that ties across a batch.
        for (auto id = it->second.rbegin(); id != it->second.rend(); ++id) {
            auto order = by_order_id_.find(*id);
            if (order != by_order_id_.end()) {
                result.push_back(&order->second);
            }
        }
        return result;
    }

    const Order* find(std::string_view order_id) const {
        auto it = by_order_id_.find(std::string(order_id));
        return it == by_order_id_.end() ? nullptr : &it->second;
    }

    size_t size() const noexcept { return by_order_id_.size(); }

    /// Folds one execution report into the order it describes, creating it on first
    /// sight. Public so tests can drive it without standing up a queue.
    void apply(const OrderResponse &response) {
        std::string order_id(response.order_id,
                             ::strnlen(response.order_id, sizeof(response.order_id)));
        if (order_id.empty()) {
            return;
        }

        auto [it, inserted] = by_order_id_.try_emplace(order_id);
        Order &order = it->second;

        if (inserted) {
            // Identity and creation are fixed at first sight; every later report for
            // this order repeats them, and PENDING_NEW is always the first.
            order.order_id = order_id;
            order.symbol = response.symbol;
            order.user_id = response.user_id;
            order.client_order_id = response.client_order_id;
            order.created_time = response.creation_time;
            order.product_type = ProductType::SPOT;
            ids_by_user_[order.user_id.str()].push_back(order_id);
        }

        order.side = response.side;
        order.type = response.order_type;
        order.time_in_force = response.time_in_force;
        order.status = response.order_status;
        order.post_only = response.post_only;
        order.price = response.price;
        order.quantity = response.qty;
        order.leaves_quantity = response.leaves_qty;
        order.cum_quantity = response.cum_qty;
        order.avg_fill_price = response.avg_fill_price;
        order.last_update_time = response.timestamp;
        order.pending_cancel = response.order_status == OrderStatus::PENDING_CANCEL;

        if (response.exec_type == ExecType::TRADE) {
            // num_fills is counted here rather than read off the report, because the
            // report has no such field - the fill count is a property of the stream.
            // Each response is consumed once, so this cannot double count.
            order.last_fill_price = response.last_fill_price;
            order.last_fill_qty = response.last_qty;
            order.last_fill_time = response.timestamp;
            ++order.num_fills;
        }

        order.filled_value = to_price_double(response.avg_fill_price)
                           * to_qty_double(response.cum_qty);

        if (response.order_status == OrderStatus::REJECTED) {
            order.reject_message = response.error_message;
        } else if (response.order_status == OrderStatus::CANCELED) {
            order.cancel_message = response.error_message;
        }
    }

private:
    slick::queue<OrderResponse> *response_queue_ = nullptr;
    uint64_t cursor_ = 0;

    std::unordered_map<std::string, Order> by_order_id_;
    /// Append-ordered per user, so listings keep a stable, meaningful sequence.
    std::unordered_map<std::string, std::vector<std::string>> ids_by_user_;
};

}   // end namespace slick::sim::order_gateway
