#include <gtest/gtest.h>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <utils/fixed_string.hpp>
#include <cstring>
#include <format>
#include <sstream>
#include <string>

using namespace slick::sim;
using slick::sim::utils::fixed_string;

// ---------------------------------------------------------------------------
// The identity fields on Order (symbol, user_id, client_order_id, order_id) used
// to be std::strings that the publish path copied with
//
//     memcpy(response.symbol, order->symbol.c_str(), sizeof(response.symbol));
//
// That reads sizeof(the wire field) bytes out of a buffer holding however many
// characters the name actually has, so every publish over-read the source - by
// 25 bytes for a name like "BTC-USD". The same idiom appeared on Symbol::symbol_
// in the market-data publish path.
//
// fixed_string replaces those: storage is exactly the width of the wire field, so
// the copy is in bounds by construction, and nothing is allocated for a value
// that was always bounded. These tests pin the properties the publish path now
// depends on.
// ---------------------------------------------------------------------------

TEST(FixedStringTest, StoresAndReadsBackValue) {
    fixed_string<32> s("BTC-USD");
    EXPECT_EQ(s.view(), "BTC-USD");
    EXPECT_EQ(s.size(), 7u);
    EXPECT_FALSE(s.empty());
    EXPECT_STREQ(s.c_str(), "BTC-USD");
}

TEST(FixedStringTest, DefaultIsEmptyAndFullyZeroed) {
    fixed_string<32> s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    for (size_t i = 0; i < s.buffer_size(); ++i) {
        EXPECT_EQ(s.data()[i], '\0') << "byte " << i << " was not zeroed";
    }
}

// The whole point: the buffer is exactly as wide as the wire field, so a publish
// that copies the full field never reads past the end of the object.
TEST(FixedStringTest, BufferIsExactlyTheDeclaredWidth) {
    EXPECT_EQ(sizeof(fixed_string<32>), 32u);
    EXPECT_EQ(sizeof(fixed_string<37>), 37u);
    EXPECT_EQ(fixed_string<32>::capacity(), 31u);
    EXPECT_EQ(fixed_string<32>::buffer_size(), 32u);
}

TEST(FixedStringTest, TruncatesOverlongValueAndStaysNullTerminated) {
    const std::string too_long(64, 'x');
    fixed_string<32> s(too_long);

    EXPECT_EQ(s.size(), 31u) << "must leave room for the terminator";
    EXPECT_EQ(s.data()[31], '\0');
    EXPECT_EQ(std::strlen(s.c_str()), 31u);
}

// A pooled Order is reused. Overwriting a long id with a short one must not leave
// the tail of the previous value readable behind the terminator - the publish path
// copies the whole field, so anything left there goes out on the wire.
TEST(FixedStringTest, ReassignmentZeroesTheTail) {
    fixed_string<37> s("11111111-2222-3333-4444-555555555555");
    ASSERT_EQ(s.size(), 36u);

    s = "short";
    EXPECT_EQ(s.view(), "short");
    for (size_t i = 5; i < s.buffer_size(); ++i) {
        EXPECT_EQ(s.data()[i], '\0') << "stale byte survived at " << i;
    }
}

TEST(FixedStringTest, EmptyAssignmentClearsEverything) {
    fixed_string<37> s("something");
    s = std::string_view();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    for (size_t i = 0; i < s.buffer_size(); ++i) {
        EXPECT_EQ(s.data()[i], '\0');
    }
}

TEST(FixedStringTest, ComparesByContent) {
    fixed_string<32> a("ETH-USD");
    fixed_string<32> b("ETH-USD");
    fixed_string<32> c("BTC-USD");

    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
    EXPECT_EQ(a, std::string_view("ETH-USD"));
    EXPECT_EQ(std::string_view("ETH-USD"), a);
}

// Every LOG_* call in the engine formats these fields.
TEST(FixedStringTest, FormatsAndStreamsAsText) {
    fixed_string<32> s("SOL-USD");
    EXPECT_EQ(std::format("{}", s), "SOL-USD");
    EXPECT_EQ(std::format("[{:>10}]", s), "[   SOL-USD]");

    std::ostringstream os;
    os << s;
    EXPECT_EQ(os.str(), "SOL-USD");
}

