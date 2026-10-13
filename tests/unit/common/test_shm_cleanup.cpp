#include <gtest/gtest.h>
#include <exchange/exchange.hpp>
#include <nlohmann/json.hpp>
#include <slick/shm/shared_memory.hpp>
#include <array>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::exch;
using namespace slick::sim::test;

// slick-queue 2.3.0 stopped unlinking a segment in the queue's destructor. On POSIX
// the names then outlived every test, and each later queue built with one attached
// to the previous test's segment. These cover the helpers in test_helpers.hpp that
// every fixture naming a segment now builds its queues and exchanges with.
//
// On Windows a segment is freed with its last handle, so the leak never existed
// there and these pass with or without the helpers.

namespace {

constexpr const char *kQueueShmName = "shm_cleanup_test_queue";

constexpr std::array<const char *, 3> kExchangeShmNames = {
    "shm_cleanup_test_req",
    "shm_cleanup_test_resp",
    "shm_cleanup_test_md",
};

/// The smallest config the base Exchange accepts, with all three queues on named
/// shared memory - the arrangement the venue fixtures use.
nlohmann::json shmConfig() {
    return nlohmann::json{
        {"request_queue_size", 1024},
        {"response_queue_size", 1024},
        {"md_queue_size", 4096},
        {"request_queue_shm_name", kExchangeShmNames[0]},
        {"response_queue_shm_name", kExchangeShmNames[1]},
        {"md_queue_shm_name", kExchangeShmNames[2]},
        {"order_gateway", nlohmann::json::object()},
        {"md_publisher", nlohmann::json::object()},
    };
}

class PlainExchange : public Exchange {
public:
    explicit PlainExchange(const nlohmann::json &config)
        : Exchange(Venue::STOCK, config) {}
};

void publishResponse(slick::queue<OrderResponse> &queue) {
    auto index = queue.reserve();
    *queue[index] = OrderResponse{};
    queue.publish(index);
}

size_t pendingResponses(slick::queue<OrderResponse> &queue) {
    return OrderResponseCollector(queue).count();
}

}   // namespace

TEST(ShmCleanupTest, QueueSegmentIsUnlinkedWhenTheTestQueueIsDestroyed) {
    auto queue = makeTestQueue<OrderResponse>(1024, kQueueShmName);
    EXPECT_TRUE(slick::shm::shared_memory::exists(kQueueShmName));

    queue.reset();
    EXPECT_FALSE(slick::shm::shared_memory::exists(kQueueShmName)) << "the segment was left behind";
}

// What the leak actually broke: the fixtures' collectors read from cursor 0, so a
// queue attached to an old segment handed them the previous test's responses.
TEST(ShmCleanupTest, ASecondQueueUnderTheSameNameStartsEmpty) {
    {
        auto first = makeTestQueue<OrderResponse>(1024, kQueueShmName);
        publishResponse(*first);
        EXPECT_EQ(pendingResponses(*first), 1u);
    }

    auto second = makeTestQueue<OrderResponse>(1024, kQueueShmName);
    EXPECT_EQ(pendingResponses(*second), 0u) << "the second queue attached to the first one's segment";
}

// A test that crashes never reaches its cleanup. Destroying a queue without the
// helper leaves the same thing behind on POSIX: the segment, with a response in it.
// makeTestQueue has to replace it rather than attach to it.
TEST(ShmCleanupTest, AQueueSegmentLeftByACrashedRunIsReplaced) {
    {
        slick::queue<OrderResponse> crashed(1024, kQueueShmName);
        publishResponse(crashed);
    }

    auto queue = makeTestQueue<OrderResponse>(1024, kQueueShmName);
    EXPECT_EQ(pendingResponses(*queue), 0u) << "the queue attached to a segment an earlier run left behind";
}

TEST(ShmCleanupTest, ExchangeSegmentsAreUnlinkedWhenTheTestExchangeIsDestroyed) {
    auto exchange = makeTestExchange<PlainExchange>(shmConfig());
    for (const char *name : kExchangeShmNames) {
        EXPECT_TRUE(slick::shm::shared_memory::exists(name)) << name;
    }

    exchange.reset();
    for (const char *name : kExchangeShmNames) {
        EXPECT_FALSE(slick::shm::shared_memory::exists(name)) << name << " was left behind";
    }
}

TEST(ShmCleanupTest, ASecondExchangeUnderTheSameNamesStartsEmpty) {
    {
        auto first = makeTestExchange<PlainExchange>(shmConfig());
        publishResponse(first->response_queue());
        EXPECT_EQ(pendingResponses(first->response_queue()), 1u);
    }

    auto second = makeTestExchange<PlainExchange>(shmConfig());
    EXPECT_EQ(pendingResponses(second->response_queue()), 0u)
        << "the second exchange attached to the first one's segment";
}

TEST(ShmCleanupTest, ExchangeSegmentsLeftByACrashedRunAreReplaced) {
    {
        PlainExchange crashed(shmConfig());
        publishResponse(crashed.response_queue());
    }

    auto exchange = makeTestExchange<PlainExchange>(shmConfig());
    EXPECT_EQ(pendingResponses(exchange->response_queue()), 0u)
        << "the exchange attached to a segment an earlier run left behind";
}
