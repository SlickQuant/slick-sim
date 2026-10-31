#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace slick::sim::order_gateway {

/// The application messages a binary session has sent, kept by sequence number
/// so a client can ask for them again. Fixed slots, indexed by `seq % capacity`:
/// a message is encoded straight into its slot, and the oldest is overwritten
/// once `capacity` newer ones have been sent. Only construction and resize() allocate.
///
/// Sequence numbers start at 1; 0 marks an empty slot.
template <size_t MessageCapacity>
class RetransmitRing {
public:
    struct Slot {
        uint32_t seq = 0;
        uint16_t size = 0;
        /// The byte a replay sets on its copy of the message: iLink 3's
        /// PossRetransFlag, or ICE BOE's header flags byte.
        uint16_t replay_flag_offset = 0;
        std::array<uint8_t, MessageCapacity> data{};
    };

    /// Empty, with no slots: holds nothing until resized.
    RetransmitRing() = default;
    explicit RetransmitRing(size_t capacity) : slots_(std::max<size_t>(capacity, 1)) {}

    /// Claims `seq`'s slot and returns it for the message to be encoded into.
    Slot &claim(uint32_t seq) noexcept {
        // A ring that was never resized has no slot to claim; find() answers nullptr
        // for it, and claiming into it would be a modulo by zero.
        assert(!slots_.empty());
        auto &slot = slots_[seq % slots_.size()];
        slot.seq = seq;
        return slot;
    }

    /// The message sent as `seq`, or null when it never was or has been overwritten.
    [[nodiscard]] const Slot *find(uint32_t seq) const noexcept {
        if (seq == 0 || slots_.empty()) {
            return nullptr;
        }
        const auto &slot = slots_[seq % slots_.size()];
        return slot.seq == seq ? &slot : nullptr;
    }

    /// True when every message in [from, from + count) is still held.
    ///
    /// Counted by offset rather than by `from + count`, so a range that runs over the
    /// end of the 32-bit sequence space neither wraps the bound nor ends the loop early.
    [[nodiscard]] bool holds(uint32_t from, uint32_t count) const noexcept {
        for (uint64_t i = 0; i < count; ++i) {
            if (!find(static_cast<uint32_t>(from + i))) {
                return false;
            }
        }
        return true;
    }

    /// Forgets every message, keeping the slots.
    void clear() noexcept {
        for (auto &slot : slots_) {
            slot.seq = 0;
        }
    }

    void resize(size_t capacity) { slots_.resize(std::max<size_t>(capacity, 1)); }
    void swap(RetransmitRing &other) noexcept { slots_.swap(other.slots_); }

    [[nodiscard]] bool empty() const noexcept { return slots_.empty(); }
    [[nodiscard]] size_t capacity() const noexcept { return slots_.size(); }

private:
    std::vector<Slot> slots_;
};

}   // namespace slick::sim::order_gateway
