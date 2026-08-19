#include <slick/logger.hpp>
#include "exchange.hpp"
#include <common/symbol_manager.hpp>
#include <utils/order.hpp>
#include <utils/timestamp.hpp>

using namespace slick::sim::exch;
using namespace slick::sim;
using namespace slick::sim::utils;
using namespace slick::sim::md_publisher;
using namespace slick::sim::order_gateway;

namespace
{
    auto &symbol_mgr = SymbolManager::instance();
}

Exchange::Exchange(
    Venue venue,
    const nlohmann::json &config)
    : venue_(venue), request_queue_(config.value("request_queue_size", 1048576), config.contains("request_queue_shm_name") ? config["request_queue_shm_name"].get_ref<const std::string &>().c_str() : nullptr), response_queue_(config.value("response_queue_size", 1048576), config.contains("response_queue_shm_name") ? config["response_queue_shm_name"].get_ref<const std::string &>().c_str() : nullptr), md_queue_(config.value("md_queue_size", 16777216), config.contains("md_queue_shm_name") ? config["md_queue_shm_name"].get_ref<const std::string &>().c_str() : nullptr), enabled_(config.value("enabled", true))
{
    if (!config.contains("order_gateway"))
    {
        throw std::runtime_error(std::format("Exchange {} Missing order_gateway config", to_string(venue)));
    }
    if (!config.contains("md_publisher"))
    {
        throw std::runtime_error(std::format("Exchange {} Missing md_publisher config", to_string(venue)));
    }

    if (config.contains("self_match_prevention"))
    {
        const auto &mode = config["self_match_prevention"].get_ref<const std::string &>();
        smp_mode_ = to_smp_mode(mode);
        if (smp_mode_ == SelfMatchPreventionMode::NONE && mode != "none")
        {
            LOG_WARN("Exchange {} unknown self_match_prevention '{}', self-match prevention disabled. "
                     "Expected one of: none, cancel_resting, cancel_newest",
                     to_string(venue), mode);
        }
        else
        {
            LOG_INFO("Exchange {} self-match prevention: {}", to_string(venue), to_string(smp_mode_));
        }
    }
}

void Exchange::start()
{
    if (!enabled_)
    {
        LOG_WARN("Exchange {} is disabled, skipping start", to_string(venue_));
        return;
    }

    run_.store(true, std::memory_order_release);

    for (auto &og : order_gateways_)
    {
        og->start();
    }

    if (md_publisher_)
    {
        md_publisher_->start();
    }

    // start MD thread
    // if (md_feed_)
    // {
    //     md_thread_ = std::thread([this]() { processMdData(); });
    // }

    thread_ = std::thread([this]() { processRequest(); });
}

void Exchange::stop()
{
    run_.store(false, std::memory_order_release);
    if (md_feed_ && md_thread_.joinable())
    {
        md_thread_.join();
    }

    for (auto &og : order_gateways_)
    {
        og->stop();
    }
    md_publisher_->stop();

    if (thread_.joinable())
    {
        thread_.join();
    }
}

void Exchange::processRequest()
{
    // process order rquests
    auto [request, size] = request_queue_.read(order_request_cursor_);
    if (request)
    {
        switch (request->msg_type)
        {
        case MessageType::NEW_ORDER_SINGLE:
        {
            handleNewOrderRequest(*request);
            break;
        }
        case MessageType::ORDER_REPLACE_REQUEST:
        {
            handleModifyOrderRequest(*request);
            break;
        }
        case MessageType::ORDER_CANCEL_REQUEST:
        {
            handleCancelOrderRequest(*request);
            break;
        }
        case MessageType::MD_SUBSCRIPTION:
        {
            handleMdSubscription(*request);
            break;
        }
        case MessageType::MD_UNSUBSCRIPTION:
        {
            handleMdUnsubscription(*request);
            break;
        }
        default:
            break;
        }
    }
}

int Exchange::clientIdFor(const char *user_id)
{
    auto len = strnlen(user_id, sizeof(AddOrderMessage::user_id));
    if (len == 0)
    {
        // No identity to compare, so this order can never self-match.
        return -1;
    }

    // Heterogeneous lookup first: this runs once per new order, and the same
    // handful of users submit all of them, so the hit path must not build a key.
    std::string_view key(user_id, len);
    if (auto it = client_ids_.find(key); it != client_ids_.end())
    {
        return it->second;
    }

    // First order from this user - only now is a key worth materialising, and it
    // is stored inline rather than on the heap.
    return client_ids_.emplace(key, next_client_id_++).first->second;
}

