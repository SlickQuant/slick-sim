#pragma once

#include "pcap_reader.hpp"

#include <slick/spsc_queue.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace slick::sim::md_feed {

/// One multicast group of a replay, as the venue knows it (an enum value below
/// MulticastPacketFeed::MAX_LINES, like the live feeds).
struct PcapLine {
    uint8_t line = 0;
    std::string address;
    uint16_t port = 0;
    /// Capture files for this group, oldest first. Empty means look them up under the
    /// replay's root directory.
    std::vector<std::string> paths;
};

/// The capture files of group `address:port` that can hold packets in [begin_ns, end_ns].
///
/// The recorded layout is `<root>/<YYYY-MM-DD>/<address>:<port>/<address>:<port>.pcap.zst`,
/// one file per group per day. A day's file starts in the evening before its date, so the
/// dates from the day before `begin_ns` to the day after `end_ns` (UTC) are all candidates.
/// Days with no file are skipped; the packets inside a file are filtered by timestamp.
[[nodiscard]] inline std::vector<std::string> resolve_pcap_files(const std::string &root, const std::string &address,
                                                                   uint16_t port, uint64_t begin_ns,
                                                                   uint64_t end_ns) {
    namespace fs = std::filesystem;
    constexpr int64_t DAY_S = 86400;
    std::vector<std::string> files;
    const std::string group = std::format("{}:{}", address, port);
    const int64_t first_day = static_cast<int64_t>(begin_ns / 1'000'000'000ull) / DAY_S - 1;
    const int64_t last_day = static_cast<int64_t>(end_ns / 1'000'000'000ull) / DAY_S + 1;
    for (int64_t day = first_day; day <= last_day; ++day) {
        const std::time_t t = static_cast<std::time_t>(day * DAY_S);
        std::tm tm{};
        gmtime_r(&t, &tm);
        const fs::path dir = fs::path(root) / std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) / group;
        for (const char *ext : {".pcap.zst", ".pcap.gz", ".pcap"}) {
            const fs::path file = dir / (group + ext);
            std::error_code ec;
            if (fs::is_regular_file(file, ec)) {
                files.push_back(file.string());
                break;
            }
        }
    }
    return files;
}

/// Replays several captured multicast groups as one packet stream in capture-time order.
///
/// A reader thread decodes the capture files, merges the lines by timestamp, and hands
/// batches to the exchange thread through a bounded lock-free ring (slick::spsc_queue). The reader waits when the ring
/// is full, so a consumer that stops draining (a replay waiting for its client to start it)
/// holds the capture where it is and loses nothing, unlike the lossy live-feed queue.
///
/// The consumer looks at the next packet with peek() before taking it with pop(), so
/// a pacer can hold a packet back until its time. Both are for one thread, and
/// the rest of the interface may be called from any thread.
///
/// Only packets with begin_ns <= timestamp <= end_ns are replayed. Files of consecutive
/// days overlap, so a line's next file skips everything up to the time its previous file
/// reached, and nothing repeats.
class PcapReplaySource {
public:
    /// A packet at the head of the stream. `data` is valid until pop().
    struct Item {
        uint64_t ts_ns = 0;
        uint8_t line = 0;
        const uint8_t *data = nullptr;
        size_t size = 0;
    };

    /// `root` is only used for lines without explicit paths.
    PcapReplaySource(std::string name, std::string root, uint64_t begin_ns, uint64_t end_ns,
                     std::vector<PcapLine> lines, size_t max_queued_batches = 64)
        : name_(std::move(name))
        , root_(std::move(root))
        , begin_ns_(begin_ns)
        , end_ns_(end_ns)
        , lines_(std::move(lines))
        , batches_(static_cast<uint32_t>(std::bit_ceil(std::max<size_t>(max_queued_batches, 1))))
    {
        if (end_ns_ < begin_ns_) {
            throw std::runtime_error(std::format("replay {}: end is before begin", name_));
        }
    }

    ~PcapReplaySource() { stop(); }

    PcapReplaySource(const PcapReplaySource &) = delete;
    PcapReplaySource &operator=(const PcapReplaySource &) = delete;

    /// Starts reading. Opening and positioning the files is slow (a zstd stream cannot be
    /// seeked), so it happens on the reader thread; watch positioned() and error().
    void start() {
        if (thread_.joinable()) {
            return;
        }
        thread_ = std::thread([this] { run(); });
    }

    void stop() {
        stopping_.store(true, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// True once every line has been read up to `begin_ns` and holds a first packet (or
    /// has none), so the replay is ready to produce. Any thread.
    [[nodiscard]] bool positioned() const { return positioned_.load(std::memory_order_acquire); }

    /// True when the reader has nothing more to add: the window is read out, or it failed
    /// (see error()). Packets may still be queued. Any thread.
    [[nodiscard]] bool reader_done() const { return done_.load(std::memory_order_acquire); }

    /// Frames the reader skipped for being cut short or malformed (not the IGMP, ARP and
    /// other non-UDP traffic a capture always holds). A count worth reading when the replay
    /// has gaps. Any thread.
    [[nodiscard]] uint64_t truncated_frames() const { return truncated_frames_.load(std::memory_order_relaxed); }

    /// What stopped the reader, empty when nothing did. Any thread.
    [[nodiscard]] std::string error() const {
        return failed_.load(std::memory_order_acquire) ? error_ : std::string();
    }

    /// Capture time of the last packet the reader has merged, nanoseconds. Any thread.
    [[nodiscard]] uint64_t read_ts_ns() const { return read_ts_ns_.load(std::memory_order_relaxed); }

    /// The next packet, without consuming it. False when none is ready; that is not the
    /// end of the replay, see finished(). Consumer thread.
    bool peek(Item &item) {
        if (cursor_ >= current_.entries.size() && !fetch()) {
            return false;
        }
        const Entry &entry = current_.entries[cursor_];
        item.ts_ns = entry.ts_ns;
        item.line = entry.line;
        item.data = current_.bytes.data() + entry.offset;
        item.size = entry.size;
        return true;
    }

    /// Consumes the packet peek() returned. Consumer thread.
    void pop() {
        if (cursor_ < current_.entries.size()) {
            ++cursor_;
        }
    }

    /// True when every packet has been consumed and the reader will add none. A failed
    /// reader ends the replay too: check error(). Consumer thread.
    [[nodiscard]] bool finished() {
        if (cursor_ < current_.entries.size()) {
            return false;
        }
        // Everything the reader pushed is visible once it is done, so an empty queue then is the end.
        return done_.load(std::memory_order_acquire) && !fetch();
    }

private:
    struct Entry {
        uint64_t ts_ns;
        uint32_t offset;
        uint32_t size;
        uint8_t line;
    };
    struct Batch {
        std::vector<uint8_t> bytes;
        std::vector<Entry> entries;
    };
    static constexpr size_t BATCH_PACKETS = 256;
    static constexpr size_t BATCH_BYTES = 1u << 20;

    /// One line's files, read in order, with its next packet held as `head`.
    struct Stream {
        PcapLine config;
        uint32_t addr = 0;
        std::vector<std::string> files;
        size_t next_file = 0;
        std::unique_ptr<PcapReader> reader;
        bool has_head = false;
        bool exhausted = false;
        uint64_t head_ts = 0;
        std::vector<uint8_t> head;
        uint64_t last_ts = 0;
        uint64_t truncated_counted = 0;   // of this reader's, already added to truncated_frames_
        /// After a file switch, packets up to this time were already replayed from the
        /// previous file, which overlaps this one.
        uint64_t overlap_until = 0;
    };

    /// Takes the next batch off the ring, if the reader has published one. Consumer thread.
    bool fetch() {
        Batch *next = batches_.read();
        if (!next) {
            return false;
        }
        // Swap, so the slot keeps the spent batch's buffers for the reader to fill again.
        std::swap(current_, *next);
        cursor_ = 0;
        batches_.release();   // the slot is the reader's again
        return !current_.entries.empty();
    }

    /// Drops the files that cannot hold anything the window needs, so a day's file that
    /// ends before `begin` is never decompressed just to be discarded.
    ///
    /// A file is known only by its first packet, since a compressed capture cannot be
    /// seeked to its end. Days overlap, and the later file holds every packet of the
    /// overlap, so a file is skipped when the next one already starts at or before
    /// `begin`; one that starts after `end` is skipped too. An empty or unreadable
    /// file is kept, and its error is reported when it is read.
    void trim_files(Stream &s) {
        if (s.files.size() < 2) {
            return;
        }
        std::vector<std::optional<uint64_t>> starts;
        for (const auto &file : s.files) {
            try {
                PcapReader reader(file);
                PcapPacket packet;
                starts.push_back(reader.next(packet) ? std::optional<uint64_t>(packet.ts_ns) : std::nullopt);
            } catch (const std::exception &) {
                starts.push_back(std::nullopt);
            }
        }
        std::vector<std::string> kept;
        for (size_t i = 0; i < s.files.size(); ++i) {
            const bool covered_by_next = i + 1 < s.files.size() && starts[i + 1] && *starts[i + 1] <= begin_ns_;
            const bool after_window = starts[i] && *starts[i] > end_ns_;
            if (!covered_by_next && !after_window) {
                kept.push_back(std::move(s.files[i]));
            }
        }
        // Everything skipped: leave the newest, so the error (or the lack of packets) is the real one.
        if (kept.empty()) {
            kept.push_back(s.files.back());
        }
        s.files = std::move(kept);
    }

    /// Reads the stream's next packet inside the window into its head.
    void advance(Stream &s) {
        s.has_head = false;
        PcapPacket packet;
        for (;;) {
            // Positioning decompresses to `begin`, which can take minutes: let stop() cut it short.
            if (stopping_.load(std::memory_order_relaxed)) {
                s.reader.reset();
                s.exhausted = true;
                return;
            }
            if (!s.reader) {
                if (s.next_file >= s.files.size()) {
                    s.exhausted = true;
                    return;
                }
                s.reader = std::make_unique<PcapReader>(s.files[s.next_file++]);
                s.truncated_counted = 0;
                s.overlap_until = s.last_ts;
            }
            const bool got = s.reader->next(packet);
            if (const uint64_t truncated = s.reader->truncated(); truncated != s.truncated_counted) {
                truncated_frames_.fetch_add(truncated - s.truncated_counted, std::memory_order_relaxed);
                s.truncated_counted = truncated;
            }
            if (!got) {
                if (!s.reader->error().empty()) {
                    throw std::runtime_error(std::format("{}: {}", s.reader->path(), s.reader->error()));
                }
                s.reader.reset();
                continue;
            }
            if (packet.dst_addr != s.addr || packet.dst_port != s.config.port) {
                continue;
            }
            if (packet.ts_ns < begin_ns_ || packet.ts_ns <= s.overlap_until) {
                continue;
            }
            s.overlap_until = 0;
            if (packet.ts_ns > end_ns_) {
                s.reader.reset();
                s.exhausted = true;
                return;
            }
            s.head.assign(packet.data, packet.data + packet.size);
            s.head_ts = s.last_ts = packet.ts_ns;
            s.has_head = true;
            return;
        }
    }

    /// Reader thread: the next free slot of the ring, emptied but with its buffers kept, to
    /// fill in place. Waits, by sleeping, while the ring is full; null once stop() is called.
    Batch *acquire_batch(uint64_t &index) {
        while ((index = batches_.try_reserve()) == slick::spsc_queue<Batch>::invalid_index) {
            if (stopping_.load(std::memory_order_relaxed)) {
                return nullptr;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        Batch *batch = batches_[index];
        batch->entries.clear();
        batch->bytes.clear();
        return batch;
    }

    void run() {
        try {
            std::vector<Stream> streams(lines_.size());
            for (size_t i = 0; i < lines_.size(); ++i) {
                streams[i].config = lines_[i];
                streams[i].addr = parse_ipv4(lines_[i].address);
                streams[i].files = lines_[i].paths.empty()
                    ? resolve_pcap_files(root_, lines_[i].address, lines_[i].port, begin_ns_, end_ns_)
                    : lines_[i].paths;
                if (streams[i].files.empty()) {
                    throw std::runtime_error(std::format("replay {}: no capture files for {}:{} in the window",
                                                         name_, lines_[i].address, lines_[i].port));
                }
            }
            // Skipping to `begin` decompresses everything before it, per line and
            // independently, so do the lines at once.
            std::vector<std::exception_ptr> failures(streams.size());
            std::vector<std::thread> positioning;
            for (size_t i = 0; i < streams.size(); ++i) {
                positioning.emplace_back([&, i] {
                    try {
                        trim_files(streams[i]);
                        advance(streams[i]);
                    } catch (...) {
                        failures[i] = std::current_exception();
                    }
                });
            }
            for (auto &t : positioning) {
                t.join();
            }
            for (auto &failure : failures) {
                if (failure) {
                    std::rethrow_exception(failure);
                }
            }
            positioned_.store(true, std::memory_order_release);

            // Earliest head first. Equal timestamps keep the line order, so A and B copies
            // of one packet replay together, A first.
            auto later = [&](size_t a, size_t b) {
                return streams[a].head_ts != streams[b].head_ts ? streams[a].head_ts > streams[b].head_ts : a > b;
            };
            std::priority_queue<size_t, std::vector<size_t>, decltype(later)> heads(later);
            for (size_t i = 0; i < streams.size(); ++i) {
                if (streams[i].has_head) {
                    heads.push(i);
                }
            }
            Batch *batch = nullptr;   // the ring slot being filled
            uint64_t slot = 0;
            while (!heads.empty()) {
                if (stopping_.load(std::memory_order_relaxed)) {
                    break;
                }
                if (!batch && !(batch = acquire_batch(slot))) {
                    break;
                }
                const size_t i = heads.top();
                heads.pop();
                Stream &s = streams[i];
                batch->entries.push_back({s.head_ts, static_cast<uint32_t>(batch->bytes.size()),
                                          static_cast<uint32_t>(s.head.size()), s.config.line});
                batch->bytes.insert(batch->bytes.end(), s.head.begin(), s.head.end());
                read_ts_ns_.store(s.head_ts, std::memory_order_relaxed);
                advance(s);
                if (s.has_head) {
                    heads.push(i);
                }
                if (batch->entries.size() >= BATCH_PACKETS || batch->bytes.size() >= BATCH_BYTES) {
                    batches_.publish(slot);
                    batch = nullptr;
                }
            }
            if (batch && !batch->entries.empty() && !stopping_.load(std::memory_order_relaxed)) {
                batches_.publish(slot);
            }
        } catch (const std::exception &e) {
            error_ = e.what();
            failed_.store(true, std::memory_order_release);   // publishes error_, which is never written again
        }
        // An error before the files were positioned must not leave a waiter hanging.
        positioned_.store(true, std::memory_order_release);
        done_.store(true, std::memory_order_release);
    }

    std::string name_;
    std::string root_;
    uint64_t begin_ns_;
    uint64_t end_ns_;
    std::vector<PcapLine> lines_;

    std::thread thread_;
    /// Batches from the reader thread to the exchange thread, both lock-free. A full ring holds the reader.
    slick::spsc_queue<Batch> batches_;
    std::string error_;   // written once by the reader, then failed_ is released
    std::atomic<bool> failed_{false};
    std::atomic<uint64_t> truncated_frames_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> positioned_{false};
    std::atomic<bool> done_{false};
    std::atomic<uint64_t> read_ts_ns_{0};

    // Consumer side.
    Batch current_;
    size_t cursor_ = 0;
};

/// Decides when a packet captured at some time is due, on the wall clock.
///
/// Speed 1 releases a packet once as much wall time has passed since the anchor as
/// separated it from the anchor packet in the capture; speed N releases it N times
/// sooner; speed 0 never waits. Not thread-safe.
class ReplayPacer {
public:
    explicit ReplayPacer(double speed = 1.0) : speed_(speed >= 0 && std::isfinite(speed) ? speed : 0) {}

    [[nodiscard]] double speed() const { return speed_; }

    /// Pins the capture time `capture_ns` to the wall time `wall_ns`. Call when the replay
    /// starts, with the first packet's time.
    void anchor(uint64_t capture_ns, uint64_t wall_ns) {
        anchor_capture_ns_ = capture_ns;
        anchor_wall_ns_ = wall_ns;
        anchored_ = true;
    }

    [[nodiscard]] bool anchored() const { return anchored_; }

    /// The wall time at which a packet captured at `capture_ns` should go out.
    [[nodiscard]] uint64_t due_ns(uint64_t capture_ns) const {
        if (speed_ == 0 || capture_ns <= anchor_capture_ns_) {
            return anchor_wall_ns_;
        }
        return anchor_wall_ns_ + static_cast<uint64_t>(static_cast<double>(capture_ns - anchor_capture_ns_) / speed_);
    }

    /// True when the packet may go out at wall time `now_ns`.
    [[nodiscard]] bool is_due(uint64_t capture_ns, uint64_t now_ns) const {
        return speed_ == 0 || now_ns >= due_ns(capture_ns);
    }

    /// The time to stamp on a packet's events when it goes out at `now_ns`: its due time,
    /// so the replay keeps the capture's spacing, or `now_ns` when unpaced (speed 0).
    /// Never later than `now_ns`.
    [[nodiscard]] uint64_t rebase_ns(uint64_t capture_ns, uint64_t now_ns) const {
        if (speed_ == 0) {
            return now_ns;
        }
        return std::min(due_ns(capture_ns), now_ns);
    }

private:
    double speed_;
    uint64_t anchor_capture_ns_ = 0;
    uint64_t anchor_wall_ns_ = 0;
    bool anchored_ = false;
};

}   // namespace slick::sim::md_feed
