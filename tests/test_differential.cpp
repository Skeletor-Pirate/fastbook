#include <catch2/catch_test_macros.hpp>
#include <fastbook/matching_engine.hpp>
#include <fastbook/messages.hpp>
#include <vector>
#include <algorithm>
#include <random>
#include <optional>

namespace fastbook::test {

// Naive reference matching engine built strictly on flat vectors
class NaiveEngine {
public:
    struct SimpleOrder {
        OrderId order_id;
        Side side;
        Price price;
        Quantity qty;
        uint64_t priority;
    };

    std::vector<Event> process(const Message& msg) {
        std::vector<Event> events;
        std::visit([this, &events](auto&& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, AddOrder>) {
                process_add(m, events);
            } else if constexpr (std::is_same_v<T, CancelOrder>) {
                process_cancel(m, events);
            } else if constexpr (std::is_same_v<T, ModifyOrder>) {
                process_modify(m, events);
            }
        }, msg);
        return events;
    }

    size_t size() const { return orders_.size(); }

    std::pair<std::optional<Price>, std::optional<Price>> best_bid_ask() const {
        std::optional<Price> best_bid = std::nullopt;
        std::optional<Price> best_ask = std::nullopt;

        for (const auto& ord : orders_) {
            if (ord.side == Side::Bid) {
                if (!best_bid.has_value() || ord.price > *best_bid) best_bid = ord.price;
            } else {
                if (!best_ask.has_value() || ord.price < *best_ask) best_ask = ord.price;
            }
        }
        return {best_bid, best_ask};
    }

    std::vector<DepthLevel> depth(size_t levels) const {
        std::map<Price, Quantity, std::greater<Price>> bids;
        std::map<Price, Quantity, std::less<Price>> asks;

        for (const auto& ord : orders_) {
            if (ord.side == Side::Bid) bids[ord.price] += ord.qty;
            else asks[ord.price] += ord.qty;
        }

        std::vector<DepthLevel> result;
        size_t b_count = 0;
        for (const auto& [p, q] : bids) {
            if (b_count++ >= levels) break;
            result.push_back({.price = p, .quantity = q, .side = Side::Bid});
        }
        size_t a_count = 0;
        for (const auto& [p, q] : asks) {
            if (a_count++ >= levels) break;
            result.push_back({.price = p, .quantity = q, .side = Side::Ask});
        }
        return result;
    }

    bool has_id(OrderId id) const {
        return std::any_of(orders_.begin(), orders_.end(), [id](const auto& o) { return o.order_id == id; });
    }

private:
    std::vector<SimpleOrder> orders_;
    uint64_t seq_{1};

    void sort_book() {
        std::stable_sort(orders_.begin(), orders_.end(), [](const SimpleOrder& a, const SimpleOrder& b) {
            if (a.side != b.side) return a.side < b.side;
            if (a.side == Side::Bid) {
                if (a.price != b.price) return a.price > b.price; // Descending
            } else {
                if (a.price != b.price) return a.price < b.price; // Ascending
            }
            return a.priority < b.priority; // FIFO
        });
    }

    void process_add(const AddOrder& m, std::vector<Event>& events) {
        if (m.quantity == 0) {
            events.emplace_back(Reject{m.order_id, 0});
            return;
        }
        if (has_id(m.order_id)) {
            events.emplace_back(Reject{m.order_id, 1});
            return;
        }

        events.emplace_back(Ack{m.order_id, true});

        Quantity rem = m.quantity;
        sort_book();

        for (auto it = orders_.begin(); it != orders_.end() && rem > 0;) {
            if (it->side == m.side) {
                ++it;
                continue;
            }

            bool crosses = (m.type == OrderType::Market)
                ? true
                : (m.side == Side::Bid ? m.price >= it->price : m.price <= it->price);

            if (!crosses) {
                if (m.type != OrderType::Market) break;
                ++it;
                continue;
            }

            Quantity fill = std::min(rem, it->qty);
            events.emplace_back(Trade{
                .price = it->price,
                .quantity = fill,
                .resting_id = it->order_id,
                .incoming_id = m.order_id,
                .ts = seq_++
            });

            rem -= fill;
            it->qty -= fill;

            if (it->qty == 0) {
                it = orders_.erase(it);
            } else {
                ++it;
            }
        }

        if (rem > 0 && m.type == OrderType::Limit) {
            orders_.push_back({m.order_id, m.side, m.price, rem, seq_++});
            sort_book();
        }
    }

    void process_cancel(const CancelOrder& m, std::vector<Event>& events) {
        auto it = std::find_if(orders_.begin(), orders_.end(), [id = m.order_id](const auto& o) {
            return o.order_id == id;
        });

        if (it != orders_.end()) {
            orders_.erase(it);
            events.emplace_back(CancelAck{m.order_id, true});
        } else {
            events.emplace_back(Reject{m.order_id, 1});
        }
    }

    void process_modify(const ModifyOrder& m, std::vector<Event>& events) {
        if (m.new_quantity == 0) {
            events.emplace_back(Reject{m.order_id, 0});
            return;
        }

        auto it = std::find_if(orders_.begin(), orders_.end(), [id = m.order_id](const auto& o) {
            return o.order_id == id;
        });

        if (it == orders_.end()) {
            events.emplace_back(Reject{m.order_id, 1});
            return;
        }

        if (m.new_quantity < it->qty) {
            it->qty = m.new_quantity;
            events.emplace_back(Ack{m.order_id, true});
            return;
        }

        // Cancel and re-add at back
        SimpleOrder resting = *it;
        orders_.erase(it);

        AddOrder re_add{
            .order_id = m.order_id,
            .side = resting.side,
            .price = (m.new_price != 0) ? m.new_price : resting.price,
            .quantity = m.new_quantity,
            .type = OrderType::Limit
        };

        process_add(re_add, events);
    }
};

}  // namespace fastbook::test

