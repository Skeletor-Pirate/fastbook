#pragma once
#include "messages.hpp"
#include "order_book.hpp"
#include <vector>
#include <variant>

namespace fastbook {

class MatchingEngine {
public:
    MatchingEngine() = default;

    // Process one incoming message, return vector of events (may be empty)
    std::vector<Event> process(const Message& msg);

    // Accessors for testing/queries
    const OrderBook& get_order_book() const { return book_; }
    size_t             size() const       { return book_.size(); }

private:
    OrderBook book_;
};

// Visitor that dispatches on the Message variant.
// Takes the book directly to avoid needing friend access.
class MessageVisitor {
public:
    std::vector<Event>& events;
    OrderBook&          book;

    void operator()(const AddOrder& m) const {
        // Reject invalid orders up front
        if (m.quantity == 0) {
            events.emplace_back(Reject{m.order_id, 0});  // zero quantity
            return;
        }
        // TODO: price range checks (will implement in v2 with bounded array)

        std::vector<Trade> trades;
        switch (m.type) {
            case OrderType::Limit: {
                auto opt = book.add(m);
                if (!opt.has_value()) {
                    events.emplace_back(Reject{m.order_id, 1});  // duplicate id
                    return;
                }
                trades = std::move(*opt);
                break;
            }
            case OrderType::Market:
                trades = book.market_order(m.side, m.quantity, 0);
                break;
            case OrderType::IOC:
                trades = book.ioc_order(m.side, m.price, m.quantity, 0, m.order_id);
                break;
        }

        // Per FR-8: Ack first, then trades
        events.emplace_back(Ack{m.order_id, true});
        events.insert(events.end(), trades.begin(), trades.end());
    }

    void operator()(const CancelOrder& m) const {
        if (book.cancel(m)) {
            events.emplace_back(CancelAck{m.order_id, true});
        } else {
            events.emplace_back(Reject{m.order_id, 1});  // unknown id
        }
    }

    void operator()(const ModifyOrder& m) const {
        auto result = book.modify(m);
        if (!result.has_value()) {
            events.emplace_back(Reject{m.order_id, 1});  // unknown id
            return;
        }
        // Per FR-8: Ack first, then trades
        events.emplace_back(Ack{m.order_id, true});
        events.insert(events.end(), result->begin(), result->end());
    }
};

inline std::vector<Event>
MatchingEngine::process(const Message& msg) {
    std::vector<Event> events;
    MessageVisitor visitor{events, book_};
    std::visit(visitor, msg);
    return events;
}

}  // namespace fastbook