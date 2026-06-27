#include "exchange.hpp"
#include <common/symbol_manager.hpp>
#include <slick/logger.hpp>
#include <utils/order.hpp>
#include <utils/timestamp.hpp>

using namespace slick::sim::exch;
using namespace slick::sim;
using namespace slick::sim::utils;
using namespace slick::sim::md_publisher;
using namespace slick::sim::order_gateway;

namespace {
    auto &symbol_mgr = SymbolManager::instance();
}

Exchange::Exchange(
    Venue venue,
    const nlohmann::json &config
) 
    : venue_(venue)
    , request_queue_(config.value("request_queue_size", 1048576), config.contains("request_queue_shm_name") ? config["request_queue_shm_name"].get_ref<const std::string&>().c_str() : nullptr)
    , response_queue_(config.value("response_queue_size", 1048576), config.contains("response_queue_shm_name") ? config["response_queue_shm_name"].get_ref<const std::string&>().c_str() : nullptr)
    , md_queue_(config.value("md_queue_size", 16777216), config.contains("md_queue_shm_name") ? config["md_queue_shm_name"].get_ref<const std::string&>().c_str() : nullptr)
{
    if (!config.contains("order_gateway")) {
        throw std::runtime_error(std::format("Exchange {} Missing order_gateway config", to_string(venue)));
    }
    if (!config.contains("md_publisher")) {
        throw std::runtime_error(std::format("Exchange {} Missing md_publisher config", to_string(venue)));
    }
}

void Exchange::start() 
{
    run_.store(true, std::memory_order_release);

    for (auto &og : order_gateways_) {
        og->start();
    }
    md_publisher_->start();

    // start MD thread
    // if (md_feed_)
    // {
    //     md_thread_ = std::thread([this]() { processMdData(); });
    // }

    thread_ = std::thread([this]() { processRequest(); });
}

void Exchange::stop() {
    run_.store(false, std::memory_order_release);
    if (md_feed_ && md_thread_.joinable()) {
        md_thread_.join();
    }

    for (auto &og : order_gateways_) {
        og->stop();
    }
    md_publisher_->stop();

    if (thread_.joinable()) {
        thread_.join();
    }
}

void Exchange::processRequest()
{
    // process order rquests
    auto [request, size] = request_queue_.read(order_request_cursor_);
    if (request)
    {
        switch(request->msg_type)
        {
        case MessageType::NEW_ORDER_SINGLE: {
            handleNewOrderRequest(*request);
            break;
        }
        case MessageType::ORDER_REPLACE_REQUEST: {
            handleModifyOrderRequest(*request);
            break;
        }
        case MessageType::ORDER_CANCEL_REQUEST: {
            handleCancelOrderRequest(*request);
            break;
        }
        case MessageType::MD_SUBSCRIPTION: {
            handleMdSubscription(*request);
        }
        }
    }
}

void Exchange::handleNewOrderRequest(const Request &request) {
    auto *symbol = symbol_mgr.getSymbol(request.symbol);
    if (!symbol)
    {
        rejectNewOrderRequest(request, OrdRejectReason::UNKNOWN_CONTRACT);
    }
    else
    {
        auto &msg  = request.add_order;
        if (msg.client_order_id[0] != '\0') {
            auto *order = symbol->order_book_->findOrderByClientOrderId(msg.client_order_id);
            if (order) [[unlikely]] {
                LOG_WARN(std::format("ClientOrderId {} already exist", msg.client_order_id));
                rejectNewOrderRequest(request, OrdRejectReason::CLIENT_ORDER_ID_ALREADY_EXISTS);
                return; 
            }
        }
        Order *order = symbol->order_book_->allocateOrder();
        order->symbol = symbol->symbol_;
        order->client_order_id = msg.client_order_id;
        order->user_id = msg.user_id;
        order->side = msg.side;
        order->type = msg.type;
        order->time_in_force = msg.time_in_force;
        order->price = msg.price;
        order->quantity = msg.qty;
        order->leaves_quantity = msg.qty;
        order->type = msg.type;
        order->status = OrderStatus::PENDING_NEW;
        
        sendOrderNewPending(order, request.time_stamp);

        auto [reject_reason, trade_summaries] = symbol->addOrder(order, request.time_stamp);

        if (!trade_summaries.empty()) {
            for (const auto &summary : trade_summaries) {
                publishTradeSummary(symbol->symbol_.c_str(), summary);
            }
        }

        if (reject_reason != OrdRejectReason::NONE && reject_reason != OrdRejectReason::FOK_CANNOT_FILL) {
            rejectNewOrderRequest(request, reject_reason);
        }
        
        if (!symbol->md_level_update_cache_.empty()) {
            publishLevelUpdate(symbol->symbol_.c_str(), symbol->md_level_update_cache_);
            symbol->md_level_update_cache_.clear();
        }

        if (!symbol->md_order_update_cache_.empty()) {
            // TODO: publish MD order update
            symbol->md_order_update_cache_.clear();
        }
    }
}

