#include "coinbase_exchange.hpp"
#include <exchange/exchange_registry.hpp>
#include <nlohmann/json.hpp>

using namespace slick::sim;
using namespace slick::sim::exch;

SLICK_SIM_REGISTER_VENUE("coinbase", Venue::COINBASE, CoinbaseExchange);
