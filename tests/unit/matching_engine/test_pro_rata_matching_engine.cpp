#include <gtest/gtest.h>
#include <matching_engine/pro_rata_matching_engine.hpp>
#include <order_book/order_book.hpp>
#include <common/order.hpp>
#include <common/messages.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include "../test_helpers.hpp"

using namespace slick::sim;
using namespace slick::sim::test;
using namespace slick::sim::engine;

namespace {

/// Records the LastInBatch flag the book stamps on each level update.
class BatchRecorder : public IOrderBookObserver {
public:
    std::vector<bool> last_in_batch;

    void onPriceLevelUpdate(const PriceLevelUpdate& update) override {
        last_in_batch.push_back((update.change_flags & slick::orderbook::ChangeFlag::LastInBatch) != 0);
    }
};

}   // namespace

class ProRataMatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        response_queue_ = makeTestQueue<OrderResponse>(4096, "test_pro_rata_responses");
        order_book_ = std::make_shared<OrderBookImpl<OrderBookType::L2>>(kSymbolId, "ES", Venue::CME);
        order_book_->addObserver(order_book_->shared_from_this());
        collector_ = std::make_unique<OrderResponseCollector>(*response_queue_);
        configure({});
    }

    void configure(const ProRataConfig& config) {
        engine_ = std::make_unique<ProRataMatchingEngine>(*response_queue_, config);
    }

    /// Rests a client order and returns its order id. The Order goes back to the
    /// pool once fully filled, so fills are read from the execution reports.
    std::string rest(Side side, price_t px, qty_t q, int client_id = 1) {
        auto* order = createTestOrder(order_book_, side, px, q, TimeInForce::GOOD_TILL_CANCEL, client_id);
        order_book_->addOrder(order, 1000, ++seq_);
        return order->order_id.c_str();
    }

    /// Rests phantom (market-data) liquidity behind whatever is already queued.
    uint64_t restPhantom(Side side, price_t px, qty_t q) {
        const auto id = utils::nextOrderId();
        order_book_->OrderBookL3::addOrder(id, to_book_side(side), px, q, 1000,
                                           OrderBook::nextOrderPriority(), ++seq_, true);
        return id;
    }

    Order* aggressor(Side side, price_t px, qty_t q, TimeInForce tif = TimeInForce::GOOD_TILL_CANCEL,
                     int client_id = 99) {
        return createTestOrder(order_book_, side, px, q, tif, client_id);
    }

    std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>> match(
        Order* order, SelfMatchPreventionMode smp = SelfMatchPreventionMode::NONE) {
        return engine_->match(order, order->price, order->quantity, *order_book_, 2000, 2000, ++seq_, smp);
    }

    /// Quantity the execution reports say an order filled.
    static qty_t filled(const std::vector<OrderResponse>& responses, const std::string& order_id) {
        qty_t total = 0;
        for (const auto& r : responses) {
            if (r.exec_type == ExecType::TRADE && order_id == r.order_id) {
                total += r.last_qty;
            }
        }
        return total;
    }

    static bool cancelled(const std::vector<OrderResponse>& responses, const std::string& order_id) {
        for (const auto& r : responses) {
            if (r.exec_type == ExecType::CANCELED && order_id == r.order_id) {
                return true;
            }
        }
        return false;
    }

    qty_t restingQty(uint64_t book_order_id) const {
        const auto* order = order_book_->OrderBookL3::findOrder(book_order_id);
        return order ? order->quantity : 0;
    }

    /// What a client order still has resting in the book, 0 once it is gone.
    qty_t bookQty(const std::string& order_id) const {
        const auto* order = order_book_->findOrderByOrderId(order_id);
        return order ? restingQty(order->id) : 0;
    }

    TestQueuePtr<OrderResponse> response_queue_;
    std::unique_ptr<ProRataMatchingEngine> engine_;
    std::shared_ptr<OrderBook> order_book_;
    std::unique_ptr<OrderResponseCollector> collector_;
    uint64_t seq_ = 0;
};

TEST_F(ProRataMatchingEngineTest, EngineType) {
    EXPECT_EQ(engine_->type(), MatchingEngine::Type::ProRata);
}

