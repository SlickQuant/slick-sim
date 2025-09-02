#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <slick_logger/logger.hpp>
#include <order_gateway/order_gateway.h>
#include <order_gateway/message_parser.h>
#include <slick_socket/tcp_server.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

using namespace exch_sim::order_gateway;

void process_orders(OrderGateway& gateway) {
    auto& order_queue = gateway.get_order_queue();
    uint64_t read_index = 0;
    
    LOG_INFO("Multi-protocol order processor started...");
    
    while (true) {
        auto [order_ptr, count] = order_queue.read(read_index);
        if (order_ptr && count > 0) {
            const Order& order = *order_ptr;
            
            LOG_INFO("Processing order: ClientOrderID={}, Symbol={}, Side={}, Qty={}, Price={} from client {}",
                     order.client_order_id, order.symbol, static_cast<char>(order.side),
                     order.quantity, order.price, order.client_id);
            
            // Simulate order processing and send execution report back to the specific client
            OrderResponse response;
            response.client_order_id = order.client_order_id;
            response.exec_id = "EXEC" + std::to_string(read_index);
            response.order_status = OrderStatus::Filled;
            response.price = order.price;
            response.quantity = order.quantity;
            response.cum_qty = order.quantity;
            response.leaves_qty = 0;
            
            gateway.send_execution_report(response, order.client_id);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void print_protocol_info() {
    LOG_INFO("=== Supported Protocols ===");
    LOG_INFO("1. FIX 4.4 - Financial Information Exchange (Traditional trading)");
    LOG_INFO("2. SBE - Simple Binary Encoding (CME-style low-latency)"); 
    LOG_INFO("3. JSON - JavaScript Object Notation (Crypto exchanges)");
    LOG_INFO("");
    LOG_INFO("=== Connection Examples ===");
    LOG_INFO("FIX: Connect and send: 8=FIX.4.4|9=...|35=D|...|10=123|");
    LOG_INFO("JSON: Connect and send: {{\"type\":\"newOrder\",\"symbol\":\"BTCUSD\",\"side\":\"buy\",...}}");
    LOG_INFO("SBE: Connect and send binary SBE-encoded messages");
    LOG_INFO("");
}

void print_client_status(OrderGateway& gateway) {
    auto protocols = gateway.get_client_protocols();
    if (!protocols.empty()) {
        LOG_INFO("=== Connected Clients ===");
        for (const auto& [client_id, protocol] : protocols) {
            std::string protocol_name;
            switch (protocol) {
                case ProtocolType::FIX: protocol_name = "FIX"; break;
                case ProtocolType::SBE: protocol_name = "SBE"; break;
                case ProtocolType::JSON: protocol_name = "JSON"; break;
                default: protocol_name = "Unknown"; break;
            }
            LOG_INFO("Client {}: Using {} protocol", client_id, protocol_name);
        }
    } else {
        LOG_INFO("No clients connected");
    }
}

int main() {
    LOG_INFO("ExchangeSimulator Multi-Protocol OrderGateway starting...");

    print_protocol_info();

    // Configure TCP server
    slick_socket::TCPServerConfig config;
    config.port = 9090;
    config.max_connections = 100;
    config.receive_buffer_size = 8192;
    config.connection_timeout = std::chrono::milliseconds(60000);

    // Create OrderGateway with auto-detection enabled (default)
    OrderGateway gateway(config, ProtocolType::FIX);
    gateway.enable_auto_protocol_detection(true);
    
    // Set order callback
    gateway.set_order_callback([&gateway](const Order& order) {
        LOG_INFO("Order received via callback: {} from client {} using auto-detected protocol", 
                 order.client_order_id, order.client_id);
        
        // Print current client status
        print_client_status(gateway);
    });

    // Example: Set specific protocol for certain clients (optional)
    // gateway.set_client_protocol(1, ProtocolType::FIX);
    // gateway.set_client_protocol(2, ProtocolType::JSON);
    // gateway.set_client_protocol(3, ProtocolType::SBE);

    // Start the gateway
    if (!gateway.start()) {
        LOG_ERROR("Failed to start OrderGateway");
        return -1;
    }

    LOG_INFO("Multi-protocol OrderGateway started on port {}", config.port);
    LOG_INFO("Auto-protocol detection: ENABLED");
    LOG_INFO("Clients can connect using FIX, JSON, or SBE protocols");

    // Start order processing thread
    std::thread order_processor(process_orders, std::ref(gateway));
    order_processor.detach();

    // Status monitoring thread
    std::thread status_monitor([&gateway]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            size_t client_count = gateway.get_active_client_count();
            if (client_count > 0) {
                LOG_INFO("Active clients: {}", client_count);
                print_client_status(gateway);
            }
        }
    });
    status_monitor.detach();

    // Wait for user input to stop
    LOG_INFO("Press Enter to stop the server...");
    std::cin.get();

    gateway.stop();
    LOG_INFO("Multi-protocol ExchangeSimulator stopped");

    return 0;
}
