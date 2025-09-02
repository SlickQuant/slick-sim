#pragma once

#include "message_parser.h"
#include <cstdint>
#include <vector>
#include <atomic>

// Forward declarations of generated CME SBE classes  
namespace sbe {
    class MessageHeader;
    class NewOrderSingle514;
    class ExecutionReportNew522;
    class OrderCancelRequest516;
}

namespace exch_sim::order_gateway {

class SbeParser : public MessageParser {
public:
    SbeParser();
    ~SbeParser() override = default;
    
    ProtocolType get_protocol_type() const override { return ProtocolType::SBE; }
    
    ParseResult parse_message(const uint8_t* data, size_t length, int client_id) override;
    std::vector<uint8_t> serialize_response(const OrderResponse& response) override;
    bool can_handle(const uint8_t* data, size_t length) const override;
    void reset() override;

private:
    Order parse_new_order_single(const sbe::NewOrderSingle514& sbe_order, int client_id);
    std::vector<uint8_t> create_execution_report(const OrderResponse& response);
    bool is_valid_sbe_header(const sbe::MessageHeader& header) const;
    uint64_t timestamp_to_microseconds(const std::chrono::system_clock::time_point& tp);
    std::chrono::system_clock::time_point microseconds_to_timestamp(uint64_t microseconds);
    
    std::vector<uint8_t> message_buffer_;
    static constexpr uint16_t CME_SCHEMA_ID = 8;      // CME iLink3 schema ID  
    static constexpr uint16_t CME_SCHEMA_VERSION = 8; // CME iLink3 schema version
    std::atomic<uint64_t> exec_id_counter_;
};

} // namespace exch_sim::order_gateway