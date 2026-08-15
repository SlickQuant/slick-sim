#include <gtest/gtest.h>
#include <exchange/exch_coinbase.hpp>
#include <common/symbol_manager.hpp>
#include <common/order.hpp>
#include <common/messages.hpp>
#include <common/types.hpp>
#include <coinbase/websocket.hpp>
#include <cstring>
#include <string>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::exch;
using namespace nlohmann::literals;

// ---------------------------------------------------------------------------
// Self-match prevention is configured on the exchange and applied per symbol.
// The engine's own SMP behaviour is covered in
// tests/unit/matching_engine/test_matching_engine_smp.cpp; what is exercised
// here is the wiring that makes it reachable from a running simulator at all —
// config parsing, the user_id -> client_id identity, and both order paths
// passing the mode through.
// ---------------------------------------------------------------------------
class TestableSMPExchange : public CoinbaseExchange {
public:
    using CoinbaseExchange::CoinbaseExchange;

    void subscribeMd(const Request &req) { handleMdSubscription(req); }
    void submitOrder(const Request &req) { handleNewOrderRequest(req); }
    void amendOrder(const Request &req) { handleModifyOrderRequest(req); }
    int clientId(const char *user_id) { return clientIdFor(user_id); }
    SelfMatchPreventionMode smpMode() const { return smp_mode_; }
};

class SelfMatchPreventionTest : public ::testing::Test {
protected:
    // Each exchange gets its own queues; the shm names are suffixed so tests
    // building several exchanges do not collide.
    std::unique_ptr<TestableSMPExchange> makeExchange(const std::string &smp_mode) {
        auto config = R"({
            "request_queue_size": 4096,
            "response_queue_size": 4096,
            "md_queue_size": 1048576,
            "order_gateway": { "rest": { "port": 4000 } },
            "md_publisher": { "port": 5000 }
        })"_json;
        auto suffix = std::to_string(++instance_);
        config["request_queue_shm_name"] = "smp_test_req" + suffix;
        config["response_queue_shm_name"] = "smp_test_resp" + suffix;
        config["md_queue_shm_name"] = "smp_test_md" + suffix;
        if (!smp_mode.empty()) {
            config["self_match_prevention"] = smp_mode;
        }
        return std::make_unique<TestableSMPExchange>(config);
    }

    // Drives the real subscription path so the symbol is created by addSymbol,
    // which is where the exchange's SMP mode is stamped onto it.
    Symbol *subscribe(TestableSMPExchange &exchange, const std::string &product_id) {
        Request req{};
        std::memset(req.symbol, 0, sizeof(req.symbol));
        std::memcpy(req.symbol, product_id.c_str(), std::min(sizeof(req.symbol) - 1, product_id.size()));
        req.msg_type = MessageType::MD_SUBSCRIPTION;
        req.md_subscription.channel = static_cast<uint8_t>(coinbase::WebSocketChannel::LEVEL2);
        exchange.subscribeMd(req);
        return SymbolManager::instance().getSymbol(product_id);
    }

    Request makeOrder(const std::string &product_id, const std::string &user_id,
                      const std::string &client_order_id, Side side, price_t price, qty_t qty) {
        Request req{};
        std::memset(req.symbol, 0, sizeof(req.symbol));
        std::memcpy(req.symbol, product_id.c_str(), std::min(sizeof(req.symbol) - 1, product_id.size()));
        req.msg_type = MessageType::NEW_ORDER_SINGLE;
        std::memset(req.add_order.user_id, 0, sizeof(req.add_order.user_id));
        std::memcpy(req.add_order.user_id, user_id.c_str(),
                    std::min(sizeof(req.add_order.user_id) - 1, user_id.size()));
        std::memset(req.add_order.client_order_id, 0, sizeof(req.add_order.client_order_id));
        std::memcpy(req.add_order.client_order_id, client_order_id.c_str(),
                    std::min(sizeof(req.add_order.client_order_id) - 1, client_order_id.size()));
        req.add_order.side = side;
        req.add_order.type = OrderType::LIMIT;
        req.add_order.time_in_force = TimeInForce::GOOD_TILL_CANCEL;
        req.add_order.price = price;
        req.add_order.qty = qty;
        return req;
    }

    static int instance_;
};

