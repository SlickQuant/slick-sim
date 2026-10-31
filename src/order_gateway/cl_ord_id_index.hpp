#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace slick::sim::order_gateway {

/// One session's ClOrdIDs, each mapped to the working order that holds it. A new
/// order or replace claims its ClOrdID when it is sent, so no other request can take
/// it before the exchange answers. Replaces of one order may share a claim with each
/// other and with that order: a ClOrdID it is renamed away from stays taken while one
/// of them still needs it. Each claim is released once, when its request is answered.
/// Order number 0 means none.
template <typename Key, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>>
class ClOrdIdIndex {
public:
    /// The working order named `id`, or 0 when no order holds it (a request may).
    template <typename K>
    [[nodiscard]] uint64_t find(const K &id) const noexcept {
        const auto it = ids_.find(id);
        return it == ids_.end() || !it->second.held ? 0 : it->second.order;
    }

    /// Claims `id` for a new order (`order_num` 0) or for a replace of `order_num`.
    /// False when another order, or a request for one, has it.
    [[nodiscard]] bool claim(const Key &id, uint64_t order_num = 0) {
        const auto [it, inserted] = ids_.try_emplace(id, Entry{order_num});
        if (!inserted && (order_num == 0 || it->second.order != order_num)) {
            return false;
        }
        ++it->second.claims;
        return true;
    }

    /// The same, by the key's lookup form. Building the key costs less than the second
    /// hash a find-then-insert would take, and a new claim stores it anyway.
    template <typename K>
    [[nodiscard]] bool claim(const K &id, uint64_t order_num = 0) {
        return claim(Key(id), order_num);
    }

    /// The request that claimed `id` was answered.
    template <typename K>
    void release(const K &id) noexcept {
        if (const auto it = ids_.find(id); it != ids_.end() && it->second.claims > 0) {
            --it->second.claims;
            erase_if_unused(it);
        }
    }

    /// `id` now names `order_num`.
    template <typename K>
    void assign(const K &id, uint64_t order_num) {
        assert(order_num != 0);
        auto &entry = ids_[Key(id)];
        // Assigning an id another order holds would leave that order unfindable: the
        // caller must `forget` it first, as `rename` does.
        assert(!entry.held || entry.order == order_num);
        entry.order = order_num;
        entry.held = true;
    }

    /// `order_num` no longer holds `id`: it is done, or was renamed.
    template <typename K>
    void forget(const K &id, uint64_t order_num) noexcept {
        if (const auto it = ids_.find(id); it != ids_.end() && it->second.order == order_num) {
            it->second.held = false;
            erase_if_unused(it);
        }
    }

    /// A replace renamed `order_num` from `from` to `to`.
    template <typename K>
    void rename(const K &from, const K &to, uint64_t order_num) {
        forget(from, order_num);
        assign(to, order_num);
    }

    /// How many ids are taken - by a working order, by a request in flight, or both.
    [[nodiscard]] size_t size() const noexcept { return ids_.size(); }
    [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }

private:
    struct Entry {
        uint64_t order = 0;    ///< the order that holds it, or that the claims are for
        uint32_t claims = 0;   ///< requests in flight that named it
        bool held = false;     ///< `order` holds it now
    };
    using Map = std::unordered_map<Key, Entry, Hash, Eq>;

    void erase_if_unused(typename Map::iterator it) noexcept {
        if (it->second.claims == 0 && !it->second.held) {
            ids_.erase(it);
        }
    }

    Map ids_;
};

}   // namespace slick::sim::order_gateway
