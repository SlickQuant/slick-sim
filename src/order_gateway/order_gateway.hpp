#pragma once

#include <format>
#include <slick/queue.h>
#include "message_parser.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>
#include <client_manager/client_manager.hpp>
#include <common/messages.hpp>

namespace slick::sim::order_gateway {

struct OrderGatewayClientInfo {
    std::unique_ptr<MessageParser> parser;
    ProtocolType protocol_type;
};

class OrderGateway {
public:
    using OrderCallback = std::function<void(const Order&)>;
    
    explicit OrderGateway(
        const nlohmann::json &config,
        slick::SlickQueue<Request> &request_queue,
        slick::SlickQueue<OrderResponse> &order_response_queue,
        ProtocolType default_protocol = ProtocolType::FIX);

    ~OrderGateway();

    void start();
    void stop();
    
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
    slick::SlickQueue<Request>& get_request_queue() { return request_queue_; }
    slick::SlickQueue<OrderResponse>& get_order_response_queue() { return order_response_queue_; }
    
    // Statistics
    size_t get_active_client_count() const { return clients_.size(); }
    std::vector<std::pair<int, ProtocolType>> get_client_protocols() const;

private:
    void process_message_result(const ParseResult& result, int client_id);
    std::unique_ptr<MessageParser> create_parser_for_client(int client_id, const uint8_t* data = nullptr, size_t length = 0);
    
private:
    OrderCallback order_callback_;
    
    // Client management
    std::vector<std::unique_ptr<ClientManager>> client_managers_;
    
    std::unordered_map<int, OrderGatewayClientInfo> clients_;
    std::unordered_map<int, ProtocolType> client_protocol_overrides_;
    
    // Order processing queue
    slick::SlickQueue<Request> &request_queue_;
    slick::SlickQueue<OrderResponse> &order_response_queue_;
    
    ProtocolType default_protocol_;
    bool auto_detect_protocol_;
};

} // namespace slick::sim::order_gateway