#include "matching_engine.hpp"
#include <common/messages.hpp>
#include <common/order.hpp>

using namespace slick::sim::engine;

std::atomic<uint64_t> MatchingEngine::next_trade_id_{1};

void MatchingEngine::publishOrderAck(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::NEW;
    response.order_status = OrderStatus::NEW;
    response.order_type = order->type;
    response.price = order->price;
    response.qty = order->quantity;
    response.cum_qty = 0;
    response.leaves_qty = order->leaves_quantity;
    response.timestamp = order->last_update_time.time_since_epoch().count();
    order_response_queue_.publish(index);
}

void MatchingEngine::publishOrderExecution(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::TRADE;
    response.order_status = order->leaves_quantity ? OrderStatus::PARTIALLY_FILLED : OrderStatus::FILLED;
    response.order_type = order->type;
    response.price = order->price;
    response.qty = order->quantity;
    response.last_qty = order->last_filled_qty;
    response.cum_qty = order->filled_quantity;
    response.leaves_qty = order->leaves_quantity;
    response.timestamp = order->last_update_time.time_since_epoch().count();
    order_response_queue_.publish(index);
}

void MatchingEngine::publishOrderModify(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::REPLACED;
    response.order_status = OrderStatus::REPLACED;
    response.order_type = order->type;
    response.price = order->price;
    response.qty = order->quantity;
    response.cum_qty = order->filled_quantity;
    response.leaves_qty = order->leaves_quantity;
    response.timestamp = order->last_update_time.time_since_epoch().count();
    order_response_queue_.publish(index);
}

void MatchingEngine::publishOrderCancel(const Order *order) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::CANCELED;
    response.order_status = OrderStatus::CANCELED;
    response.order_type = order->type;
    response.price = order->price;
    response.qty = order->quantity;
    response.cum_qty = order->filled_quantity;
    response.leaves_qty = 0;
    response.timestamp = order->last_update_time.time_since_epoch().count();
    order_response_queue_.publish(index);
}

