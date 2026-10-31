#pragma once

#include <nlohmann/json.hpp>
#include <slick/logger.hpp>
#include <slick/queue.hpp>
#include <slick/socket/multicast_receiver.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace slick::sim::md_feed {

/// Counts the queue bytes a receiver overwrote before the exchange thread drained them.
///
/// slick::queue only counts that in a debug build (debug_queue_traits), so ask for it
/// explicitly rather than have a release build report the same loss as nothing. Costs
/// one cacheline, and one relaxed fetch_add each time the exchange thread finds it
/// was lapped.
struct packet_feed_traits : slick::queue_traits {
    static constexpr bool enable_loss_detection = true;
};

/// The transport half of a live multicast feed: one receiver thread per joined
/// group ("line"), all handing their packets to the exchange thread through one
/// lock-free queue. Nothing here takes a lock. Only the exchange thread touches the
/// book, as it drains the queue into the venue's protocol handler.
///
/// A line is a small integer below MAX_LINES that the venue chooses (its enum).
/// The venue can mute a line it has no use for, such as a snapshot loop while the
/// book is live, and its packets are then dropped on arrival. The receiver stays
/// joined, because leaving and rejoining a group takes longer than a gap may last.
///
/// Header-only, so the md_feed target stays free of slick-socket: a venue that
/// includes this links slick::socket itself.
///
/// Config keys, read from the feed's config entry: `queue_size` (bytes, default
/// 16 MiB), `max_packets_per_poll` (256), `interface` and `receive_buffer_size`
/// for every receiver. Each line's group is `{ "address", "port" }` under its key.
///
/// The queue is fixed and lossy, like every slick::queue: should the exchange thread
/// fall far enough behind, a receiver overwrites packets nobody has drained, and
/// loss_count() counts their bytes. The venue's own sequence numbers are what turn that into
/// a gap it can recover from. A packet is also held back from the queue until the
/// receiver that reserved its slot has copied it in, so one stalled copy delays the
/// packets behind it - never reorders them.
class MulticastPacketFeed {
public:
    static constexpr uint8_t MAX_LINES = 32;

    MulticastPacketFeed(std::string name, const nlohmann::json &config)
        : name_(std::move(name))
        , config_(config)
        , packets_(config.value("queue_size", 1u << 24))
        , cursor_(packets_.initial_reading_index())
        , max_packets_per_poll_(config.value("max_packets_per_poll", 256u))
    {
        // drain() would otherwise hand over nothing, for ever, and the feed would sit
        // silent with every packet queued behind it. It is config, so check in every build.
        if (max_packets_per_poll_ == 0) {
            throw std::runtime_error(std::format("feed {}: max_packets_per_poll must be above 0", name_));
        }
    }

    ~MulticastPacketFeed() { stop(); }

    MulticastPacketFeed(const MulticastPacketFeed &) = delete;
    MulticastPacketFeed &operator=(const MulticastPacketFeed &) = delete;

    /// True when the config names a group for `key`.
    [[nodiscard]] bool has_group(const char *key) const { return config_.contains(key); }

    /// Joins the group under `key`, if the config names one, as `line`. Call before
    /// start(). Returns whether a receiver was added.
    bool add_line(const char *key, uint8_t line) {
        assert(!started_);
        if (!config_.contains(key) || line >= MAX_LINES) {
            return false;
        }
        const auto &group = config_[key];
        slick::socket::MulticastReceiverConfig receiver;
        receiver.multicast_address = group.at("address").get<std::string>();
        receiver.port = group.at("port").get<uint16_t>();
        receiver.interface_address = config_.value("interface", receiver.interface_address);
        // slick-socket sizes both the socket's receive buffer and the per-datagram
        // read buffer from this, so it must exceed the largest packet, as
        // slick-socket's own 64 KiB default does.
        receiver.receive_buffer_size = config_.value("receive_buffer_size", 1 << 22);
        receivers_.push_back(std::make_unique<Receiver>(std::format("{} {}", name_, key), receiver, *this, line));
        return true;
    }

    void start() {
        started_ = true;
        for (auto &receiver : receivers_) {
            if (!receiver->start()) {
                LOG_ERROR("feed {}: a multicast receiver failed to start", name_);
            }
        }
    }

