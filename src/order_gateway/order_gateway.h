#pragma once

#include <slick_socket/tcp_server.h>
#include <slick_queue/slick_queue.h>
#include <slick_logger/logger.hpp>
#include "order.h"
#include "message_parser.h"
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>

namespace exch_sim::order_gateway {

struct OrderGatewayClientInfo {
    std::unique_ptr<MessageParser> parser;
    ProtocolType protocol_type;
};

class OrderGateway : public slick_socket::TCPServerBase<OrderGateway> {
public:
    using OrderCallback = std::function<void(const Order&)>;
    
    explicit OrderGateway(const slick_socket::TCPServerConfig& config, 
                         ProtocolType default_protocol = ProtocolType::FIX);
    ~OrderGateway();
    
    void set_order_callback(OrderCallback callback);
    void send_execution_report(const OrderResponse& response, int client_id);
    
    // Configure protocol for specific client (before connection)
    void set_client_protocol(int client_id, ProtocolType protocol);
    
    // Auto-detect protocol (default behavior)
    void enable_auto_protocol_detection(bool enable) { auto_detect_protocol_ = enable; }
    
    // Required by slick_socket::TCPServerBase (CRTP callbacks)
    void onClientConnected(int client_id, const std::string& client_address);
    void onClientDisconnected(int client_id);
    void onClientData(int client_id, const uint8_t* data, size_t length);
    
    // Order queue interface
    slick::SlickQueue<Order>& get_order_queue() { return *order_queue_; }
    
    // Statistics
    size_t get_active_client_count() const { return clients_.size(); }
    std::vector<std::pair<int, ProtocolType>> get_client_protocols() const;

private:
    void process_message_result(const ParseResult& result, int client_id);
    std::unique_ptr<MessageParser> create_parser_for_client(int client_id, const uint8_t* data = nullptr, size_t length = 0);
    
    OrderCallback order_callback_;
    
    // Client management
    std::unordered_map<int, OrderGatewayClientInfo> clients_;
    std::unordered_map<int, ProtocolType> client_protocol_overrides_;
    
    // Order processing queue
    std::unique_ptr<slick::SlickQueue<Order>> order_queue_;
    
    ProtocolType default_protocol_;
    bool auto_detect_protocol_;
};

} // namespace exch_sim::order_gateway