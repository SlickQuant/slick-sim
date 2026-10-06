#pragma once

#include "order_gateway.hpp"
#include <slick/queue.h>
#include <slick/socket/tcp_server.h>
#include <common/messages.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace slick::sim::order_gateway {

using TCPServerConfig = slick::socket::TCPServerConfig;

/// The transport half of a binary order-entry gateway: a TCP server that hands a
/// venue's protocol whole byte runs, and hands it every engine response, on one
/// thread.
///
/// What it does:
///  - runs the slick-socket server thread, and nothing else runs gateway code;
///  - keeps the unconsumed tail of each connection's stream, so the protocol only
///    ever sees bytes starting at a message boundary;
///  - drains `response_queue_` on that same thread, from the server loop's
///    `onPoll()`, so a protocol can answer from `on_response` with `send()` - the
///    server only allows sends from its own thread;
///  - gives the protocol a two-step publish onto `request_queue_`.
///
/// What it leaves to the venue: framing, sessions, sequencing, and the mapping
/// between wire messages and `Request`/`OrderResponse`. A venue derives from this
/// and implements `on_data` and `on_response`.
///
/// Response latency is the server loop's pace: back to back with
/// `TCPServerConfig::cpu_affinity` set, up to ~1 ms apart otherwise.
///
/// A derived class must call `stop()` in its own destructor. The server thread calls
/// into the derived class, and by the time this base destructor runs the derived
/// members are already gone.
class TcpOrderGateway : public OrderGateway, public slick::socket::TCPServerBase<TcpOrderGateway> {
    using Server = slick::socket::TCPServerBase<TcpOrderGateway>;

public:
    TcpOrderGateway(Venue venue, slick::queue<Request> &request_queue,
                    slick::queue<OrderResponse> &response_queue, const TCPServerConfig &tcp_config);

    ~TcpOrderGateway() override;

    void start() override;
    void stop() override;

    /// The bound port; with a configured port of 0, the one the OS assigned.
    [[nodiscard]] uint16_t listen_port() const noexcept { return Server::port(); }

    // slick::socket::TCPServerBase callbacks. Server thread only.
    void onClientConnected(int client_id, const std::string &client_address);
    void onClientDisconnected(int client_id);
    void onClientData(int client_id, const uint8_t *data, size_t length);
    void onPoll();

protected:
    virtual void on_connect(int /*connection_id*/, const std::string & /*address*/) {}
    virtual void on_disconnect(int /*connection_id*/) {}
    /// Consumes as many whole messages from the front of `data` as are there and
    /// returns how many bytes that was. The remainder is kept and presented again,
    /// at the front, once more bytes arrive.
    virtual size_t on_data(int connection_id, const uint8_t *data, size_t size) = 0;
    /// Every response on the venue's response queue, in order.
    virtual void on_response(const OrderResponse &response) = 0;
    /// Once per server loop iteration, after the responses are drained.
    virtual void on_poll() {}

    /// Server thread only, like everything a protocol does.
    bool send(int connection_id, const uint8_t *data, size_t size) {
        return Server::send_data(connection_id, data, size);
    }
    void close(int connection_id) { Server::disconnect_client(connection_id); }

    Request &reserve_request() {
        pending_request_index_ = request_queue_.reserve();
        return *request_queue_[pending_request_index_];
    }
    void publish_request() { request_queue_.publish(pending_request_index_); }

private:
    /// Responses handled per loop iteration, so a burst of fills cannot hold off
    /// the sockets for longer than this many encodes.
    static constexpr int MAX_RESPONSES_PER_POLL = 1024;

    /// Bytes received but not yet consumed, per connection. Empty for a connection
    /// whose last read ended on a message boundary, which is the usual case - data
    /// is then handed to the protocol straight from the socket buffer, uncopied.
    std::unordered_map<int, std::vector<uint8_t>> partial_;
    uint64_t response_cursor_;
    uint64_t pending_request_index_ = 0;
};

} // namespace slick::sim::order_gateway
