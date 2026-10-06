#include <gtest/gtest.h>
#include <exchange/exchange.hpp>
#include <exchange/symbol.hpp>
#include <matching_engine/fifo_matching_engine.hpp>
#include <common/symbol_manager.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <string>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::exch;
using namespace slick::sim::test;

// ---------------------------------------------------------------------------
// Two venue-neutral pieces a binary order-entry venue depends on:
//
//  * MDUpdateType::ORDER frames - every order-book change, order by order. Built
//    into md_order_update_cache_ all along, but flushMarketDataCaches dropped the
//    cache behind a TODO, so no order-by-order data ever left the exchange.
//  * request_tag / order_num on OrderResponse - the correlation a gateway needs to
//    echo a binary protocol's request id on every response, and the numeric order
//    id that the order-by-order feed carries.
// ---------------------------------------------------------------------------
namespace {

nlohmann::json testConfig(const std::string &smp = "none") {
    return nlohmann::json{
        {"request_queue_size", 1024},
        {"response_queue_size", 4096},
        {"md_queue_size", 1 << 20},
        {"order_gateway", nlohmann::json::object()},
        {"md_publisher", nlohmann::json::object()},
        {"self_match_prevention", smp},
    };
}

/// The smallest adapter that can trade: symbols created directly, the shared FIFO
/// engine attached, and the order handlers reachable without a request thread.
class OrderUpdateExchange : public Exchange {
public:
    OrderUpdateExchange(const nlohmann::json &config, bool publish_orders)
        : Exchange(Venue::STOCK, config) {
        publish_order_updates_ = publish_orders;
    }

    Symbol *addSymbol(std::string_view name) {
        auto *symbol = SymbolManager::instance().createSymbol(name, Venue::STOCK);
        if (!matching_engines_[engine::MatchingEngine::Type::FIFO]) {
            matching_engines_[engine::MatchingEngine::Type::FIFO] =
                std::make_unique<engine::FifoMatchingEngine>(response_queue_);
        }
        symbol->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
        symbol->smp_mode_ = smp_mode_;
        symbol->createOrderBook<OrderBookType::L2>();
        return symbol;
    }

    void submit(const Request &request) { handleNewOrderRequest(request); }
    void amend(const Request &request) { handleModifyOrderRequest(request); }
    void cancel(const Request &request) { handleCancelOrderRequest(request); }
};

/// Every test trades its own instrument: SymbolManager is process-wide.
std::string uniqueSymbol(const char *stem) {
    static int counter = 0;
    return std::string(stem) + "-" + std::to_string(++counter);
}

template <size_t N>
void copyField(char (&dst)[N], const std::string &src) {
    std::memset(dst, 0, N);
    std::memcpy(dst, src.c_str(), std::min(N - 1, src.size()));
}

Request newOrder(const std::string &symbol, const std::string &user, const std::string &cl_ord_id,
                 Side side, price_t px, qty_t q, uint64_t tag) {
    Request request{};
    copyField(request.symbol, symbol);
    request.msg_type = MessageType::NEW_ORDER_SINGLE;
    request.request_tag = tag;
    copyField(request.add_order.user_id, user);
    copyField(request.add_order.client_order_id, cl_ord_id);
    request.add_order.side = side;
    request.add_order.type = OrderType::LIMIT;
    request.add_order.time_in_force = TimeInForce::GOOD_TILL_CANCEL;
    request.add_order.price = px;
    request.add_order.qty = q;
    return request;
}

Request cancelOrder(const std::string &symbol, const std::string &user, const std::string &cl_ord_id,
                    uint64_t tag) {
    Request request{};
    copyField(request.symbol, symbol);
    request.msg_type = MessageType::ORDER_CANCEL_REQUEST;
    request.request_tag = tag;
    copyField(request.cancel_order.user_id, user);
    copyField(request.cancel_order.client_order_id, cl_ord_id);
    std::memset(request.cancel_order.order_id, 0, sizeof(request.cancel_order.order_id));
    return request;
}

Request amendOrder(const std::string &symbol, const std::string &user, const std::string &cl_ord_id,
                   price_t px, qty_t q, uint64_t tag) {
    Request request{};
    copyField(request.symbol, symbol);
    request.msg_type = MessageType::ORDER_REPLACE_REQUEST;
    request.request_tag = tag;
    copyField(request.modify_order.user_id, user);
    copyField(request.modify_order.client_order_id, cl_ord_id);
    std::memset(request.modify_order.order_id, 0, sizeof(request.modify_order.order_id));
    request.modify_order.new_price = px;
    request.modify_order.new_qty = q;
    return request;
}

std::vector<MDOrder> ordersOf(MarketDataUpdate *update) {
    auto *order_update = reinterpret_cast<MDOrderUpdate *>(update->data);
    std::vector<MDOrder> orders(order_update->num_orders);
    std::memcpy(orders.data(), order_update->orders, orders.size() * sizeof(MDOrder));
    return orders;
}

std::vector<OrderResponse> responsesFor(const std::vector<OrderResponse> &all, const char *cl_ord_id) {
    std::vector<OrderResponse> matched;
    for (const auto &response : all) {
        if (std::strcmp(response.client_order_id, cl_ord_id) == 0) {
            matched.push_back(response);
        }
    }
    return matched;
}

}   // namespace

