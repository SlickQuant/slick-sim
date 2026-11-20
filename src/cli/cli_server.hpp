#pragma once

#include "cli_endpoint.hpp"
#include <common/control_messages.hpp>
#include <common/control_stream.hpp>
#include <slick/socket/tcp_server.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace slick::sim::cli {

/// The simulator's command port: a small TCP server on its own thread that takes
/// CliRequestMessages (common/control_messages.hpp) and hands each one, through a
/// lock-free CliEndpoint, to the thread that owns what it changes. `sim` is the main
/// thread; a venue's commands go to its order gateways' threads, which keep the state
/// they change, so the matching loop never sees a command.
///
/// A target names a set of endpoints: a venue's config key (`cme`) is all of its
/// endpoints, and every `/`-prefix of an endpoint's name addresses those under it
/// (`ice/<silo>` is every BGW of that silo). A command goes to each endpoint of its
/// target and is answered once all of them have, or after `reply_timeout`, with one
/// line per endpoint.
///
/// A client has one request answered at a time, so its replies come in order; the rest
/// wait in the server. A client that sends anything malformed is disconnected.
///
/// Like every slick-socket server it listens on all interfaces, with no authentication.
///
/// stop() must run before the endpoints go; the destructor does it.
class CliServer : public slick::socket::TCPServerBase<CliServer> {
    using Server = slick::socket::TCPServerBase<CliServer>;

public:
    static constexpr uint16_t DEFAULT_PORT = 17400;
    static constexpr std::chrono::milliseconds DEFAULT_REPLY_TIMEOUT{2000};
    /// For TCPServerConfig::idle_timeout, so dead connections can't hold every slot.
    static constexpr std::chrono::seconds DEFAULT_IDLE_TIMEOUT{300};

    explicit CliServer(const slick::socket::TCPServerConfig &config,
                       std::chrono::milliseconds reply_timeout = DEFAULT_REPLY_TIMEOUT);
    ~CliServer();

    /// Routes `group`, and every `/`-prefix of each endpoint's name, to the endpoints.
    /// Adding to a target that exists extends it. Before start().
    void add_endpoints(const std::string &group, const std::vector<CliEndpoint *> &endpoints);

    /// False when the port could not be bound.
    bool start();
    void stop();

    /// Waits up to `limit` for the replies still owed to clients to go out, so the
    /// answer to a SHUTDOWN gets sent before the server stops.
    void flush(std::chrono::milliseconds limit);

    /// The bound port; with a configured port of 0, the one the OS assigned.
    [[nodiscard]] uint16_t listen_port() const noexcept { return Server::port(); }

    // slick::socket::TCPServerBase callbacks. Server thread only.
    void onClientConnected(int client_id, const std::string &client_address);
    void onClientDisconnected(int client_id);
    void onClientData(int client_id, const uint8_t *data, size_t length);
    void onPoll();

private:
    /// Requests a client may queue behind the one being answered.
    static constexpr size_t MAX_WAITING = 64;

    struct Answer {
        size_t endpoint;          ///< Index into endpoints_.
        bool answered = false;
        CliStatus status = CliStatus::FAILED;
        std::string text;
    };
    struct Pending {
        int client_id;            ///< -1 once the client has gone.
        uint32_t request_id;
        std::vector<Answer> answers;
        size_t remaining;
        std::chrono::steady_clock::time_point deadline;
    };
    struct Client {
        std::deque<CliRequestMessage> waiting;
        uint64_t ticket = 0;      ///< The request being answered; 0 for none.
    };

    /// Starts the client's next request unless one is being answered. A request the
    /// server answers itself, such as LIST_TARGETS or a refusal, is answered here, and
    /// it moves on to the next.
    void dispatch(int client_id);
    void on_reply(size_t endpoint, uint64_t ticket, CliStatus status, std::string_view text);
    /// Answers the request with what its endpoints said, and starts the client's next.
    void finish(uint64_t ticket);
    void send_reply(int client_id, uint32_t request_id, CliStatus status, std::string_view text);
    [[nodiscard]] std::string list_targets() const;

    std::chrono::milliseconds reply_timeout_;
    /// Each endpoint once, in the order added. Fixed once started.
    std::vector<CliEndpoint *> endpoints_;
    /// Target name to indices into endpoints_. Ordered, so the listing is sorted.
    std::map<std::string, std::vector<size_t>, std::less<>> targets_;

    // Server thread only.
    ControlStreams streams_{sizeof(CliRequestMessage)};
    std::unordered_map<int, Client> clients_;
    std::unordered_map<uint64_t, Pending> pending_;
    uint64_t next_ticket_ = 1;
    std::vector<uint8_t> reply_;
    std::vector<uint64_t> expired_;
    /// pending_.size(), for flush() on another thread.
    std::atomic<size_t> pending_count_{0};
};

}   // namespace slick::sim::cli
