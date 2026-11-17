#pragma once

#include <cstdint>

// wingdi.h defines ERROR, which would turn FeedState::ERROR into a number wherever
// windows.h is included first, as slick-socket does in every gateway.
#ifdef ERROR
#undef ERROR
#endif

namespace slick::sim {

/// Where a pcap replay feed is. The values are on the wire: append, never renumber.
enum class FeedState : uint8_t {
    IDLE = 0,       ///< Not started.
    LOADING = 1,    ///< Opening the capture files and reading up to the window's begin.
    SYNCING = 2,    ///< Building the book from the snapshot and incremental captures. Nothing is published.
    READY = 3,      ///< The book is built and held. Replay starts when a client sends START.
    REPLAYING = 4,  ///< Publishing the capture.
    FINISHED = 5,   ///< The window is replayed out.
    ERROR = 6,      ///< The feed failed; `message` says why.
};

[[nodiscard]] constexpr const char *to_string(FeedState state) noexcept {
    switch (state) {
    case FeedState::IDLE: return "IDLE";
    case FeedState::LOADING: return "LOADING";
    case FeedState::SYNCING: return "SYNCING";
    case FeedState::READY: return "READY";
    case FeedState::REPLAYING: return "REPLAYING";
    case FeedState::FINISHED: return "FINISHED";
    case FeedState::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

enum class ControlMessageType : uint8_t {
    FEED_STATUS = 1,  ///< Sim to client.
    START = 2,        ///< Client to sim: start the replay.
    CLI_REQUEST = 3,  ///< Client to sim, on the CLI port: CliRequestMessage.
    CLI_REPLY = 4,    ///< Sim to client, on the CLI port: CliReplyMessage.
};

/// What a CLI request asks for. The values are on the wire: append, never renumber.
enum class CliCommandType : uint8_t {
    LIST_TARGETS = 1,       ///< Any target. The CLI server answers with every target it routes to.
    SET_LOG_LEVEL = 2,      ///< `sim`. `arg` is the level; empty reports the current one.
    SHUTDOWN = 3,           ///< `sim`. Stops the simulator.
    REJECT_LOGON = 4,       ///< A venue. `flag` 1 refuses the logons of ID `arg` from now on, 0 stops.
    REJECT_NEW_ORDERS = 5,  ///< A venue. `flag` 1 rejects every new order of ID `arg`, 0 stops.
    LIST_REJECTS = 6,       ///< A venue. The IDs both of the above apply to.
};

/// The command's name, as the CLI client spells it.
[[nodiscard]] constexpr const char *to_string(CliCommandType command) noexcept {
    switch (command) {
    case CliCommandType::LIST_TARGETS: return "targets";
    case CliCommandType::SET_LOG_LEVEL: return "log_level";
    case CliCommandType::SHUTDOWN: return "shutdown";
    case CliCommandType::REJECT_LOGON: return "reject_logon";
    case CliCommandType::REJECT_NEW_ORDERS: return "reject_new_orders";
    case CliCommandType::LIST_REJECTS: return "rejects";
    }
    return "unknown";
}

enum class CliStatus : uint8_t {
    OK = 0,
    FAILED = 1,
};

#pragma pack(push, 1)

/// Every control message starts with this. `length` is the whole message, header
/// included, so a reader can frame the stream without knowing every type.
/// Integers are little-endian, as everywhere on the sim's wires.
struct ControlHeader {
    uint16_t length;
    ControlMessageType type;
};

/// The replay feed's state. Sent to a client when it connects, whenever the state
/// changes, and about once a second while the feed is working (progress).
struct FeedStatusMessage {
    ControlHeader header;
    uint8_t venue;          ///< common/types.hpp Venue.
    FeedState state;
    char feed[32];          ///< The feed's name, as configured.
    /// The configured window and the capture time reached, nanoseconds since the
    /// epoch. The published market data is stamped with the wall clock, so this is
    /// the only place the capture's own time is reported.
    uint64_t begin_ns;
    uint64_t end_ns;
    uint64_t current_ns;
    uint16_t progress_permille;   ///< 0-1000 through the window, by capture time.
    double speed;                 ///< The configured replay speed; 0 is unpaced.
    char message[96];             ///< Free text, NUL-terminated. The reason, in ERROR.
};

/// Starts the replay of every loaded instrument. Sent once the status says READY, or
/// before: the replay then starts as soon as it gets there. Ignored once started.
struct StartMessage {
    ControlHeader header;
};

/// One command to the simulator, on the CLI port. A client has one request answered at
/// a time: later ones wait until the reply to this one is sent.
struct CliRequestMessage {
    ControlHeader header;
    uint32_t request_id;     ///< The client's own, echoed in the reply.
    CliCommandType command;
    uint8_t flag;            ///< 1 on, 0 off, for the commands that toggle.
    char target[32];         ///< `sim`, a venue (`cme`), an instance (`cme/<segment>`). NUL-padded.
    char arg[64];            ///< The ID or level. NUL-padded.
};

/// The answer to a CliRequestMessage. Its text follows, up to `header.length` (the whole
/// message's length): UTF-8, one line per endpoint the command reached, each
/// "<endpoint>: <what it said>".
struct CliReplyMessage {
    ControlHeader header;
    uint32_t request_id;
    CliStatus status;        ///< OK only when every endpoint the command reached said OK.
};

#pragma pack(pop)

static_assert(sizeof(ControlHeader) == 3);
static_assert(sizeof(CliRequestMessage) == 105);
static_assert(sizeof(CliReplyMessage) == 8);

}   // namespace slick::sim
