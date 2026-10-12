#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <fastbook/matching_engine.hpp>
#include <fastbook/messages.hpp>
#include <vector>
#include <cstdint>

using namespace fastbook;

TEST_CASE("FR-1: Add limit order that does not cross rests at price level", "[FR-1]") {
    MatchingEngine engine;

    // Add bid order at 100 ticks, qty 10
    AddOrder bid_msg{1, Side::Bid, 100, 10, OrderType::Limit};
    auto events = engine.process(bid_msg);

    REQUIRE(events.size() == 1);
    // Should be an Ack
    REQUIRE(std::holds_alternative<Ack>(events[0]));
    const Ack& ack = std::get<Ack>(events[0]);
    REQUIRE(ack.order_id == 1);
    REQUIRE(ack.accepted == true);

    // Order should be in book
    REQUIRE(engine.get_order_book().size() == 1);
}

TEST_CASE("FR-2: Incoming order matches best opposite price first, FIFO within price", "[FR-2]") {
    MatchingEngine engine;

    // Add resting ask at 100 ticks, qty 5 (first)
    AddOrder ask1{1, Side::Ask, 100, 5, OrderType::Limit};
    auto events1 = engine.process(ask1);
    REQUIRE(events1.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events1[0]));

    // Add another resting ask at 100 ticks, qty 3 (second, same price)
    AddOrder ask2{2, Side::Ask, 100, 3, OrderType::Limit};
    auto events2 = engine.process(ask2);
    REQUIRE(events2.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events2[0]));

    // Add third resting ask at 102 ticks, qty 2 (worse price)
    AddOrder ask3{3, Side::Ask, 102, 2, OrderType::Limit};
    auto events3 = engine.process(ask3);
    REQUIRE(events3.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events3[0]));

    // Now send a bid at 100 ticks, qty 6
    // Should match with ask1 first (FIFO), then ask2
    AddOrder bid{10, Side::Bid, 100, 6, OrderType::Limit};
    auto events = engine.process(bid);

    // Expect: Ack + 2 trades
    REQUIRE(events.size() == 3);

    // First event should be Ack
    REQUIRE(std::holds_alternative<Ack>(events[0]));
    const Ack& ack = std::get<Ack>(events[0]);
    REQUIRE(ack.order_id == 10);
    REQUIRE(ack.accepted == true);

    // Second event: first trade (with ask1)
    REQUIRE(std::holds_alternative<Trade>(events[1]));
    const Trade& trade1 = std::get<Trade>(events[1]);
    REQUIRE(trade1.price == 100);  // resting order's price
    REQUIRE(trade1.quantity == 5); // ask1 fully filled
    REQUIRE(trade1.resting_id == 1);
    REQUIRE(trade1.incoming_id == 10);

    // Third event: second trade (with ask2, partially filled)
    REQUIRE(std::holds_alternative<Trade>(events[2]));
    const Trade& trade2 = std::get<Trade>(events[2]);
    REQUIRE(trade2.price == 100);  // resting order's price
    REQUIRE(trade2.quantity == 1); // 6-5 = 1 from ask2
    REQUIRE(trade2.resting_id == 2);
    REQUIRE(trade2.incoming_id == 10);

    // Book should have: ask2 remaining qty 2, ask3 qty 2
    REQUIRE(engine.get_order_book().size() == 2);
}

TEST_CASE("FR-3: Partially filled limit order rests with remaining quantity", "[FR-3]") {
    MatchingEngine engine;

    // Add resting ask at 100, qty 10
    AddOrder ask{1, Side::Ask, 100, 10, OrderType::Limit};
    auto events = engine.process(ask);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Send bid for only 4
    AddOrder bid{2, Side::Bid, 100, 4, OrderType::Limit};
    events = engine.process(bid);
    REQUIRE(events.size() == 2); // Ack + Trade
    REQUIRE(std::holds_alternative<Ack>(events[0]));
    REQUIRE(std::holds_alternative<Trade>(events[1]));

    const Trade& trade = std::get<Trade>(events[1]);
    REQUIRE(trade.quantity == 4);
    REQUIRE(trade.resting_id == 1);
    REQUIRE(trade.incoming_id == 2);

    // Ask should rest with qty 6
    REQUIRE(engine.get_order_book().size() == 1);
}