void Exchange::handleModifyOrderRequest(const Request &request) {
    auto *symbol = symbol_mgr.getSymbol(request.symbol);
    if (!symbol)
    {
        rejectModifyOrderRequest(request, OrdRejectReason::UNKNOWN_CONTRACT);
    }
    else
    {
        Order *order;
        if (request.modify_order.client_order_id[0] != '\0') {
            order = symbol->findOrderByClientOrderId(request.modify_order.client_order_id);
        }
        else {
            order = symbol->findOrderByOrderId(request.modify_order.order_id);
        }

        if (!order) {
            LOG_WARN(std::format("{} OrderId {} not found for modification", symbol->symbol_, request.modify_order.order_id));
            rejectModifyOrderRequest(request, OrdRejectReason::UNKNOWN_ORDER);
            return;
        }
        order->status = OrderStatus::PENDING_REPLACE;
        order->last_update_time = utils::get_current_time_ns();
        sendOrderReplacePending(order, request.time_stamp);
        auto [reject_reason, trade_summaries] = symbol->modifyOrder(order, request.modify_order.new_price, request.modify_order.new_qty, request.time_stamp);
        
        if (!trade_summaries.empty()) {
            for (const auto &summary : trade_summaries) {
                publishTradeSummary(symbol->symbol_.c_str(), summary);
            }
        }
        
        if (reject_reason != OrdRejectReason::NONE) {
            rejectModifyOrderRequest(request, reject_reason);
        }

        if (!symbol->md_level_update_cache_.empty()) {
            publishLevelUpdate(symbol->symbol_.c_str(), symbol->md_level_update_cache_);
            symbol->md_level_update_cache_.clear();
        }

        if (!symbol->md_order_update_cache_.empty()) {
            // TODO: publish MD order update
            symbol->md_order_update_cache_.clear();
        }
    }
}

void Exchange::handleCancelOrderRequest(const Request &request) {
    auto *symbol = symbol_mgr.getSymbol(request.symbol);
    if (!symbol)
    {
        rejectCancelOrderRequest(request, OrdRejectReason::UNKNOWN_CONTRACT);
    }
    else
    {
        Order *order;
        if (request.cancel_order.client_order_id[0] != '\0') {
            order = symbol->findOrderByClientOrderId(request.cancel_order.client_order_id);
        }
        else {
            order = symbol->findOrderByOrderId(request.cancel_order.order_id);
        }

        if (!order) {
            LOG_WARN(std::format("{} OrderId {} not found for cancellation", symbol->symbol_, request.cancel_order.order_id));
            rejectCancelOrderRequest(request, OrdRejectReason::UNKNOWN_ORDER);
            return;
        }
        order->status = OrderStatus::PENDING_CANCEL;
        sendOrderCancelPending(order, request.time_stamp);
        symbol->cancelOrder(order, request.time_stamp);
    }
}

void Exchange::rejectMdSubscription(const Request &request, [[maybe_unused]] MDSubscriptionRejectReason reason) {
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse));
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_queue_[index]);
    update->type = MDUpdateType::SUB_RESPONSE;
    update->venue = venue_;
    memcpy(update->symbol, request.symbol, sizeof(update->symbol));
    auto *rsp = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    rsp->channel = request.md_subscription.channel;
    md_queue_.publish(index, sz);
}

