#pragma once

#include "client_manager.hpp"
#include <slick/socket/tcp_server.h>

namespace slick::sim::order_gateway {

using TCPServerConfig = slick::socket::TCPServerConfig;

class TcpClientManager : public ClientManager, public slick::socket::TCPServerBase<TcpClientManager> {
public:
    TcpClientManager(const json& config, slick::SlickQueue<Request> &request_queue, slick::SlickQueue<OrderResponse> &response_queue, const TCPServerConfig& tcp_config = TCPServerConfig())
        : ClientManager(config, request_queue, response_queue)
        , slick::socket::TCPServerBase<TcpClientManager>(config.value("exchange", ""), tcp_config)
    {
        LOG_INFO("TCP {} OrderGateway initialized on port {}", name_, tcp_config.port);
    }
    
    ~TcpClientManager() override {
        stop();
    }

    void start() override {
        slick::socket::TCPServerBase<TcpClientManager>::start();
    }
    
    void stop() override {
        slick::socket::TCPServerBase<TcpClientManager>::stop();
    }

    void onClientConnected(int client_id, const std::string& client_address)
    {
        LOG_INFO("{} client connected: ID={}, Address={}", name_, client_id, client_address);
    }

    void onClientDisconnected(int client_id)
    {
        LOG_INFO("{} client disconnected: ID={}", name_, client_id);
    }

    void onClientData(int client_id, const uint8_t* data, size_t length)
    {
        std::string msg((char*)data, length);
        std::cout << "Data received from client ID=" << client_id << ", " << msg << std::endl;
        send_data(client_id, msg);
    }
};

}