void Exchange::handleNewOrderRequest(const Request &request)
{
    auto *symbol = symbol_mgr.getSymbol(request.symbol, venue_);
    if (!symbol)
    {
        rejectNewOrderRequest(request, OrdRejectReason::UNKNOWN_CONTRACT);
    }
    else
    {
        auto &msg = request.add_order;
        if (msg.client_order_id[0] != '\0')
        {
            auto *order = symbol->order_book_->findOrderByClientOrderId(msg.client_order_id);
            if (order) [[unlikely]]
            {
                LOG_WARN(std::format("ClientOrderId {} already exist", msg.client_order_id));
                rejectNewOrderRequest(request, OrdRejectReason::CLIENT_ORDER_ID_ALREADY_EXISTS);
                return;
            }
        }
        Order *order = symbol->order_book_->allocateOrder();
        order->symbol = symbol->symbol_;
        order->client_order_id = msg.client_order_id;
        order->user_id = msg.user_id;
        // Identity for self-match prevention. Scoped to the authenticated user,
        // matching how real venues scope SMP - to the account, not the connection.
        order->client_id = clientIdFor(msg.user_id);
        order->side = msg.side;
        order->type = msg.type;
        order->time_in_force = msg.time_in_force;
        order->price = msg.price;
        order->resetFillAccounting(msg.qty);
        order->type = msg.type;
        order->status = OrderStatus::PENDING_NEW;

        sendOrderNewPending(order, request.time_stamp);

        auto [reject_reason, trade_summaries] = symbol->addOrder(order, request.time_stamp);

        if (!trade_summaries.empty())
        {
            for (const auto &summary : trade_summaries)
            {
                publishTradeSummary(symbol, summary);
            }
        }

        if (reject_reason != OrdRejectReason::NONE && reject_reason != OrdRejectReason::FOK_CANNOT_FILL)
        {
            rejectNewOrderRequest(request, reject_reason);
        }

        if (!symbol->md_level_update_cache_.empty())
        {
            publishLevelUpdate(symbol->symbol_, symbol->md_level_update_cache_);
            symbol->md_level_update_cache_.clear();
        }

        if (!symbol->md_order_update_cache_.empty())
        {
            // TODO: publish MD order update
            symbol->md_order_update_cache_.clear();
        }
    }
}

void Exchange::handleModifyOrderRequest(const Request &request)
{
    auto *symbol = symbol_mgr.getSymbol(request.symbol, venue_);
    if (!symbol)
    {
        rejectModifyOrderRequest(request, OrdRejectReason::UNKNOWN_CONTRACT);
    }
    else
    {
        Order *order;
        if (request.modify_order.client_order_id[0] != '\0')
        {
            order = symbol->findOrderByClientOrderId(request.modify_order.client_order_id);
        }
        else
        {
            order = symbol->findOrderByOrderId(request.modify_order.order_id);
        }

        if (!order)
        {
            LOG_WARN(std::format("{} OrderId {} not found for modification", symbol->symbol_.view(), request.modify_order.order_id));
            rejectModifyOrderRequest(request, OrdRejectReason::UNKNOWN_ORDER);
            return;
        }
        order->status = OrderStatus::PENDING_REPLACE;
        order->last_update_time = utils::get_current_time_ns();
        sendOrderReplacePending(order, request.time_stamp);
        auto [reject_reason, trade_summaries] = symbol->modifyOrder(order, request.modify_order.new_price, request.modify_order.new_qty, request.time_stamp);

        if (!trade_summaries.empty())
        {
            for (const auto &summary : trade_summaries)
            {
                publishTradeSummary(symbol, summary);
            }
        }

        if (reject_reason != OrdRejectReason::NONE)
        {
            rejectModifyOrderRequest(request, reject_reason);
        }

        if (!symbol->md_level_update_cache_.empty())
        {
            publishLevelUpdate(symbol->symbol_, symbol->md_level_update_cache_);
            symbol->md_level_update_cache_.clear();
        }

        if (!symbol->md_order_update_cache_.empty())
        {
            // TODO: publish MD order update
            symbol->md_order_update_cache_.clear();
        }
    }
}

