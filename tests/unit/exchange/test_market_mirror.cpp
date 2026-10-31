#include <gtest/gtest.h>
#include <exchange/exchange.hpp>
#include <exchange/symbol.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <common/symbol_manager.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::exch;
using namespace slick::sim::test;

// Exchange::applyMarketOrder / replayMarketTrade: how a venue with a live MBO/FOD
// feed (CME, ICE) mirrors its market's orders into the book.
namespace {

nlohmann::json mirrorConfig() {
    return nlohmann::json{
        {"request_queue_size", 1024},
        {"response_queue_size", 4096},
        {"md_queue_size", 1 << 20},
        {"order_gateway", nlohmann::json::object()},
        {"md_publisher", nlohmann::json::object()},
    };
}

class MirrorExchange : public Exchange {
public:
    explicit MirrorExchange(const nlohmann::json &config) : Exchange(Venue::STOCK, config) {}

    Symbol *addSymbol(std::string_view name) {
        auto *symbol = SymbolManager::instance().createSymbol(name, Venue::STOCK);
        if (!matching_engines_[engine::MatchingEngine::Type::FIFO]) {
            matching_engines_[engine::MatchingEngine::Type::FIFO] =
                std::make_unique<engine::FifoMatchingEngine>(response_queue_);
        }
        symbol->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
        symbol->createOrderBook<OrderBookType::L2>();
        return symbol;
    }

    void submit(const Request &request) { handleNewOrderRequest(request); }
    void apply(Symbol *symbol, const MarketOrderEvent &event) { applyMarketOrder(symbol, event); }
    void trade(Symbol *symbol, const MarketTradeEvent &event) { replayMarketTrade(symbol, event); }
    void flush() { flushMarketEvents(); }
};

std::string uniqueSymbol(const char *stem) {
    static int counter = 0;
    return std::string(stem) + "-" + std::to_string(++counter);
}

template <size_t N>
void copyField(char (&dst)[N], std::string_view src) {
    std::memset(dst, 0, N);
    std::memcpy(dst, src.data(), std::min(N - 1, src.size()));
}

Request clientOrder(const std::string &symbol, const char *cl_ord_id, Side side, price_t px, qty_t q) {
    Request request{};
    copyField(request.symbol, symbol);
    request.msg_type = MessageType::NEW_ORDER_SINGLE;
    copyField(request.add_order.user_id, "client");
    copyField(request.add_order.client_order_id, cl_ord_id);
    request.add_order.side = side;
    request.add_order.type = OrderType::LIMIT;
    request.add_order.time_in_force = TimeInForce::GOOD_TILL_CANCEL;
    request.add_order.price = px;
    request.add_order.qty = q;
    return request;
}

MarketOrderEvent marketOrder(MDUpdateAction action, Side side, price_t px, qty_t q, uint64_t id = 0,
                             uint64_t priority = 0) {
    MarketOrderEvent event;
    event.book_order_id = id ? id : utils::nextOrderId();
    event.book_priority = priority ? priority : OrderBook::nextOrderPriority();
    event.transact_time = 1;
    event.price = px;
    event.qty = q;
    event.side = side;
    event.action = action;
    return event;
}

qty_t restingQty(Symbol *symbol, uint64_t book_order_id) {
    const auto *order = symbol->order_book_->OrderBookL3::findOrder(book_order_id);
    return order ? order->quantity : 0;
}

/// The order ids resting at a price, front of the queue first.
std::vector<uint64_t> queueAt(Symbol *symbol, Side side, price_t px) {
    std::vector<uint64_t> ids;
    auto [level, index] = symbol->order_book_->getLevel(to_book_side(side), px);
    if (level) {
        for (const auto &order : level->orders) {
            ids.push_back(order.order_id);
        }
    }
    return ids;
}

qty_t filledQty(const std::vector<OrderResponse> &responses, const char *cl_ord_id) {
    qty_t filled = 0;
    for (const auto &r : responses) {
        if (std::strcmp(r.client_order_id, cl_ord_id) == 0 && r.exec_type == ExecType::TRADE) {
            filled += r.last_qty;
        }
    }
    return filled;
}

}   // namespace

