#include "exchange.hpp"
#include <common/symbol_manager.hpp>
#include <slick/logger.hpp>

using namespace slick::sim::exch;
using namespace slick::sim;

namespace {
    auto &symbol_mgr = SymbolManager::instance();
}

Exchange::Exchange(
    Venue venue,
    const nlohmann::json &config,
    slick::SlickQueue<Request> &request_queue,
    slick::SlickQueue<OrderResponse> &order_response_queue,
    slick::SlickQueue<uint8_t> &md_update_queue
) 
    : venue_(venue)
    , config_(config)
    , request_queue_(request_queue)
    , order_response_queue_(order_response_queue)
    , md_update_queue_(md_update_queue)
{
}

Exchange::~Exchange()
{
    stop();
}

void Exchange::run() 
{
    run_.store(true, std::memory_order_release);

    // start MD thread
    if (md_feed_)
    {
        md_thread_ = std::thread([this]() { processMdData(); });
    }

    thread_ = std::thread([this]() { processRequest(); });
}

void Exchange::stop()
{
    run_.store(false, std::memory_order_release);
    if (md_feed_ && md_thread_.joinable())
    {
        md_thread_.join();
    }

    if (thread_.joinable())
    {
        thread_.join();
    }
}

void Exchange::processMdData()
{
    while(run_.load(std::memory_order_relaxed))
    {

    }
}

void Exchange::processRequest()
{
    // process order rquests
    auto [request, size] = request_queue_.read(order_request_cursor_);
    if (request)
    {
        // auto *symbol = symbol_mgr.getSymbol(request->symbol);
        // if (!symbol)
        // {
        //     if (request->msg_type == MessageType::MD_SUBSCRIPTION) {

        //         rejectMdSubscription(*request, MDSubscriptionRejectReason::UNKNOWN_CONTRACT);
        //     }
        //     else {
        //         rejectNewOrderRequest(*request, OrdRejectReason::UNKNOWN_CONTRACT);
        //     }
        // }
        // else
        // {
        switch(request->msg_type)
        {
        case MessageType::NEW_ORDER_SINGLE: {
            auto *symbol = symbol_mgr.getSymbol(request->symbol);
            if (!symbol)
            {
                rejectNewOrderRequest(*request, OrdRejectReason::UNKNOWN_CONTRACT);
            }
            else
            {
                sendOrderNewPending(*request);
                auto [reject_reason, order, fills] = symbol->addOrder(request->add_order);
                if (reject_reason == OrdRejectReason::NONE) {
                    sendOrderAck(order);
                }
                else {
                    rejectNewOrderRequest(*request, reject_reason);
                }
            }
            break;
        }
        case MessageType::ORDER_REPLACE_REQUEST: {
            auto *symbol = symbol_mgr.getSymbol(request->symbol);
            if (!symbol)
            {
                rejectModifyOrderRequest(*request, OrdRejectReason::UNKNOWN_CONTRACT);
            }
            else
            {
                sendOrderReplacePending(*request);
                auto [reject_reason, order, fills] = symbol->modifyOrder(request->modify_order);
                if (reject_reason == OrdRejectReason::NONE) {
                }
            }
            break;
        }
        case MessageType::ORDER_CANCEL_REQUEST: {
            auto *symbol = symbol_mgr.getSymbol(request->symbol);
            if (!symbol)
            {
                rejectCancelOrderRequest(*request, OrdRejectReason::UNKNOWN_CONTRACT);
            }
            else
            {
                sendOrderCancelPending(*request);
                auto [reject_reason, order] = symbol->cancelOrder(request->cancel_order);
                if (reject_reason == OrdRejectReason::NONE) {
                }
            }
            break;
        }
        case MessageType::MD_SUBSCRIPTION: {
            handleMdSubscription(*request);
        }
        }
        // }
    }
}

void Exchange::rejectMdSubscription(const Request &request, MDSubscriptionRejectReason reason) {
    auto sz = sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse);
    auto index = md_update_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate*>(md_update_queue_[index]);
    update->type = MDUpdateType::SUB_RESPONSE;
    update->venue = venue_;
    memcpy(update->symbol, request.symbol, sizeof(update->symbol));
    auto *rsp = reinterpret_cast<MDSubscriptionResponse*>(update->data);
    rsp->channel = request.md_subscription.channel;
    md_update_queue_.publish(index, sz);
}