void Exchange::handleCancelOrderRequest(const Request &request)
{
    auto *symbol = symbol_mgr.getSymbol(request.symbol, venue_);
    if (!symbol)
    {
        rejectCancelOrderRequest(request, OrdRejectReason::UNKNOWN_CONTRACT);
    }
    else
    {
        Order *order;
        if (request.cancel_order.client_order_id[0] != '\0')
        {
            order = symbol->findOrderByClientOrderId(request.cancel_order.client_order_id);
        }
        else
        {
            order = symbol->findOrderByOrderId(request.cancel_order.order_id);
        }

        if (!order)
        {
            LOG_WARN(std::format("{} OrderId {} not found for cancellation", symbol->symbol_.view(), request.cancel_order.order_id));
            rejectCancelOrderRequest(request, OrdRejectReason::UNKNOWN_ORDER);
            return;
        }
        order->status = OrderStatus::PENDING_CANCEL;
        sendOrderCancelPending(order, request.time_stamp);
        symbol->cancelOrder(order, request.time_stamp);
    }
}

void Exchange::rejectMdSubscription(const Request &request, MDSubscriptionRejectReason reason)
{
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse));
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    update->type = MDUpdateType::SUB_RESPONSE;
    update->venue = venue_;
    memcpy(update->symbol, request.symbol, sizeof(update->symbol));
    auto *rsp = reinterpret_cast<MDSubscriptionResponse *>(update->data);
    rsp->channel = request.md_subscription.channel;
    // The reason was accepted and then dropped, leaving the field holding whatever
    // the recycled queue slot did - so a reject could arrive claiming NONE, and the
    // publisher would go on to read a book snapshot this frame never reserved.
    rsp->reject_reason = reason;
    md_queue_.publish(index, sz);
}

void Exchange::rejectNewOrderRequest(const Request &request, OrdRejectReason reason)
{
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

void Exchange::rejectModifyOrderRequest(const Request &request, OrdRejectReason reason)
{
    // Request is a union: reading add_order here would alias ModifyOrderMessage,
    // putting the order id in the client_order_id field and taking price/qty from
    // the wrong offsets entirely.
    const auto &msg = request.modify_order;
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    std::memcpy(response.symbol, request.symbol, sizeof(response.symbol));
    std::memcpy(response.user_id, msg.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, msg.client_order_id, sizeof(response.client_order_id));
    std::memcpy(response.order_id, msg.order_id, sizeof(response.order_id));
    response.response_type = MessageType::REJECT;
    response.order_status = OrderStatus::REJECTED;
    response.exec_type = ExecType::REJECTED;
    response.price = msg.new_price;
    response.qty = msg.new_qty;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    response.reject_reason = reason;
    response.timestamp = utils::get_current_time_ns();
    response.request_time = request.time_stamp;
    response_queue_.publish(index);
}

void Exchange::rejectCancelOrderRequest(const Request &request, OrdRejectReason reason)
{
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    std::memcpy(response.symbol, request.symbol, sizeof(response.symbol));
    // Request is a union: add_order.client_order_id aliases CancelOrderMessage's
    // order_id, and there is no price/qty on a cancel at all.
    const auto &msg = request.cancel_order;
    std::memcpy(response.user_id, msg.user_id, sizeof(response.user_id));
    std::memcpy(response.client_order_id, msg.client_order_id, sizeof(response.client_order_id));
    std::memcpy(response.order_id, msg.order_id, sizeof(response.order_id));
    response.response_type = MessageType::REJECT;
    response.order_status = OrderStatus::REJECTED;
    response.exec_type = ExecType::REJECTED;
    response.price = NULL_PRICE;
    response.qty = 0;
    response.cum_qty = 0;
    response.leaves_qty = 0;
    response.reject_reason = reason;
    response.timestamp = utils::get_current_time_ns();
    response.request_time = request.time_stamp;
    response_queue_.publish(index);
}

void Exchange::publishMDBookUpdate([[maybe_unused]] symid_t sid, OrderBook &order_book, const std::array<uint8_t, 2> &indices)
{
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDBookUpdate));
    uint64_t index = md_queue_.reserve(sz);
    MarketDataUpdate *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    update->type = MDUpdateType::BOOK;
    update->venue = venue_;
    std::memcpy(update->symbol, order_book.symbolName().data(), sizeof(update->symbol));
    MDBookUpdate *book_update = reinterpret_cast<MDBookUpdate *>(update->data);
    book_update->update_index[0] = indices[0];
    book_update->update_index[1] = indices[1];
    order_book.populateMDBookUpdate(*book_update);
    md_queue_.publish(index, sz);
}

