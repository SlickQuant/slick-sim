#include <gtest/gtest.h>
#include <common/symbol_manager.hpp>
#include <common/types.hpp>
#include <string>
#include <vector>

using namespace slick::sim;

namespace {

// The manager is a process-wide singleton that other suites also populate, so
// every name here is prefixed to keep this file's instruments to itself.
std::string name(std::string_view suffix) {
    return "SYMMGR-" + std::string(suffix);
}

}  // namespace

// A name only identifies an instrument within a venue. Keyed on the name alone,
// the second create found the name present and its Symbol became unreachable:
// both lookups returned the first venue's instrument.
TEST(SymbolManagerTest, SameNameOnTwoVenuesAreDistinctInstruments) {
    auto &mgr = SymbolManager::instance();
    const auto shared_name = name("DUAL-LISTED");

    auto *coinbase = mgr.createSymbol(shared_name, Venue::COINBASE);
    auto *hyperliquid = mgr.createSymbol(shared_name, Venue::HYPERLIQUID);

    ASSERT_NE(coinbase, nullptr);
    ASSERT_NE(hyperliquid, nullptr);
    EXPECT_NE(coinbase, hyperliquid) << "one Symbol was shared across two venues";

    EXPECT_EQ(mgr.getSymbol(shared_name, Venue::COINBASE), coinbase);
    EXPECT_EQ(mgr.getSymbol(shared_name, Venue::HYPERLIQUID), hyperliquid)
        << "the second venue's instrument was unreachable by name";

    EXPECT_EQ(coinbase->venue_, Venue::COINBASE);
    EXPECT_EQ(hyperliquid->venue_, Venue::HYPERLIQUID);
    EXPECT_NE(coinbase->id_, hyperliquid->id_);
}

// Creating the same instrument twice must not strand a second Symbol that
// nothing can look up.
TEST(SymbolManagerTest, RecreatingTheSameInstrumentReturnsTheExistingSymbol) {
    auto &mgr = SymbolManager::instance();
    const auto reused = name("REUSED");

    const size_t before = mgr.size();
    auto *first = mgr.createSymbol(reused, Venue::COINBASE);
    auto *second = mgr.createSymbol(reused, Venue::COINBASE);

    EXPECT_EQ(first, second);
    EXPECT_EQ(mgr.size(), before + 1) << "a duplicate create allocated a second Symbol";
}

// Every Symbol* the manager hands out is retained - by its own lookup map, by the
// exchanges, and by the matching path. Backed by a std::vector reserved for 512,
// creating the 513th moved all of them and left every previously issued pointer
// dangling. Crossing that boundary here must leave the earlier ones intact.
TEST(SymbolManagerTest, SymbolPointersSurviveGrowthPastTheReservedCapacity) {
    auto &mgr = SymbolManager::instance();

    // Comfortably past the old 512 reserve even with symbols other suites created.
    constexpr int kCount = 600;

    std::vector<exch::Symbol *> issued;
    std::vector<std::string> names;
    std::vector<symid_t> ids;
    issued.reserve(kCount);
    names.reserve(kCount);
    ids.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        names.push_back(name("GROW-" + std::to_string(i)));
        auto *symbol = mgr.createSymbol(names.back(), Venue::COINBASE);
        ASSERT_NE(symbol, nullptr);
        issued.push_back(symbol);
        ids.push_back(symbol->id_);
    }

    for (int i = 0; i < kCount; ++i) {
        // Address stability: what the manager returns now must be what it returned
        // at creation, by either route.
        EXPECT_EQ(mgr.getSymbol(names[i], Venue::COINBASE), issued[i])
            << "name lookup moved for symbol " << i;
        EXPECT_EQ(mgr.getSymbol(ids[i]), issued[i])
            << "id lookup moved for symbol " << i;

        // And the object at that address must still be the one we created, not
        // whatever now occupies the freed storage.
        EXPECT_EQ(issued[i]->id_, ids[i]);
        EXPECT_EQ(issued[i]->symbol_.view(), names[i]);
        EXPECT_EQ(issued[i]->venue_, Venue::COINBASE);
    }
}
