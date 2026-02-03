#pragma once

#include "message_parser.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <atomic>

namespace slick::sim::order_gateway {

class JsonParser : public MessageParser {
public:
    JsonParser();
    ~JsonParser() override = default;
    
    ProtocolType get_protocol_type() const override { return ProtocolType::JSON; }
    
    ParseResult parse_message(const uint8_t* data, size_t length, int client_id) override;
    std::vector<uint8_t> serialize_response(const OrderResponse& response) override;
    bool can_handle(const uint8_t* data, size_t length) const override;
    void reset() override;

private:
    Order parse_new_order_json(const nlohmann::json& json_order, int client_id);
    nlohmann::json create_execution_report_json(const OrderResponse& response);
    bool is_complete_json_message(const std::string& buffer) const;
    
    std::string message_buffer_;
    std::atomic<uint64_t> exec_id_counter_;
    
    // JSON field mappings for different crypto exchanges
    struct JsonFieldMapping {
        std::string client_order_id = "clientOrderId";
        std::string symbol = "symbol";
        std::string side = "side";
        std::string type = "type";
        std::string quantity = "quantity";
        std::string price = "price";
        std::string time_in_force = "timeInForce";
    };
    
    JsonFieldMapping field_mapping_;
};

} // namespace slick::sim::order_gateway