TEST_F(ProRataMatchingEngineTest, SplitsByShareOfTheLevel) {
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(30));
    const auto c = rest(Side::SELL, kPrice100, qty(60));
    auto* buy = aggressor(Side::BUY, kPrice100, qty(50));
    const std::string buy_id = buy->order_id.c_str();

    auto [reason, trades] = match(buy);
    const auto responses = collector_->collect();

    EXPECT_EQ(reason, OrdRejectReason::NONE);
    EXPECT_EQ(filled(responses, a), qty(5));
    EXPECT_EQ(filled(responses, b), qty(15));
    EXPECT_EQ(filled(responses, c), qty(30));
    EXPECT_EQ(filled(responses, buy_id), qty(50));
    EXPECT_EQ(bookQty(a), qty(5));
    EXPECT_EQ(bookQty(b), qty(15));
    EXPECT_EQ(bookQty(c), qty(30));

    // One summary for the level: the aggressor's total, then one leg per resting
    // order in queue order, with num_orders in step with Trades.
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, qty(50));
    EXPECT_EQ(trades[0].phantom_qty, 0);
    EXPECT_EQ(trades[0].aggressor_side, Side::BUY);
    ASSERT_EQ(trades[0].Trades.size(), 4u);
    EXPECT_EQ(static_cast<size_t>(trades[0].num_orders), trades[0].Trades.size());
    EXPECT_EQ(trades[0].Trades[0].qty, qty(50));
    EXPECT_EQ(trades[0].Trades[1].qty, qty(5));
    EXPECT_EQ(trades[0].Trades[2].qty, qty(15));
    EXPECT_EQ(trades[0].Trades[3].qty, qty(30));
}

// 10 split three ways is 3.33 each: 3 apiece, and the lot rounding left over
// goes to the front of the queue.
TEST_F(ProRataMatchingEngineTest, RoundingLeftoverGoesFifo) {
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(10));
    const auto c = rest(Side::SELL, kPrice100, qty(10));

    match(aggressor(Side::BUY, kPrice100, qty(10)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(4));
    EXPECT_EQ(filled(responses, b), qty(3));
    EXPECT_EQ(filled(responses, c), qty(3));
}

// A quantity that is not a whole number of lots still fills exactly: the sub-lot
// residue goes out FIFO with the rest of the leftover.
TEST_F(ProRataMatchingEngineTest, SubLotResidueGoesFifo) {
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(10));

    match(aggressor(Side::BUY, kPrice100, qty(5.5)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(3.5));
    EXPECT_EQ(filled(responses, b), qty(2));
}

// With a 2-lot minimum, a 1-lot share is dropped. The order behind gets nothing;
// the leftover goes FIFO, to the order in front.
TEST_F(ProRataMatchingEngineTest, ShareBelowMinAllocationIsDropped) {
    configure({.min_allocation = qty(2)});
    const auto big = rest(Side::SELL, kPrice100, qty(97));
    const auto small = rest(Side::SELL, kPrice100, qty(3));

    match(aggressor(Side::BUY, kPrice100, qty(50)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, big), qty(50));
    EXPECT_EQ(filled(responses, small), 0);
}

// The same small order at the front of the queue still picks up the leftover.
TEST_F(ProRataMatchingEngineTest, DroppedShareCanStillTakeTheLeftover) {
    configure({.min_allocation = qty(2)});
    const auto small = rest(Side::SELL, kPrice100, qty(3));
    const auto big = rest(Side::SELL, kPrice100, qty(97));

    match(aggressor(Side::BUY, kPrice100, qty(50)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, small), qty(2));
    EXPECT_EQ(filled(responses, big), qty(48));
}

// Shares round down to whole lots. With 5-lot rounding, 20 split three ways is
// 6.67 -> 5 each, and the 5 left over goes FIFO.
TEST_F(ProRataMatchingEngineTest, SharesRoundDownToLotSize) {
    configure({.lot_size = qty(5)});
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(10));
    const auto c = rest(Side::SELL, kPrice100, qty(10));

    match(aggressor(Side::BUY, kPrice100, qty(20)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(10));
    EXPECT_EQ(filled(responses, b), qty(5));
    EXPECT_EQ(filled(responses, c), qty(5));
}

