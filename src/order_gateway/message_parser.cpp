#include "message_parser.h"
#include "fix_parser.h"
#include "sbe_parser.h"
#include "json_parser.h"
#include <slick_logger/logger.hpp>

namespace exch_sim::order_gateway {

OrderSide MessageParser::parse_side(const std::string& side_str) {
    if (side_str == "1" || side_str == "B" || side_str == "BUY" || side_str == "buy") {
        return OrderSide::Buy;
    } else if (side_str == "2" || side_str == "S" || side_str == "SELL" || side_str == "sell") {
        return OrderSide::Sell;
    }
    return OrderSide::Buy; // Default
}

OrderType MessageParser::parse_order_type(const std::string& type_str) {
    if (type_str == "1" || type_str == "MARKET" || type_str == "market") {
        return OrderType::Market;
    } else if (type_str == "2" || type_str == "LIMIT" || type_str == "limit") {
        return OrderType::Limit;
    } else if (type_str == "3" || type_str == "STOP" || type_str == "stop") {
        return OrderType::Stop;
    } else if (type_str == "4" || type_str == "STOP_LIMIT" || type_str == "stop_limit") {
        return OrderType::StopLimit;
    }
    return OrderType::Market; // Default
}

TimeInForce MessageParser::parse_time_in_force(const std::string& tif_str) {
    if (tif_str == "0" || tif_str == "DAY" || tif_str == "day") {
        return TimeInForce::Day;
    } else if (tif_str == "1" || tif_str == "GTC" || tif_str == "gtc") {
        return TimeInForce::GoodTillCancel;
    } else if (tif_str == "3" || tif_str == "IOC" || tif_str == "ioc") {
        return TimeInForce::ImmediateOrCancel;
    } else if (tif_str == "4" || tif_str == "FOK" || tif_str == "fok") {
        return TimeInForce::FillOrKill;
    }
    return TimeInForce::Day; // Default
}

std::string MessageParser::side_to_string(OrderSide side) {
    return std::string(1, static_cast<char>(side));
}

std::string MessageParser::order_type_to_string(OrderType type) {
    return std::string(1, static_cast<char>(type));
}

std::string MessageParser::order_status_to_string(OrderStatus status) {
    return std::string(1, static_cast<char>(status));
}

std::unique_ptr<MessageParser> MessageParserFactory::create_parser(ProtocolType type) {
    switch (type) {
        case ProtocolType::FIX:
            return std::make_unique<FixParser>();
        case ProtocolType::SBE:
            return std::make_unique<SbeParser>();
        case ProtocolType::JSON:
            return std::make_unique<JsonParser>();
        default:
            LOG_ERROR("Unsupported protocol type: {}", static_cast<int>(type));
            return nullptr;
    }
}

std::unique_ptr<MessageParser> MessageParserFactory::create_parser_from_data(const uint8_t* data, size_t length) {
    if (length == 0) return nullptr;
    
    // Try each available parser to see which one can handle the data
    std::vector<std::unique_ptr<MessageParser>> parsers;
    parsers.push_back(std::make_unique<JsonParser>());
    parsers.push_back(std::make_unique<FixParser>());
    parsers.push_back(std::make_unique<SbeParser>());
    
    for (auto& parser : parsers) {
        if (parser->can_handle(data, length)) {
            LOG_DEBUG("Auto-detected protocol: {}", static_cast<int>(parser->get_protocol_type()));
            return std::move(parser);
        }
    }
    
    LOG_WARN("No parser found for data, defaulting to JSON");
    return std::make_unique<JsonParser>();
}

} // namespace exch_sim::order_gateway