// ---------------------------------------------------------------------------
// copy_wire_field is what the gateways and publishers use to fill the fixed char
// arrays on a Request. It replaced two idioms, each broken in its own way:
//
//   memcpy(dst, s.c_str(), sizeof(dst))                        // over-reads
//   memset(dst, 0, sizeof(dst));                               // bounded, but
//   memcpy(dst, s.c_str(), min(sizeof(dst), s.size()));        // unterminated
//
// The second is the subtler one: a value that exactly fills the field overwrites
// the last zero the memset wrote, and the receiver - which reads these as C
// strings - then runs past the end of the field.
// ---------------------------------------------------------------------------

TEST(CopyWireFieldTest, CopiesAndZeroFillsTheRemainder) {
    char field[32];
    std::memset(field, 0x7F, sizeof(field));

    EXPECT_FALSE(utils::copy_wire_field(field, "BTC-USD"));

    EXPECT_STREQ(field, "BTC-USD");
    for (size_t i = 7; i < sizeof(field); ++i) {
        ASSERT_EQ(field[i], '\0') << "byte " << i << " was not cleared";
    }
}

TEST(CopyWireFieldTest, ExactlyFillingValueStaysNullTerminated) {
    char field[8];
    // 7 characters plus the terminator: the largest value that fits.
    EXPECT_FALSE(utils::copy_wire_field(field, "1234567"));
    EXPECT_STREQ(field, "1234567");
    EXPECT_EQ(std::strlen(field), 7u);

    // 8 characters cannot fit. The old idiom filled all 8 bytes and left no
    // terminator; this must truncate and report it.
    EXPECT_TRUE(utils::copy_wire_field(field, "12345678"));
    EXPECT_EQ(std::strlen(field), 7u) << "field must stay null-terminated";
    EXPECT_STREQ(field, "1234567");
}

TEST(CopyWireFieldTest, ReportsTruncationOnlyWhenItHappens) {
    char field[8];
    EXPECT_FALSE(utils::copy_wire_field(field, ""));
    EXPECT_FALSE(utils::copy_wire_field(field, "abc"));
    EXPECT_FALSE(utils::copy_wire_field(field, "1234567"));
    EXPECT_TRUE(utils::copy_wire_field(field, "12345678"));
    EXPECT_TRUE(utils::copy_wire_field(field, std::string(100, 'z')));
}

TEST(CopyWireFieldTest, EmptyValueClearsAWholeRecycledField) {
    // A Request comes out of a ring buffer still holding the previous message.
    char field[16];
    std::memset(field, 'A', sizeof(field));

    EXPECT_FALSE(utils::copy_wire_field(field, std::string_view()));
    for (size_t i = 0; i < sizeof(field); ++i) {
        ASSERT_EQ(field[i], '\0') << "stale byte survived at " << i;
    }
}

// A short value must not leave the tail of the previous, longer one behind it.
TEST(CopyWireFieldTest, ShorterValueDoesNotLeavePreviousTail) {
    char field[37];
    utils::copy_wire_field(field, "11111111-2222-3333-4444-555555555555");
    ASSERT_EQ(std::strlen(field), 36u);

    utils::copy_wire_field(field, "cl-2");
    EXPECT_STREQ(field, "cl-2");
    for (size_t i = 4; i < sizeof(field); ++i) {
        ASSERT_EQ(field[i], '\0') << "stale byte survived at " << i;
    }
}

// ---------------------------------------------------------------------------
// OrderIdentity is copied onto the head of an OrderResponse as a single block, so
// its layout has to track OrderResponse exactly. messages.hpp static_asserts this
// at compile time; these assert the same thing at runtime so a failure names the
// field that moved.
// ---------------------------------------------------------------------------