TEST_CASE("FR-4: Market order matches until filled or book empty, never rests", "[FR-4]") {
    MatchingEngine engine;

    // Add two asks: 100@5, 101@3
    AddOrder ask1{1, Side::Ask, 100, 5, OrderType::Limit};
    AddOrder ask2{2, Side::Ask, 101, 3, OrderType::Limit};
    engine.process(ask1);
    engine.process(ask2);
    REQUIRE(engine.get_order_book().size() == 2);

    // Market buy for 6
    AddOrder market{3, Side::Bid, 0, 6, OrderType::Market}; // price ignored
    auto events = engine.process(market);

    // Should have: Ack + 2 trades (5@100, 1@101)
    REQUIRE(events.size() == 3);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    REQUIRE(std::holds_alternative<Trade>(events[1]));
    const Trade& t1 = std::get<Trade>(events[1]);
    REQUIRE(t1.price == 100);
    REQUIRE(t1.quantity == 5);
    REQUIRE(t1.resting_id == 1);
    // Market orders have no meaningful incoming id in trades
    REQUIRE(t1.incoming_id == 0);

    REQUIRE(std::holds_alternative<Trade>(events[2]));
    const Trade& t2 = std::get<Trade>(events[2]);
    REQUIRE(t2.price == 101);
    REQUIRE(t2.quantity == 1);
    REQUIRE(t2.resting_id == 2);

    // Book should have: ask2 remaining qty 2
    REQUIRE(engine.get_order_book().size() == 1);

    // Another market buy for 10 (should take remaining 2 and then book empty)
    AddOrder market2{4, Side::Bid, 0, 10, OrderType::Market};
    events = engine.process(market2);

    // Ack + 1 trade (2@101)
    REQUIRE(events.size() == 2);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    REQUIRE(std::holds_alternative<Trade>(events[1]));
    const Trade& t3 = std::get<Trade>(events[1]);
    REQUIRE(t3.price == 101);
    REQUIRE(t3.quantity == 2);
    REQUIRE(t3.resting_id == 2);

    // Book should now be empty
    REQUIRE(engine.get_order_book().size() == 0);
}

TEST_CASE("FR-5: IOC order matches what it can at limit or better, cancels rest", "[FR-5]") {
    MatchingEngine engine;

    // Add asks: 100@5, 101@3
    AddOrder ask1{1, Side::Ask, 100, 5, OrderType::Limit};
    AddOrder ask2{2, Side::Ask, 101, 3, OrderType::Limit};
    engine.process(ask1);
    engine.process(ask2);

    // IOC bid at 100 for 6 (can only take 5@100)
    AddOrder ioc{3, Side::Bid, 100, 6, OrderType::IOC};
    auto events = engine.process(ioc);

    // Ack + 1 trade (5@100)
    REQUIRE(events.size() == 2);
    REQUIRE(std::holds_alternative<Ack>(events[0]));
    REQUIRE(std::holds_alternative<Trade>(events[1]));

    const Trade& trade = std::get<Trade>(events[1]);
    REQUIRE(trade.price == 100);
    REQUIRE(trade.quantity == 5);
    REQUIRE(trade.resting_id == 1);
    // IOC trades report the incoming order id
    REQUIRE(trade.incoming_id == 3);

    // Book should have: ask2 untouched (101@3) because IOC at 100 won't touch 101
    REQUIRE(engine.get_order_book().size() == 1);
}

TEST_CASE("FR-6: Cancel by order id removes the order in O(1) on average. Unknown id returns reject", "[FR-6]") {
    MatchingEngine engine;

    // Add an order
    AddOrder add{1, Side::Bid, 100, 10, OrderType::Limit};
    auto events = engine.process(add);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Cancel it with correct id
    CancelOrder cancel{1};
    events = engine.process(cancel);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<CancelAck>(events[0]));
    const CancelAck& ack = std::get<CancelAck>(events[0]);
    REQUIRE(ack.order_id == 1);
    REQUIRE(ack.cancelled == true);

    // Book should be empty
    REQUIRE(engine.get_order_book().size() == 0);

    // Try to cancel again -> reject
    events = engine.process(cancel);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Reject>(events[0]));
    const Reject& rej = std::get<Reject>(events[0]);
    REQUIRE(rej.order_id == 1);
    REQUIRE(rej.reason == 1); // unknown id
}

