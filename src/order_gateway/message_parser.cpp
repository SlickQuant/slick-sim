#include "message_parser.hpp"
#include "fix_parser.hpp"
#include "sbe_parser.hpp"
#include "json_parser.hpp"
#include <slick/logger.hpp>

namespace slick::sim::order_gateway {

Side MessageParser::parse_side(const std::string& side_str) {
    if (side_str == "1" || side_str == "B" || side_str == "BUY" || side_str == "buy") {
        return Side::BUY;
    } else if (side_str == "2" || side_str == "S" || side_str == "SELL" || side_str == "sell") {
        return Side::SELL;
    }
    return Side::BUY; // Default
}

OrderType MessageParser::parse_order_type(const std::string& type_str) {
    if (type_str == "1" || type_str == "MARKET" || type_str == "market") {
        return OrderType::MARKET;
    } else if (type_str == "2" || type_str == "LIMIT" || type_str == "limit") {
        return OrderType::LIMIT;
    } else if (type_str == "3" || type_str == "STOP" || type_str == "stop") {
        return OrderType::STOP;
    } else if (type_str == "4" || type_str == "STOP_LIMIT" || type_str == "stop_limit") {
        return OrderType::STOP_LIMIT;
    }
    return OrderType::MARKET; // Default
}

TimeInForce MessageParser::parse_time_in_force(const std::string& tif_str) {
    if (tif_str == "0" || tif_str == "DAY" || tif_str == "day") {
        return TimeInForce::DAY;
    } else if (tif_str == "1" || tif_str == "GTC" || tif_str == "gtc") {
        return TimeInForce::GOOD_TILL_CANCEL;
    } else if (tif_str == "3" || tif_str == "IOC" || tif_str == "ioc") {
        return TimeInForce::IMMEDIATE_OR_CANCEL;
    } else if (tif_str == "4" || tif_str == "FOK" || tif_str == "fok") {
        return TimeInForce::FILL_OR_KILL;
    }
    return TimeInForce::DAY; // Default
}

std::string MessageParser::side_to_string(Side side) {
    return std::to_string(static_cast<uint8_t>(side));
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

} // namespace slick::sim::order_gateway