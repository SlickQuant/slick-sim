#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace exch_sim::order_gateway {

enum class OrderSide : char {
    Buy = '1',
    Sell = '2'
};

enum class OrderType : char {
    Market = '1',
    Limit = '2',
    Stop = '3',
    StopLimit = '4'
};

enum class OrderStatus : char {
    New = '0',
    PartiallyFilled = '1',
    Filled = '2',
    DoneForDay = '3',
    Canceled = '4',
    Replaced = '5',
    PendingCancel = '6',
    Stopped = '7',
    Rejected = '8',
    Suspended = '9',
    PendingNew = 'A',
    Calculated = 'B',
    Expired = 'C',
    AcceptedForBidding = 'D',
    PendingReplace = 'E'
};

enum class TimeInForce : char {
    Day = '0',
    GoodTillCancel = '1',
    AtTheOpening = '2',
    ImmediateOrCancel = '3',
    FillOrKill = '4',
    GoodTillCrossing = '5',
    GoodTillDate = '6'
};

struct Order {
    std::string client_order_id;
    std::string symbol;
    OrderSide side;
    OrderType type;
    OrderStatus status;
    TimeInForce time_in_force;
    
    double price;
    uint64_t quantity;
    uint64_t filled_quantity;
    uint64_t remaining_quantity;
    
    int client_id;
    std::chrono::system_clock::time_point timestamp;
    
    Order() 
        : side(OrderSide::Buy)
        , type(OrderType::Market)
        , status(OrderStatus::New)
        , time_in_force(TimeInForce::Day)
        , price(0.0)
        , quantity(0)
        , filled_quantity(0)
        , remaining_quantity(0)
        , client_id(-1)
        , timestamp(std::chrono::system_clock::now()) 
    {}
};

struct OrderResponse {
    std::string client_order_id;
    std::string exec_id;
    OrderStatus order_status;
    double price;
    uint64_t quantity;
    uint64_t cum_qty;
    uint64_t leaves_qty;
    std::string reject_reason;
    std::chrono::system_clock::time_point timestamp;
    
    OrderResponse()
        : order_status(OrderStatus::New)
        , price(0.0)
        , quantity(0)
        , cum_qty(0)
        , leaves_qty(0)
        , timestamp(std::chrono::system_clock::now())
    {}
};

} // namespace exch_sim::order_gateway