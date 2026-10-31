#include <gtest/gtest.h>
#include <order_gateway/retransmit_ring.hpp>

using slick::sim::order_gateway::RetransmitRing;

TEST(RetransmitRingTest, KeepsTheLastCapacityMessages) {
    RetransmitRing<16> ring(4);
    for (uint32_t seq = 1; seq <= 6; ++seq) {
        auto &slot = ring.claim(seq);
        slot.size = static_cast<uint16_t>(seq);
        slot.data[0] = static_cast<uint8_t>(seq);
    }
    EXPECT_EQ(ring.find(1), nullptr);
    EXPECT_EQ(ring.find(2), nullptr);
    ASSERT_NE(ring.find(3), nullptr);
    EXPECT_EQ(ring.find(3)->data[0], 3);
    EXPECT_EQ(ring.find(6)->size, 6);
    EXPECT_EQ(ring.find(7), nullptr);   // not sent yet
    EXPECT_TRUE(ring.holds(3, 4));
    EXPECT_FALSE(ring.holds(2, 2));
}

TEST(RetransmitRingTest, SequenceZeroIsNeverHeld) {
    RetransmitRing<8> ring(4);
    EXPECT_EQ(ring.find(0), nullptr);
    EXPECT_EQ(RetransmitRing<8>().find(1), nullptr);   // no slots at all
}

// A request may name a range that runs over the end of the 32-bit sequence space. It
// must not be reported as held, and the walk must not run past the wrap.
TEST(RetransmitRingTest, ARangeAcrossTheSequenceWrapIsNotHeld) {
    RetransmitRing<8> ring(4);
    ring.claim(0xFFFFFFFFu).size = 1;
    EXPECT_TRUE(ring.holds(0xFFFFFFFFu, 1));
    EXPECT_FALSE(ring.holds(0xFFFFFFFFu, 2));   // 0 is never a sent message
}

TEST(RetransmitRingTest, ClearAndSwapKeepTheSlots) {
    RetransmitRing<8> current(4);
    RetransmitRing<8> previous;
    EXPECT_TRUE(previous.empty());
    current.claim(1).size = 1;

    previous.resize(current.capacity());
    previous.swap(current);
    current.clear();

    EXPECT_NE(previous.find(1), nullptr);
    EXPECT_EQ(current.find(1), nullptr);
    EXPECT_EQ(current.capacity(), 4u);
}
