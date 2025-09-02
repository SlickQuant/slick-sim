#pragma once

#include "message_parser.h"

#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/fix44/NewOrderSingle.h>
#include <quickfix/fix44/ExecutionReport.h>

#include <string>
#include <atomic>

namespace exch_sim::order_gateway {

class FixParser : public MessageParser, public FIX::MessageCracker
{
public:
    FixParser();
    ~FixParser() override = default;
    
    ProtocolType get_protocol_type() const override { return ProtocolType::FIX; }
    
    ParseResult parse_message(const uint8_t* data, size_t length, int client_id) override;
    std::vector<uint8_t> serialize_response(const OrderResponse& response) override;
    bool can_handle(const uint8_t* data, size_t length) const override;
    void reset() override;
    
    
    // FIX MessageCracker interface
    void onMessage(const FIX44::NewOrderSingle& message, const FIX::SessionID& sessionID);

private:
    Order parse_new_order_single(const FIX44::NewOrderSingle& message, int client_id);
    FIX44::ExecutionReport create_execution_report(const OrderResponse& response);
    std::string generate_exec_id();
    bool is_complete_fix_message(const std::string& buffer) const;
    
    std::string message_buffer_;
    ParseResult last_result_;
    std::atomic<uint64_t> exec_id_counter_;
    int current_client_id_;
};

} // namespace exch_sim::order_gateway