#include "json_parser.hpp"
#include <slick/logger.hpp>
#include <algorithm>

namespace slick::sim::order_gateway {

JsonParser::JsonParser() : exec_id_counter_(1) {}

bool JsonParser::can_handle(const uint8_t* data, size_t length) const {
    if (length == 0) return false;
    
    // Look for JSON indicators in first few bytes
    std::string str(reinterpret_cast<const char*>(data), std::min(length, size_t(20)));
    
    // Skip whitespace
    auto first_non_space = str.find_first_not_of(" \t\n\r");
    if (first_non_space == std::string::npos) return false;
    
    return str[first_non_space] == '{' || str[first_non_space] == '[';
}

ParseResult JsonParser::parse_message(const uint8_t* data, size_t length, int client_id) {
    ParseResult result;
    
    try {
        // Accumulate data in buffer
        message_buffer_.append(reinterpret_cast<const char*>(data), length);
        
        // Check for complete JSON messages
        if (!is_complete_json_message(message_buffer_)) {
            result.is_complete_message = false;
            return result;
        }
        
        // Parse JSON
        nlohmann::json json_msg = nlohmann::json::parse(message_buffer_);
        message_buffer_.clear();
        
        // Determine message type
        if (json_msg.contains("method") || json_msg.contains("type")) {
            std::string msg_type;
            if (json_msg.contains("method")) {
                msg_type = json_msg["method"];
            } else if (json_msg.contains("type")) {
                msg_type = json_msg["type"];
            }
            
            // Handle different message types
            if (msg_type == "newOrder" || msg_type == "ORDER" || msg_type == "order.place") {
                result.order = parse_new_order_json(json_msg, client_id);
                result.is_complete_message = true;
                
                LOG_DEBUG("JSON: Parsed order for client {}: {}", 
                         client_id, result.order->client_order_id.view());
            } else {
                result.error_message = "Unknown JSON message type: " + msg_type;
            }
        } else {
            // Try to parse as direct order object (some exchanges use this format)
            if (json_msg.contains(field_mapping_.client_order_id) && 
                json_msg.contains(field_mapping_.symbol)) {
                result.order = parse_new_order_json(json_msg, client_id);
                result.is_complete_message = true;
                
                LOG_DEBUG("JSON: Parsed direct order for client {}: {}", 
                         client_id, result.order->client_order_id.view());
            } else {
                result.error_message = "Invalid JSON order format - missing required fields";
            }
        }
        
    } catch (const nlohmann::json::parse_error& e) {
        result.error_message = "JSON parsing error: " + std::string(e.what());
        LOG_ERROR("JSON parsing error: {}", e.what());
        message_buffer_.clear(); // Reset buffer on parse error
    } catch (const std::exception& e) {
        result.error_message = "JSON processing error: " + std::string(e.what());
        LOG_ERROR("JSON processing error: {}", e.what());
    }
    
    return result;
}

Order JsonParser::parse_new_order_json(const nlohmann::json& json_order, int client_id) {
    Order order;
    order.client_id = client_id;
    
    // Extract order data - handle nested structures for different exchanges
    nlohmann::json order_data = json_order;
    if (json_order.contains("params")) {
        order_data = json_order["params"];
    } else if (json_order.contains("data")) {
        order_data = json_order["data"];
    }
    
    // Required fields
    order.client_order_id = order_data[field_mapping_.client_order_id].get<std::string>();
    order.symbol = order_data[field_mapping_.symbol].get<std::string>();
    
    // Side - handle different formats
    std::string side_str = order_data[field_mapping_.side].get<std::string>();
    order.side = parse_side(side_str);
    
    // Order type
    std::string type_str = order_data[field_mapping_.type].get<std::string>();
    order.type = parse_order_type(type_str);
    
    // Quantity - handle both string and numeric formats
    if (order_data[field_mapping_.quantity].is_string()) {
        order.quantity = std::stoull(order_data[field_mapping_.quantity].get<std::string>());
    } else {
        order.quantity = order_data[field_mapping_.quantity].get<uint64_t>();
    }
    order.leaves_quantity = order.quantity;
    
    // Price (if present)
    if (order_data.contains(field_mapping_.price) && !order_data[field_mapping_.price].is_null()) {
        if (order_data[field_mapping_.price].is_string()) {
            order.price = to_price_t(std::stod(order_data[field_mapping_.price].get<std::string>()));
        } else {
            order.price = to_price_t(order_data[field_mapping_.price].get<double>());
        }
    }
    
    // Time in Force (optional)
    if (order_data.contains(field_mapping_.time_in_force)) {
        std::string tif_str = order_data[field_mapping_.time_in_force].get<std::string>();
        order.time_in_force = parse_time_in_force(tif_str);
    }
    
    return order;
}

std::vector<uint8_t> JsonParser::serialize_response(const OrderResponse& response) {
    try {
        nlohmann::json json_response = create_execution_report_json(response);
        std::string json_str = json_response.dump();
        
        return std::vector<uint8_t>(json_str.begin(), json_str.end());
    } catch (const std::exception& e) {
        LOG_ERROR("JSON serialization error: {}", e.what());
        return {};
    }
}

nlohmann::json JsonParser::create_execution_report_json(const OrderResponse& response) {
    nlohmann::json json_response;
    
    // Common crypto exchange execution report format
    json_response["type"] = "executionReport";
    json_response["clientOrderId"] = response.client_order_id;
    json_response["exchId"] = response.order_id;
    json_response["orderStatus"] = order_status_to_string(response.order_status);
    json_response["price"] = std::to_string(response.price);
    json_response["quantity"] = std::to_string(response.qty);
    json_response["cumQty"] = std::to_string(response.cum_qty);
    json_response["leavesQty"] = std::to_string(response.leaves_qty);
    json_response["timestamp"] = response.timestamp;
    
    if (response.reject_reason != OrdRejectReason::NONE) {
        json_response["rejectReason"] = response.reject_reason;
    }
    
    return json_response;
}

bool JsonParser::is_complete_json_message(const std::string& buffer) const {
    if (buffer.empty()) return false;
    
    int brace_count = 0;
    int bracket_count = 0;
    bool in_string = false;
    bool escape_next = false;
    
    for (char c : buffer) {
        if (escape_next) {
            escape_next = false;
            continue;
        }
        
        if (c == '\\' && in_string) {
            escape_next = true;
            continue;
        }
        
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        
        if (!in_string) {
            switch (c) {
                case '{': brace_count++; break;
                case '}': brace_count--; break;
                case '[': bracket_count++; break;
                case ']': bracket_count--; break;
            }
        }
    }
    
    return brace_count == 0 && bracket_count == 0;
}

void JsonParser::reset() {
    message_buffer_.clear();
}

} // namespace slick::sim::order_gateway