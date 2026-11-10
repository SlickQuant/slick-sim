#include "pcap_control_server.hpp"
#include <slick/logger.hpp>
#include <cstring>

namespace slick::sim::order_gateway {

PcapControlServer::PcapControlServer(std::string name, const slick::socket::TCPServerConfig &config)
    : Server(name, config)
    , name_(std::move(name))
{
}

PcapControlServer::~PcapControlServer() {
    stop();
}

bool PcapControlServer::start() {
    if (!Server::start()) {
        LOG_ERROR("{} control channel failed to start on port {}", name_, Server::port());
        return false;
    }
    LOG_INFO("{} control channel listening on port {}", name_, Server::port());
    return true;
}

void PcapControlServer::stop() {
    Server::stop();
}

void PcapControlServer::publish_status(const FeedStatusMessage &status) {
    const auto index = outbox_.reserve();
    std::memcpy(outbox_[index]->bytes, &status, sizeof(status));
    outbox_.publish(index);
}

void PcapControlServer::onClientConnected(int client_id, const std::string &client_address) {
    LOG_INFO("{} control channel: connection {} from {}", name_, client_id, client_address);
    // Deliver what is queued to the clients already here, so that last_status_ is current
    // and this client is not sent a stale state followed by the newer ones.
    onPoll();
    clients_.insert(client_id);
    if (last_status_) {
        Server::send_data(client_id, last_status_->bytes, sizeof(last_status_->bytes));
    }
}

void PcapControlServer::onClientDisconnected(int client_id) {
    LOG_INFO("{} control channel: connection {} closed", name_, client_id);
    clients_.erase(client_id);
    partial_.erase(client_id);
}

void PcapControlServer::onClientData(int client_id, const uint8_t *data, size_t length) {
    auto &buffer = partial_[client_id];
    size_t consumed;
    if (buffer.empty()) {
        consumed = parse(data, length);
        if (consumed != SIZE_MAX && consumed < length) {
            buffer.assign(data + consumed, data + length);
        }
    } else {
        buffer.insert(buffer.end(), data, data + length);
        consumed = parse(buffer.data(), buffer.size());
        if (consumed != SIZE_MAX) {
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
    }
    if (consumed == SIZE_MAX) {
        LOG_WARN("{} control channel: malformed message from connection {}, closing it", name_, client_id);
        Server::disconnect_client(client_id);
    }
}

size_t PcapControlServer::parse(const uint8_t *data, size_t size) {
    size_t used = 0;
    while (size - used >= sizeof(ControlHeader)) {
        ControlHeader header;
        std::memcpy(&header, data + used, sizeof(header));
        if (header.type != ControlMessageType::START || header.length != sizeof(StartMessage)) {
            return SIZE_MAX;
        }
        if (size - used < sizeof(StartMessage)) {
            break;
        }
        start_requested_.store(true, std::memory_order_release);
        used += sizeof(StartMessage);
    }
    return used;
}

void PcapControlServer::onPoll() {
    for (;;) {
        auto [slot, size] = outbox_.read(outbox_cursor_);
        if (slot == nullptr) {
            return;
        }
        last_status_ = *slot;
        for (int client : clients_) {
            Server::send_data(client, slot->bytes, sizeof(slot->bytes));
        }
    }
}

}   // namespace slick::sim::order_gateway
