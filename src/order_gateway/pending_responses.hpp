#pragma once

#include <common/messages.hpp>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace slick::sim::order_gateway {

/// Correlates an in-flight HTTP request with the execution report that answers it.
///
/// The REST handlers used to poll `response_queue_` with a 100us sleep until their
/// own report arrived. That ran on the gateway's only event-loop thread, so one
/// order stalled every other client's request for up to the full timeout - and the
/// batch handlers polled once per entry, stalling for that long per order in the
/// batch. A handler now records what it is waiting for and returns immediately; the
/// gateway's drain completes the HTTP response when the report arrives, and the
/// request's own timer completes it as a timeout if it does not.
///
/// Waiters are matched by predicate rather than a keyed lookup because the handlers
/// correlate on different things: a create knows its `client_order_id`, a cancel
/// only the timestamp it stamped on its request. The list is bounded by the requests
/// actually in flight, so scanning it is cheaper than maintaining several indexes.
///
/// Single-threaded by construction - every method runs on the gateway's loop thread,
/// the same rule the rest of the gateway follows.
class PendingResponses {
public:
    using Id = uint64_t;
    /// Invoked with the matching report, or with nullptr when the wait timed out.
    /// Called exactly once per waiter unless the waiter is abandoned.
    using Completion = std::function<void(const OrderResponse*)>;
    using Predicate = std::function<bool(const OrderResponse&)>;

    static constexpr Id kNoId = 0;

    Id add(Predicate matches, Completion complete) {
        const Id id = ++next_id_;
        waiters_.push_back({id, std::move(matches), std::move(complete)});
        return id;
    }

    /// Offers a report to the waiters, completing at most one of them.
    ///
    /// A report answers a single request, so the first match wins and the rest keep
    /// waiting. Returns whether anything claimed it.
    bool dispatch(const OrderResponse &response) {
        for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
            if (!it->matches(response)) {
                continue;
            }
            // Lifted out and erased before running: a completion may register a new
            // waiter, which would reallocate the vector under us.
            Completion complete = std::move(it->complete);
            waiters_.erase(it);
            complete(&response);
            return true;
        }
        return false;
    }

    /// Completes a waiter as timed out. A no-op if its report already arrived, so a
    /// timer that fires after the fact is harmless.
    void expire(Id id) {
        auto it = find(id);
        if (it == waiters_.end()) {
            return;
        }
        Completion complete = std::move(it->complete);
        waiters_.erase(it);
        complete(nullptr);
    }

    /// Drops a waiter without completing it, for a client that went away. The
    /// completion is never run, because it would write to a dead connection.
    void abandon(Id id) {
        auto it = find(id);
        if (it != waiters_.end()) {
            waiters_.erase(it);
        }
    }

    bool waiting(Id id) const {
        for (const auto &waiter : waiters_) {
            if (waiter.id == id) {
                return true;
            }
        }
        return false;
    }

    size_t size() const noexcept { return waiters_.size(); }

private:
    struct Waiter {
        Id id;
        Predicate matches;
        Completion complete;
    };

    std::vector<Waiter>::iterator find(Id id) {
        for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
            if (it->id == id) {
                return it;
            }
        }
        return waiters_.end();
    }

    std::vector<Waiter> waiters_;
    Id next_id_ = kNoId;
};

}   // end namespace slick::sim::order_gateway
