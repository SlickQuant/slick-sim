#include "pcap_feed.hpp"

#include <slick/logger.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <stdexcept>

namespace slick::sim::md_feed {

namespace {

uint64_t time_from_json(const nlohmann::json &config, const char *key, const std::string &feed) {
    if (!config.contains(key)) {
        throw std::runtime_error(std::format("pcap feed {}: '{}' is required", feed, key));
    }
    const auto &value = config[key];
    try {
        if (value.is_number_unsigned()) {
            return value.get<uint64_t>();
        }
        return parse_utc_time_ns(value.get<std::string>());
    } catch (const std::exception &e) {
        throw std::runtime_error(std::format("pcap feed {}: '{}': {}", feed, key, e.what()));
    }
}

void copy_text(char *dest, size_t size, const std::string &text) {
    std::memset(dest, 0, size);
    std::memcpy(dest, text.data(), std::min(text.size(), size - 1));
}

}   // namespace

uint64_t parse_utc_time_ns(const std::string &text) {
    int year, month, day, hour, minute, second;
    int consumed = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &year, &month, &day, &hour, &minute, &second, &consumed) != 6) {
        throw std::runtime_error(std::format("'{}' is not a UTC time like 2026-11-06T14:00:00Z", text));
    }
    uint64_t fraction_ns = 0;
    size_t pos = static_cast<size_t>(consumed);
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        uint64_t scale = 100'000'000;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            fraction_ns += static_cast<uint64_t>(text[pos] - '0') * scale;
            scale /= 10;
            ++pos;
        }
    }
    if (pos < text.size() && text[pos] == 'Z') {
        ++pos;
    }
    if (pos != text.size() || year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 ||
        hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        throw std::runtime_error(std::format("'{}' is not a UTC time like 2026-11-06T14:00:00Z", text));
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    const time_t seconds = timegm(&tm);
    // timegm() normalizes an impossible date (Feb 31 -> Mar 3); reject it instead.
    if (seconds == static_cast<time_t>(-1)) {
        throw std::runtime_error(std::format("'{}' is not a time this system can represent", text));
    }
    if (tm.tm_mday != day || tm.tm_mon != month - 1) {
        throw std::runtime_error(std::format("'{}' is not a valid date", text));
    }
    return static_cast<uint64_t>(seconds) * 1'000'000'000ull + fraction_ns;
}

PcapFeedConfig PcapFeedConfig::from_json(const nlohmann::json &config, const std::string &feed) {
    PcapFeedConfig c;
    c.root = config.value("pcap_root", std::string());
    c.begin_ns = time_from_json(config, "begin", feed);
    c.end_ns = time_from_json(config, "end", feed);
    if (c.end_ns <= c.begin_ns) {
        throw std::runtime_error(std::format("pcap feed {}: 'end' must be after 'begin'", feed));
    }
    c.speed = config.value("speed", 1.0);
    if (!(c.speed >= 0) || !std::isfinite(c.speed)) {
        throw std::runtime_error(std::format("pcap feed {}: 'speed' must be 0 (unpaced) or above", feed));
    }
    if (!config.contains("control_channel") || !config["control_channel"].contains("port")) {
        throw std::runtime_error(std::format(
            "pcap feed {}: needs control_channel.port - clients start the replay over it, and it waits for them", feed));
    }
    const auto port = config["control_channel"]["port"].get<int64_t>();
    if (port < 0 || port > 65535) {
        throw std::runtime_error(std::format("pcap feed {}: control_channel.port {} is not a port", feed, port));
    }
    c.control_port = static_cast<uint16_t>(port);
    c.control_max_connections = config["control_channel"].value("max_connections", c.control_max_connections);
    return c;
}

std::vector<PcapLine> parse_pcap_lines(const nlohmann::json &config,
                                       std::span<const std::pair<const char *, uint8_t>> keys) {
    std::vector<PcapLine> lines;
    for (const auto &[key, line] : keys) {
        if (!config.contains(key)) {
            continue;
        }
        const auto &group = config[key];
        PcapLine l;
        l.line = line;
        l.address = group.at("address").get<std::string>();
        l.port = group.at("port").get<uint16_t>();
        if (group.contains("path")) {
            l.paths.push_back(group["path"].get<std::string>());
        }
        if (group.contains("paths")) {
            for (const auto &path : group["paths"]) {
                l.paths.push_back(path.get<std::string>());
            }
        }
        lines.push_back(std::move(l));
    }
    return lines;
}

PcapFeedBase::PcapFeedBase(std::string name, Venue venue, const PcapFeedConfig &config, std::vector<PcapLine> lines,
                           std::shared_ptr<ReplayGate> gate)
    : name_(std::move(name))
    , venue_(venue)
    , config_(config)
    , gate_(std::move(gate))
    , source_(name_, config.root, config.begin_ns, config.end_ns, std::move(lines))
    , control_(std::format("{} pcap {}", to_string(venue), name_), [&] {
        slick::socket::TCPServerConfig tcp;
        tcp.port = config.control_port;
        tcp.max_connections = config.control_max_connections;
        return tcp;
    }())
    , pacer_(config.speed)
{
    if (!gate_) {
        throw std::logic_error("PcapFeedBase needs a replay gate");
    }
}

PcapFeedBase::~PcapFeedBase() {
    stop();
}

void PcapFeedBase::start() {
    if (!control_.start()) {
        enter(FeedState::ERROR, 0, "the control channel could not be started");
        return;
    }
    source_.start();
    enter(FeedState::LOADING, 0);
    LOG_INFO("pcap feed {}: loading {} .. {} (speed {})", name_, config_.begin_ns, config_.end_ns, config_.speed);
}

void PcapFeedBase::stop() {
    source_.stop();
    control_.stop();
}