TEST(OrderIdentityTest, LayoutMatchesOrderResponseHeader) {
    EXPECT_EQ(offsetof(OrderIdentity, symbol), offsetof(OrderResponse, symbol));
    EXPECT_EQ(offsetof(OrderIdentity, user_id), offsetof(OrderResponse, user_id));
    EXPECT_EQ(offsetof(OrderIdentity, client_order_id), offsetof(OrderResponse, client_order_id));
    EXPECT_EQ(offsetof(OrderIdentity, order_id), offsetof(OrderResponse, order_id));

    // No gaps, and it stops exactly where the non-identity fields begin.
    EXPECT_EQ(sizeof(OrderIdentity), 32u + 37u + 37u + 37u);
    EXPECT_EQ(sizeof(OrderIdentity), offsetof(OrderResponse, error_message));
}

TEST(OrderIdentityTest, SetOrderIdentityFillsAllFourFields) {
    Order order;
    order.symbol = "BTC-USD";
    order.user_id = "user-1";
    order.client_order_id = "cl-42";
    order.order_id = "11111111-2222-3333-4444-555555555555";

    OrderResponse response{};
    setOrderIdentity(response, order);

    EXPECT_STREQ(response.symbol, "BTC-USD");
    EXPECT_STREQ(response.user_id, "user-1");
    EXPECT_STREQ(response.client_order_id, "cl-42");
    EXPECT_STREQ(response.order_id, "11111111-2222-3333-4444-555555555555");
}

// The copy writes exactly the identity header and stops: error_message sits
// immediately after it and must survive untouched.
TEST(OrderIdentityTest, SetOrderIdentityDoesNotOverrunIntoErrorMessage) {
    Order order;
    order.symbol = "ETH-USD";
    order.user_id = "u";
    order.client_order_id = "c";
    order.order_id = "o";

    OrderResponse response{};
    std::memset(response.error_message, 0xAB, sizeof(response.error_message));

    setOrderIdentity(response, order);

    for (size_t i = 0; i < sizeof(response.error_message); ++i) {
        ASSERT_EQ(static_cast<unsigned char>(response.error_message[i]), 0xABu)
            << "identity copy ran past its block at error_message[" << i << "]";
    }
}

// A short name must not drag the bytes that follow it in the Order onto the wire.
TEST(OrderIdentityTest, ShortValuesArriveZeroPaddedNotWithNeighbouringBytes) {
    Order order;
    order.symbol = "BTC-USD";
    order.user_id = "abcdefghijklmnopqrstuvwxyz0123456789";  // fills user_id
    order.client_order_id = "cl";
    order.order_id = "id";

    OrderResponse response{};
    setOrderIdentity(response, order);

    // symbol is 7 characters in a 32-byte field: the remaining 25 must be zero,
    // not the first bytes of user_id.
    for (size_t i = 7; i < sizeof(response.symbol); ++i) {
        ASSERT_EQ(response.symbol[i], '\0')
            << "symbol field carried a neighbouring byte at " << i;
    }
    EXPECT_STREQ(response.user_id, "abcdefghijklmnopqrstuvwxyz0123456789");
}

// A recycled Order that gets a shorter id must not publish the tail of the id the
// previous order had.
TEST(OrderIdentityTest, RecycledOrderDoesNotPublishPreviousIdentity) {
    Order order;
    order.symbol = "BTC-USD";
    order.client_order_id = "client-order-id-that-is-long";
    order.order_id = "11111111-2222-3333-4444-555555555555";

    OrderResponse first{};
    setOrderIdentity(first, order);
    ASSERT_STREQ(first.client_order_id, "client-order-id-that-is-long");

    // Same storage, reused for the next order.
    order.client_order_id = "cl-2";
    order.order_id = "abc";

    OrderResponse second{};
    setOrderIdentity(second, order);

    EXPECT_STREQ(second.client_order_id, "cl-2");
    EXPECT_STREQ(second.order_id, "abc");
    for (size_t i = 4; i < sizeof(second.client_order_id); ++i) {
        ASSERT_EQ(second.client_order_id[i], '\0')
            << "previous client_order_id survived at byte " << i;
    }
}

// order_id holds a rendered UUID exactly; OrderBook::allocateOrder writes one
// straight into this buffer, so a byte less would truncate every id.
TEST(OrderIdentityTest, OrderIdHoldsARenderedUuidExactly) {
    EXPECT_EQ(decltype(Order::order_id)::capacity(), 36u);
}
