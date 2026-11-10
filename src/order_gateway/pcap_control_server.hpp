#pragma once

#include <common/control_messages.hpp>
#include <slick/queue.hpp>
#include <slick/socket/tcp_server.h>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slick::sim::order_gateway {

/// The channel between a pcap replay and its clients, apart from the market data
/// (multicast) and the orders (the TCP order gateway), neither of which can say that
/// the book is still loading or ask for the replay to begin.
///
/// A small TCP server. A client connects, is told the current FeedStatusMessage, and
/// is sent every later one; it sends START when it is ready for the market data. Framing
/// is ControlHeader (common/control_messages.hpp). A client that sends anything else is
/// disconnected.
///
/// Threads: the server thread owns the sockets and parses what arrives; the exchange
/// thread calls publish_status() and take_start(). Status goes through a lock-free
/// slick::queue, which overwrites the oldest entry if the server thread falls
/// QUEUE_SLOTS behind; START is an atomic flag.
///
/// stop() must run before destruction, as with TcpOrderGateway; the destructor does it.
class PcapControlServer : public slick::socket::TCPServerBase<PcapControlServer> {
    using Server = slick::socket::TCPServerBase<PcapControlServer>;

public:
    PcapControlServer(std::string name, const slick::socket::TCPServerConfig &config);
    ~PcapControlServer();

    /// False when the port could not be bound.
    bool start();
    void stop();

    /// The bound port; with a configured port of 0, the one the OS assigned.
    [[nodiscard]] uint16_t listen_port() const noexcept { return Server::port(); }

    /// Sends `status` to every client now connected, and to each one that connects
    /// before the next. The exchange thread (one producer at a time).
    void publish_status(const FeedStatusMessage &status);

    /// True once, after a client has sent START. Exchange thread.
    [[nodiscard]] bool take_start() noexcept { return start_requested_.exchange(false, std::memory_order_acq_rel); }

    // slick::socket::TCPServerBase callbacks. Server thread only.
    void onClientConnected(int client_id, const std::string &client_address);
    void onClientDisconnected(int client_id);
    void onClientData(int client_id, const uint8_t *data, size_t length);
    void onPoll();

private:
    static constexpr uint32_t QUEUE_SLOTS = 1024;

    struct StatusSlot {
        uint8_t bytes[sizeof(FeedStatusMessage)];
    };

    /// Consumes the whole frames at the front of `data`; returns the bytes used, or
    /// SIZE_MAX when the stream is malformed.
    size_t parse(const uint8_t *data, size_t size);

    std::string name_;
    slick::queue<StatusSlot> outbox_{QUEUE_SLOTS};   // exchange thread -> server thread
    std::atomic<bool> start_requested_{false};       // server thread -> exchange thread

    // Server thread only.
    uint64_t outbox_cursor_ = 0;
    std::optional<StatusSlot> last_status_;
    std::unordered_set<int> clients_;
    std::unordered_map<int, std::vector<uint8_t>> partial_;
};

}   // namespace slick::sim::order_gateway
