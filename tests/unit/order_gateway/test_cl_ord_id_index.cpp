#include <gtest/gtest.h>
#include <order_gateway/cl_ord_id_index.hpp>
#include <utils/fixed_string.hpp>
#include <string_view>

using slick::sim::order_gateway::ClOrdIdIndex;
using Id = slick::sim::utils::fixed_string<21>;
using Index = ClOrdIdIndex<Id, slick::sim::utils::StringViewHash, slick::sim::utils::StringViewEq>;

// Two requests in flight can't share a ClOrdID, though neither is an order yet.
TEST(ClOrdIdIndexTest, AClaimHoldsTheIdUntilTheOrderIsDone) {
    Index ids;
    ASSERT_TRUE(ids.claim(Id("A")));
    EXPECT_FALSE(ids.claim(Id("A")));
    EXPECT_EQ(ids.find(std::string_view("A")), 0u);   // not an order yet

    ids.assign(Id("A"), 7);
    ids.release(std::string_view("A"));   // answered: the order holds it now
    EXPECT_EQ(ids.find(std::string_view("A")), 7u);
    EXPECT_FALSE(ids.claim(Id("A")));

    ids.forget(std::string_view("A"), 7);
    EXPECT_TRUE(ids.claim(Id("A")));
}

TEST(ClOrdIdIndexTest, ARejectedRequestFreesItsId) {
    Index ids;
    ASSERT_TRUE(ids.claim(Id("A")));
    ids.release(std::string_view("A"));
    EXPECT_TRUE(ids.claim(Id("A")));
}

// A replace may keep its order's ClOrdID, but not take another order's.
TEST(ClOrdIdIndexTest, AReplaceRenamesOnlyOntoAFreeId) {
    ClOrdIdIndex<int64_t> ids;
    ids.assign(42, 7);
    ids.assign(44, 8);
    EXPECT_FALSE(ids.claim(44, 7));
    ASSERT_TRUE(ids.claim(43, 7));
    EXPECT_FALSE(ids.claim(43, 8));
    EXPECT_FALSE(ids.claim(43));

    ids.rename(42, 43, 7);
    ids.release(43);
    EXPECT_EQ(ids.find(43), 7u);
    EXPECT_EQ(ids.find(42), 0u);
    EXPECT_TRUE(ids.claim(42));
}

// Replaces 42 -> 43 and 42 -> 42 in flight together: the first renaming the order
// must not free 42 while the second still needs it.
TEST(ClOrdIdIndexTest, AnIdAPendingReplaceKeepsStaysTaken) {
    ClOrdIdIndex<int64_t> ids;
    ids.assign(42, 7);
    ASSERT_TRUE(ids.claim(43, 7));
    ASSERT_TRUE(ids.claim(42, 7));

    ids.rename(42, 43, 7);
    ids.release(43);
    EXPECT_EQ(ids.find(42), 0u);
    EXPECT_FALSE(ids.claim(42));   // a new order may not take it

    ids.rename(43, 42, 7);
    ids.release(42);
    EXPECT_EQ(ids.find(42), 7u);
    EXPECT_EQ(ids.find(43), 0u);
    EXPECT_TRUE(ids.claim(43));
}

// CME clients often keep one ClOrdID through every modify, sending the next
// before the last is answered.
TEST(ClOrdIdIndexTest, ReplacesOfOneOrderShareItsId) {
    ClOrdIdIndex<int64_t> ids;
    ids.assign(42, 7);
    ASSERT_TRUE(ids.claim(42, 7));
    ASSERT_TRUE(ids.claim(42, 7));
    ids.rename(42, 42, 7);
    ids.release(42);
    ids.release(42);
    EXPECT_EQ(ids.find(42), 7u);

    ids.forget(42, 7);
    EXPECT_TRUE(ids.claim(42));
}

// An id is taken while either an order holds it or a request naming it is in flight;
// anything else is forgotten, so a session's index does not grow without bound.
TEST(ClOrdIdIndexTest, SizeCountsTheIdsThatAreTaken) {
    Index ids;
    EXPECT_TRUE(ids.empty());

    ASSERT_TRUE(ids.claim(Id("A")));
    EXPECT_EQ(ids.size(), 1u);

    ids.assign(Id("A"), 7);
    ids.release(Id("A"));
    EXPECT_EQ(ids.size(), 1u);   // the order holds it now
    EXPECT_EQ(ids.find(std::string_view("A")), 7u);

    ids.forget(std::string_view("A"), 7);
    EXPECT_EQ(ids.size(), 0u);
    EXPECT_TRUE(ids.empty());
}

// The mutating calls take the key type's own lookup form too, so a caller holding a
// string_view does not have to build an id it only wants to name.
TEST(ClOrdIdIndexTest, EveryCallAcceptsTheLookupForm) {
    Index ids;
    ids.assign(std::string_view("B"), 9);              // order 9 holds B
    ASSERT_TRUE(ids.claim(std::string_view("C"), 9));  // a replace names C
    ids.rename(std::string_view("B"), std::string_view("C"), 9);
    ids.release(std::string_view("C"));                // the replace was answered
    EXPECT_EQ(ids.find(std::string_view("B")), 0u);
    EXPECT_EQ(ids.find(std::string_view("C")), 9u);

    ids.forget(std::string_view("C"), 9);
    EXPECT_TRUE(ids.empty());
}
