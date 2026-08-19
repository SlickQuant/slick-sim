#pragma once

#include <common/types.hpp>
#include <common/order.hpp>
#include <array>
#include <cstdint>

namespace slick::sim {

#pragma pack(push, 1)

enum MDUpdateType : uint8_t {
    BOOK,
    LEVEL,
    ORDER,
    /// One executed trade. The only source of the public trade tape: a venue's
    /// own trade print is replayed through the matching engine as an aggressor
    /// order, and the resulting fills are published as these.
    TRADE_SUMMARY,
    /// Reserved. Held so the following values keep their numbering - nothing
    /// produces or consumes it since venue prints stopped being relayed raw.
    TRADE,
    BOOK_SNAPSHOT,
    ORDER_SNAPSHOT,
    SUB_RESPONSE,
};

enum UpdateFlags : uint8_t {
    F_NONE = 0,
    F_IS_SNAPSHOT = 1,
    F_END_EVENT = 1 << 1,
    /// On an MDTrade: this print was never applied to the order book. Set only
    /// for prints seeded from a venue's own trade snapshot, which happened
    /// before the simulator was listening. Every trade the matching engine
    /// produced is in the book by construction, whatever its timestamp.
    F_NOT_IN_BOOK = 1 << 2,
};

enum MDUpdateAction: uint8_t {
    ACTION_NEW,
    ACTION_CHANGE,
    ACTION_DELETE,
};

struct MDLevel {
    uint64_t event_time;
    uint64_t seq_num;
    price_t price;
    qty_t qty;
    uint32_t num_orders;
    uint16_t level_index;
    MDUpdateAction update_action;
    uint8_t flags;
    Side side;
};

struct MDBookUpdate {
    MDLevel sell[10];
    MDLevel buy[10];
    uint8_t update_index[2];
};

struct MDLevelUpdate {
    uint64_t event_time;
    uint32_t num_level_update;
    MDLevel levels[0];
};

struct MDOrder {
    uint64_t order_id;
    uint64_t event_time;
    uint64_t priority;
    price_t price;
    qty_t qty;
    Side side;
};

struct MDOrderUpdate {
    uint32_t num_orders;
    MDOrder orders[0];
};

struct MarketDataUpdate {
    char symbol[32];
    Venue venue;
    MDUpdateType type;
    uint8_t data[0];
};

#pragma pack(pop)

/// An instrument name at its wire width.
///
/// Symbol and OrderBook hold their name in this type, so publishing one is a
/// fixed-size copy of the whole field rather than
/// `memcpy(update->symbol, name.c_str(), sizeof(update->symbol))`, which reads
/// past the end of any name shorter than the field - which is all of them.
///
/// Deliberately derived from the wire field rather than spelled out: widening
/// `MarketDataUpdate::symbol` widens the storage with it, instead of silently
/// reintroducing the over-read at every publish site.
using symbol_name_t = utils::fixed_string<sizeof(MarketDataUpdate::symbol)>;

#pragma pack(push, 1)

enum MDSubscriptionRejectReason : uint8_t {
    NONE,
    UNKNOWN_CONTRACT,
    MARKET_CLOSED,
};

inline constexpr std::string_view to_string(MDSubscriptionRejectReason reason) {
    switch (reason) {
    case MDSubscriptionRejectReason::NONE:
        return "NONE";
    case MDSubscriptionRejectReason::UNKNOWN_CONTRACT:
        return "UNKNOWN_CONTRACT";
    case MDSubscriptionRejectReason::MARKET_CLOSED:
        return "MARKET_CLOSED";
    }
    return "UNKNOWN_REJECT_REASON";
}

struct MDSubscriptionResponse {
    uint8_t channel;
    MDSubscriptionRejectReason reject_reason;
    uint8_t data[0];
};

struct BookSnapshot {
    uint32_t num_bid;
    uint32_t num_ask;
    MDLevel levels[0];
};

struct MDTrade {
    uint64_t trade_id;
    uint64_t event_time;
    uint64_t seq_num;
    price_t price;
    qty_t qty;
    UpdateFlags flags;
    Side side;
};

struct MDTradeUpdate {
    uint32_t num_trades;
    MDTrade trades[0];
};

/// Payload of a SUB_RESPONSE for a venue's trade channel.
///
/// The trades are split against the book snapshot the subscriber receives
/// alongside this response: a trade stamped after the book's last update is not
/// reflected in that book, so replaying it as history would misrepresent it as
/// settled. Those trades go in the update block instead and are delivered as a
/// normal trade update.
struct MDTradeSnapshotResponse {
    uint32_t num_snapshot;  ///< trades the subscriber's book snapshot reflects
    uint32_t num_update;    ///< trades it does not - deliver as a normal update
    /// The snapshot block, then the update block, each contiguous and each
    /// chronological within itself. The producer partitions into these two blocks
    /// rather than copying in history order: a trade the book absorbed is history
    /// whatever its timestamp, so an applied trade can follow an unapplied print
    /// that is newer than the book, and history order would interleave the blocks.
    MDTrade trades[0];
};

#pragma pack(pop)

}   // end namespace slick::sim