    void stop() {
        for (auto &receiver : receivers_) {
            receiver->stop();
        }
        started_ = false;
    }

    /// Queues a packet as if `line`'s receiver had received it. Any thread. False when
    /// the packet is dropped: the line is muted, or the packet cannot fit the queue.
    ///
    /// The second case has to be refused here. slick::queue::reserve() throws above the
    /// queue's size, and this runs on a receiver thread, where an escaping exception is
    /// std::terminate - a `queue_size` smaller than one datagram would otherwise take
    /// the process down.
    bool deliver(uint8_t line, const uint8_t *data, size_t size) {
        assert(line < MAX_LINES);
        if (muted_.load(std::memory_order_relaxed) & (1u << line)) {
            return false;
        }
        const auto n = static_cast<uint32_t>(size + 1);
        if (n > packets_.size()) [[unlikely]] {
            // A queue too small for one packet is too small for all of them: log the
            // first, count the rest, rather than flood the log at the feed's rate.
            if (oversized_.fetch_add(1, std::memory_order_relaxed) == 0) {
                LOG_ERROR("feed {}: line {} packet of {} bytes exceeds the {} byte queue; dropping it and any like it",
                          name_, line, size, packets_.size());
            }
            return false;
        }
        const uint64_t index = packets_.reserve(n);
        uint8_t *entry = packets_[index];
        entry[0] = line;
        std::memcpy(entry + 1, data, size);
        packets_.publish(index, n);
        return true;
    }

    /// Drops `line`'s packets on arrival while muted. Any thread.
    void mute(uint8_t line, bool muted) noexcept {
        assert(line < MAX_LINES);
        const uint32_t bit = 1u << line;
        if (muted) {
            muted_.fetch_or(bit, std::memory_order_relaxed);
        } else {
            muted_.fetch_and(~bit, std::memory_order_relaxed);
        }
    }

    /// Exchange thread. Hands at most `max_packets_per_poll` packets to
    /// `on_packet(line, data, size)`, pointing into the queue's memory. The cap
    /// keeps a busy feed from starving client requests. Returns how many it handed
    /// over.
    template <typename OnPacket>
    uint32_t drain(OnPacket &&on_packet) {
        uint32_t handled = 0;
        for (; handled < max_packets_per_poll_; ++handled) {
            auto [entry, size] = packets_.read(cursor_);
            if (!entry) {
                break;
            }
            if (size < 1) {
                continue;
            }
            on_packet(entry[0], entry + 1, static_cast<size_t>(size - 1));
        }
        return handled;
    }

    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /// Queue bytes a receiver overwrote before this thread drained them, each lost packet
    /// counting its size plus the line byte. Non-zero means packets were lost, and the
    /// venue's sequence numbers are the only thing that can repair the gap.
    [[nodiscard]] uint64_t loss_count() const noexcept { return packets_.loss_count(); }

    /// Packets refused for being larger than the queue. Only the first is logged.
    [[nodiscard]] uint64_t oversized_count() const noexcept { return oversized_.load(std::memory_order_relaxed); }

private:
    class Receiver : public slick::socket::MulticastReceiverBase<Receiver> {
    public:
        Receiver(std::string name, const slick::socket::MulticastReceiverConfig &config, MulticastPacketFeed &feed,
                 uint8_t line)
            : MulticastReceiverBase<Receiver>(std::move(name), config)
            , feed_(feed)
            , line_(line)
        {}
        ~Receiver() { stop(); }

        void handle_multicast_data(const std::vector<uint8_t> &data, const std::string & /*sender*/) {
            feed_.deliver(line_, data.data(), data.size());
        }

    private:
        MulticastPacketFeed &feed_;
        uint8_t line_;
    };

    std::string name_;
    nlohmann::json config_;
    /// Each entry is one byte of line, then the packet as received.
    slick::queue<uint8_t, packet_feed_traits> packets_;
    uint64_t cursor_;
    uint32_t max_packets_per_poll_;
    std::atomic<uint32_t> muted_{0};
    std::atomic<uint64_t> oversized_{0};
    /// Whether a receiver thread may be running, so add_line knows to refuse.
    bool started_ = false;
    std::vector<std::unique_ptr<Receiver>> receivers_;
};

}   // namespace slick::sim::md_feed
