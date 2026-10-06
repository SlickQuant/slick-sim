#include <slick/logger.hpp>
#include "tcp_order_gateway.hpp"
#include <common/types.hpp>

namespace slick::sim::order_gateway {

TcpOrderGateway::TcpOrderGateway(
    Venue venue,
    slick::queue<Request> &request_queue,
    slick::queue<OrderResponse> &response_queue,
    const TCPServerConfig &tcp_config)
    : OrderGateway(venue, request_queue, response_queue)
    , Server(to_string(venue), tcp_config)
    // Responses published before the gateway existed answered nobody it serves.
    , response_cursor_(response_queue.initial_reading_index())
{
}

TcpOrderGateway::~TcpOrderGateway() {
    stop();
}

void TcpOrderGateway::start() {
    if (!Server::start()) {
        LOG_ERROR("{} TCP order gateway failed to start on port {}", to_string(venue_), Server::port());
        return;
    }
    LOG_INFO("{} TCP order gateway listening on port {}", to_string(venue_), Server::port());
}

void TcpOrderGateway::stop() {
    Server::stop();
}

void TcpOrderGateway::onClientConnected(int client_id, const std::string &client_address) {
    LOG_INFO("{} order gateway: connection {} from {}", to_string(venue_), client_id, client_address);
    on_connect(client_id, client_address);
}

void TcpOrderGateway::onClientDisconnected(int client_id) {
    LOG_INFO("{} order gateway: connection {} closed", to_string(venue_), client_id);
    partial_.erase(client_id);
    on_disconnect(client_id);
}

void TcpOrderGateway::onClientData(int client_id, const uint8_t *data, size_t length) {
    auto it = partial_.find(client_id);
    if (it == partial_.end() || it->second.empty()) {
        // On a message boundary: parse straight out of the socket buffer, and keep
        // only what the protocol could not use yet.
        const size_t consumed = on_data(client_id, data, length);
        if (consumed < length) {
            partial_[client_id].assign(data + consumed, data + length);
        }
        return;
    }

    // Mid-message: the new bytes complete what is already buffered.
    auto &buffer = it->second;
    buffer.insert(buffer.end(), data, data + length);
    const size_t consumed = on_data(client_id, buffer.data(), buffer.size());
    // on_data may have closed the connection, which erases its entry only once this
    // callback returns - so the iterator is still good here.
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
}

void TcpOrderGateway::onPoll() {
    for (int i = 0; i < MAX_RESPONSES_PER_POLL; ++i) {
        auto [response, size] = response_queue_.read(response_cursor_);
        if (!response) {
            break;
        }
        on_response(*response);
    }
    on_poll();
}

} // namespace slick::sim::order_gateway