// Lots round shares. An aggressor that takes the whole level takes every order in
// full, even one that is not a lot multiple.
TEST_F(ProRataMatchingEngineTest, WholeLevelSweepIgnoresLotSize) {
    configure({.lot_size = qty(5)});
    const auto a = rest(Side::SELL, kPrice100, qty(3));
    const auto b = rest(Side::SELL, kPrice100, qty(4));

    match(aggressor(Side::BUY, kPrice100, qty(10)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(3));
    EXPECT_EQ(filled(responses, b), qty(4));
}

// On a level of many small orders every share rounds below the minimum, and the
// leftover goes FIFO with no minimum: the fill is FIFO, one lot per order.
TEST_F(ProRataMatchingEngineTest, FragmentedLevelLeftoverIgnoresMinAllocation) {
    configure({.min_allocation = qty(2)});
    std::vector<std::string> orders;
    for (int i = 0; i < 100; ++i) {
        orders.push_back(rest(Side::SELL, kPrice100, qty(1)));
    }

    match(aggressor(Side::BUY, kPrice100, qty(50)));
    const auto responses = collector_->collect();

    for (size_t i = 0; i < orders.size(); ++i) {
        EXPECT_EQ(filled(responses, orders[i]), i < 50 ? qty(1) : 0) << "order " << i;
    }
}

// 40% of 50 goes FIFO first (10 + 10), then 30 is split over what is left
// (0/20/60): 7.5 and 22.5 round to 7 and 22, and the leftover 1 goes FIFO.
TEST_F(ProRataMatchingEngineTest, FifoPercentFillsTheQueueFirst) {
    configure({.fifo_percent = 40});
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(30));
    const auto c = rest(Side::SELL, kPrice100, qty(60));

    match(aggressor(Side::BUY, kPrice100, qty(50)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(10));
    EXPECT_EQ(filled(responses, b), qty(18));
    EXPECT_EQ(filled(responses, c), qty(22));
}

TEST_F(ProRataMatchingEngineTest, FifoPercentHundredIsFifo) {
    configure({.fifo_percent = 100});
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(30));
    const auto c = rest(Side::SELL, kPrice100, qty(60));

    match(aggressor(Side::BUY, kPrice100, qty(50)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(10));
    EXPECT_EQ(filled(responses, b), qty(30));
    EXPECT_EQ(filled(responses, c), qty(10));
}

// 20% of 50 fills the front order. Of the 40 left over 0/87/3, the 3-lot order's
// 1.33 is under the 2-lot minimum, so the 2 left after rounding go to the 87.
TEST_F(ProRataMatchingEngineTest, FifoPercentWithMinAllocation) {
    configure({.min_allocation = qty(2), .fifo_percent = 20});
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(87));
    const auto c = rest(Side::SELL, kPrice100, qty(3));

    match(aggressor(Side::BUY, kPrice100, qty(50)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(10));
    EXPECT_EQ(filled(responses, b), qty(40));
    EXPECT_EQ(filled(responses, c), 0);
}

TEST_F(ProRataMatchingEngineTest, ConstructorRejectsBadConfig) {
    EXPECT_THROW((void)ProRataMatchingEngine(*response_queue_, ProRataConfig{.lot_size = 0}), std::invalid_argument);
    EXPECT_THROW((void)ProRataMatchingEngine(*response_queue_, ProRataConfig{.min_allocation = -1}), std::invalid_argument);
    EXPECT_THROW((void)ProRataMatchingEngine(*response_queue_, ProRataConfig{.fifo_percent = 101}), std::invalid_argument);
}

// An aggressor that takes a whole level fills every order on it, then is split
// pro-rata at the next.
TEST_F(ProRataMatchingEngineTest, SweepsALevelThenSplitsTheNext) {
    const auto a = rest(Side::SELL, kPrice100, qty(5));
    const auto b = rest(Side::SELL, kPrice101, qty(10));
    const auto c = rest(Side::SELL, kPrice101, qty(10));

    auto [reason, trades] = match(aggressor(Side::BUY, kPrice101, qty(15)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(5));
    EXPECT_EQ(filled(responses, b), qty(5));
    EXPECT_EQ(filled(responses, c), qty(5));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, kPrice100);
    EXPECT_EQ(trades[0].num_orders, 2);
    EXPECT_EQ(trades[1].price, kPrice101);
    EXPECT_EQ(trades[1].num_orders, 3);
    EXPECT_EQ(trades[1].Trades.size(), 3u);
}

// A limit stops at its price: the remainder rests (it is not cancelled).
TEST_F(ProRataMatchingEngineTest, LimitStopsAtItsPrice) {
    rest(Side::SELL, kPrice100, qty(5));
    rest(Side::SELL, kPrice101, qty(10));
    auto* buy = aggressor(Side::BUY, kPrice100, qty(15));
    const std::string buy_id = buy->order_id.c_str();

    match(buy);
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, buy_id), qty(5));
    EXPECT_EQ(buy->leaves_quantity, qty(10));
    EXPECT_FALSE(cancelled(responses, buy_id));
}

TEST_F(ProRataMatchingEngineTest, MarketOrderRemainderIsCancelled) {
    rest(Side::SELL, kPrice100, qty(10));
    auto* buy = aggressor(Side::BUY, NULL_PRICE, qty(15), TimeInForce::IMMEDIATE_OR_CANCEL);
    buy->type = OrderType::MARKET;
    const std::string buy_id = buy->order_id.c_str();

    match(buy);
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, buy_id), qty(10));
    EXPECT_TRUE(cancelled(responses, buy_id));
}

