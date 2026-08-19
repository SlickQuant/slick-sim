#include <gtest/gtest.h>
#include <market_data_publisher/sequenced_message.hpp>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;
using slick::sim::md_publisher::SequencedMessage;

namespace {

json makeUpdate() {
    return {
        {"channel", "l2_data"},
        {"client_id", ""},
        {"timestamp", "2026-08-19T12:00:00.000000000Z"},
        {"events", {{
            {"type", "update"},
            {"product_id", "BTC-USD"},
            {"updates", json::array({{{"side", "bid"},
                                      {"price_level", "60455.46"},
                                      {"new_quantity", "0.00011628"}}})}
        }}}
    };
}

}  // namespace

// Each subscriber must receive the same body carrying its own sequence number, and
// the result has to be the document nlohmann would have produced for it.
TEST(SequencedMessageTest, StampsEachSubscribersOwnSequenceNumber) {
    auto message = makeUpdate();
    SequencedMessage payload(message);

    for (uint64_t seq : {0ull, 1ull, 7ull, 42ull, 1000000ull}) {
        auto expected = makeUpdate();
        expected["sequence_num"] = seq;

        auto rendered = std::string(payload.render(seq));
        EXPECT_EQ(json::parse(rendered), expected) << "sequence " << seq;
        EXPECT_EQ(rendered, expected.dump())
            << "spliced text diverged from a full dump at sequence " << seq;
    }
}

// The point of the class: the body is serialised once, so rendering repeatedly must
// keep producing correct output rather than accumulating or truncating.
TEST(SequencedMessageTest, RenderIsRepeatableAcrossManySubscribers) {
    auto message = makeUpdate();
    SequencedMessage payload(message);

    std::string first(payload.render(1));
    for (int i = 0; i < 100; ++i) {
        (void)payload.render(static_cast<uint64_t>(i));
    }
    EXPECT_EQ(std::string(payload.render(1)), first)
        << "the reused buffer did not reset between renders";
}

// Digit-count changes must not disturb the surrounding document.
TEST(SequencedMessageTest, HandlesSequenceNumbersOfDifferingWidth) {
    auto message = makeUpdate();
    SequencedMessage payload(message);

    auto narrow = json::parse(std::string(payload.render(9)));
    auto wide = json::parse(std::string(payload.render(18446744073709551615ull)));

    EXPECT_EQ(narrow["sequence_num"], 9ull);
    EXPECT_EQ(wide["sequence_num"], 18446744073709551615ull);
    EXPECT_EQ(narrow["events"], wide["events"]) << "the body changed with the number";
}

// A message with no sequence_num field is sent through untouched rather than
// corrupted by a splice with nowhere to go.
TEST(SequencedMessageTest, MessageWithoutASequenceFieldIsStillValid) {
    json message = {{"channel", "heartbeats"}, {"data", "tick"}};
    SequencedMessage payload(message);

    auto rendered = json::parse(std::string(payload.render(5)));
    EXPECT_EQ(rendered["channel"], "heartbeats");
    EXPECT_EQ(rendered["data"], "tick");
}
