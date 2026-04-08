#include "matching_engine.hpp"
#include <common/messages.hpp>
#include <common/order.hpp>

using namespace slick::sim::engine;

std::atomic<uint64_t> MatchingEngine::next_trade_id_{1};

void MatchingEngine::publishOrderAck(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::NEW;
    response.order_status = OrderStatus::NEW;
    response.order_type = order->type;
    response.time_in_force = order->time_in_force;
    response.price = order->price;
    response.last_fill_price = order->last_fill_price;
    response.avg_fill_price = order->avg_fill_price;
    response.qty = order->quantity;
    response.cum_qty = 0;
    response.leaves_qty = order->leaves_quantity;
    response.creation_time = order->created_time;
    response.timestamp = order->last_update_time;
    response.side = order->side;
    response.post_only = order->post_only;
    order_response_queue_.publish(index);
}

void MatchingEngine::publishOrderExecution(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::TRADE;
    response.order_status = order->leaves_quantity ? OrderStatus::PARTIALLY_FILLED : OrderStatus::FILLED;
    response.order_type = order->type;
    response.time_in_force = order->time_in_force;
    response.price = order->price;
    response.last_fill_price = order->last_fill_price;
    response.avg_fill_price = order->avg_fill_price;
    response.qty = order->quantity;
    response.last_qty = order->last_fill_qty;
    response.cum_qty = order->cum_quantity;
    response.leaves_qty = order->leaves_quantity;
    response.creation_time = order->created_time;
    response.timestamp = order->last_update_time;
    response.side = order->side;
    response.post_only = order->post_only;
    order_response_queue_.publish(index);
}

void MatchingEngine::publishOrderModify(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::REPLACED;
    response.order_status = OrderStatus::REPLACED;
    response.order_type = order->type;
    response.time_in_force = order->time_in_force;
    response.price = order->price;
    response.qty = order->quantity;
    response.last_qty = order->last_fill_qty;
    response.cum_qty = order->cum_quantity;
    response.leaves_qty = order->leaves_quantity;
    response.creation_time = order->created_time;
    response.timestamp = order->last_update_time;
    response.side = order->side;
    response.post_only = order->post_only;
    order_response_queue_.publish(index);
}

void MatchingEngine::publishOrderCancel(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::CANCELED;
    response.order_status = OrderStatus::CANCELED;
    response.order_type = order->type;
    response.time_in_force = order->time_in_force;
    response.price = order->price;
    response.avg_fill_price = order->avg_fill_price;
    response.qty = order->quantity;
    response.cum_qty = order->cum_quantity;
    response.leaves_qty = 0;
    response.creation_time = order->created_time;
    response.timestamp = order->last_update_time;
    response.side = order->side;
    response.post_only = order->post_only;
    order_response_queue_.publish(index);
}

