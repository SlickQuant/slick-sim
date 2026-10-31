#include <gtest/gtest.h>
#include <exchange/instance_expansion.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>

using namespace slick::sim::exch;
using json = nlohmann::json;

namespace {

std::string numbered(const json &, size_t i) {
    return std::format("part #{}", i);
}

}   // namespace

TEST(InstanceExpansionTest, EntryWithoutAListIsOneInstanceNamedAfterTheVenue) {
    const auto out = expand_instances(json{{"port", 1}}, "silos", "ice", numbered);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]["name"], "ice");
    EXPECT_EQ(out[0]["port"], 1);
}

// RFC 7386: objects merge key by key, arrays are replaced.
TEST(InstanceExpansionTest, InstancesMergeOverTheSharedKeys) {
    const json config = {
        {"self_match_prevention", "none"},
        {"md_publisher", {{"interface", "127.0.0.1"}, {"ttl", 1}}},
        {"instruments", json::array({1, 2})},
        {"silos", json::array({
            {{"name", "a"}, {"md_publisher", {{"ttl", 4}}}},
            {{"instruments", json::array({3})}},
        })},
    };
    const auto out = expand_instances(config, "silos", "ice", numbered);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0]["name"], "a");
    EXPECT_EQ(out[0]["md_publisher"]["interface"], "127.0.0.1");
    EXPECT_EQ(out[0]["md_publisher"]["ttl"], 4);
    EXPECT_EQ(out[0]["instruments"], json::array({1, 2}));
    EXPECT_EQ(out[1]["name"], "part #1");
    EXPECT_EQ(out[1]["instruments"], json::array({3}));
    EXPECT_FALSE(out[1].contains("silos"));
}

TEST(InstanceExpansionTest, DisabledInstancesAreLeftOut) {
    const json config = {{"silos", json::array({{{"name", "a"}, {"enabled", false}}, {{"name", "b"}}})}};
    const auto out = expand_instances(config, "silos", "ice", numbered);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]["name"], "b");
}

TEST(InstanceExpansionTest, AListMustBeANonEmptyArray) {
    EXPECT_THROW(expand_instances(json{{"silos", json::array()}}, "silos", "ice", numbered), std::runtime_error);
    EXPECT_THROW(expand_instances(json{{"silos", json::object()}}, "silos", "ice", numbered), std::runtime_error);
}

// RFC 7386 would let a string replace the whole instance and a null blank it, either of
// which drops every shared key: a config typo has to fail here, naming the entry.
TEST(InstanceExpansionTest, AnInstanceMustBeAnObject) {
    EXPECT_THROW(expand_instances(json{{"silos", json::array({"a"})}}, "silos", "ice", numbered), std::runtime_error);
    EXPECT_THROW(expand_instances(json{{"silos", json::array({nullptr})}}, "silos", "ice", numbered),
                 std::runtime_error);
    EXPECT_THROW(expand_instances(json{{"silos", json::array({1})}}, "silos", "ice", numbered), std::runtime_error);
}

TEST(InstanceExpansionTest, ClaimRejectsAValueUsedTwice) {
    std::unordered_map<int, std::string> ports;
    claim(ports, 5100, std::string("a"), "port", "ice");
    claim(ports, 5101, std::string("b"), "port", "ice");
    EXPECT_THROW(claim(ports, 5100, std::string("b"), "port", "ice"), std::runtime_error);
    EXPECT_THROW(claim(ports, 5101, std::string("b"), "port", "ice"), std::runtime_error);
}

TEST(InstanceExpansionTest, GroupKeyIsAddressAndPort) {
    EXPECT_EQ(group_key(json{{"address", "239.1.1.1"}, {"port", 5000}}), "239.1.1.1:5000");
}
