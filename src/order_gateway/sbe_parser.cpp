#include "sbe_parser.hpp"
#include <slick/logger.hpp>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace slick::sim::order_gateway {

SbeParser::SbeParser() : exec_id_counter_(1) {}

bool SbeParser::can_handle(const uint8_t* data, size_t length) const {
    // For now, return false to disable SBE parsing until proper implementation
    LOG_DEBUG("SBE parsing not yet fully implemented");
    return false;
}

ParseResult SbeParser::parse_message(const uint8_t* data, size_t length, int client_id) {
    ParseResult result;
    result.error_message = "SBE parsing not yet fully implemented";
    LOG_ERROR("SBE parsing not yet fully implemented");
    return result;
}

Order SbeParser::parse_new_order_single(const sbe::NewOrderSingle514& sbe_order, int client_id) {
    Order order;
    order.client_id = client_id;
    // Minimal placeholder implementation
    LOG_ERROR("SBE NewOrderSingle parsing not yet implemented");
    return order;
}

std::vector<uint8_t> SbeParser::serialize_response(const OrderResponse& response) {
    LOG_ERROR("SBE response serialization not yet implemented");
    return {};
}

std::vector<uint8_t> SbeParser::create_execution_report(const OrderResponse& response) {
    LOG_ERROR("SBE execution report creation not yet implemented");
    return {};
}

bool SbeParser::is_valid_sbe_header(const sbe::MessageHeader& header) const {
    return false;
}

uint64_t SbeParser::timestamp_to_microseconds(const std::chrono::system_clock::time_point& tp) {
    auto duration = tp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

std::chrono::system_clock::time_point SbeParser::microseconds_to_timestamp(uint64_t microseconds) {
    std::chrono::microseconds dur(microseconds);
    return std::chrono::system_clock::time_point(dur);
}

void SbeParser::reset() {
    message_buffer_.clear();
}

} // namespace slick::sim::order_gateway