void Exchange::rejectNewOrderRequest(const Request &request, OrdRejectReason reason) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    memcpy(response.user_id, request.add_order.user_id, sizeof(response.user_id));
    memcpy(response.client_order_id, request.add_order.client_order_id, sizeof(response.client_order_id));
    memset(response.order_id, 0, sizeof(response.order_id));
    response.request_type = MessageType::NEW_ORDER_SINGLE;
    response.order_status = OrderStatus::REJECTED;
    response.price = request.add_order.price;
    response.qty = request.add_order.qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    OrdRejectReason reject_reason = reason;
    uint64_t timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    order_response_queue_.publish(index);
}

void Exchange::rejectModifyOrderRequest(const Request &request, OrdRejectReason reason) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    std::memcpy(response.user_id, request.add_order.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, request.add_order.client_order_id, sizeof(response.client_order_id));
    std::memset(response.order_id, 0, sizeof(response.order_id));
    response.order_status = OrderStatus::REJECTED;
    response.price = request.add_order.price;
    response.qty = request.add_order.qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    OrdRejectReason reject_reason = reason;
    uint64_t timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    order_response_queue_.publish(index);
}

void Exchange::rejectCancelOrderRequest(const Request &request, OrdRejectReason reason) {
    auto index = order_response_queue_.reserve();
    auto &response = *order_response_queue_[index];
    std::memcpy(response.user_id, request.add_order.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, request.add_order.client_order_id, sizeof(response.client_order_id));
    std::memset(response.order_id, 0, sizeof(response.order_id));
    response.order_status = OrderStatus::REJECTED;
    response.price = request.add_order.price;
    response.qty = request.add_order.qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    OrdRejectReason reject_reason = reason;
    uint64_t timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    order_response_queue_.publish(index);
}

void Exchange::publishMDBookUpdate(symid_t sid, OrderBook &order_book, const std::array<uint8_t, 2> &indices) {
    auto sz = sizeof(MarketDataUpdate) + sizeof(MDBookUpdate);
    auto index = md_update_queue_.reserve(sz);
    MarketDataUpdate* update = reinterpret_cast<MarketDataUpdate*>(md_update_queue_[index]);
    update->type = MDUpdateType::BOOK;
    update->venue = venue_;
    std::memcpy(update->symbol, order_book.symbol().data(), sizeof(update->symbol));
    MDBookUpdate* book_update = reinterpret_cast<MDBookUpdate*>(update->data);
    book_update->update_index[0] = indices[0];
    book_update->update_index[1] = indices[1];
    order_book.populateMDBookUpdate(*book_update);
    md_update_queue_.publish(index, sz);
}

void Exchange::publishLevelUpdate(const char* symbol, const std::vector<MDLevel> &level_updates) {
    auto sz = sizeof(MarketDataUpdate) + sizeof(MDLevelUpdate) + level_updates.size() * sizeof(MDLevel);
    auto index = md_update_queue_.reserve(sz);
    MarketDataUpdate* update = reinterpret_cast<MarketDataUpdate*>(md_update_queue_[index]);
    update->type = MDUpdateType::LEVEL;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol, sizeof(update->symbol));
    MDLevelUpdate* level_update = reinterpret_cast<MDLevelUpdate*>(update->data);
    level_update->num_level_update = static_cast<uint32_t>(level_updates.size());
    std::memcpy(level_update->levels, level_updates.data(), level_updates.size() * sizeof(MDLevel));
    md_update_queue_.publish(index, sz);
}

void Exchange::publishMDTrades(const char* symbol, const std::vector<MDTrade> &trade_updates) {
    auto sz = sizeof(MarketDataUpdate) + sizeof(MDTradeUpdate) + trade_updates.size() * sizeof(MDTrade);
    auto index = md_update_queue_.reserve(sz);
    MarketDataUpdate* update = reinterpret_cast<MarketDataUpdate*>(md_update_queue_[index]);
    update->type = MDUpdateType::TRADE;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol, sizeof(update->symbol));
    MDTradeUpdate* trade_update = reinterpret_cast<MDTradeUpdate*>(update->data);
    trade_update->num_trades = static_cast<uint32_t>(trade_updates.size());
    std::memcpy(trade_update->trades, trade_updates.data(), trade_updates.size() * sizeof(MDTrade));
    md_update_queue_.publish(index, sz);
}

void Exchange::sendOrderNewPending(const Request &request) {}
void Exchange::sendOrderAck(const Order *order) {}
void Exchange::sendOrderReplacePending(const Request &request) {}
void Exchange::sendOrderReplaced(const Order *order) {}
void Exchange::sendOrderCancelPending(const Request &request) {}
void Exchange::sendOrderCanceled(const Order *order) {}