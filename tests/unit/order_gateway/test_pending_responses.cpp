#include <gtest/gtest.h>
#include <order_gateway/pending_responses.hpp>
#include <common/messages.hpp>
#include <string>
#include <vector>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::order_gateway;

namespace {

OrderResponse makeResponse(const std::string &user_id,
                           const std::string &client_order_id,
                           slick::sim::time_t request_time = 0) {
    OrderResponse r{};
    utils::copy_wire_field(r.user_id, user_id);
    utils::copy_wire_field(r.client_order_id, client_order_id);
    r.request_time = request_time;
    r.order_status = OrderStatus::PENDING_NEW;
    return r;
}

// The correlation a create-order handler uses.
PendingResponses::Predicate byClientOrderId(std::string user_id, std::string coid) {
    return [user_id = std::move(user_id), coid = std::move(coid)](const OrderResponse &r) {
        return std::string_view(r.user_id) == user_id
            && std::string_view(r.client_order_id) == coid;
    };
}

}  // namespace

TEST(PendingResponsesTest, MatchingReportCompletesTheWaiter) {
    PendingResponses pending;
    const OrderResponse *seen = nullptr;
    bool called = false;

    pending.add(byClientOrderId("alice", "coid-1"),
                [&](const OrderResponse *r) { seen = r; called = true; });

    auto response = makeResponse("alice", "coid-1");
    EXPECT_TRUE(pending.dispatch(response));
    EXPECT_TRUE(called);
    ASSERT_NE(seen, nullptr);
    EXPECT_EQ(std::string_view(seen->client_order_id), "coid-1");
    EXPECT_EQ(pending.size(), 0u) << "a completed waiter must not linger";
}

// A report for somebody else must leave the waiter alone - this is what stops one
// client's order from completing another client's request.
TEST(PendingResponsesTest, UnrelatedReportsLeaveWaitersInPlace) {
    PendingResponses pending;
    bool called = false;
    pending.add(byClientOrderId("alice", "coid-1"),
                [&](const OrderResponse *) { called = true; });

    auto other_user = makeResponse("bob", "coid-1");
    auto other_order = makeResponse("alice", "coid-2");
    EXPECT_FALSE(pending.dispatch(other_user));
    EXPECT_FALSE(pending.dispatch(other_order));

    EXPECT_FALSE(called);
    EXPECT_EQ(pending.size(), 1u);
}

// One report answers one request, so concurrent waiters must each get their own.
TEST(PendingResponsesTest, ConcurrentWaitersEachGetTheirOwnReport) {
    PendingResponses pending;
    std::vector<std::string> completed;

    pending.add(byClientOrderId("alice", "coid-1"),
                [&](const OrderResponse *r) { completed.push_back(r ? "a" : "a-timeout"); });
    pending.add(byClientOrderId("bob", "coid-2"),
                [&](const OrderResponse *r) { completed.push_back(r ? "b" : "b-timeout"); });

    auto second = makeResponse("bob", "coid-2");
    pending.dispatch(second);
    EXPECT_EQ(completed, (std::vector<std::string>{"b"}))
        << "the wrong waiter claimed the report";
    EXPECT_EQ(pending.size(), 1u);

    auto first = makeResponse("alice", "coid-1");
    pending.dispatch(first);
    EXPECT_EQ(completed, (std::vector<std::string>{"b", "a"}));
    EXPECT_EQ(pending.size(), 0u);
}

// The timeout path hands the completion a nullptr so it can answer 504.
TEST(PendingResponsesTest, ExpiryCompletesWithNoReport) {
    PendingResponses pending;
    bool called = false;
    const OrderResponse *seen = reinterpret_cast<const OrderResponse *>(1);

    auto id = pending.add(byClientOrderId("alice", "coid-1"),
                          [&](const OrderResponse *r) { seen = r; called = true; });
    pending.expire(id);

    EXPECT_TRUE(called);
    EXPECT_EQ(seen, nullptr);
    EXPECT_EQ(pending.size(), 0u);
}

// A per-request timer can fire after its report already arrived. That must not
// complete the request a second time - the HTTP response is already sent.
TEST(PendingResponsesTest, ExpiryAfterCompletionIsHarmless) {
    PendingResponses pending;
    int completions = 0;

    auto id = pending.add(byClientOrderId("alice", "coid-1"),
                          [&](const OrderResponse *) { ++completions; });

    auto response = makeResponse("alice", "coid-1");
    pending.dispatch(response);
    EXPECT_EQ(completions, 1);

    pending.expire(id);
    EXPECT_EQ(completions, 1) << "a late timer completed the request twice";
}

// An aborted connection drops its waiter without running the completion, which
// would otherwise write to a closed socket.
TEST(PendingResponsesTest, AbandonedWaitersAreNeverCompleted) {
    PendingResponses pending;
    bool called = false;

    auto id = pending.add(byClientOrderId("alice", "coid-1"),
                          [&](const OrderResponse *) { called = true; });
    pending.abandon(id);
    EXPECT_FALSE(pending.waiting(id));

    auto response = makeResponse("alice", "coid-1");
    EXPECT_FALSE(pending.dispatch(response));
    pending.expire(id);

    EXPECT_FALSE(called) << "an abandoned request was still answered";
}

// Completions run while the waiter list is being walked, and a handler may chain a
// follow-up wait - that must not corrupt the container.
TEST(PendingResponsesTest, CompletionMayRegisterAnotherWaiter) {
    PendingResponses pending;
    int first = 0, second = 0;

    pending.add(byClientOrderId("alice", "coid-1"), [&](const OrderResponse *) {
        ++first;
        pending.add(byClientOrderId("alice", "coid-2"),
                    [&](const OrderResponse *) { ++second; });
    });

    auto one = makeResponse("alice", "coid-1");
    pending.dispatch(one);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(pending.size(), 1u);

    auto two = makeResponse("alice", "coid-2");
    pending.dispatch(two);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(pending.size(), 0u);
}

// The cancel handlers correlate on the timestamp they stamped, not a client id.
TEST(PendingResponsesTest, SupportsCorrelationOnRequestTime) {
    PendingResponses pending;
    bool called = false;

    pending.add(
        [](const OrderResponse &r) {
            return std::string_view(r.user_id) == "alice" && r.request_time == 4242;
        },
        [&](const OrderResponse *) { called = true; });

    auto wrong_time = makeResponse("alice", "", 1111);
    EXPECT_FALSE(pending.dispatch(wrong_time));
    EXPECT_FALSE(called);

    auto right_time = makeResponse("alice", "", 4242);
    EXPECT_TRUE(pending.dispatch(right_time));
    EXPECT_TRUE(called);
}