// ===========================================================================
// ORDER frames
// ===========================================================================

// Off by default: a venue with no order-by-order feed must not pay md_queue
// bandwidth for frames its publisher would discard.
TEST(OrderUpdatePublishTest, NotPublishedUnlessTheVenueAsks) {
    OrderUpdateExchange exchange(testConfig(), false);
    const auto sym = uniqueSymbol("OU-OFF");
    exchange.addSymbol(sym);
    MarketDataCollector md(exchange.md_queue());

    exchange.submit(newOrder(sym, "alice", "A1", Side::BUY, kPrice100, kQty5, 1));

    EXPECT_TRUE(md.collect(MDUpdateType::ORDER).empty());
}

// Regression: flushMarketDataCaches cleared md_order_update_cache_ behind a TODO,
// so an order resting and then leaving the book reached no subscriber.
TEST(OrderUpdatePublishTest, RestThenCancelPublishesNewThenDelete) {
    OrderUpdateExchange exchange(testConfig(), true);
    const auto sym = uniqueSymbol("OU-REST");
    exchange.addSymbol(sym);
    MarketDataCollector md(exchange.md_queue());
    OrderResponseCollector responses(exchange.response_queue());

    exchange.submit(newOrder(sym, "alice", "A1", Side::BUY, kPrice100, kQty5, 1));

    auto frames = md.collect(MDUpdateType::ORDER);
    ASSERT_EQ(frames.size(), 1u);
    auto added = ordersOf(frames[0]);
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(added[0].update_action, MDUpdateAction::ACTION_NEW);
    EXPECT_EQ(added[0].side, Side::BUY);
    EXPECT_EQ(added[0].price, kPrice100);
    EXPECT_EQ(added[0].qty, kQty5);

    // The id the feed shows is the id the owner's execution reports carry, so a
    // client can recognise its own order in the order-by-order feed.
    auto acks = responses.collect();
    ASSERT_FALSE(acks.empty());
    EXPECT_EQ(added[0].order_id, acks.back().order_num);

    exchange.cancel(cancelOrder(sym, "alice", "A1", 2));

    frames = md.collect(MDUpdateType::ORDER);
    ASSERT_EQ(frames.size(), 1u);
    auto removed = ordersOf(frames[0]);
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0].update_action, MDUpdateAction::ACTION_DELETE);
    EXPECT_EQ(removed[0].order_id, added[0].order_id);
    EXPECT_EQ(removed[0].qty, 0);
}

// A partial fill is a change to the resting order, and the ORDER frame is the last
// frame the event produces - after the trade and the level - which is what lets a
// publisher close the event on it.
TEST(OrderUpdatePublishTest, PartialFillIsAChangeAndClosesTheEvent) {
    OrderUpdateExchange exchange(testConfig(), true);
    const auto sym = uniqueSymbol("OU-FILL");
    exchange.addSymbol(sym);
    exchange.submit(newOrder(sym, "alice", "S1", Side::SELL, kPrice100, kQty5, 1));
    MarketDataCollector md(exchange.md_queue());
    md.collect();   // drop the resting order's own frames

    exchange.submit(newOrder(sym, "bob", "B1", Side::BUY, kPrice100, kQty2, 2));

    auto frames = md.collect();
    ASSERT_GE(frames.size(), 2u);
    EXPECT_EQ(frames.front()->type, MDUpdateType::TRADE_SUMMARY);
    EXPECT_EQ(frames.back()->type, MDUpdateType::ORDER);

    auto changed = ordersOf(frames.back());
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0].update_action, MDUpdateAction::ACTION_CHANGE);
    EXPECT_EQ(changed[0].qty, kQty3);
}