void Exchange::publishLevelUpdate(const symbol_name_t &symbol, const std::vector<MDLevel> &level_updates)
{
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDLevelUpdate) + level_updates.size() * sizeof(MDLevel));
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    update->type = MDUpdateType::LEVEL;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol.data(), sizeof(update->symbol));
    auto *level_update = reinterpret_cast<MDLevelUpdate *>(update->data);
    level_update->num_level_update = static_cast<uint32_t>(level_updates.size());
    std::memcpy(level_update->levels, level_updates.data(), level_updates.size() * sizeof(MDLevel));
    md_queue_.publish(index, sz);
}

void Exchange::publishMDOrderUpdate(const symbol_name_t &symbol, const std::vector<MDOrder> &order_updates)
{
    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDOrderUpdate) + order_updates.size() * sizeof(MDOrder));
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    update->type = MDUpdateType::ORDER;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol.data(), sizeof(update->symbol));
    auto *order_update = reinterpret_cast<MDOrderUpdate *>(update->data);
    order_update->num_orders = static_cast<uint32_t>(order_updates.size());
    std::memcpy(order_update->orders, order_updates.data(), order_updates.size() * sizeof(MDOrder));
    md_queue_.publish(index, sz);
}

void Exchange::publishTradeSummary(Symbol *symbol, const TradeSummaryInfo &trade_summary)
{
    // The engine's own trade id comes from a single static counter shared by
    // every engine and every venue, so it cannot serve as the public id. Each
    // venue stamps its own sequence here, once, so a live print and the same
    // trade replayed in a snapshot carry identical ids for every client.
    auto trade_id = nextVenueTradeId();

    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(TradeSummary) + trade_summary.num_orders * sizeof(Trade));
    auto index = md_queue_.reserve(sz);
    auto update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    update->type = MDUpdateType::TRADE_SUMMARY;
    update->venue = venue_;
    std::memcpy(update->symbol, symbol->symbol_.data(), sizeof(update->symbol));
    auto summary = reinterpret_cast<TradeSummary *>(update->data);
    summary->timestamp = trade_summary.timestamp;
    summary->trade_id = trade_id;
    summary->security_id = symbol->id_;
    summary->aggressor_side = trade_summary.aggressor_side;
    summary->price = trade_summary.price;
    summary->qty = trade_summary.qty;
    summary->num_orders = trade_summary.num_orders;
    for (size_t i = 0; i < trade_summary.Trades.size(); ++i)
    {
        summary->trades[i].order_id = trade_summary.Trades[i].order_id;
        summary->trades[i].qty = trade_summary.Trades[i].qty;
    }
    md_queue_.publish(index, sz);

    symbol->recordTrade(MDTrade{
        .trade_id = trade_id,
        .event_time = static_cast<uint64_t>(trade_summary.timestamp),
        .seq_num = 0,
        .price = trade_summary.price,
        .qty = trade_summary.qty,
        .flags = UpdateFlags::F_NONE,
        .side = trade_summary.aggressor_side});
}

