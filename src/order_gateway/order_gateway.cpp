#include "order_gateway.hpp"
#include "client_manager/tcp_client_manager.hpp"
#include "client_manager/coinbase_rest_client_manager.hpp"
#include "client_manager/coinbase_ws_client_manager.hpp"
#include "client_manager/hyperliquid_rest_client_manager.hpp"

using TCPServerConfig = slick::socket::TCPServerConfig;

namespace slick::sim::order_gateway {

OrderGateway::OrderGateway(
    const json& config,
    slick::SlickQueue<Request> &request_queue,
    slick::SlickQueue<OrderResponse> &order_response_queue,
    ProtocolType default_protocol)
    : request_queue_(request_queue)
    , order_response_queue_(order_response_queue)
    , default_protocol_(default_protocol)
    , auto_detect_protocol_(true)
{
    for (auto it = config.begin(); it != config.end(); ++it) {
        const std::string& exch = it.key();
        const json& exch_cfg = it.value();

        if (exch_cfg.contains("tcp")) {
            const json& tcp = exch_cfg["tcp"];
            TCPServerConfig tcp_config;
            tcp_config.port = tcp.value("port", tcp_config.port);
            tcp_config.receive_buffer_size = tcp.value("receive_buffer_size", 65535);
            tcp_config.cpu_affinity = tcp.value("cpu_affinity", tcp_config.cpu_affinity);
            json tcp_cfg = tcp; tcp_cfg["exchange"] = exch;
            client_managers_.emplace_back(
                std::make_unique<TcpClientManager>(tcp_cfg, request_queue_, order_response_queue_, tcp_config));
        }

        if (exch_cfg.contains("rest")) {
            const json& rest = exch_cfg["rest"];
            json rest_cfg = rest; rest_cfg["exchange"] = exch;
            if (exch == "coinbase") {
                client_managers_.emplace_back(
                    std::make_unique<coinbase::CoinbaseRestClientManager>(rest_cfg, request_queue_, order_response_queue_));
            } else if (exch == "hyperliquid") {
                client_managers_.emplace_back(
                    std::make_unique<hyperliquid_hl::HyperliquidRestClientManager>(rest_cfg, request_queue_, order_response_queue_));
            } else {
                throw std::runtime_error(std::format("Unsupported exchange '{}' for REST client", exch));
            }
        }

        if (exch_cfg.contains("ws")) {
            const json& ws = exch_cfg["ws"];
            json ws_cfg = ws; ws_cfg["exchange"] = exch;
            if (exch == "coinbase") {
                client_managers_.emplace_back(
                    std::make_unique<coinbase::CoinbaseWebsocketClientManager>(ws_cfg, request_queue_, order_response_queue_));
            } else {
                throw std::runtime_error(std::format("Unsupported exchange '{}' for WS client", exch));
            }
        }
    }
}

OrderGateway::~OrderGateway() {
    // stop();
}

void OrderGateway::start() {
    LOG_INFO("ExchangeSimulator OrderGateway starting...");
    for (auto &mgr : client_managers_) {
        mgr->start();
    }
}

void OrderGateway::stop() {
    for (auto &mgr : client_managers_) {
        mgr->stop();
    }
}

void OrderGateway::set_order_callback(OrderCallback callback) {
    order_callback_ = std::move(callback);
}

void OrderGateway::set_client_protocol(int client_id, ProtocolType protocol) {
    client_protocol_overrides_[client_id] = protocol;
    LOG_INFO("Protocol override set for client {}: {}", client_id, static_cast<int>(protocol));
}

void OrderGateway::onClientConnected(int client_id, const std::string& client_address) {
    LOG_INFO("Client {} connected from {}", client_id, client_address);
    
    OrderGatewayClientInfo client_info;
    client_info.parser = nullptr; // Will be created when first data arrives
    client_info.protocol_type = default_protocol_;
    
    clients_[client_id] = std::move(client_info);
}

void OrderGateway::onClientDisconnected(int client_id) {
    LOG_INFO("Client {} disconnected", client_id);
    clients_.erase(client_id);
    client_protocol_overrides_.erase(client_id);
}

void OrderGateway::onClientData(int client_id, const uint8_t* data, size_t length) {
    LOG_TRACE("Received {} bytes from client {}", length, client_id);
    
    auto client_it = clients_.find(client_id);
    if (client_it == clients_.end()) {
        LOG_ERROR("Unknown client {}", client_id);
        return;
    }
    
    OrderGatewayClientInfo& client = client_it->second;
    
    // Create parser if not exists
    if (!client.parser) {
        client.parser = create_parser_for_client(client_id, data, length);
        if (!client.parser) {
            LOG_ERROR("Failed to create parser for client {}", client_id);
            return;
        }
        client.protocol_type = client.parser->get_protocol_type();
        LOG_INFO("Client {} using protocol: {}", client_id, static_cast<int>(client.protocol_type));
    }
    
    // Parse the message
    ParseResult result = client.parser->parse_message(data, length, client_id);
    process_message_result(result, client_id);
}

void OrderGateway::process_message_result(const ParseResult& result, int client_id) {
    if (result.has_error()) {
        LOG_ERROR("Parse error from client {}: {}", client_id, result.error_message);
        return;
    }
    
    if (!result.is_complete_message) {
        LOG_DEBUG("Incomplete message from client {}, waiting for more data", client_id);
        return;
    }
    
    if (result.has_order()) {
        const Order& order = result.order.value();
        
        LOG_INFO("Received order: ClientOrderID={}, Symbol={}, Side={}, Qty={}, Price={} from client {}", 
                 order.client_order_id, order.symbol, static_cast<char>(order.side), 
                 order.quantity, order.price, client_id);
        
        // Push to lock-free queue
        uint64_t index = request_queue_.reserve();
        // *(*request_queue_)[index] = order;
        request_queue_.publish(index);
        
        // Call callback if set
        if (order_callback_) {
            order_callback_(order);
        }
    }
    
    if (result.has_response()) {
        // Handle response messages if needed
        LOG_DEBUG("Received response message from client {}", client_id);
    }
}

std::unique_ptr<MessageParser> OrderGateway::create_parser_for_client(int client_id, const uint8_t* data, size_t length) {
    ProtocolType protocol = default_protocol_;
    
    // Check for client-specific protocol override
    auto override_it = client_protocol_overrides_.find(client_id);
    if (override_it != client_protocol_overrides_.end()) {
        protocol = override_it->second;
        LOG_DEBUG("Using protocol override for client {}: {}", client_id, static_cast<int>(protocol));
        return MessageParserFactory::create_parser(protocol);
    }
    
    // Auto-detect protocol if enabled and data is available
    if (auto_detect_protocol_ && data && length > 0) {
        auto parser = MessageParserFactory::create_parser_from_data(data, length);
        if (parser) {
            LOG_DEBUG("Auto-detected protocol for client {}: {}", 
                     client_id, static_cast<int>(parser->get_protocol_type()));
            return parser;
        }
    }
    
    // Fall back to default protocol
    LOG_DEBUG("Using default protocol for client {}: {}", client_id, static_cast<int>(protocol));
    return MessageParserFactory::create_parser(protocol);
}

void OrderGateway::send_execution_report(const OrderResponse& response, int client_id) {
    auto client_it = clients_.find(client_id);
    if (client_it == clients_.end()) {
        LOG_ERROR("Cannot send execution report to unknown client {}", client_id);
        return;
    }
    
    OrderGatewayClientInfo& client = client_it->second;
    if (!client.parser) {
        LOG_ERROR("No parser available for client {}", client_id);
        return;
    }
    
    try {
        std::vector<uint8_t> response_data = client.parser->serialize_response(response);
        if (!response_data.empty()) {
            // send_data(client_id, response_data);
            LOG_DEBUG("Sent execution report to client {} using protocol {}", 
                     client_id, static_cast<int>(client.protocol_type));
        } else {
            LOG_ERROR("Failed to serialize response for client {}", client_id);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending execution report to client {}: {}", client_id, e.what());
    }
}

std::vector<std::pair<int, ProtocolType>> OrderGateway::get_client_protocols() const {
    std::vector<std::pair<int, ProtocolType>> result;
    for (const auto& [client_id, client_info] : clients_) {
        result.emplace_back(client_id, client_info.protocol_type);
    }
    return result;
}

} // namespace slick::sim::order_gateway