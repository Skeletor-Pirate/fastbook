#include <fastbook/matching_engine.hpp>
#include <fastbook/messages.hpp>
#include <iostream>
#include <vector>

int main() {
    std::cout << "FastBook v0 - Reference Model\n";
    std::cout << "=============================\n\n";

    fastbook::MatchingEngine engine;

    // Simple demo: add two orders that match
    fastbook::AddOrder ask{1, fastbook::Side::Ask, 100, 10, fastbook::OrderType::Limit};
    fastbook::AddOrder bid{2, fastbook::Side::Bid, 100, 5, fastbook::OrderType::Limit};

    std::cout << "Adding ask: id=1, side=ask, price=100, qty=10\n";
    auto events1 = engine.process(ask);
    std::cout << "  -> Generated " << events1.size() << " event(s)\n";

    std::cout << "Adding bid: id=2, side=bid, price=100, qty=5\n";
    auto events2 = engine.process(bid);
    std::cout << "  -> Generated " << events2.size() << " event(s)\n";

    // Process and display events
    for (const auto& ev : events1) {
        std::visit([](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, fastbook::Ack>) {
                std::cout << "    Ack: id=" << arg.order_id << ", accepted=" << std::boolalpha << arg.accepted << '\n';
            } else if constexpr (std::is_same_v<T, fastbook::Trade>) {
                std::cout << "    Trade: price=" << arg.price << ", qty=" << arg.quantity
                          << ", resting_id=" << arg.resting_id << ", incoming_id=" << arg.incoming_id << '\n';
            } else if constexpr (std::is_same_v<T, fastbook::CancelAck>) {
                std::cout << "    CancelAck: id=" << arg.order_id << ", cancelled=" << std::boolalpha << arg.cancelled << '\n';
            } else if constexpr (std::is_same_v<T, fastbook::Reject>) {
                std::cout << "    Reject: id=" << arg.order_id << ", reason=" << arg.reason << '\n';
            }
        }, ev);
    }

    for (const auto& ev : events2) {
        std::visit([](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, fastbook::Ack>) {
                std::cout << "    Ack: id=" << arg.order_id << ", accepted=" << std::boolalpha << arg.accepted << '\n';
            } else if constexpr (std::is_same_v<T, fastbook::Trade>) {
                std::cout << "    Trade: price=" << arg.price << ", qty=" << arg.quantity
                          << ", resting_id=" << arg.resting_id << ", incoming_id=" << arg.incoming_id << '\n';
            } else if constexpr (std::is_same_v<T, fastbook::CancelAck>) {
                std::cout << "    CancelAck: id=" << arg.order_id << ", cancelled=" << std::boolalpha << arg.cancelled << '\n';
            } else if constexpr (std::is_same_v<T, fastbook::Reject>) {
                std::cout << "    Reject: id=" << arg.order_id << ", reason=" << arg.reason << '\n';
            }
        }, ev);
    }

    // Show book state
    auto [best_bid, best_ask] = engine.get_order_book().best_bid_ask();
    std::cout << "\nBook state:\n";
    std::cout << "  Best bid: " << (best_bid == 0 ? "none" : std::to_string(best_bid)) << "\n";
    std::cout << "  Best ask: " << (best_ask == std::numeric_limits<fastbook::Price>::max() ? "none" : std::to_string(best_ask)) << "\n";
    std::cout << "  Order count: " << engine.get_order_book().size() << "\n";

    auto depth = engine.get_order_book().depth(5);
    std::cout << "  Depth (up to 5 levels):\n";
    for (const auto& [price, qty] : depth) {
        std::cout << "    Price " << price << ": qty " << qty << "\n";
    }

    std::cout << "\nDemo complete.\n";
    return 0;
}