int SelfMatchPreventionTest::instance_ = 0;

// ===========================================================================
// Configuration
// ===========================================================================

TEST_F(SelfMatchPreventionTest, Config_DefaultsToNone) {
    EXPECT_EQ(makeExchange("")->smpMode(), SelfMatchPreventionMode::NONE);
}

TEST_F(SelfMatchPreventionTest, Config_ParsesEachMode) {
    EXPECT_EQ(makeExchange("none")->smpMode(), SelfMatchPreventionMode::NONE);
    EXPECT_EQ(makeExchange("cancel_resting")->smpMode(), SelfMatchPreventionMode::CANCEL_RESTING);
    EXPECT_EQ(makeExchange("cancel_newest")->smpMode(), SelfMatchPreventionMode::CANCEL_NEWEST);
}

// A typo must not silently enable the wrong policy, nor abort startup.
TEST_F(SelfMatchPreventionTest, Config_UnknownModeFallsBackToNone) {
    EXPECT_EQ(makeExchange("cancel_oldest")->smpMode(), SelfMatchPreventionMode::NONE);
}

TEST_F(SelfMatchPreventionTest, Config_ModeIsStampedOntoEachSymbol) {
    auto exchange = makeExchange("cancel_resting");
    auto *sym = subscribe(*exchange, "SMP-CFG-1");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->smp_mode_, SelfMatchPreventionMode::CANCEL_RESTING);
}

// ===========================================================================
// user_id -> client_id identity
// ===========================================================================

TEST_F(SelfMatchPreventionTest, ClientId_SameUserResolvesToSameId) {
    auto exchange = makeExchange("none");
    EXPECT_EQ(exchange->clientId("alice"), exchange->clientId("alice"));
}

TEST_F(SelfMatchPreventionTest, ClientId_DifferentUsersDiffer) {
    auto exchange = makeExchange("none");
    EXPECT_NE(exchange->clientId("alice"), exchange->clientId("bob"));
}

// -1 is what isSelfMatch treats as "no identity", so an unauthenticated order is
// left out of SMP rather than lumped in with every other unauthenticated one.
TEST_F(SelfMatchPreventionTest, ClientId_EmptyUserIsUnidentified) {
    auto exchange = makeExchange("none");
    EXPECT_EQ(exchange->clientId(""), -1);
}

// ===========================================================================
// End to end through the order path
// ===========================================================================