// ===========================================================================
// request_tag / order_num
// ===========================================================================

TEST(RequestTagTest, EveryResponseEchoesTheRequestThatCausedIt) {
    OrderUpdateExchange exchange(testConfig(), false);
    const auto sym = uniqueSymbol("TAG-ECHO");
    exchange.addSymbol(sym);
    OrderResponseCollector responses(exchange.response_queue());

    exchange.submit(newOrder(sym, "alice", "A1", Side::BUY, kPrice100, kQty5, 0xA11CE));
    auto placed = responses.collect();
    ASSERT_FALSE(placed.empty());
    const uint64_t order_num = placed.front().order_num;
    EXPECT_NE(order_num, 0u);
    for (const auto &response : placed) {
        EXPECT_EQ(response.request_tag, 0xA11CEu);
        EXPECT_EQ(response.order_num, order_num);
    }

    exchange.cancel(cancelOrder(sym, "alice", "A1", 0xCA11));
    auto cancelled = responses.collect();
    ASSERT_FALSE(cancelled.empty());
    for (const auto &response : cancelled) {
        EXPECT_EQ(response.request_tag, 0xCA11u);
        EXPECT_EQ(response.order_num, order_num);
    }
}

// A reject never created an order, so it has no number - but it still answers a
// request, and the gateway needs the tag to find out which.
TEST(RequestTagTest, RejectEchoesTagWithNoOrderNumber) {
    OrderUpdateExchange exchange(testConfig(), false);
    OrderResponseCollector responses(exchange.response_queue());

    exchange.submit(newOrder("TAG-NO-SUCH-SYMBOL", "alice", "A1", Side::BUY, kPrice100, kQty5, 77));

    auto rejected = responses.collect();
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].exec_type, ExecType::REJECTED);
    EXPECT_EQ(rejected[0].reject_reason, OrdRejectReason::UNKNOWN_CONTRACT);
    EXPECT_EQ(rejected[0].request_tag, 77u);
    EXPECT_EQ(rejected[0].order_num, 0u);
}

// A passive fill answers no request, so it carries the last request the order
// accepted. An amendment the engine rejected was not accepted, and must not leave
// its tag behind on the order.
TEST(RequestTagTest, PassiveFillCarriesLastAcceptedTagNotARejectedAmendment) {
    OrderUpdateExchange exchange(testConfig("cancel_newest"), false);
    const auto sym = uniqueSymbol("TAG-PASSIVE");
    exchange.addSymbol(sym);
    OrderResponseCollector responses(exchange.response_queue());

    exchange.submit(newOrder(sym, "alice", "AB", Side::BUY, kPrice99, kQty5, 10));
    exchange.submit(newOrder(sym, "alice", "AS", Side::SELL, kPrice101, kQty5, 21));

    // Moving the sell onto alice's own bid would self-match: rejected by SMP.
    exchange.amend(amendOrder(sym, "alice", "AS", kPrice99, kQty5, 22));
    auto amend_responses = responsesFor(responses.collect(), "AS");
    ASSERT_FALSE(amend_responses.empty());
    EXPECT_EQ(amend_responses.back().exec_type, ExecType::REJECTED);
    EXPECT_EQ(amend_responses.back().request_tag, 22u);

    exchange.submit(newOrder(sym, "bob", "B1", Side::BUY, kPrice101, kQty2, 30));
    auto all = responses.collect();
    auto fills = responsesFor(all, "AS");
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].exec_type, ExecType::TRADE);
    EXPECT_EQ(fills[0].request_tag, 21u);
    EXPECT_FALSE(fills[0].aggressor);

    // The other side of the same trade crossed the spread.
    auto bob = responsesFor(all, "B1");
    auto bob_fill = std::find_if(bob.begin(), bob.end(),
                                 [](const OrderResponse &r) { return r.exec_type == ExecType::TRADE; });
    ASSERT_NE(bob_fill, bob.end());
    EXPECT_TRUE(bob_fill->aggressor);
    EXPECT_EQ(bob_fill->request_tag, 30u);
}
