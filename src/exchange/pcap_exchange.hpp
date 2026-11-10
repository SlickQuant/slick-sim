#pragma once

#include "instance_expansion.hpp"

#include <md_feed/replay_gate.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

/// What a venue's exchange instance (CME segment, ICE silo) does for a pcap feed, the
/// same way for every venue. None of it needs libpcap: a config that names a pcap feed has
/// to be understood, and refused, in every build.
namespace slick::sim::exch {

/// True for a `md_feeds` entry of `type` that is not disabled.
[[nodiscard]] inline bool is_enabled_feed(const nlohmann::json &feed, std::string_view type) {
    return feed.value("enabled", true) && feed.value("type", std::string()) == type;
}

/// True when the instance's `md_feeds` has an enabled feed of `type`.
[[nodiscard]] inline bool has_enabled_feed(const nlohmann::json &instance, std::string_view type) {
    for (const auto &feed : instance.value("md_feeds", nlohmann::json::array())) {
        if (is_enabled_feed(feed, type)) {
            return true;
        }
    }
    return false;
}

/// Claims the TCP control port of each enabled pcap feed of `instance` for `owner`. Port 0
/// asks the OS for a free one, so any number of feeds may use it.
inline void claim_pcap_control_ports(std::unordered_map<uint16_t, std::string> &ports, const nlohmann::json &instance,
                                     std::string_view feed_type, const std::string &owner, std::string_view venue) {
    for (const auto &feed : instance.value("md_feeds", nlohmann::json::array())) {
        if (is_enabled_feed(feed, feed_type) && feed.contains("control_channel")) {
            const uint16_t port = feed["control_channel"].value("port", uint16_t{0});
            if (port != 0) {
                claim(ports, port, owner, "pcap control channel port", venue);
            }
        }
    }
}

/// The gate a replaying instance's publisher waits on, or null when its config has no
/// enabled feed of `feed_type`. `md_queue` is the one the publisher reads.
template <typename Queue>
[[nodiscard]] std::shared_ptr<md_feed::ReplayGate> make_replay_gate(const nlohmann::json &instance,
                                                                    std::string_view feed_type, Queue &md_queue) {
    if (!has_enabled_feed(instance, feed_type)) {
        return nullptr;
    }
    return std::make_shared<md_feed::ReplayGate>([&md_queue] { return md_queue.initial_reading_index(); });
}

/// Throws when a replay and live feeds would both write the one book.
inline void reject_pcap_with_live_feeds(bool has_pcap_feed, size_t live_feeds, std::string_view venue,
                                        std::string_view kind, const std::string &name) {
    if (has_pcap_feed && live_feeds != 0) {
        throw std::runtime_error(std::format("{}: {} '{}' mixes a pcap feed with live feeds", venue, kind, name));
    }
}

/// `event` with its time set from the replay: a replay is stamped on the wall clock, not with
/// the capture's own time. `feed` is the instance's pcap feed (null: the event as it is).
template <typename Event, typename Feed>
[[nodiscard]] Event replay_stamped(Event event, const Feed *feed) {
    if (feed) {
        event.transact_time = feed->event_time_ns();
    }
    return event;
}

}   // namespace slick::sim::exch