TEST(MarketMirrorTest, NewOrderRestsBesideClientOrders) {
    MirrorExchange exchange(mirrorConfig());
    auto *symbol = exchange.addSymbol(uniqueSymbol("MM-NEW"));

    const auto event = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty5);
    exchange.apply(symbol, event);

    EXPECT_EQ(restingQty(symbol, event.book_order_id), kQty5);
    // A phantom order is not one of the simulator's own.
    EXPECT_EQ(symbol->order_book_->findOrder(event.book_order_id), nullptr);
}

// The venue's book is never crossed, so a market order that crosses can only be
// crossing a client: the client is filled and the rest of the order rests.
TEST(MarketMirrorTest, CrossingNewOrderFillsTheClientAndRestsTheRest) {
    MirrorExchange exchange(mirrorConfig());
    const auto sym = uniqueSymbol("MM-CROSS");
    auto *symbol = exchange.addSymbol(sym);
    OrderResponseCollector responses(exchange.response_queue());
    exchange.submit(clientOrder(sym, "C1", Side::BUY, kPrice100, kQty5));

    const auto event = marketOrder(MDUpdateAction::ACTION_NEW, Side::SELL, kPrice100, kQty15);
    exchange.apply(symbol, event);

    EXPECT_EQ(filledQty(responses.collect(), "C1"), kQty5);
    EXPECT_EQ(restingQty(symbol, event.book_order_id), kQty10);
}

// A client took part of a market order the venue still reports whole: the next
// CHANGE restores the venue's quantity, in place.
TEST(MarketMirrorTest, ChangeRestoresTheVenueQuantityInPlace) {
    MirrorExchange exchange(mirrorConfig());
    const auto sym = uniqueSymbol("MM-RESTORE");
    auto *symbol = exchange.addSymbol(sym);
    const auto front = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty5);
    const auto back = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty5);
    exchange.apply(symbol, front);
    exchange.apply(symbol, back);

    exchange.submit(clientOrder(sym, "S1", Side::SELL, kPrice100, kQty2));
    ASSERT_EQ(restingQty(symbol, front.book_order_id), kQty3);

    auto change = front;
    change.action = MDUpdateAction::ACTION_CHANGE;
    exchange.apply(symbol, change);

    EXPECT_EQ(restingQty(symbol, front.book_order_id), kQty5);
    EXPECT_EQ(queueAt(symbol, Side::BUY, kPrice100),
              (std::vector<uint64_t>{front.book_order_id, back.book_order_id}));
}

// Taken entirely by a client, then reported again: it goes back where it was,
// under the priority it joined with, ahead of orders that joined after it.
TEST(MarketMirrorTest, OrderTakenEntirelyReturnsToItsPlace) {
    MirrorExchange exchange(mirrorConfig());
    const auto sym = uniqueSymbol("MM-RETURN");
    auto *symbol = exchange.addSymbol(sym);
    const auto front = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty2);
    const auto back = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty5);
    exchange.apply(symbol, front);
    exchange.apply(symbol, back);

    exchange.submit(clientOrder(sym, "S1", Side::SELL, kPrice100, kQty2));
    ASSERT_EQ(restingQty(symbol, front.book_order_id), 0);

    auto change = front;
    change.action = MDUpdateAction::ACTION_CHANGE;
    change.qty = kQty3;
    exchange.apply(symbol, change);

    EXPECT_EQ(queueAt(symbol, Side::BUY, kPrice100),
              (std::vector<uint64_t>{front.book_order_id, back.book_order_id}));
    EXPECT_EQ(restingQty(symbol, front.book_order_id), kQty3);
}