void Exchange::publishTradeSubscriptionResponse(Symbol *symbol, uint8_t channel)
{
    auto &history = symbol->trade_history_;
    auto num_trades = static_cast<uint32_t>(history.size());

    auto sz = static_cast<uint32_t>(sizeof(MarketDataUpdate) + sizeof(MDSubscriptionResponse) + sizeof(MDTradeSnapshotResponse) + num_trades * sizeof(MDTrade));
    auto index = md_queue_.reserve(sz);
    auto *update = reinterpret_cast<MarketDataUpdate *>(md_queue_[index]);
    std::memcpy(update->symbol, symbol->symbol_.data(), sizeof(update->symbol));
    update->venue = venue_;
    update->type = MDUpdateType::SUB_RESPONSE;

    auto *response = reinterpret_cast<MDSubscriptionResponse *>(update->data);
    response->channel = channel;
    response->reject_reason = MDSubscriptionRejectReason::NONE;

    // A trade the book absorbed is settled history for this subscriber whatever
    // its timestamp, because the L2 snapshot they receive is generated from that
    // same book. Only a print the simulator never applied depends on timing, and
    // then on whether the book's own data reaches it: `lastUpdateTime()` tracks
    // level updates only, so it cannot stand in for trade application here.
    auto book_time = symbol->order_book_ ? symbol->order_book_->lastUpdateTime() : 0;

    auto reflected_in_book = [book_time](const MDTrade &trade) {
        return !(trade.flags & UpdateFlags::F_NOT_IN_BOOK) || trade.event_time <= book_time;
    };

    auto *snapshot = reinterpret_cast<MDTradeSnapshotResponse *>(response->data);
    auto *trades = snapshot->trades;
    uint32_t num_snapshot = 0;
    uint32_t num_update = 0;

    // The consumer reads the two blocks as contiguous ranges, so they must be
    // written that way — hence two passes rather than one. Classification is not
    // monotonic over the history: an applied trade can follow an unapplied print
    // that is newer than book_time, so a single pass in history order would
    // interleave the blocks and silently swap their contents. Each pass walks the
    // history in order, so both blocks stay chronological within themselves.
    for (uint32_t i = 0; i < num_trades; ++i)
    {
        const auto *trade = history[i];
        if (trade == nullptr) [[unlikely]]
        {
            continue;
        }
        if (reflected_in_book(*trade))
        {
            *trades++ = *trade;
            ++num_snapshot;
        }
    }
    for (uint32_t i = 0; i < num_trades; ++i)
    {
        const auto *trade = history[i];
        if (trade == nullptr) [[unlikely]]
        {
            continue;
        }
        if (!reflected_in_book(*trade))
        {
            *trades++ = *trade;
            ++num_update;
        }
    }
    snapshot->num_snapshot = num_snapshot;
    snapshot->num_update = num_update;
    md_queue_.publish(index, sz);
}

void Exchange::reconcilePhantomQty(Symbol *symbol, Side side, price_t price, qty_t target_qty,
                                   time_t event_time, uint64_t seq_num, bool end_event)
{
    auto &book = *symbol->order_book_;
    auto [level, index] = book.getLevel(to_book_side(side), price);
    if (!level)
    {
        return;
    }

    // A trade print and the venue's level update for it describe the same event.
    // The usual case needs no help: the update carries the post-trade quantity
    // and the book has already absorbed the trade, so the two agree and nothing
    // moves. The exception is a trade this update is too old to have seen - the
    // update still reports the pre-trade quantity there, and taking it at face
    // value would add that quantity back only for the next update to remove it
    // again. Offset by exactly the trades it has not accounted for. Timestamps
    // that tie count as reflecting the trade: a venue emits the print and its
    // level update together, and the sequencer applies the trade first.
    auto unreflected = symbol->takeTradedQty(side, price, event_time);
    if (unreflected > 0)
    {
        target_qty = target_qty > unreflected ? target_qty - unreflected : 0;
    }

    auto md_level_qty = book.getMDLevelQty(price);
    if (target_qty > md_level_qty)
    {
        // Level qty increased, assume new orders added at the back. Create new phantom order to represent then increment
        auto size = target_qty - md_level_qty;
        book.addBookOrder(utils::nextOrderId(), to_book_side(side), price, size, event_time, seq_num);
    }
    else if (target_qty < md_level_qty)
    {
        // Level qty decreased. Always assume the worst case, that the quantity decreased at the back
        auto diff = md_level_qty - target_qty;
        for (auto it = level->orders.rbegin(); it != level->orders.rend(); ++it)
        {
            auto &order = *it;
            if (symbol->findOrder(order.order_id))
            {
                // Belongs to the simulator's own user - never touch it.
                continue;
            }

            auto to_reduce = std::min<qty_t>(diff, order.quantity);
            book.modifyOrder(order.order_id, price, order.quantity - to_reduce, event_time, order.priority, seq_num, end_event && (diff == to_reduce));
            diff -= to_reduce;
            if (diff == 0 || book.getMDLevelQty(price) == 0)
            {
                break;
            }
        }
    }
}

void Exchange::sendOrderNewPending(const Order *order, time_t request_time)
{
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    setOrderIdentity(response, *order);
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

void Exchange::sendOrderReplacePending(const Order *order, time_t request_time)
{
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    setOrderIdentity(response, *order);
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

void Exchange::sendOrderCancelPending(const Order *order, time_t request_time)
{
    auto index = response_queue_.reserve();
    auto &response = *response_queue_[index];
    setOrderIdentity(response, *order);
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