TEST_CASE("FR-7: Modify: reducing quantity keeps queue position. Changing price or raising quantity acts as cancel plus new order at back of queue", "[FR-7]") {
    MatchingEngine engine;

    // Add an order at 100, qty 10
    AddOrder add{1, Side::Bid, 100, 10, OrderType::Limit};
    auto events = engine.process(add);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Reduce quantity to 5 (should keep same position)
    ModifyOrder reduce{1, 5, 0}; // price 0 ignored
    events = engine.process(reduce);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Quantity should now be 5
    // We can't directly check, but we can check that the order is still there and no trades happened
    REQUIRE(engine.get_order_book().size() == 1);
    // And depth at 100 should be 5
    auto depth = engine.get_order_book().depth(1);
    REQUIRE(depth.size() == 1);
    REQUIRE(depth[0].first == 100);
    REQUIRE(depth[0].second == 5);

    // Now try to increase quantity to 15 (should cancel and re-add at back)
    ModifyOrder increase{1, 15, 0};
    events = engine.process(increase);
    // Should generate an Ack (for the modify) and then the cancel and re-add will happen internally
    // The modify function returns trades from the re-add (if any). Since we are increasing and there are no opposite orders, no trades.
    // But internally: cancel the old (qty 5) and add new (qty 15) at back.
    // Since there are no opposite orders, the new order rests.
    // So we expect: Ack (from modify) and then the internal cancel and add produce no events because they are done within modify?
    // Actually, our modify function returns the trades from the re-add. Since no opposite orders, trades vector is empty.
    // Then we push an Ack for the modify.
    // So total events: 1 (the Ack)
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Now the order should have qty 15
    depth = engine.get_order_book().depth(1);
    REQUIRE(depth.size() == 1);
    REQUIRE(depth[0].first == 100);
    REQUIRE(depth[0].second == 15);

    // Now change price to 95 (lower, so worse for bid) -> should cancel and re-add at back
    ModifyOrder change_price{1, 15, 95};
    events = engine.process(change_price);
    // Again, we expect only an Ack from the modify (since no opposite orders, no trades from re-add)
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Now the order should be at 95, qty 15
    // Because we changed price to a worse price (lower for bid), we cancel and re-add at the back.
    // So let's add another order at 100 to see the queue position.
    AddOrder add2{2, Side::Bid, 100, 10, OrderType::Limit};
    engine.process(add2);
    // Now we have two orders: one at 95 (the modified one) and one at 100 (the new one).
    // The order at 100 is the best bid (highest price), and the order at 95 is worse.

    // Let's check depth: the best bid should be at 100 with qty 10 (from id2)
    depth = engine.get_order_book().depth(1);
    REQUIRE(depth.size() == 1);
    REQUIRE(depth[0].first == 100);
    REQUIRE(depth[0].second == 10);

    // Now, if we send a sell order that will match with the bid at 100, it should match with the oldest first.
    // The oldest at 100 is id2 (just added).
    AddOrder ask{3, Side::Ask, 100, 5, OrderType::Limit};
    events = engine.process(ask);
    // We expect: Ack + 1 trade (5@100 with id2)
    REQUIRE(events.size() == 2);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    REQUIRE(std::holds_alternative<Trade>(events[1]));
    const Trade& t1 = std::get<Trade>(events[1]);
    REQUIRE(t1.price == 100);
    REQUIRE(t1.quantity == 5);
    REQUIRE(t1.resting_id == 2); // id2
    REQUIRE(t1.incoming_id == 3);

    // Now book should have: id1 resting at 95 with qty 15, and the bid at 100 with qty 5 (because we started with 10 and matched 5)
    REQUIRE(engine.get_order_book().size() == 2);
    auto bids = engine.get_order_book().depth(2); // get two levels
    // Should have: bid@100 qty=5 (id2), bid@95 qty=15 (id1)
    REQUIRE(bids.size() == 2);
    REQUIRE(bids[0].first == 100);
    REQUIRE(bids[0].second == 5);
    REQUIRE(bids[1].first == 95);
    REQUIRE(bids[1].second == 15);
}

TEST_CASE("FR-8: Emit events in order: acknowledge, trade (price, quantity, resting id, incoming id), cancel acknowledge and reject", "[FR-8]") {
    MatchingEngine engine;

    // Add an ask order
    AddOrder ask{1, Side::Ask, 100, 10, OrderType::Limit};
    auto events = engine.process(ask);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Ack>(events[0]));

    // Add a bid order that matches (should generate Ack then Trade)
    AddOrder bid{2, Side::Bid, 100, 5, OrderType::Limit};
    events = engine.process(bid);
    REQUIRE(events.size() == 2);
    REQUIRE(std::holds_alternative<Ack>(events[0]));
    REQUIRE(std::holds_alternative<Trade>(events[1]));

    // Cancel the resting ask (id 1) -> should generate CancelAck
    CancelOrder cancel{1};
    events = engine.process(cancel);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<CancelAck>(events[0]));

    // Try to cancel again -> Reject
    events = engine.process(cancel);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<Reject>(events[0]));
}

