#pragma once

#include "order_gateway.hpp"
#include <slick/queue.h>
#include "message_parser.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <format>
#include <common/messages.hpp>
#include <slick/socket/tcp_server.h>

namespace slick::sim::order_gateway {

struct TcpOrderGatewayClientInfo {
    std::unique_ptr<MessageParser> parser;
    ProtocolType protocol_type;
};

using TCPServerConfig = slick::socket::TCPServerConfig;

class TcpOrderGateway : public OrderGateway, public slick::socket::TCPServerBase<TcpOrderGateway> {
public:
    using OrderCallback = std::function<void(const Order&)>;
    
    explicit TcpOrderGateway(
        Venue venue,
        slick::queue<Request> &request_queue,
        slick::queue<OrderResponse> &response_queue,
        const TCPServerConfig& tcp_config = TCPServerConfig(),
        ProtocolType default_protocol = ProtocolType::FIX);

    ~TcpOrderGateway() override {
        stop();
    }

    void start() override;
    void stop() override;
    
    void set_order_callback(OrderCallback callback);
    void send_execution_report(const OrderResponse& response, int client_id);
    
    // Configure protocol for specific client (before connection)
    void set_client_protocol(int client_id, ProtocolType protocol);
    
    // Auto-detect protocol (default behavior)
    void enable_auto_protocol_detection(bool enable) { auto_detect_protocol_ = enable; }
    
    // Required by slick::socket::TCPServerBase (CRTP callbacks)
    void onClientConnected(int client_id, const std::string& client_address);
    void onClientDisconnected(int client_id);
    void onClientData(int client_id, const uint8_t* data, size_t length);
    
    // Order queue interface
    slick::queue<Request>& get_request_queue() { return request_queue_; }
    slick::queue<OrderResponse>& get_response_queue() { return response_queue_; }
    
    // Statistics
    size_t get_active_client_count() const { return clients_.size(); }
    std::vector<std::pair<int, ProtocolType>> get_client_protocols() const;

protected:
    void process_message_result(const ParseResult& result, int client_id);
    std::unique_ptr<MessageParser> create_parser_for_client(int client_id, const uint8_t* data = nullptr, size_t length = 0);
    
protected:
    OrderCallback order_callback_;
    
    // Client management
    std::unordered_map<int, TcpOrderGatewayClientInfo> clients_;
    std::unordered_map<int, ProtocolType> client_protocol_overrides_;
    
    ProtocolType default_protocol_;
    bool auto_detect_protocol_ {true};
};

} // namespace slick::sim::order_gateway