TEST(MarketMirrorTest, RequeueMovesTheOrderToTheBack) {
    MirrorExchange exchange(mirrorConfig());
    auto *symbol = exchange.addSymbol(uniqueSymbol("MM-REQUEUE"));
    const auto front = marketOrder(MDUpdateAction::ACTION_NEW, Side::SELL, kPrice101, kQty5);
    const auto back = marketOrder(MDUpdateAction::ACTION_NEW, Side::SELL, kPrice101, kQty5);
    exchange.apply(symbol, front);
    exchange.apply(symbol, back);

    auto change = front;
    change.action = MDUpdateAction::ACTION_CHANGE;
    change.requeue = true;
    change.book_priority = OrderBook::nextOrderPriority();
    change.qty = kQty10;
    exchange.apply(symbol, change);

    EXPECT_EQ(queueAt(symbol, Side::SELL, kPrice101),
              (std::vector<uint64_t>{back.book_order_id, front.book_order_id}));
    EXPECT_EQ(restingQty(symbol, front.book_order_id), kQty10);
}

TEST(MarketMirrorTest, DeleteOfAnOrderAlreadyTakenIsHarmless) {
    MirrorExchange exchange(mirrorConfig());
    const auto sym = uniqueSymbol("MM-DELETE");
    auto *symbol = exchange.addSymbol(sym);
    const auto event = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty2);
    exchange.apply(symbol, event);
    exchange.submit(clientOrder(sym, "S1", Side::SELL, kPrice100, kQty2));

    auto remove = event;
    remove.action = MDUpdateAction::ACTION_DELETE;
    remove.qty = 0;
    exchange.apply(symbol, remove);

    EXPECT_TRUE(queueAt(symbol, Side::BUY, kPrice100).empty());
}

// A market trade fills a client order queued ahead of the market orders it hit.
TEST(MarketMirrorTest, TradeReplayFillsTheClientQueuedAhead) {
    MirrorExchange exchange(mirrorConfig());
    const auto sym = uniqueSymbol("MM-TRADE");
    auto *symbol = exchange.addSymbol(sym);
    OrderResponseCollector responses(exchange.response_queue());
    exchange.submit(clientOrder(sym, "B1", Side::BUY, kPrice100, kQty2));
    const auto market = marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty5);
    exchange.apply(symbol, market);

    MarketTradeEvent trade;
    trade.price = kPrice100;
    trade.qty = kQty3;
    trade.aggressor = Side::SELL;
    trade.transact_time = 2;
    exchange.trade(symbol, trade);

    EXPECT_EQ(filledQty(responses.collect(), "B1"), kQty2);
    EXPECT_EQ(restingQty(symbol, market.book_order_id), kQty5 - (kQty3 - kQty2));
}

// The venue reports the aggressor's residual as an order of its own, so a replayed
// trade rests nothing: only the liquidity it hit shrinks.
TEST(MarketMirrorTest, ATradeReplayRestsNoRemainder) {
    MirrorExchange exchange(mirrorConfig());
    const auto sym = uniqueSymbol("MM-REMAINDER");
    auto *symbol = exchange.addSymbol(sym);
    const auto market = marketOrder(MDUpdateAction::ACTION_NEW, Side::SELL, kPrice101, kQty2);
    exchange.apply(symbol, market);

    MarketTradeEvent trade;
    trade.price = kPrice101;
    trade.qty = kQty5;
    trade.aggressor = Side::BUY;
    trade.transact_time = 2;
    exchange.trade(symbol, trade);

    EXPECT_EQ(restingQty(symbol, market.book_order_id), 0);
    EXPECT_TRUE(queueAt(symbol, Side::SELL, kPrice101).empty());
}

TEST(MarketMirrorTest, ChangesArePublishedOnFlush) {
    MirrorExchange exchange(mirrorConfig());
    auto *symbol = exchange.addSymbol(uniqueSymbol("MM-FLUSH"));
    MarketDataCollector md(exchange.md_queue());

    exchange.apply(symbol, marketOrder(MDUpdateAction::ACTION_NEW, Side::BUY, kPrice100, kQty5));
    EXPECT_TRUE(md.collect().empty());

    exchange.flush();
    EXPECT_FALSE(md.collect().empty());
}