void PcapFeedBase::enter(FeedState next, uint64_t now_ns, std::string message) {
    if (state_ == next && message == message_) {
        return;
    }
    state_ = next;
    message_ = std::move(message);
    if (next == FeedState::ERROR) {
        LOG_ERROR("pcap feed {}: {}", name_, message_);
    } else {
        LOG_INFO("pcap feed {}: {}{}{}", name_, to_string(next), message_.empty() ? "" : " - ", message_);
    }
    // What the handler made of the capture, where a gap or a malformed packet would show.
    if (next == FeedState::READY || next == FeedState::FINISHED || next == FeedState::ERROR) {
        if (const auto detail = describe(); !detail.empty()) {
            LOG_INFO("pcap feed {}: {}", name_, detail);
        }
        if (const auto cut = source_.truncated_frames(); cut != 0) {
            LOG_WARN("pcap feed {}: {} captured frames were cut short or malformed and skipped", name_, cut);
        }
    }
    publish_status(now_ns);
}

void PcapFeedBase::publish_status(uint64_t now_ns) {
    FeedStatusMessage status{};
    status.header = {static_cast<uint16_t>(sizeof(status)), ControlMessageType::FEED_STATUS};
    status.venue = static_cast<uint8_t>(venue_);
    status.state = state_;
    copy_text(status.feed, sizeof(status.feed), name_);
    status.begin_ns = config_.begin_ns;
    status.end_ns = config_.end_ns;
    status.current_ns = std::clamp(current_capture_ns_, config_.begin_ns, config_.end_ns);
    if (state_ == FeedState::FINISHED) {
        status.current_ns = config_.end_ns;
    }
    const uint64_t span = config_.end_ns - config_.begin_ns;
    status.progress_permille = state_ == FeedState::FINISHED ? 1000 : static_cast<uint16_t>(
        std::min<uint64_t>(1000, (status.current_ns - config_.begin_ns) * 1000 / span));
    status.speed = config_.speed;
    copy_text(status.message, sizeof(status.message), message_);
    control_.publish_status(status);
    // start() has no clock yet (now_ns 0); the first poll arms the heartbeat.
    next_heartbeat_ns_ = now_ns == 0 ? 0 : now_ns + HEARTBEAT_NS;
}

void PcapFeedBase::serve_control() {
    if (control_.take_start()) {
        start_requested_ = true;
    }
}

void PcapFeedBase::poll(uint64_t now_ns) {
    serve_control();
    if (state_ == FeedState::IDLE || state_ == FeedState::FINISHED || state_ == FeedState::ERROR) {
        return;
    }
    feed(now_ns);
    if (next_heartbeat_ns_ == 0) {
        next_heartbeat_ns_ = now_ns + HEARTBEAT_NS;
    }
    if (now_ns >= next_heartbeat_ns_ && state_ != FeedState::READY) {
        publish_status(now_ns);
    }
}

void PcapFeedBase::feed(uint64_t now_ns) {
    PcapReplaySource::Item item;

    if (state_ == FeedState::LOADING) {
        if (!source_.positioned()) {
            return;
        }
        if (const auto error = source_.error(); !error.empty()) {
            enter(FeedState::ERROR, now_ns, error);
            return;
        }
        enter(FeedState::SYNCING, now_ns);
    }

    if (state_ == FeedState::SYNCING) {
        // Unpaced and unstamped: the book is being built, and nothing goes out.
        event_time_ns_ = now_ns;
        for (uint32_t i = 0; i < MAX_PACKETS_PER_POLL && source_.peek(item); ++i) {
            current_capture_ns_ = item.ts_ns;
            on_packet(item.line, item.data, item.size, now_ns);
            source_.pop();
            if (synchronized()) {
                on_timer(now_ns);
                enter(FeedState::READY, now_ns);
                break;
            }
        }
        if (state_ == FeedState::SYNCING) {
            on_timer(now_ns);
            if (synchronized()) {
                enter(FeedState::READY, now_ns);
            } else {
                if (source_.finished()) {
                    const auto error = source_.error();
                    enter(FeedState::ERROR, now_ns,
                          error.empty() ? "the capture ended before the book was synchronized; widen the window "
                                          "or check that the snapshot capture covers its start"
                                        : error);
                }
                return;
            }
        }
    }

    if (state_ == FeedState::READY) {
        if (!start_requested_) {
            return;
        }
        // The held book is published from now, even if no packet is left to replay.
        gate_->open();
        if (!source_.peek(item)) {
            if (source_.finished()) {
                if (const auto error = source_.error(); !error.empty()) {
                    enter(FeedState::ERROR, now_ns, error);
                } else {
                    enter(FeedState::FINISHED, now_ns);
                }
            }
            return;
        }
        // The capture time of the next packet is what the wall clock starts from.
        pacer_.anchor(item.ts_ns, now_ns);
        enter(FeedState::REPLAYING, now_ns);
    }

    if (state_ == FeedState::REPLAYING) {
        for (uint32_t i = 0; i < MAX_PACKETS_PER_POLL && source_.peek(item); ++i) {
            if (!pacer_.is_due(item.ts_ns, now_ns)) {
                break;
            }
            current_capture_ns_ = item.ts_ns;
            event_time_ns_ = pacer_.rebase_ns(item.ts_ns, now_ns);
            on_packet(item.line, item.data, item.size, now_ns);
            source_.pop();
        }
        // The handler's timers fire now, not at the last packet's time.
        event_time_ns_ = now_ns;
        on_timer(now_ns);
        if (source_.finished()) {
            if (const auto error = source_.error(); !error.empty()) {
                enter(FeedState::ERROR, now_ns, error);
            } else {
                enter(FeedState::FINISHED, now_ns);
            }
        }
    }
}

}   // namespace slick::sim::md_feed
