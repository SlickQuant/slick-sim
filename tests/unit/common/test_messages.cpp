#include <gtest/gtest.h>
#include <common/messages.hpp>
#include <common/order.hpp>
#include <common/types.hpp>

using namespace slick::sim;

class MessagesTest : public ::testing::Test {};

TEST_F(MessagesTest, Side_ToString_AllValues) {
    EXPECT_STREQ(to_string(Side::BUY).data(), "BUY");
    EXPECT_STREQ(to_string(Side::SELL).data(), "SELL");
    EXPECT_STREQ(to_string(Side::UNKNOWN_SIDE).data(), "UNKNOWN");
}

TEST_F(MessagesTest, OrderType_ToString_AllValues) {
    EXPECT_STREQ(to_string(OrderType::MARKET).data(), "MARKET");
    EXPECT_STREQ(to_string(OrderType::LIMIT).data(), "LIMIT");
    EXPECT_STREQ(to_string(OrderType::STOP).data(), "STOP");
    EXPECT_STREQ(to_string(OrderType::STOP_LIMIT).data(), "STOP_LIMIT");
    EXPECT_STREQ(to_string(OrderType::BRACKET).data(), "BRAKET"); // Note: typo in original code
    EXPECT_STREQ(to_string(OrderType::TWAP).data(), "TWAP");
    EXPECT_STREQ(to_string(OrderType::ROLL_OPEN).data(), "ROLL_OPEN");
    EXPECT_STREQ(to_string(OrderType::ROLL_CLOSE).data(), "ROLL_CLOSE");
    EXPECT_STREQ(to_string(OrderType::LIQUIDATION).data(), "LIQUIDATION");
    EXPECT_STREQ(to_string(OrderType::SCALED).data(), "SCALED");
}

TEST_F(MessagesTest, ProductType_ToString_AllValues) {
    EXPECT_STREQ(to_string(ProductType::SPOT).data(), "SPOT");
    EXPECT_STREQ(to_string(ProductType::FUTURE).data(), "FUTURE");
    EXPECT_STREQ(to_string(ProductType::UNKNOWN).data(), "UNKNOWN_PRODUCT_TYPE");
}

TEST_F(MessagesTest, OppositeSide_Function) {
    EXPECT_EQ(opposite_side(Side::BUY), Side::SELL);
    EXPECT_EQ(opposite_side(Side::SELL), Side::BUY);
    EXPECT_EQ(opposite_side(Side::UNKNOWN_SIDE), Side::UNKNOWN_SIDE);
}

TEST_F(MessagesTest, ExecType_EnumValues) {
    // Verify enum values are correct (char-based)
    EXPECT_EQ(static_cast<char>(ExecType::NEW), '0');
    EXPECT_EQ(static_cast<char>(ExecType::PARTIAL_FILL), '1');
    EXPECT_EQ(static_cast<char>(ExecType::FILL), '2');
    EXPECT_EQ(static_cast<char>(ExecType::CANCELED), '3');
    EXPECT_EQ(static_cast<char>(ExecType::REPLACED), '4');
    EXPECT_EQ(static_cast<char>(ExecType::PENDING_NEW), '5');
    EXPECT_EQ(static_cast<char>(ExecType::PENDING_CANCEL), '6');
    EXPECT_EQ(static_cast<char>(ExecType::TRADE), '8');
}

TEST_F(MessagesTest, OrderStatus_EnumValues) {
    // Verify enum values are correct (char-based)
    EXPECT_EQ(static_cast<char>(OrderStatus::NEW), '0');
    EXPECT_EQ(static_cast<char>(OrderStatus::PARTIALLY_FILLED), '1');
    EXPECT_EQ(static_cast<char>(OrderStatus::FILLED), '2');
    EXPECT_EQ(static_cast<char>(OrderStatus::CANCELED), '4');
    EXPECT_EQ(static_cast<char>(OrderStatus::REJECTED), '8');
}

TEST_F(MessagesTest, TimeInForce_EnumValues) {
    // Verify enum values are correct (char-based)
    EXPECT_EQ(static_cast<char>(TimeInForce::DAY), '0');
    EXPECT_EQ(static_cast<char>(TimeInForce::GOOD_TILL_CANCEL), '1');
    EXPECT_EQ(static_cast<char>(TimeInForce::IMMEDIATE_OR_CANCEL), '3');
    EXPECT_EQ(static_cast<char>(TimeInForce::FILL_OR_KILL), '4');
}

TEST_F(MessagesTest, SelfMatchPreventionMode_EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(SelfMatchPreventionMode::NONE), 0);
    EXPECT_EQ(static_cast<uint8_t>(SelfMatchPreventionMode::CANCEL_RESTING), 1);
    EXPECT_EQ(static_cast<uint8_t>(SelfMatchPreventionMode::CANCEL_NEWEST), 2);
}

TEST_F(MessagesTest, OrderTypeEnum_CharacterValues) {
    EXPECT_EQ(static_cast<char>(OrderType::MARKET), '1');
    EXPECT_EQ(static_cast<char>(OrderType::LIMIT), '2');
    EXPECT_EQ(static_cast<char>(OrderType::STOP), '3');
    EXPECT_EQ(static_cast<char>(OrderType::STOP_LIMIT), '4');
}

TEST_F(MessagesTest, OrderResponse_DefaultConstruction) {
    OrderResponse response;
    // Basic smoke test - ensure we can create OrderResponse
    // Actual fields depend on the OrderResponse structure in messages.hpp
}

TEST_F(MessagesTest, AddOrderMessage_Struct) {
    // Basic smoke test for AddOrderMessage existence
    AddOrderMessage msg;
    msg.side = Side::BUY;
    msg.type = OrderType::LIMIT;
    msg.time_in_force = TimeInForce::GOOD_TILL_CANCEL;
    msg.price = to_price_t(100.0);
    msg.qty = to_qty_t(10.0);

    EXPECT_EQ(msg.side, Side::BUY);
    EXPECT_EQ(msg.type, OrderType::LIMIT);
    EXPECT_EQ(msg.time_in_force, TimeInForce::GOOD_TILL_CANCEL);
    EXPECT_EQ(msg.price, 10000000000);
    EXPECT_EQ(msg.qty, 1000000000);
}

TEST_F(MessagesTest, TradeSummary_Structure) {
    TradeSummary summary;
    summary.price = to_price_t(100.0); // 100.0 * 1e8
    summary.qty = to_qty_t(10.0);    // 10.0 * 1e8

    EXPECT_EQ(summary.price, 10000000000);
    EXPECT_EQ(summary.qty, 1000000000);
}
