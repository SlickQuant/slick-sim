#pragma once

#include "md_feed.hpp"
#include "pcap_replay_source.hpp"
#include "replay_gate.hpp"

#include <common/control_messages.hpp>
#include <common/types.hpp>
#include <nlohmann/json.hpp>
#include <order_gateway/pcap_control_server.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace slick::sim::md_feed {

/// What a pcap feed's config entry says, beyond its venue's multicast groups.
///
///   "pcap_root":  directory of `<date>/<group>:<port>/<group>:<port>.pcap.zst` files
///                 (not needed when every group names its own file)
///   "begin","end": the window, UTC ISO 8601 ("2026-11-06T14:00:00Z", fractions allowed)
///                 or integer nanoseconds since the epoch
///   "speed":      1.0 (default) replays at the capture's own pace, N times faster,
///                 0 as fast as the sim can publish
///   "control_channel": { "port": N } - the TCP control channel clients use for
///                 status and to start the replay; optional "max_connections" (default 100)
///                 caps how many clients it accepts
struct PcapFeedConfig {
    std::string root;
    uint64_t begin_ns = 0;
    uint64_t end_ns = 0;
    double speed = 1.0;
    uint16_t control_port = 0;
    static constexpr int DEFAULT_CONTROL_MAX_CONNECTIONS = 100;
    int control_max_connections = DEFAULT_CONTROL_MAX_CONNECTIONS;

    /// Throws std::runtime_error naming `feed` when a key is missing or invalid.
    static PcapFeedConfig from_json(const nlohmann::json &config, const std::string &feed);
};

/// Parses a UTC "YYYY-MM-DDTHH:MM:SS[.fraction][Z]" into nanoseconds since the epoch.
/// Throws std::runtime_error when `text` is anything else.
[[nodiscard]] uint64_t parse_utc_time_ns(const std::string &text);

/// The multicast groups of a pcap feed's config: for each `{key, line}` that the
/// config names, `{ "address", "port" }` and optionally `"path"` (one capture file,
/// used instead of the root directory's layout) or `"paths"` (several, oldest first).
[[nodiscard]] std::vector<PcapLine> parse_pcap_lines(const nlohmann::json &config,
                                                      std::span<const std::pair<const char *, uint8_t>> keys);

/// A venue's market data replayed from captured multicast, behind a control channel.
///
/// The venue derives from this and supplies what is protocol: how a packet reaches its
/// handler, and whether the handler has built the book. This class owns the rest:
///
///   LOADING    open the capture files and read each line up to the window's `begin`
///   SYNCING    feed the handler at full speed until it reports the book built, which
///              the snapshot capture does. No market data is published: the publisher's gate
///              is closed.
///   READY      the book is held at that point. The capture is not read further.
///   REPLAYING  a client's START begins it: the gate opens, so the publisher sends every
///              instrument from the held book on, and packets go to the handler paced
///              by `speed`. A START that comes earlier is kept until READY.
///   FINISHED   the window is replayed out.  (ERROR: a file or the sync failed.)
///
/// Every state change goes to the clients on the control channel, along with
/// progress about once a second while the feed works.
///
/// Time: the sim runs on the wall clock. Pacing waits on it, and events are stamped
/// with event_time_ns() - the wall time their capture time maps to - not the
/// capture's own time. FeedStatusMessage::current_ns reports the capture time.
///
/// Threads: poll() and everything virtual run on the exchange thread, like every feed.
/// The control channel and the capture reader have threads of their own.
class PcapFeedBase : public MDFeed {
public:
    /// `gate` is shared with the market-data publisher, which holds market data back until it opens.
    PcapFeedBase(std::string name, Venue venue, const PcapFeedConfig &config, std::vector<PcapLine> lines,
                 std::shared_ptr<ReplayGate> gate);
    ~PcapFeedBase() override;

    void start() override;
    void stop() override;

    /// Exchange thread: serves control requests, advances the state machine and
    /// feeds the handler what is due. Bounded work per call.
    void poll(uint64_t now_ns);

    [[nodiscard]] FeedState state() const noexcept { return state_; }
    [[nodiscard]] const std::string &name() const noexcept { return name_; }
    /// The listening port of the control channel (0 before start()).
    [[nodiscard]] uint16_t control_port() const noexcept { return control_.listen_port(); }

    /// Wall time to stamp on the events of the packet being handled. Meant to be read
    /// from the on_packet() callbacks the handler makes.
    [[nodiscard]] uint64_t event_time_ns() const noexcept { return event_time_ns_; }

protected:
    /// Hands one captured packet to the venue's protocol handler. `line` is the value
    /// the venue gave its PcapLine. `now_ns` is the wall clock.
    virtual void on_packet(uint8_t line, const uint8_t *data, size_t size, uint64_t now_ns) = 0;
    /// The handler's timers (gap timeouts), while it is being fed.
    virtual void on_timer(uint64_t now_ns) = 0;
    /// True once the handler holds a complete book.
    [[nodiscard]] virtual bool synchronized() const = 0;
    /// What the handler has done so far, for the log at READY and at the end: packets,
    /// gaps, resting orders. Empty for nothing to say.
    [[nodiscard]] virtual std::string describe() const { return {}; }

private:
    static constexpr uint32_t MAX_PACKETS_PER_POLL = 4096;
    static constexpr uint64_t HEARTBEAT_NS = 1'000'000'000;

    void serve_control();
    void enter(FeedState next, uint64_t now_ns, std::string message = {});
    void publish_status(uint64_t now_ns);
    void feed(uint64_t now_ns);

    std::string name_;
    Venue venue_;
    PcapFeedConfig config_;
    std::shared_ptr<ReplayGate> gate_;
    PcapReplaySource source_;
    order_gateway::PcapControlServer control_;
    ReplayPacer pacer_;

    bool start_requested_ = false;
    FeedState state_ = FeedState::IDLE;
    std::string message_;
    uint64_t current_capture_ns_ = 0;
    uint64_t event_time_ns_ = 0;
    uint64_t next_heartbeat_ns_ = 0;
};

}   // namespace slick::sim::md_feed
