#pragma once
#include "messages.hpp"
#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <optional>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fastbook {

// v0 reference model: simplest correct design
// price -> list of orders (FIFO within price)
// order_id -> iterator into that list for O(1) cancel/modify

struct Order {
    OrderId   id;
    Side      side;
    Price     price;
    Quantity  qty;        // remaining quantity
    Timestamp ts;         // arrival time for priority
};

class OrderBook {
public:
    // Returns trades executed (may be empty).
    // Returns std::nullopt if the order was rejected (e.g., duplicate id).
    std::optional<std::vector<Trade>> add(const AddOrder& msg);

    // Returns true if order existed and was cancelled
    bool cancel(const CancelOrder& msg);

    // Modify semantics:
    // - reduce quantity: keep same iterator (price/time priority), no trades
    // - increase or change price: cancel + re-add as new order at back of queue (may generate trades)
    // Returns nullopt if order id is unknown; otherwise returns trades from the re-add (may be empty)
    std::optional<std::vector<Trade>> modify(const ModifyOrder& msg);

    // Process market/IOC orders (immediate execution or cancel)
    std::vector<Trade> market_order(Side side, Quantity qty, Timestamp ts);
    std::vector<Trade> ioc_order(Side side, Price limit_price, Quantity qty, Timestamp ts, OrderId order_id);

    // Query functions
    std::pair<Price, Price> best_bid_ask() const;
    std::vector<std::pair<Price, Quantity>> depth(size_t levels) const;

    // Accessors for testing/invariants
    size_t size() const { return orders_by_id_.size(); }
    bool empty() const { return orders_by_id_.empty(); }

private:
    // Helper: insert resting order into book, return iterator
    typename std::list<Order>::iterator insert_order(const Order& order);

    // Helper: remove order from book given iterator
    void erase_order(typename std::list<Order>::iterator it);

    // Execute trades against opposite side (member template)
    template <typename OppositeLevels>
    std::vector<Trade> execute_trades(Order& incoming, OppositeLevels& opposite_levels);

    // Price levels: map(price -> list of orders at that price)
    // Bid side: descending prices (highest first) -> use greater<Price>
    // Ask side: ascending prices (lowest first) -> use less<Price>
    using BidLevels = std::map<Price, std::list<Order>, std::greater<Price>>;
    using AskLevels = std::map<Price, std::list<Order>, std::less<Price>>;

    BidLevels bids_;
    AskLevels asks_;

    // id -> iterator into the list in bids_ or asks_
    std::unordered_map<OrderId, typename std::list<Order>::iterator> orders_by_id_;

    // Timestamp generator for deterministic ordering
    Timestamp next_ts_{1};
};

// Inline implementations for header-only v0 (simplest)
inline typename std::list<Order>::iterator
OrderBook::insert_order(const Order& order) {
    auto& level_list = (order.side == Side::Bid) ? bids_[order.price] : asks_[order.price];
    level_list.push_back(order);
    auto it = std::prev(level_list.end());
    orders_by_id_[order.id] = it;
    return it;
}

inline void
OrderBook::erase_order(typename std::list<Order>::iterator it) {
    orders_by_id_.erase(it->id);
    auto& level_list = (it->side == Side::Bid) ? bids_[it->price] : asks_[it->price];
    level_list.erase(it);
    if (level_list.empty()) {
        if (it->side == Side::Bid) bids_.erase(it->price);
        else asks_.erase(it->price);
    }
}

}  // namespace fastbook