void Exchange::rejectNewOrderRequest(const Request &request, OrdRejectReason reason) {
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    std::memcpy(response.symbol, request.symbol, sizeof(response.symbol));
    std::memcpy(response.user_id, request.add_order.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, request.add_order.client_order_id, sizeof(response.client_order_id));
    std::memset(response.order_id, 0, sizeof(response.order_id));
    response.request_time = request.time_stamp;
    response.response_type = MessageType::REJECT;
    response.order_status = OrderStatus::REJECTED;
    response.exec_type = ExecType::REJECTED;
    response.price = request.add_order.price;
    response.qty = request.add_order.qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    response.reject_reason = reason;
    response.timestamp = utils::get_current_time_ns();
    response_queue_.publish(index);
}

void Exchange::rejectModifyOrderRequest(const Request &request, OrdRejectReason reason) {
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    std::memcpy(response.symbol, request.symbol, sizeof(response.symbol));
    std::memcpy(response.user_id, request.add_order.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, request.add_order.client_order_id, sizeof(response.client_order_id));
    std::memset(response.order_id, 0, sizeof(response.order_id));
    response.response_type = MessageType::REJECT;
    response.order_status = OrderStatus::REJECTED;
    response.exec_type = ExecType::REJECTED;
    response.price = request.add_order.price;
    response.qty = request.add_order.qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    response.reject_reason = reason;
    response.timestamp = utils::get_current_time_ns();
    response.request_time = request.time_stamp;
    response_queue_.publish(index);
}

void Exchange::rejectCancelOrderRequest(const Request &request, OrdRejectReason reason) {
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    std::memcpy(response.symbol, request.symbol, sizeof(response.symbol));
    std::memcpy(response.user_id, request.add_order.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, request.add_order.client_order_id, sizeof(response.client_order_id));
    std::memset(response.order_id, 0, sizeof(response.order_id));
    response.response_type = MessageType::REJECT;
    response.order_status = OrderStatus::REJECTED;
    response.exec_type = ExecType::REJECTED;
    response.price = request.add_order.price;
    response.qty = request.add_order.qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    response.reject_reason = reason;
    response.timestamp = utils::get_current_time_ns();
    response.request_time = request.time_stamp;
    response_queue_.publish(index);
}

void Exchange::publishMDBookUpdate([[maybe_unused]] symid_t sid, OrderBook &order_book, const std::array<uint8_t, 2> &indices) {
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDBookUpdate));
    uint64_t index = md_queue_.reserve(sz);
    MarketDataUpdate* update = reinterpret_cast<MarketDataUpdate*>(md_queue_[index]);
    update->type = MDUpdateType::BOOK;
    update->venue = venue_;
    std::strncpy(update->symbol, order_book.symbolName().data(), sizeof(update->symbol));
    MDBookUpdate* book_update = reinterpret_cast<MDBookUpdate*>(update->data);
    book_update->update_index[0] = indices[0];
    book_update->update_index[1] = indices[1];
    order_book.populateMDBookUpdate(*book_update);
    md_queue_.publish(index, sz);
}

void Exchange::publishLevelUpdate(const char* symbol, const std::vector<MDLevel> &level_updates) {
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDLevelUpdate) + level_updates.size() * sizeof(MDLevel));
    auto index = md_queue_.reserve(sz);
    auto* update = reinterpret_cast<MarketDataUpdate*>(md_queue_[index]);
    update->type = MDUpdateType::LEVEL;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol, sizeof(update->symbol));
    auto* level_update = reinterpret_cast<MDLevelUpdate*>(update->data);
    level_update->num_level_update = static_cast<uint32_t>(level_updates.size());
    std::memcpy(level_update->levels, level_updates.data(), level_updates.size() * sizeof(MDLevel));
    md_queue_.publish(index, sz);
}

void Exchange::publishMDOrderUpdate(const char* symbol, const std::vector<MDOrder> &order_updates) {
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDOrderUpdate) + order_updates.size() * sizeof(MDOrder));
    auto index = md_queue_.reserve(sz);
    auto* update = reinterpret_cast<MarketDataUpdate*>(md_queue_[index]);
    update->type = MDUpdateType::ORDER;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol, sizeof(update->symbol));
    auto* order_update = reinterpret_cast<MDOrderUpdate*>(update->data);
    order_update->num_orders = static_cast<uint32_t>(order_updates.size());
    std::memcpy(order_update->orders, order_updates.data(), order_updates.size() * sizeof(MDOrder));
    md_queue_.publish(index, sz);
}

