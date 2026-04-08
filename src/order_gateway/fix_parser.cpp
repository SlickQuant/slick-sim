#include "fix_parser.hpp"
#include <slick/logger.hpp>
#include <sstream>
#include <iomanip>
#include <quickfix/Message.h>
#include <quickfix/Field.h>

namespace slick::sim::order_gateway {

FixParser::FixParser() : exec_id_counter_(1), current_client_id_(-1) {}

bool FixParser::can_handle(const uint8_t* data, size_t length) const {
    if (length < 10) return false;
    
    std::string str(reinterpret_cast<const char*>(data), std::min(length, size_t(50)));
    
    // Check for FIX protocol markers
    return str.find("8=FIX") == 0 || str.find("8=FIXT") == 0;
}

ParseResult FixParser::parse_message(const uint8_t* data, size_t length, int client_id) {
    ParseResult result;
    
    current_client_id_ = client_id;
    
    try {
        // Accumulate data in buffer
        message_buffer_.append(reinterpret_cast<const char*>(data), length);
        
        // Process complete FIX messages (ending with SOH - ASCII 1)
        size_t pos = 0;
        while ((pos = message_buffer_.find('\001')) != std::string::npos) {
            std::string fix_message = message_buffer_.substr(0, pos + 1);
            message_buffer_.erase(0, pos + 1);
            
            if (is_complete_fix_message(fix_message)) {
                FIX::Message message(fix_message);
                
                // Create a fake session ID for message cracking
                FIX::SessionID session_id("FIX.4.4", "EXCHANGE", std::to_string(client_id));
                
                // Reset last result and crack the message
                last_result_ = ParseResult{};
                crack(message, session_id);
                
                result = std::move(last_result_);
                result.is_complete_message = true;
                
                if (result.has_order()) {
                    LOG_DEBUG("FIX: Parsed order for client {}: {}", client_id, 
                             result.order->client_order_id);
                    return result;
                }
            }
        }
        
        // If we get here, no complete message was found
        result.is_complete_message = false;
        
    } catch (const std::exception& e) {
        result.error_message = "FIX parsing error: " + std::string(e.what());
        LOG_ERROR("FIX parsing error: {}", e.what());
    }
    return result;
}

void FixParser::onMessage(const FIX44::NewOrderSingle& message, const FIX::SessionID& sessionID) {
    try {
        Order order = parse_new_order_single(message, current_client_id_);
        last_result_.order = std::move(order);
    } catch (const std::exception& e) {
        last_result_.error_message = "Error processing NewOrderSingle: " + std::string(e.what());
    }
}

Order FixParser::parse_new_order_single(const FIX44::NewOrderSingle& message, int client_id) {
    Order order;
    order.client_id = client_id;
    
    // Required fields
    FIX::ClOrdID clOrdID;
    message.get(clOrdID);
    order.client_order_id = clOrdID.getValue();
    
    FIX::Symbol symbol;
    message.get(symbol);
    order.symbol = symbol.getValue();
    
    // Side
    FIX::Side side;
    message.get(side);
    order.side = static_cast<Side>(side.getValue());
    
    // Order Type
    FIX::OrdType ordType;
    message.get(ordType);
    order.type = static_cast<OrderType>(ordType.getValue());
    
    // Quantity
    FIX::OrderQty qty;
    message.get(qty);
    order.quantity = static_cast<uint64_t>(qty.getValue());
    order.leaves_quantity = order.quantity;
    
    // Price (if limit order)
    if (order.type == OrderType::LIMIT || order.type == OrderType::STOP_LIMIT) {
        FIX::Price price;
        message.get(price);
        order.price = to_price_t(price.getValue());
    }
    
    // Time in Force
    if (message.isSetField(FIX::FIELD::TimeInForce)) {
        FIX::TimeInForce tif;
        message.get(tif);
        order.time_in_force = static_cast<TimeInForce>(tif.getValue());
    }
    
    return order;
}

std::vector<uint8_t> FixParser::serialize_response(const OrderResponse& response) {
    try {
        FIX44::ExecutionReport execReport = create_execution_report(response);
        std::string fix_string = execReport.toString();
        
        return std::vector<uint8_t>(fix_string.begin(), fix_string.end());
    } catch (const std::exception& e) {
        LOG_ERROR("FIX serialization error: {}", e.what());
        return {};
    }
}

FIX44::ExecutionReport FixParser::create_execution_report(const OrderResponse& response) {
    FIX44::ExecutionReport execReport;
    
    execReport.set(FIX::OrderID(response.client_order_id));
    execReport.set(FIX::ExecID(response.order_id));
    execReport.set(FIX::ExecType(static_cast<char>(response.order_status)));
    execReport.set(FIX::OrdStatus(static_cast<char>(response.order_status)));
    execReport.set(FIX::Side(FIX::Side_BUY)); // TODO: Should come from original order
    execReport.set(FIX::LeavesQty(to_qty_double(response.leaves_qty)));
    execReport.set(FIX::CumQty(to_qty_double(response.cum_qty)));
    execReport.set(FIX::AvgPx(to_price_double(response.price)));
    
    // if (response.reject_reason != OrdRejectReason::NONE) {
    //     execReport.set(FIX::Text(response.reject_reason));
    // }
    
    return execReport;
}

bool FixParser::is_complete_fix_message(const std::string& buffer) const {
    // Very basic check - a complete FIX message should have BeginString and end with SOH
    return buffer.find("8=FIX") == 0 && buffer.back() == '\001';
}

std::string FixParser::generate_exec_id() {
    auto counter = exec_id_counter_.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream oss;
    oss << "FIX_EXEC" << std::setfill('0') << std::setw(8) << counter;
    return oss.str();
}

void FixParser::reset() {
    message_buffer_.clear();
    last_result_ = ParseResult{};
    current_client_id_ = -1;
}

} // namespace slick::sim::order_gateway