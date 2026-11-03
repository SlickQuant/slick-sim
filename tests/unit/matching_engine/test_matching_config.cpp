#include <gtest/gtest.h>
#include <matching_engine/matching_config.hpp>
#include <matching_engine/pro_rata_matching_engine.hpp>
#include <nlohmann/json.hpp>
#include <format>
#include <stdexcept>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::engine;

TEST(MatchingConfigTest, DefaultsToFifo) {
    EXPECT_EQ(MatchingConfig::from_json(nlohmann::json::object()).type, MatchingEngine::Type::FIFO);
    EXPECT_EQ(MatchingConfig::from_json({{"matching_algorithm", "fifo"}}).type, MatchingEngine::Type::FIFO);
}

TEST(MatchingConfigTest, ReadsProRataInVenueUnits) {
    const auto config = MatchingConfig::from_json({
        {"matching_algorithm", "pro_rata"},
        {"pro_rata", {{"lot_size", 1}, {"min_allocation", 2}, {"fifo_percent", 40}}},
    });
    EXPECT_EQ(config.type, MatchingEngine::Type::ProRata);
    EXPECT_EQ(config.pro_rata.lot_size, qty(1));
    EXPECT_EQ(config.pro_rata.min_allocation, qty(2));
    EXPECT_EQ(config.pro_rata.fifo_percent, 40);
}

TEST(MatchingConfigTest, ProRataDefaults) {
    const auto config = MatchingConfig::from_json({{"matching_algorithm", "pro_rata"}});
    EXPECT_EQ(config.pro_rata, ProRataConfig{});
}

TEST(MatchingConfigTest, RejectsBadValues) {
    EXPECT_THROW(MatchingConfig::from_json({{"matching_algorithm", "prorata"}}), std::invalid_argument);
    const auto with = [](nlohmann::json params) {
        return nlohmann::json{{"matching_algorithm", "pro_rata"}, {"pro_rata", std::move(params)}};
    };
    EXPECT_THROW(MatchingConfig::from_json(with({{"lot_size", 0}})), std::invalid_argument);
    EXPECT_THROW(MatchingConfig::from_json(with({{"min_allocation", -1}})), std::invalid_argument);
    EXPECT_THROW(MatchingConfig::from_json(with({{"fifo_percent", 101}})), std::invalid_argument);
    EXPECT_THROW(MatchingConfig::from_json(with({{"fifo_percent", -1}})), std::invalid_argument);
}

// Each value is checked as JSON before narrowing. Before this, get<int>() took
// 100.5 as 100 and 2^32 as 0, and an oversized lot_size overflowed qty_t.
TEST(MatchingConfigTest, RejectsValuesThatWouldNarrow) {
    const auto parse = [](const char *params) {
        return MatchingConfig::from_json(nlohmann::json::parse(
            std::format(R"({{"matching_algorithm": "pro_rata", "pro_rata": {}}})", params)));
    };
    EXPECT_EQ(parse(R"({"fifo_percent": 100})").pro_rata.fifo_percent, 100);
    EXPECT_EQ(parse(R"({"lot_size": 0.5})").pro_rata.lot_size, to_qty_t(0.5));
    for (const char *bad : {R"({"fifo_percent": 100.5})", R"({"fifo_percent": 4294967296})",
                            R"({"fifo_percent": 18446744073709551615})", R"({"fifo_percent": true})",
                            R"({"fifo_percent": "40"})", R"({"lot_size": 1e12})", R"({"min_allocation": 1e300})",
                            R"({"lot_size": null})", "[]"}) {
        EXPECT_THROW(parse(bad), std::invalid_argument) << bad;
    }
    // An overflowing cast comes out negative on x64, so validate() would reject it
    // anyway, but saturates on ARM. Check the range test is what fired.
    try {
        parse(R"({"lot_size": 1e12})");
        ADD_FAILURE() << "oversized lot_size accepted";
    } catch (const std::invalid_argument &e) {
        EXPECT_NE(std::string_view(e.what()).find("within"), std::string_view::npos) << e.what();
    }
}

// A wrong JSON type is reported like any other bad value, not as a json exception.
TEST(MatchingConfigTest, RejectsWrongTypesAsInvalidArgument) {
    EXPECT_THROW(MatchingConfig::from_json({{"matching_algorithm", 1}}), std::invalid_argument);
    EXPECT_THROW(MatchingConfig::from_json({{"matching_algorithm", "pro_rata"}, {"pro_rata", 5}}), std::invalid_argument);
    EXPECT_THROW(MatchingConfig::from_json({{"matching_algorithm", "pro_rata"}, {"pro_rata", {{"lot_size", "one"}}}}),
                 std::invalid_argument);
}

TEST(MatchingConfigTest, CreatesTheConfiguredEngine) {
    auto queue = makeTestQueue<OrderResponse>(64, "test_matching_config_responses");

    MatchingConfig fifo;
    EXPECT_EQ(createMatchingEngine(fifo, *queue)->type(), MatchingEngine::Type::FIFO);

    MatchingConfig pro_rata;
    pro_rata.type = MatchingEngine::Type::ProRata;
    pro_rata.pro_rata.min_allocation = qty(2);
    auto engine = createMatchingEngine(pro_rata, *queue);
    ASSERT_EQ(engine->type(), MatchingEngine::Type::ProRata);
    EXPECT_EQ(static_cast<ProRataMatchingEngine&>(*engine).config(), pro_rata.pro_rata);
}
