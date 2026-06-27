#include "market_data_publisher.hpp"
#include "coinbase_publisher.hpp"
#include "hyperliquid_publisher.hpp"

using namespace slick::sim;
using namespace slick::sim::md_publisher;

MarketDataPublisher::MarketDataPublisher(Venue venue, const nlohmann::json &config, slick::queue<Request> &request_queue, slick::queue<uint8_t> &market_data_queue)
    : request_queue_(request_queue)
    , market_data_queue_(market_data_queue)
{
    // for (auto it = config.begin(); it != config.end(); ++it) {
    //     auto venue = to_venue(it.key());
    //     switch(venue) {
    //         case Venue::COINBASE:
    //             publishers_[Venue::COINBASE] = std::make_unique<CoinbasePublisher>(it.value(), request_queue,  market_data_queue);
    //             break;
    //         case Venue::HYPERLIQUID:
    //             publishers_[Venue::HYPERLIQUID] = std::make_unique<HyperliquidPublisher>(it.value(), request_queue, market_data_queue);
    //             break;
    //     }
    // }
}

// void MarketDataPublisher::start() {
//     for (auto &publisher : publishers_) {
//         if (publisher) {
//             publisher->start();
//         }
//     }
// }

// void MarketDataPublisher::stop() {
//     for (auto &publisher : publishers_) {
//         if (publisher) {
//             publisher->stop();
//         }
//     }
// }
