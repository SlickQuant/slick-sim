#pragma once

// Before anything below can pull in windows.h (slick-socket does, through winsock2.h),
// or its min/max macros are already defined.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <nlohmann/json.hpp>
#include <slick/logger.hpp>
#include <slick/queue.hpp>
#include <slick/socket/polled_multicast_receiver.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

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

/// The transport half of a live multicast feed: one receiver thread that polls every
/// joined group ("line"), handing the packets to the exchange thread through one
/// lock-free queue. Nothing here takes a lock. Only the exchange thread touches the
/// book, as it drains the queue into the venue's protocol handler.
///
/// A line is a small integer below MAX_LINES that the venue chooses (its enum).
/// The venue can leave a line's group while it has no use for it, such as a
/// snapshot loop while the book is live, and rejoin it on a gap (set_joined).
///
/// Header-only, so the md_feed target stays free of slick-socket: a venue that
/// includes this links slick::socket itself.
///
/// Config keys, read from the feed's config entry: `queue_size` (bytes, default
/// 16 MiB), `max_packets_per_poll` (256), `interface` and `receive_buffer_size`
/// for every receiver. Each line's group is `{ "address", "port" }` under its key.
///
/// `receive_buffer_size` (bytes, default 64 MiB) is each socket's SO_RCVBUF: what the
/// kernel holds while the receiver thread is descheduled. The kernel drops a burst
/// beyond it, so size it for the stall, not for one packet. The read buffer is separate
/// and fixed (MAX_DATAGRAM). The kernel caps it at `net.core.rmem_max` (Linux), so raise
/// that too. `receiver_cpu` (default -1) pins the receiver thread to that core. It
/// busy-polls its sockets, so a pinned thread keeps the core at 100%; an unpinned one
/// sleeps RECEIVER_IDLE_SLEEP between empty sweeps instead. A pin that cannot be made
/// (no such core, outside the process's cpuset, an unsupported platform) is logged and
/// leaves the thread unpinned, so it sleeps too rather than spin on a core it does not own.
/// A `receiver_cpu` that is not a number throws from the constructor, like `queue_size`.
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
    /// How long a failed join or leave waits before set_joined() tries it again.
    static constexpr uint64_t MEMBERSHIP_RETRY_NS = 100'000'000;
    /// The receiver thread's read buffer: above any UDP payload a venue sends.
    static constexpr size_t MAX_DATAGRAM = 1u << 16;
    static constexpr std::chrono::microseconds RECEIVER_IDLE_SLEEP{50};

    MulticastPacketFeed(std::string name, const nlohmann::json &config)
        : name_(std::move(name))
        , config_(config)
        , packets_(config.value("queue_size", 1u << 24))
        , cursor_(packets_.initial_reading_index())
        , max_packets_per_poll_(config.value("max_packets_per_poll", 256u))
        , receiver_cpu_(config.value("receiver_cpu", -1))
    {
        // drain() would otherwise hand over nothing, for ever, and the feed would sit
        // silent with every packet queued behind it. It is config, so check in every build.
        if (max_packets_per_poll_ == 0) {
            throw std::runtime_error(std::format("feed {}: max_packets_per_poll must be above 0", name_));
        }
    }

    virtual ~MulticastPacketFeed() { stop(); }

    MulticastPacketFeed(const MulticastPacketFeed &) = delete;
    MulticastPacketFeed &operator=(const MulticastPacketFeed &) = delete;

    /// True when the config names a group for `key`.
    [[nodiscard]] bool has_group(const char *key) const { return config_.contains(key); }

    /// Joins the group under `key`, if the config names one, as `line`. Call before
    /// start(). Returns whether a receiver was added.
    bool add_line(const char *key, uint8_t line) {
        // The receiver thread walks lines_, so a line added under it would be a data race.
        if (started_ || !config_.contains(key) || line >= MAX_LINES) {
            return false;
        }
        const auto &group = config_[key];
        slick::socket::PolledMulticastReceiverConfig receiver;
        receiver.multicast_address = group.at("address").get<std::string>();
        receiver.port = group.at("port").get<uint16_t>();
        receiver.interface_address = config_.value("interface", receiver.interface_address);
        receiver.receive_buffer_size = config_.value("receive_buffer_size", 1 << 26);
        if (receivers_[line]) {
            return false;   // the line is taken
        }
        receivers_[line] = std::make_unique<slick::socket::PolledMulticastReceiver>(
            std::format("{} {}", name_, key), receiver);
        lines_.push_back(line);
        return true;
    }

    void start() {
        if (thread_.joinable()) {
            LOG_WARN("feed {}: already started", name_);   // as slick-socket's own receivers do
            return;
        }
        started_ = true;
        for (const uint8_t line : lines_) {
            if (!receivers_[line]->open()) {
                LOG_ERROR("feed {}: a multicast receiver failed to start", name_);
            }
        }
        // Starting joined every group. A line the venue still wants left stays
        // muted, and its next set_joined() leaves the group again.
        left_ = 0;
        failing_ = 0;
        retry_at_ns_.fill(0);
        for (auto &request : request_) {
            request.store(NO_REQUEST, std::memory_order_relaxed);
        }
        pending_op_.fill(NO_REQUEST);
        if (!lines_.empty()) {
            running_.store(true, std::memory_order_release);
            thread_ = std::thread([this] { receive_loop(); });
        }
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
        for (const uint8_t line : lines_) {
            receivers_[line]->close();
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

    /// Exchange thread, from poll(). Leaves or rejoins `line`'s group, so the network
    /// stops sending what nobody reads. A line not wanted is muted at once, which
    /// also drops what is already in the socket buffer. The membership counts as
    /// changed only once the socket call succeeds: a failed one is logged once and
    /// tried again every MEMBERSHIP_RETRY_NS until it does, so recovery never waits
    /// on a group that was not rejoined. The receiver thread makes the change itself,
    /// as its sockets are its own: this hands it over and returns, then finds the answer
    /// on a later call (see change_membership), so the exchange thread never waits for it.
    /// Two compares when nothing is pending, so it is cheap to call every poll.
    /// Before start(), or for a line with no group, only the mute changes.
    void set_joined(uint8_t line, bool joined, uint64_t now_ns) {
        assert(line < MAX_LINES);
        const uint32_t bit = 1u << line;
        const uint32_t want_left = joined ? 0 : bit;
        if ((wanted_left_ & bit) != want_left) {
            wanted_left_ ^= bit;
            // Mute before leaving and unmute before joining, so nothing that arrives
            // while joined is dropped.
            mute(line, !joined);
            failing_ &= ~bit;
            retry_at_ns_[line] = 0;
        }
        // A change still in flight has to be followed up even when the socket already is
        // as wanted: it may be the opposite one, to be withdrawn or its answer collected.
        if (((left_ & bit) == want_left && pending_op_[line] == NO_REQUEST) || now_ns < retry_at_ns_[line]) {
            return;
        }
        const int error = change_membership(line, joined);
        if (error == MEMBERSHIP_PENDING) {
            return;   // the receiver thread has not answered yet; ask again next poll
        }
        if (error != 0) {
            retry_at_ns_[line] = now_ns + MEMBERSHIP_RETRY_NS;
            if (!(failing_ & bit)) {
                failing_ |= bit;
                LOG_ERROR("feed {}: line {} could not {} its group, error {}; retrying every {} ms", name_, line,
                          joined ? "rejoin" : "leave", error, MEMBERSHIP_RETRY_NS / 1'000'000);
            }
            return;
        }
        left_ = (left_ & ~bit) | want_left;
        failing_ &= ~bit;
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

    /// Reads a socket reported as failed, across every line. Logged at most once a second
    /// per line, so a non-zero count that grows is a socket that is broken, not a quiet feed.
    [[nodiscard]] uint64_t receive_error_count() const noexcept {
        return receive_errors_.load(std::memory_order_relaxed);
    }

    /// Packets refused for being larger than the queue. Only the first is logged.
    [[nodiscard]] uint64_t oversized_count() const noexcept { return oversized_.load(std::memory_order_relaxed); }

protected:
    /// What change_membership() returns while the receiver thread has not yet answered.
    static constexpr int MEMBERSHIP_PENDING = -1;

    /// Joins or leaves `line`'s group. Returns 0 once the socket is as asked, an error,
    /// or MEMBERSHIP_PENDING: the change is under way and the same call is to be made
    /// again. Virtual so a test can make it fail; only called when the membership has to
    /// change, or a change handed to the receiver thread is still in flight.
    ///
    /// A socket is only ever touched by the receiver thread, so the change is handed to
    /// it, and each call looks for its answer without waiting, so the exchange thread
    /// cannot be held up by a receiver that is descheduled. A change that was handed over
    /// and is no longer wanted is withdrawn if the thread has not taken it yet, so it is
    /// never made behind the caller's back; one the thread has begun is waited out
    /// across calls, then followed by the one now wanted. EIO is a join the thread could
    /// not make; slick-socket logs the socket error behind it.
    virtual int change_membership(uint8_t line, bool join) {
        if (!receivers_[line] || !running_.load(std::memory_order_acquire)) {
            return 0;   // no thread running: nothing is open that could be changed
        }
        auto &request = request_[line];
        uint8_t &pending = pending_op_[line];   // what this thread last handed over; NO_REQUEST for none
        const uint8_t op = join ? REQUEST_JOIN : REQUEST_LEAVE;
        for (;;) {
            if (pending == NO_REQUEST) {
                pending = op;
                request.store(op, std::memory_order_release);
                return MEMBERSHIP_PENDING;
            }
            const uint8_t state = request.load(std::memory_order_acquire);
            if (state == REQUEST_DONE || state == REQUEST_FAILED) {
                request.store(NO_REQUEST, std::memory_order_relaxed);
                const uint8_t handed = std::exchange(pending, NO_REQUEST);
                // The socket has changed as it was handed, whatever is wanted now: record it
                // here, or a failed change after this one would be retried from a stale state.
                if (state == REQUEST_DONE) {
                    const uint32_t bit = 1u << line;
                    left_ = handed == REQUEST_LEAVE ? (left_ | bit) : (left_ & ~bit);
                }
                if (handed == op) {
                    return state == REQUEST_DONE ? 0 : EIO;
                }
                continue;   // the answer to the opposite change: now hand over the wanted one
            }
            if (state == pending && pending != op) {
                uint8_t untouched = pending;
                if (request.compare_exchange_strong(untouched, NO_REQUEST, std::memory_order_acq_rel)) {
                    // Never made, so the socket is still as it was before it: as `op` asks.
                    pending = NO_REQUEST;
                    return 0;
                }
                // The thread took it between the read and the withdrawal: wait for it.
            }
            return MEMBERSHIP_PENDING;
        }
    }

private:
    friend struct MulticastPacketFeedTestAccess;   // lets a test stall the receiver thread

    static constexpr uint8_t NO_REQUEST = 0;
    static constexpr uint8_t REQUEST_JOIN = 1;
    static constexpr uint8_t REQUEST_LEAVE = 2;
    static constexpr uint8_t REQUEST_BUSY = 3;     ///< the thread took it and is making the change
    static constexpr uint8_t REQUEST_DONE = 4;     ///< the thread made the change
    static constexpr uint8_t REQUEST_FAILED = 5;   ///< it tried and could not
    /// How often a failing socket's read errors are logged, per line.
    static constexpr uint64_t RECEIVE_ERROR_LOG_NS = 1'000'000'000;

    /// Pins the calling thread to `cpu`. Returns whether it is pinned; false for no
    /// request (a negative `cpu`) too.
    static bool pin_current_thread(int cpu, const std::string &name) {
        if (cpu < 0) {
            return false;
        }
        bool ok = false;
#if defined(__linux__)
        cpu_set_t set;
        CPU_ZERO(&set);
        if (cpu < CPU_SETSIZE) {
            CPU_SET(cpu, &set);
            ok = pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
        }
#elif defined(_WIN32)
        ok = cpu < 64 && SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << cpu) != 0;
#endif
        if (ok) {
            LOG_INFO("feed {}: receiver thread pinned to CPU core {}", name, cpu);
        } else {
            LOG_WARN("feed {}: could not pin the receiver thread to CPU core {}; it will sleep between sweeps", name,
                     cpu);
        }
        return ok;
    }

    /// The receiver thread: polls every open line's socket, and makes the membership
    /// changes change_membership() hands it.
    void receive_loop() {
        // Spin only on a core this thread owns: a failed pin leaves it sharing one.
        const bool spin = pin_current_thread(receiver_cpu_, name_);
        std::vector<uint8_t> buffer(MAX_DATAGRAM);
        std::array<uint64_t, MAX_LINES> next_error_log_ns{};
        constexpr int MAX_BURST = 64;   // per line per sweep, so one hot line cannot starve the rest
        while (running_.load(std::memory_order_acquire)) {
            bool idle = true;
            for (const uint8_t line : lines_) {
                auto &receiver = *receivers_[line];
                auto &request = request_[line];
                // Taking the request with a compare-exchange lets change_membership()
                // withdraw one this thread has not reached, so only one of them wins.
                if (uint8_t op = request.load(std::memory_order_acquire);
                    (op == REQUEST_JOIN || op == REQUEST_LEAVE) &&
                    request.compare_exchange_strong(op, REQUEST_BUSY, std::memory_order_acq_rel)) {
                    bool ok = true;
                    if (op == REQUEST_JOIN) {
                        ok = receiver.open();
                    } else {
                        receiver.close();
                    }
                    request.store(ok ? REQUEST_DONE : REQUEST_FAILED, std::memory_order_release);
                }
                if (!receiver.is_open()) {
                    continue;
                }
                for (int i = 0; i < MAX_BURST; ++i) {
                    const auto n = receiver.receive(buffer);
                    if (n < 0) {
                        // A failing socket looks like a quiet feed otherwise. slick-socket
                        // counts the errors but does not say which; it keeps the socket open.
                        const uint64_t total = receive_errors_.fetch_add(1, std::memory_order_relaxed) + 1;
                        const auto now = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count());
                        if (now >= next_error_log_ns[line]) {
                            next_error_log_ns[line] = now + RECEIVE_ERROR_LOG_NS;
                            LOG_ERROR("feed {}: line {} failed to receive ({} read errors so far on the feed)", name_,
                                      line, total);
                        }
                        break;
                    }
                    if (n == 0) {
                        break;
                    }
                    idle = false;
                    deliver(line, buffer.data(), static_cast<size_t>(n));
                }
            }
            if (idle && !spin) {
                std::this_thread::sleep_for(RECEIVER_IDLE_SLEEP);
            }
        }
    }

    std::string name_;
    nlohmann::json config_;
    /// Each entry is one byte of line, then the packet as received.
    slick::queue<uint8_t, packet_feed_traits> packets_;
    uint64_t cursor_;
    uint32_t max_packets_per_poll_;
    int receiver_cpu_;   ///< the core to pin the receiver thread to; negative for none
    std::atomic<uint32_t> muted_{0};
    std::atomic<uint64_t> oversized_{0};
    std::atomic<uint64_t> receive_errors_{0};
    // set_joined()'s state, exchange thread only. Bitmasks by line: the lines the
    // venue wants left, the lines whose group the socket has left (set only once the
    // call succeeded), and the lines whose last attempt failed, so a failure is
    // logged once per streak. Then when each line may try again.
    uint32_t wanted_left_ = 0;
    uint32_t left_ = 0;
    uint32_t failing_ = 0;
    std::array<uint64_t, MAX_LINES> retry_at_ns_{};
    /// Whether start() ran, so add_line knows to refuse.
    bool started_ = false;
    std::atomic<bool> running_{false};
    std::thread thread_;
    /// By line; null for a line with no group. Once start() has run, only the receiver
    /// thread touches the sockets.
    std::array<std::unique_ptr<slick::socket::PolledMulticastReceiver>, MAX_LINES> receivers_{};
    std::vector<uint8_t> lines_;
    /// change_membership()'s hand-off to the receiver thread, by line. NO_REQUEST except
    /// while a change is in flight, and then the thread's answer once it has one.
    std::array<std::atomic<uint8_t>, MAX_LINES> request_{};
    /// The change last handed over and not yet answered for (exchange thread only).
    std::array<uint8_t, MAX_LINES> pending_op_{};
};

}   // namespace slick::sim::md_feed
