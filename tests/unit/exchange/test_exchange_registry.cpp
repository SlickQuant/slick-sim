#include <gtest/gtest.h>
#include <exchange/exchange_registry.hpp>

using namespace slick::sim;
using namespace slick::sim::exch;

// These guard the one thing about the venue split that would otherwise fail
// silently. Each adapter registers itself from a namespace-scope initialiser, and
// nothing in the core ever names that symbol - so if cmake/SlickSimVenue.cmake
// ever stops declaring venue targets as OBJECT libraries, the linker drops the
// registration as unreferenced and every configured exchange is rejected at
// startup with no build error anywhere. This turns that into a red test.
//
// SLICK_SIM_HAS_<VENUE> comes from the venue target's INTERFACE compile
// definitions, so each assertion applies only when that venue is actually built.

TEST(ExchangeRegistryTest, EnabledVenuesAreRegistered) {
#ifdef SLICK_SIM_HAS_COINBASE
    EXPECT_NE(ExchangeRegistry::instance().find("coinbase"), nullptr)
        << "Coinbase was built but did not register - is its target still an OBJECT library?";
#endif
#ifdef SLICK_SIM_HAS_HYPERLIQUID
    EXPECT_NE(ExchangeRegistry::instance().find("hyperliquid"), nullptr)
        << "Hyperliquid was built but did not register - is its target still an OBJECT library?";
#endif
}

TEST(ExchangeRegistryTest, LookupIsCaseInsensitive) {
#ifdef SLICK_SIM_HAS_COINBASE
    EXPECT_EQ(ExchangeRegistry::instance().find("COINBASE"),
              ExchangeRegistry::instance().find("coinbase"));
    EXPECT_EQ(ExchangeRegistry::instance().find("CoinBase"),
              ExchangeRegistry::instance().find("coinbase"));
#else
    GTEST_SKIP() << "no case-insensitivity to check without a registered venue";
#endif
}

TEST(ExchangeRegistryTest, UnknownKeyHasNoFactory) {
    EXPECT_EQ(ExchangeRegistry::instance().find("not-a-venue"), nullptr);
    EXPECT_EQ(ExchangeRegistry::instance().find(""), nullptr);
}

// A partial match must not resolve - "coin" is not Coinbase.
TEST(ExchangeRegistryTest, PartialKeyHasNoFactory) {
#ifdef SLICK_SIM_HAS_COINBASE
    EXPECT_EQ(ExchangeRegistry::instance().find("coin"), nullptr);
    EXPECT_EQ(ExchangeRegistry::instance().find("coinbase2"), nullptr);
#else
    GTEST_SKIP() << "no registered venue to take a prefix of";
#endif
}

TEST(ExchangeRegistryTest, KeysMatchWhatIsBuiltIn) {
    const auto keys = ExchangeRegistry::instance().keys();
    for (const auto &key : keys) {
        EXPECT_NE(ExchangeRegistry::instance().find(key), nullptr)
            << "keys() reported '" << key << "' but find() cannot resolve it";
    }
#ifdef SLICK_SIM_HAS_COINBASE
    EXPECT_NE(std::find(keys.begin(), keys.end(), "coinbase"), keys.end());
#endif
#ifdef SLICK_SIM_HAS_HYPERLIQUID
    EXPECT_NE(std::find(keys.begin(), keys.end(), "hyperliquid"), keys.end());
#endif
}
