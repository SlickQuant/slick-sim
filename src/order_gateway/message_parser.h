#pragma once

#include "order.h"
#include <vector>
#include <memory>
#include <string>
#include <optional>

namespace exch_sim::order_gateway {

enum class ProtocolType {
    FIX,
    SBE,
    JSON,
    BINARY
};

struct ParseResult {
    std::optional<Order> order;
    std::optional<OrderResponse> response;
    std::string error_message;
    bool is_complete_message = false;
    
    bool has_order() const { return order.has_value(); }
    bool has_response() const { return response.has_value(); }
    bool has_error() const { return !error_message.empty(); }
};

class MessageParser {
public:
    virtual ~MessageParser() = default;
    
    virtual ProtocolType get_protocol_type() const = 0;
    
    // Parse incoming message data, may be called multiple times for fragmented messages
    virtual ParseResult parse_message(const uint8_t* data, size_t length, int client_id) = 0;
    
    // Serialize order response back to client format
    virtual std::vector<uint8_t> serialize_response(const OrderResponse& response) = 0;
    
    // Check if this parser can handle the given data format
    virtual bool can_handle(const uint8_t* data, size_t length) const = 0;
    
    // Reset parser state (useful for stateful parsers)
    virtual void reset() {}
    
protected:
    // Helper to convert protocol-specific fields to internal enums
    static OrderSide parse_side(const std::string& side_str);
    static OrderType parse_order_type(const std::string& type_str);
    static TimeInForce parse_time_in_force(const std::string& tif_str);
    
    static std::string side_to_string(OrderSide side);
    static std::string order_type_to_string(OrderType type);
    static std::string order_status_to_string(OrderStatus status);
};

// Factory for creating parsers
class MessageParserFactory {
public:
    static std::unique_ptr<MessageParser> create_parser(ProtocolType type);
    
    // Auto-detect protocol from message data
    static std::unique_ptr<MessageParser> create_parser_from_data(const uint8_t* data, size_t length);
    
    // Register custom parsers
    template<typename ParserType>
    static void register_parser() {
        static_assert(std::is_base_of_v<MessageParser, ParserType>, "ParserType must inherit from MessageParser");
        // Implementation would store parser creation function
    }
};

} // namespace exch_sim::order_gateway