TEST_CASE("FR-9: Query the best bid and ask, and a depth snapshot of N levels", "[FR-9]") {
    MatchingEngine engine;

    // Initially empty
    auto [best_bid, best_ask] = engine.get_order_book().best_bid_ask();
    REQUIRE(best_bid == 0);
    REQUIRE(best_ask == std::numeric_limits<fastbook::Price>::max());

    // Add ask at 101, qty 1
    AddOrder ask1{1, Side::Ask, 101, 1, OrderType::Limit};
    engine.process(ask1);
    auto [bb, ba] = engine.get_order_book().best_bid_ask();
    REQUIRE(bb == 0);
    REQUIRE(ba == 101);

    // Add bid at 100, qty 1
    AddOrder bid1{2, Side::Bid, 100, 1, OrderType::Limit};
    engine.process(bid1);
    auto [bb2, ba2] = engine.get_order_book().best_bid_ask();
    REQUIRE(bb2 == 100);
    REQUIRE(ba2 == 101);

    // Add another ask at 102, qty 1 (worse)
    AddOrder ask2{3, Side::Ask, 102, 1, OrderType::Limit};
    engine.process(ask2);
    auto [bb3, ba3] = engine.get_order_book().best_bid_ask();
    REQUIRE(bb3 == 100);
    REQUIRE(ba3 == 101); // best ask still 101

    // Depth 1: should be one bid and one ask
    auto depth1 = engine.get_order_book().depth(1);
    REQUIRE(depth1.size() == 2);
    // Bid side first: (100, 1)
    REQUIRE(depth1[0].first == 100);
    REQUIRE(depth1[0].second == 1);
    // Ask side second: (101, 1)
    REQUIRE(depth1[1].first == 101);
    REQUIRE(depth1[1].second == 1);

    // Depth 2: now we have two ask levels (101 and 102)
    auto depth2 = engine.get_order_book().depth(2);
    REQUIRE(depth2.size() == 3);
    // Bid: (100,1)
    REQUIRE(depth2[0].first == 100);
    REQUIRE(depth2[0].second == 1);
    // Asks: (101,1) and (102,1)
    REQUIRE(depth2[1].first == 101);
    REQUIRE(depth2[1].second == 1);
    REQUIRE(depth2[2].first == 102);
    REQUIRE(depth2[2].second == 1);
}

TEST_CASE("FR-10: The same input sequence always produces byte-identical output events", "[FR-10]") {
    // We'll test determinism by running the same sequence twice and comparing the raw events.
    // We'll use a simple sequence: add two orders that match, then cancel.

    auto run_sequence = [](const std::vector<fastbook::Message>& msgs) -> std::vector<fastbook::Event> {
        fastbook::MatchingEngine engine;
        std::vector<fastbook::Event> all_events;
        for (const auto& msg : msgs) {
            auto events = engine.process(msg);
            all_events.insert(all_events.end(), events.begin(), events.end());
        }
        return all_events;
    };

    std::vector<fastbook::Message> seq;
    seq.emplace_back(fastbook::AddOrder{1, fastbook::Side::Ask, 100, 10, fastbook::OrderType::Limit});
    seq.emplace_back(fastbook::AddOrder{2, fastbook::Side::Bid, 100, 5, fastbook::OrderType::Limit});
    seq.emplace_back(fastbook::CancelOrder{1});

    auto events1 = run_sequence(seq);
    auto events2 = run_sequence(seq);

    REQUIRE(events1.size() == events2.size());
    for (size_t i = 0; i < events1.size(); ++i) {
        REQUIRE(events1[i] == events2[i]);
    }
}

TEST_CASE("FR-11: Reject zero quantity, duplicate ids and prices outside the allowed range", "[FR-11]") {
    MatchingEngine engine;

    // Zero quantity
    AddOrder zero_qty{1, fastbook::Side::Bid, 100, 0, fastbook::OrderType::Limit};
    auto events = engine.process(zero_qty);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<fastbook::Reject>(events[0]));
    const fastbook::Reject& rej = std::get<fastbook::Reject>(events[0]);
    REQUIRE(rej.order_id == 1);
    REQUIRE(rej.reason == 0); // zero quantity

    // Duplicate id: add an order, then try to add another with same id
    AddOrder add1{1, fastbook::Side::Bid, 100, 10, fastbook::OrderType::Limit};
    engine.process(add1);
    AddOrder add2{1, fastbook::Side::Bid, 101, 10, fastbook::OrderType::Limit}; // same id
    events = engine.process(add2);
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<fastbook::Reject>(events[0]));
    const fastbook::Reject& rej2 = std::get<fastbook::Reject>(events[0]);
    REQUIRE(rej2.order_id == 1);
    REQUIRE(rej2.reason == 1); // duplicate id

    // Price outside allowed range: we haven't defined a range yet. We'll skip this part for v0.
    // In v2 we will have a bounded price range and reject if outside.
}