TEST_F(ProRataMatchingEngineTest, FillOrKillThatCannotFillTradesNothing) {
    rest(Side::SELL, kPrice100, qty(10));
    auto* buy = aggressor(Side::BUY, kPrice100, qty(15), TimeInForce::FILL_OR_KILL);

    auto [reason, trades] = match(buy);

    EXPECT_EQ(reason, OrdRejectReason::FOK_CANNOT_FILL);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(buy->cum_quantity, 0);
}

TEST_F(ProRataMatchingEngineTest, FillOrKillThatCanFillIsSplit) {
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(10));
    auto* buy = aggressor(Side::BUY, kPrice100, qty(10), TimeInForce::FILL_OR_KILL);

    auto [reason, trades] = match(buy);
    const auto responses = collector_->collect();

    EXPECT_EQ(reason, OrdRejectReason::NONE);
    EXPECT_EQ(filled(responses, a), qty(5));
    EXPECT_EQ(filled(responses, b), qty(5));
}

// An amendment that makes a resting order marketable re-matches pro-rata.
TEST_F(ProRataMatchingEngineTest, AmendmentMatchesProRata) {
    const auto a = rest(Side::SELL, kPrice100, qty(10));
    const auto b = rest(Side::SELL, kPrice100, qty(10));
    auto* buy = aggressor(Side::BUY, kPrice99, qty(10));
    buy->status = OrderStatus::PENDING_REPLACE;
    const std::string buy_id = buy->order_id.c_str();

    engine_->match(buy, kPrice100, qty(10), *order_book_, 2000, 2000, ++seq_, SelfMatchPreventionMode::NONE);
    const auto responses = collector_->collect();

    ASSERT_FALSE(responses.empty());
    EXPECT_EQ(responses.front().exec_type, ExecType::REPLACED);
    EXPECT_EQ(filled(responses, a), qty(5));
    EXPECT_EQ(filled(responses, b), qty(5));
    EXPECT_EQ(filled(responses, buy_id), qty(10));
}

// Pro-rata may allocate to any order on a level it reaches, so an own order
// behind others on that level is a self-match, even though FIFO would never
// have reached it.
TEST_F(ProRataMatchingEngineTest, CancelRestingCancelsOwnOrderAnywhereOnTheLevel) {
    const auto other = rest(Side::SELL, kPrice100, qty(10), /*client_id=*/1);
    const auto own = rest(Side::SELL, kPrice100, qty(10), /*client_id=*/99);
    auto* buy = aggressor(Side::BUY, kPrice100, qty(5), TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/99);

    auto [reason, trades] = match(buy, SelfMatchPreventionMode::CANCEL_RESTING);
    const auto responses = collector_->collect();

    EXPECT_EQ(reason, OrdRejectReason::NONE);
    EXPECT_TRUE(cancelled(responses, own));
    EXPECT_EQ(filled(responses, own), 0);
    EXPECT_EQ(filled(responses, other), qty(5));
    EXPECT_EQ(buy->cum_quantity, qty(5));
}

// Even an own order whose share rounds to nothing is on a reached level, so it is
// cancelled too.
TEST_F(ProRataMatchingEngineTest, CancelRestingCancelsOwnOrderWithNoShare) {
    const auto other = rest(Side::SELL, kPrice100, qty(100), /*client_id=*/1);
    const auto own = rest(Side::SELL, kPrice100, qty(1), /*client_id=*/99);
    auto* buy = aggressor(Side::BUY, kPrice100, qty(5), TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/99);

    match(buy, SelfMatchPreventionMode::CANCEL_RESTING);
    const auto responses = collector_->collect();

    EXPECT_TRUE(cancelled(responses, own));
    EXPECT_EQ(filled(responses, other), qty(5));
}

TEST_F(ProRataMatchingEngineTest, CancelNewestRejectsOnOwnOrderAnywhereOnTheLevel) {
    rest(Side::SELL, kPrice100, qty(10), /*client_id=*/1);
    rest(Side::SELL, kPrice100, qty(10), /*client_id=*/99);
    auto* buy = aggressor(Side::BUY, kPrice100, qty(5), TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/99);
    buy->status = OrderStatus::PENDING_NEW;

    auto [reason, trades] = match(buy, SelfMatchPreventionMode::CANCEL_NEWEST);

    EXPECT_EQ(reason, OrdRejectReason::SMP);
    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(collector_->collect().empty()) << "a rejected order must not be acked or filled";
    EXPECT_EQ(buy->cum_quantity, 0);
}