void Exchange::publishMDTrades(const char* symbol, const std::vector<MDTrade> &trade_updates) {
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDTradeUpdate) + trade_updates.size() * sizeof(MDTrade));
    auto index = md_queue_.reserve(sz);
    MarketDataUpdate* update = reinterpret_cast<MarketDataUpdate*>(md_queue_[index]);
    update->type = MDUpdateType::TRADE;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol, sizeof(update->symbol));
    MDTradeUpdate* trade_update = reinterpret_cast<MDTradeUpdate*>(update->data);
    trade_update->num_trades = static_cast<uint32_t>(trade_updates.size());
    std::memcpy(trade_update->trades, trade_updates.data(), trade_updates.size() * sizeof(MDTrade));
    md_queue_.publish(index, sz);
}

void Exchange::publishTradeSummary(const char* symbol, const TradeSummaryInfo &trade_summary) {
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(TradeSummary) + trade_summary.num_orders * sizeof(Trade));
    auto index = md_queue_.reserve(sz);
    auto update = reinterpret_cast<MarketDataUpdate*>(md_queue_[index]);
    update->type = MDUpdateType::TRADE_SUMMARY;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol, sizeof(update->symbol));
    auto summary = reinterpret_cast<TradeSummary*>(update->data);
    summary->timestamp = trade_summary.timestamp;
    summary->trade_id = trade_summary.trade_id;
    summary->aggressor_side = trade_summary.aggressor_side;
    summary->price = trade_summary.price;
    summary->qty = trade_summary.qty;
    summary->num_orders = trade_summary.num_orders;
    for (size_t i = 0; i < trade_summary.Trades.size(); ++i) {
        summary->trades[i].order_id = trade_summary.Trades[i].order_id;
        summary->trades[i].qty = trade_summary.Trades[i].qty;
    }
    md_queue_.publish(index, sz);
}

void Exchange::sendOrderNewPending(const Order *order, time_t request_time) {
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::NEW;
    response.order_status = OrderStatus::PENDING_NEW;
    response.order_type = order->type;
    response.price = order->price;
    response.avg_fill_price = order->avg_fill_price;
    response.qty = order->quantity;
    response.cum_qty = 0;
    response.leaves_qty = order->leaves_quantity;
    assert(response.leaves_qty == response.qty);
    response.timestamp = order->created_time;
    response.request_time = request_time;
    response.side = order->side;
    response.post_only = order->post_only;
    response_queue_.publish(index);
}

void Exchange::sendOrderReplacePending(const Order *order, time_t request_time) {    
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::PENDING_REPLACE;
    response.order_status = OrderStatus::PENDING_REPLACE;
    response.order_type = order->type;
    response.request_time = request_time;
    response.price = order->price;
    response.avg_fill_price = order->avg_fill_price;
    response.qty = order->quantity;
    response.cum_qty = 0;
    response.leaves_qty = order->leaves_quantity;
    response.timestamp = order->created_time;
    response.side = order->side;
    response.post_only = order->post_only;
    response_queue_.publish(index);
}

void Exchange::sendOrderCancelPending(const Order *order, time_t request_time) {
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
    memcpy(response.user_id, order->user_id.c_str(), sizeof(response.user_id));
    memcpy(response.client_order_id, order->client_order_id.c_str(), sizeof(response.client_order_id));
    memcpy(response.order_id, order->order_id.c_str(), sizeof(response.order_id));
    response.response_type = MessageType::EXECUTION_REPORT;
    response.exec_type = ExecType::PENDING_CANCEL;
    response.order_status = OrderStatus::PENDING_CANCEL;
    response.order_type = order->type;
    response.request_time = request_time;
    response.price = order->price;
    response.avg_fill_price = order->avg_fill_price;
    response.last_fill_price = order->last_fill_price;
    response.last_qty = order->last_fill_qty;
    response.qty = order->quantity;
    response.cum_qty = 0;
    response.leaves_qty = order->leaves_quantity;
    assert(response.leaves_qty == response.qty);
    response.timestamp = order->created_time;
    response.side = order->side;
    response.post_only = order->post_only;
    response_queue_.publish(index);
}