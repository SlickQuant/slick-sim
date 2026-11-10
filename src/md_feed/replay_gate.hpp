#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace slick::sim::md_feed {

/// Whether a pcap replay has been started. Closed while the book is built, so the
/// market-data publisher sends no market data; opened once, by the exchange thread, when a client
/// starts the replay, after which every instrument is published. Read from the
/// publisher's thread.
///
/// The publisher runs behind the exchange thread, so the frames written to md_queue while
/// the book was built may still be waiting when the gate opens. The gate therefore
/// records where the queue's write position was, and the publisher treats it as open only
/// for the frames from there on: what was written before describes the held book, and goes
/// out as the snapshot, not as updates.
class ReplayGate {
public:
    /// `position` returns the md_queue's write index (see slick::queue::initial_reading_index),
    /// called on the exchange thread when the gate opens. Without it the gate opens for
    /// every frame, which is right where nothing is queued.
    explicit ReplayGate(std::function<uint64_t()> position = {}) : position_(std::move(position)) {}

    /// Exchange thread, after its last write to md_queue for the held book. Opening an open
    /// gate does nothing: the publisher may be reading open_index_, so it is written once.
    void open() noexcept {
        if (open_.load(std::memory_order_relaxed)) {
            return;
        }
        open_index_ = position_ ? position_() : 0;
        open_.store(true, std::memory_order_release);
    }

    /// True once open, for a publisher that is about to read the md_queue frame at `index`.
    [[nodiscard]] bool is_open_at(uint64_t index) const noexcept {
        return open_.load(std::memory_order_acquire) && index >= open_index_;
    }

    [[nodiscard]] bool is_open() const noexcept { return open_.load(std::memory_order_acquire); }

private:
    std::function<uint64_t()> position_;
    uint64_t open_index_ = 0;   // Written before open_ is released.
    std::atomic<bool> open_{false};
};

/// A publisher channel's view of the gate: whether it may publish yet, for the md_queue frame
/// it is working on. Publisher thread only. Without a gate everything is published.
class ReplayGateView {
public:
    /// Call before the channel is used. A channel with a gate publishes nothing until it opens.
    void set(std::shared_ptr<ReplayGate> gate) {
        gate_ = std::move(gate);
        opened_ = false;
    }
    [[nodiscard]] bool gated() const noexcept { return gate_ != nullptr; }
    /// False while a gate is closed (or not yet open for the frame being read).
    [[nodiscard]] bool publishing() const noexcept { return !gate_ || opened_; }

    /// The md_queue index of the next frame the publisher thread will read.
    void set_position(uint64_t index) noexcept { position_ = index; }

    /// True once, when the gate has opened for the frames now being read: the moment
    /// the channel starts publishing what it held back.
    [[nodiscard]] bool opened_now() noexcept {
        if (!gate_ || opened_ || !gate_->is_open_at(position_)) {
            return false;
        }
        opened_ = true;
        return true;
    }

private:
    std::shared_ptr<ReplayGate> gate_;
    bool opened_ = false;
    uint64_t position_ = 0;
};

}   // namespace slick::sim::md_feed