TEST_F(SelfMatchPreventionTest, CancelNewest_RejectsCrossingOrderFromSameUser) {
    auto exchange = makeExchange("cancel_newest");
    auto *sym = subscribe(*exchange, "SMP-E2E-1");
    ASSERT_NE(sym, nullptr);
    OrderResponseCollector responses(exchange->response_queue());

    exchange->submitOrder(makeOrder("SMP-E2E-1", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-1", "alice", "a-2", Side::BUY, kPrice100, kQty10));

    bool rejected_for_smp = false;
    for (const auto &response : responses.collect()) {
        if (response.exec_type == ExecType::REJECTED &&
            response.reject_reason == OrdRejectReason::SMP) {
            rejected_for_smp = true;
        }
    }
    EXPECT_TRUE(rejected_for_smp) << "crossing order from the same user was not rejected";

    // The resting order survives under CANCEL_NEWEST.
    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);
}

TEST_F(SelfMatchPreventionTest, CancelResting_RemovesRestingOrderFromSameUser) {
    auto exchange = makeExchange("cancel_resting");
    auto *sym = subscribe(*exchange, "SMP-E2E-2");
    ASSERT_NE(sym, nullptr);

    exchange->submitOrder(makeOrder("SMP-E2E-2", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-2", "alice", "a-2", Side::BUY, kPrice100, kQty10));

    // The resting sell is cancelled rather than traded against.
    auto [sell_level, si] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    EXPECT_EQ(sell_level, nullptr);
}

// Two different accounts crossing is an ordinary trade, whatever the SMP mode.
TEST_F(SelfMatchPreventionTest, DifferentUsers_MatchNormally) {
    auto exchange = makeExchange("cancel_newest");
    auto *sym = subscribe(*exchange, "SMP-E2E-3");
    ASSERT_NE(sym, nullptr);

    exchange->submitOrder(makeOrder("SMP-E2E-3", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-3", "bob", "b-1", Side::BUY, kPrice100, kQty10));

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    EXPECT_EQ(level, nullptr) << "the resting order should have been filled";
}

// Orders with no user_id carry no identity, so SMP must not fire between them
// even though their client_ids are equal (-1 == -1).
TEST_F(SelfMatchPreventionTest, UnidentifiedOrders_MatchNormally) {
    auto exchange = makeExchange("cancel_newest");
    auto *sym = subscribe(*exchange, "SMP-E2E-4");
    ASSERT_NE(sym, nullptr);

    exchange->submitOrder(makeOrder("SMP-E2E-4", "", "n-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-4", "", "n-2", Side::BUY, kPrice100, kQty10));

    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    EXPECT_EQ(level, nullptr) << "unidentified orders should trade normally";
}

// A rejected order must leave no trace on the wire. Under CANCEL_NEWEST the
// decision is taken before the order is acked, so the client sees exactly one
// terminal response and never a NEW or CANCELED it has to reconcile.
TEST_F(SelfMatchPreventionTest, CancelNewest_EmitsOneTerminalResponse) {
    auto exchange = makeExchange("cancel_newest");
    ASSERT_NE(subscribe(*exchange, "SMP-RSP-1"), nullptr);
    OrderResponseCollector responses(exchange->response_queue());

    exchange->submitOrder(makeOrder("SMP-RSP-1", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    responses.collect();   // drop the responses for the resting order

    exchange->submitOrder(makeOrder("SMP-RSP-1", "alice", "a-2", Side::BUY, kPrice100, kQty10));

    // The pending and the working ack share ExecType::NEW and differ by status,
    // so both fields matter here.
    auto emitted = responses.collect();
    ASSERT_EQ(emitted.size(), 2u);
    EXPECT_EQ(emitted[0].exec_type, ExecType::NEW);
    EXPECT_EQ(emitted[0].order_status, OrderStatus::PENDING_NEW);
    EXPECT_EQ(emitted[1].exec_type, ExecType::REJECTED);
    EXPECT_EQ(emitted[1].order_status, OrderStatus::REJECTED);

    // Specifically: the order was never acked as working, and never cancelled.
    for (const auto &response : emitted) {
        EXPECT_FALSE(response.exec_type == ExecType::NEW &&
                     response.order_status == OrderStatus::NEW) << "rejected order was acked as working";
        EXPECT_NE(response.exec_type, ExecType::CANCELED) << "rejected order also reported CANCELED";
    }
}

// CANCEL_RESTING is a real cancel: the owner has to be told, or it goes on
// believing the order is live.
TEST_F(SelfMatchPreventionTest, CancelResting_NotifiesTheRestingOrdersOwner) {
    auto exchange = makeExchange("cancel_resting");
    ASSERT_NE(subscribe(*exchange, "SMP-RSP-2"), nullptr);
    OrderResponseCollector responses(exchange->response_queue());

    exchange->submitOrder(makeOrder("SMP-RSP-2", "alice", "resting", Side::SELL, kPrice100, kQty10));
    responses.collect();

    exchange->submitOrder(makeOrder("SMP-RSP-2", "alice", "incoming", Side::BUY, kPrice100, kQty10));

    bool resting_cancelled = false;
    for (const auto &response : responses.collect()) {
        if (response.exec_type == ExecType::CANCELED &&
            std::string(response.client_order_id) == "resting") {
            resting_cancelled = true;
        }
    }
    EXPECT_TRUE(resting_cancelled) << "the cancelled resting order's owner was never told";
}

// An amendment re-enters the matching loop, so it must carry the symbol's SMP
// mode too — Symbol::modifyOrder previously dropped it and used the NONE default.
TEST_F(SelfMatchPreventionTest, Amendment_AppliesSelfMatchPrevention) {
    auto exchange = makeExchange("cancel_newest");
    auto *sym = subscribe(*exchange, "SMP-E2E-5");
    ASSERT_NE(sym, nullptr);
    OrderResponseCollector responses(exchange->response_queue());

    // Resting sell at 100, plus a non-crossing buy at 98 from the same user.
    exchange->submitOrder(makeOrder("SMP-E2E-5", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-5", "alice", "a-2", Side::BUY, kPrice98, kQty10));
    responses.collect();   // drop the acks from the two adds

    // Amend the buy up to 100 so it would cross its owner's own resting sell.
    Request amend{};
    std::memset(amend.symbol, 0, sizeof(amend.symbol));
    std::memcpy(amend.symbol, "SMP-E2E-5", sizeof("SMP-E2E-5") - 1);
    amend.msg_type = MessageType::ORDER_REPLACE_REQUEST;
    std::memset(&amend.modify_order, 0, sizeof(amend.modify_order));
    std::memcpy(amend.modify_order.client_order_id, "a-2", sizeof("a-2") - 1);
    amend.modify_order.new_price = kPrice100;
    amend.modify_order.new_qty = kQty10;
    exchange->amendOrder(amend);

    bool rejected_for_smp = false;
    for (const auto &response : responses.collect()) {
        if (response.exec_type == ExecType::REJECTED &&
            response.reject_reason == OrdRejectReason::SMP) {
            rejected_for_smp = true;
        }
    }
    EXPECT_TRUE(rejected_for_smp) << "amendment crossing the owner's own order was not prevented";

    // The resting sell is untouched under CANCEL_NEWEST.
    auto [level, idx] = sym->order_book_->getLevel(to_book_side(Side::SELL), kPrice100);
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_quantity, kQty10);
}

// A rejected amendment must not half-apply. The order keeps its original price
// and quantity, and no REPLACED is reported for a change that did not happen.
TEST_F(SelfMatchPreventionTest, RejectedAmendment_LeavesOrderUnchanged) {
    auto exchange = makeExchange("cancel_newest");
    auto *sym = subscribe(*exchange, "SMP-E2E-6");
    ASSERT_NE(sym, nullptr);
    OrderResponseCollector responses(exchange->response_queue());

    exchange->submitOrder(makeOrder("SMP-E2E-6", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-6", "alice", "a-2", Side::BUY, kPrice98, kQty10));
    responses.collect();

    auto *amended = sym->findOrderByClientOrderId("a-2");
    ASSERT_NE(amended, nullptr);

    Request amend{};
    std::memset(amend.symbol, 0, sizeof(amend.symbol));
    std::memcpy(amend.symbol, "SMP-E2E-6", sizeof("SMP-E2E-6") - 1);
    amend.msg_type = MessageType::ORDER_REPLACE_REQUEST;
    std::memset(&amend.modify_order, 0, sizeof(amend.modify_order));
    std::memcpy(amend.modify_order.client_order_id, "a-2", sizeof("a-2") - 1);
    amend.modify_order.new_price = kPrice100;
    amend.modify_order.new_qty = kQty5;
    exchange->amendOrder(amend);

    // The amendment never took effect, so the stored order is as it was.
    EXPECT_EQ(amended->price, kPrice98);
    EXPECT_EQ(amended->quantity, kQty10);
    EXPECT_EQ(amended->leaves_quantity, kQty10);

    for (const auto &response : responses.collect()) {
        EXPECT_NE(response.exec_type, ExecType::REPLACED)
            << "a rejected amendment reported REPLACED";
    }
}

// The pre-check must scan only as deep as the order will really trade. A
// partially filled amendment's total quantity exceeds its remaining leaves, so
// scanning by total could reach one of the owner's resting orders the amendment
// would never touch and reject a fill that should have happened.
TEST_F(SelfMatchPreventionTest, PartiallyFilledAmendment_NotRejectedForUnreachableOwnOrder) {
    auto exchange = makeExchange("cancel_newest");
    auto *sym = subscribe(*exchange, "SMP-E2E-8");
    ASSERT_NE(sym, nullptr);

    // alice rests a buy of 10 at 98 and gets 8 of it filled by bob.
    exchange->submitOrder(makeOrder("SMP-E2E-8", "alice", "a-1", Side::BUY, kPrice98, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-8", "bob", "b-1", Side::SELL, kPrice98, to_qty_t(8.0)));
    auto *partial = sym->findOrderByClientOrderId("a-1");
    ASSERT_NE(partial, nullptr);
    ASSERT_EQ(partial->cum_quantity, to_qty_t(8.0));
    ASSERT_EQ(partial->leaves_quantity, to_qty_t(2.0));

    // bob offers 2 at 100, and alice's own sell of 10 sits behind it at 101.
    exchange->submitOrder(makeOrder("SMP-E2E-8", "bob", "b-2", Side::SELL, kPrice100, to_qty_t(2.0)));
    exchange->submitOrder(makeOrder("SMP-E2E-8", "alice", "a-2", Side::SELL, kPrice101, kQty10));

    OrderResponseCollector responses(exchange->response_queue());

    // Amend alice's buy up to 101. Its leaves are 2, which bob's offer absorbs
    // entirely — alice's own sell at 101 is never reached. Scanning by the
    // order's total quantity of 10 would reach it and reject.
    Request amend{};
    std::memset(amend.symbol, 0, sizeof(amend.symbol));
    std::memcpy(amend.symbol, "SMP-E2E-8", sizeof("SMP-E2E-8") - 1);
    amend.msg_type = MessageType::ORDER_REPLACE_REQUEST;
    std::memset(&amend.modify_order, 0, sizeof(amend.modify_order));
    std::memcpy(amend.modify_order.client_order_id, "a-1", sizeof("a-1") - 1);
    amend.modify_order.new_price = kPrice101;
    amend.modify_order.new_qty = kQty10;
    exchange->amendOrder(amend);

    for (const auto &response : responses.collect()) {
        EXPECT_NE(response.reject_reason, OrdRejectReason::SMP)
            << "amendment rejected against an order its leaves could never reach";
    }
    EXPECT_EQ(partial->cum_quantity, kQty10) << "the reachable liquidity was not taken";
}

// Request is a union, so a modify reject that reads add_order fields lands the
// order id in client_order_id and takes price/qty from the wrong offsets.
TEST_F(SelfMatchPreventionTest, RejectedAmendment_CarriesModifyFields) {
    auto exchange = makeExchange("cancel_newest");
    ASSERT_NE(subscribe(*exchange, "SMP-E2E-7"), nullptr);
    OrderResponseCollector responses(exchange->response_queue());

    exchange->submitOrder(makeOrder("SMP-E2E-7", "alice", "a-1", Side::SELL, kPrice100, kQty10));
    exchange->submitOrder(makeOrder("SMP-E2E-7", "alice", "a-2", Side::BUY, kPrice98, kQty10));
    responses.collect();

    Request amend{};
    std::memset(amend.symbol, 0, sizeof(amend.symbol));
    std::memcpy(amend.symbol, "SMP-E2E-7", sizeof("SMP-E2E-7") - 1);
    amend.msg_type = MessageType::ORDER_REPLACE_REQUEST;
    std::memset(&amend.modify_order, 0, sizeof(amend.modify_order));
    std::memcpy(amend.modify_order.user_id, "alice", sizeof("alice") - 1);
    std::memcpy(amend.modify_order.order_id, "ORDER-XYZ", sizeof("ORDER-XYZ") - 1);
    std::memcpy(amend.modify_order.client_order_id, "a-2", sizeof("a-2") - 1);
    amend.modify_order.new_price = kPrice100;
    amend.modify_order.new_qty = kQty5;
    exchange->amendOrder(amend);

    bool checked = false;
    for (const auto &response : responses.collect()) {
        if (response.exec_type != ExecType::REJECTED) {
            continue;
        }
        EXPECT_STREQ(response.client_order_id, "a-2");
        EXPECT_STREQ(response.order_id, "ORDER-XYZ");
        EXPECT_STREQ(response.user_id, "alice");
        EXPECT_EQ(response.price, kPrice100);
        EXPECT_EQ(response.qty, kQty5);
        checked = true;
    }
    EXPECT_TRUE(checked) << "no reject response to inspect";
}
