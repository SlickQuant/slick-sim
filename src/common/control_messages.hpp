#pragma once

#include <cstdint>

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

#pragma pack(pop)

static_assert(sizeof(ControlHeader) == 3);

}   // namespace slick::sim