TEST_CASE("Randomized Differential Fuzz Test against Naive Reference Engine", "[Differential]") {
    fastbook::MatchingEngine dut;
    fastbook::test::NaiveEngine ref;

    std::mt19937_64 rng(0xCAFEBABE1337ULL);
    std::uniform_int_distribution<int> op_dist(0, 4);
    std::uniform_int_distribution<fastbook::Price> price_dist(90, 110);
    std::uniform_int_distribution<fastbook::Quantity> qty_dist(1, 25);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 2);

    fastbook::OrderId next_id = 1;
    std::vector<fastbook::OrderId> active_ids;

    auto compare_events = [](const std::vector<fastbook::Event>& dut_ev, const std::vector<fastbook::Event>& ref_ev) {
        REQUIRE(dut_ev.size() == ref_ev.size());
        for (size_t i = 0; i < dut_ev.size(); ++i) {
            if (std::holds_alternative<fastbook::Trade>(dut_ev[i])) {
                REQUIRE(std::holds_alternative<fastbook::Trade>(ref_ev[i]));
                const auto& t_dut = std::get<fastbook::Trade>(dut_ev[i]);
                const auto& t_ref = std::get<fastbook::Trade>(ref_ev[i]);
                REQUIRE(t_dut.price == t_ref.price);
                REQUIRE(t_dut.quantity == t_ref.quantity);
                REQUIRE(t_dut.resting_id == t_ref.resting_id);
                REQUIRE(t_dut.incoming_id == t_ref.incoming_id);
            } else {
                REQUIRE(dut_ev[i] == ref_ev[i]);
            }
        }
    };

    constexpr size_t NUM_ITERATIONS = 5000;

    for (size_t iter = 0; iter < NUM_ITERATIONS; ++iter) {
        int op = op_dist(rng);

        if (op == 0 || active_ids.empty()) {
            // Add Order
            fastbook::OrderId id = next_id++;
            fastbook::Side side = (side_dist(rng) == 0) ? fastbook::Side::Bid : fastbook::Side::Ask;
            fastbook::Price price = price_dist(rng);
            fastbook::Quantity qty = qty_dist(rng);
            fastbook::OrderType type = static_cast<fastbook::OrderType>(type_dist(rng));

            fastbook::AddOrder msg{id, side, price, qty, type};
            auto dut_events = dut.process(msg);
            auto ref_events = ref.process(msg);

            compare_events(dut_events, ref_events);
            active_ids.push_back(id);
        } else if (op == 1) {
            // Cancel Order
            std::uniform_int_distribution<size_t> idx_dist(0, active_ids.size() - 1);
            size_t idx = idx_dist(rng);
            fastbook::OrderId id = active_ids[idx];

            fastbook::CancelOrder msg{id};
            auto dut_events = dut.process(msg);
            auto ref_events = ref.process(msg);

            compare_events(dut_events, ref_events);
        } else if (op == 2) {
            // Modify Order
            std::uniform_int_distribution<size_t> idx_dist(0, active_ids.size() - 1);
            fastbook::OrderId id = active_ids[idx_dist(rng)];
            fastbook::Quantity new_qty = qty_dist(rng);
            fastbook::Price new_price = (rng() % 2 == 0) ? price_dist(rng) : 0;

            fastbook::ModifyOrder msg{id, new_qty, new_price};
            auto dut_events = dut.process(msg);
            auto ref_events = ref.process(msg);

            compare_events(dut_events, ref_events);
        } else {
            // Edge Cases: Duplicate ID on live order, Zero Qty Add, Zero Qty Modify
            int edge = rng() % 3;
            if (edge == 0 && !active_ids.empty()) {
                // Find an ID that is currently resting in the book
                fastbook::OrderId live_id = 0;
                for (auto candidate : active_ids) {
                    if (dut.get_order_book().has_order(candidate)) {
                        live_id = candidate;
                        break;
                    }
                }
                if (live_id != 0) {
                    fastbook::AddOrder msg{live_id, fastbook::Side::Bid, 100, 10, fastbook::OrderType::Limit};
                    auto dut_events = dut.process(msg);
                    auto ref_events = ref.process(msg);
                    compare_events(dut_events, ref_events);
                }
            } else if (edge == 1) {
                // Zero Qty Add
                fastbook::AddOrder msg{next_id++, fastbook::Side::Ask, 100, 0, fastbook::OrderType::Limit};
                auto dut_events = dut.process(msg);
                auto ref_events = ref.process(msg);
                compare_events(dut_events, ref_events);
            } else if (!active_ids.empty()) {
                // Zero Qty Modify
                fastbook::OrderId id = active_ids[rng() % active_ids.size()];
                fastbook::ModifyOrder msg{id, 0, 100};
                auto dut_events = dut.process(msg);
                auto ref_events = ref.process(msg);
                compare_events(dut_events, ref_events);
            }
        }

        // Verify Invariants after every operation
        REQUIRE(dut.size() == ref.size());
        REQUIRE(dut.get_order_book().best_bid_ask() == ref.best_bid_ask());
        REQUIRE(dut.get_order_book().depth(5) == ref.depth(5));
    }
}