// An own order on a level the aggressor never reaches is not a self-match.
TEST_F(ProRataMatchingEngineTest, CancelNewestIgnoresOwnOrderBeyondReach) {
    const auto other = rest(Side::SELL, kPrice100, qty(10), /*client_id=*/1);
    rest(Side::SELL, kPrice101, qty(10), /*client_id=*/99);
    auto* buy = aggressor(Side::BUY, kPrice101, qty(5), TimeInForce::GOOD_TILL_CANCEL, /*client_id=*/99);

    auto [reason, trades] = match(buy, SelfMatchPreventionMode::CANCEL_NEWEST);

    EXPECT_EQ(reason, OrdRejectReason::NONE);
    EXPECT_EQ(filled(collector_->collect(), other), qty(5));
}

// The market-data path: a replayed trade splits across phantom liquidity and the
// simulator's own order alike. Behind 8 phantom lots, a 2-lot order gets 1 of 5.
TEST_F(ProRataMatchingEngineTest, FeedTradeSplitsAcrossPhantomAndOwnOrders) {
    const auto phantom = restPhantom(Side::BUY, kPrice100, qty(8));
    const auto own = rest(Side::BUY, kPrice100, qty(2));

    qty_t q = qty(5);
    auto trades = engine_->match(Side::SELL, utils::nextOrderId(), kPrice100, q, *order_book_, 2000, ++seq_);
    const auto responses = collector_->collect();

    EXPECT_EQ(q, 0);
    EXPECT_EQ(filled(responses, own), qty(1));
    EXPECT_EQ(restingQty(phantom), qty(4));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].qty, qty(5));
    EXPECT_EQ(trades[0].phantom_qty, qty(4));
    EXPECT_EQ(static_cast<size_t>(trades[0].num_orders), trades[0].Trades.size());
}

// The feed path also takes a whole level before splitting the next one.
TEST_F(ProRataMatchingEngineTest, FeedTradeSweepsALevelThenSplitsTheNext) {
    const auto first = restPhantom(Side::SELL, kPrice100, qty(5));
    const auto own = rest(Side::SELL, kPrice101, qty(10));
    const auto behind = restPhantom(Side::SELL, kPrice101, qty(10));

    qty_t q = qty(15);
    auto trades = engine_->match(Side::BUY, utils::nextOrderId(), kPrice101, q, *order_book_, 2000, ++seq_);
    const auto responses = collector_->collect();

    EXPECT_EQ(q, 0);
    EXPECT_EQ(restingQty(first), 0);
    EXPECT_EQ(filled(responses, own), qty(5));
    EXPECT_EQ(bookQty(own), qty(5));
    EXPECT_EQ(restingQty(behind), qty(5));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[1].phantom_qty, qty(5));
}

// Only the final fill closes the batch, including when several fills land on one
// level.
TEST_F(ProRataMatchingEngineTest, FeedTradeClosesTheBatchOnTheLastFillOnly) {
    restPhantom(Side::SELL, kPrice100, qty(10));
    restPhantom(Side::SELL, kPrice100, qty(10));
    restPhantom(Side::SELL, kPrice100, qty(10));
    auto recorder = std::make_shared<BatchRecorder>();
    order_book_->addObserver(recorder);

    qty_t q = qty(10);
    engine_->match(Side::BUY, utils::nextOrderId(), kPrice100, q, *order_book_, 2000, ++seq_);

    ASSERT_EQ(q, 0);
    ASSERT_GE(recorder->last_in_batch.size(), 3u) << "one level update per fill";
    EXPECT_TRUE(recorder->last_in_batch.back());
    for (size_t i = 0; i + 1 < recorder->last_in_batch.size(); ++i) {
        EXPECT_FALSE(recorder->last_in_batch[i]) << "update " << i << " closed the batch early";
    }
}

// Quantities carry a 1e8 scale, so share * size overflows 64 bits long before
// the lot counts get large. 2e9 lots against 1e9 + 3e9 must still split exactly.
TEST_F(ProRataMatchingEngineTest, LargeQuantitiesSplitExactly) {
    const auto a = rest(Side::SELL, kPrice100, qty(1e9));
    const auto b = rest(Side::SELL, kPrice100, qty(3e9));

    match(aggressor(Side::BUY, kPrice100, qty(2e9)));
    const auto responses = collector_->collect();

    EXPECT_EQ(filled(responses, a), qty(5e8));
    EXPECT_EQ(filled(responses, b), qty(1.5e9));
}
