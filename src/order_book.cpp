#include "fastbook/order_book.hpp"
#include <algorithm>
#include <limits>

namespace fastbook {

// Execute trades against the opposite side (helper template)
template <typename OppositeLevels>
std::vector<Trade>
OrderBook::execute_trades(Order& incoming, OppositeLevels& opposite_levels) {
    std::vector<Trade> trades;

    while (incoming.qty > 0 && !opposite_levels.empty()) {
        auto best_it = opposite_levels.begin();
        Price   best_price = best_it->first;
        auto&   level_list = best_it->second;

        // Check price crossing:
        // incoming is a bid: match if incoming.price >= best.ask
        // incoming is an ask: match if incoming.price <= best.bid
        bool prices_cross = (incoming.side == Side::Bid)
                            ? (incoming.price >= best_price)
                            : (incoming.price <= best_price);
        if (!prices_cross) {
            break;  // No more matches possible
        }

        // Match against orders at this price level (FIFO)
        while (!level_list.empty() && incoming.qty > 0) {
            Order& resting = level_list.front();
            Quantity traded = std::min(incoming.qty, resting.qty);

            // Generate trade at resting order's price (price-time priority)
            trades.push_back({
                .price       = resting.price,
                .quantity    = traded,
                .resting_id  = resting.id,
                .incoming_id = incoming.id,
                .ts          = next_ts_++
            });

            // Update quantities
            incoming.qty   -= traded;
            resting.qty    -= traded;

            // If resting order is fully filled, remove it.
            // Note: Erase from orders_by_id_ BEFORE popping from list to prevent UAF.
            if (resting.qty == 0) {
                orders_by_id_.erase(resting.id);
                level_list.pop_front();
            }
        }

        // If no more orders at this price level, remove the price level
        if (level_list.empty()) {
            opposite_levels.erase(best_it);
        }
    }

    return trades;
}

// Explicit instantiations for the member template
template std::vector<Trade> OrderBook::execute_trades<OrderBook::AskLevels>(Order& incoming, OrderBook::AskLevels& opposite_levels);
template std::vector<Trade> OrderBook::execute_trades<OrderBook::BidLevels>(Order& incoming, OrderBook::BidLevels& opposite_levels);

std::optional<std::vector<Trade>>
OrderBook::add(const AddOrder& msg) {
    // Check for duplicate id
    if (orders_by_id_.find(msg.order_id) != orders_by_id_.end()) {
        return std::nullopt;  // duplicate id
    }

    Order incoming{msg.order_id, msg.side, msg.price, msg.quantity, next_ts_++};

    // Execute trades against the opposite side
    std::vector<Trade> trades = (msg.side == Side::Bid)
        ? execute_trades(incoming, asks_)
        : execute_trades(incoming, bids_);

    if (incoming.qty > 0) {
        insert_order(incoming);
    }
    return trades;
}

bool
OrderBook::cancel(const CancelOrder& msg) {
    auto it = orders_by_id_.find(msg.order_id);
    if (it == orders_by_id_.end()) return false;
    erase_order(it->second);
    return true;
}

std::optional<std::vector<Trade>>
OrderBook::modify(const ModifyOrder& msg) {
    if (msg.new_quantity == 0) {
        return std::nullopt; // Reject 0 quantity modification
    }

    auto it = orders_by_id_.find(msg.order_id);
    if (it == orders_by_id_.end()) return std::nullopt;

    // Make an explicit copy of resting order before erase to avoid UAF
    Order resting = *it->second;

    if (msg.new_quantity < resting.qty) {
        it->second->qty = msg.new_quantity;
        return std::vector<Trade>{};
    }

    erase_order(it->second);

    AddOrder add_msg{
        .order_id = msg.order_id,
        .side     = resting.side,
        .price    = (msg.new_price != 0) ? msg.new_price : resting.price,
        .quantity = msg.new_quantity,
        .type     = OrderType::Limit
    };

    return add(add_msg);
}

std::optional<std::vector<Trade>>
OrderBook::market_order(Side side, Quantity qty, Timestamp ts, OrderId order_id) {
    if (orders_by_id_.find(order_id) != orders_by_id_.end()) {
        return std::nullopt; // duplicate id
    }

    // Market orders match anything: set price to extreme so crossing check always passes
    Price extreme = (side == Side::Bid) ? std::numeric_limits<Price>::max()
                                        : std::numeric_limits<Price>::min();
    Order incoming{order_id, side, extreme, qty, ts};

    return (side == Side::Bid) ? execute_trades(incoming, asks_)
                               : execute_trades(incoming, bids_);
}

std::optional<std::vector<Trade>>
OrderBook::ioc_order(Side side, Price limit_price, Quantity qty, Timestamp ts, OrderId order_id) {
    if (orders_by_id_.find(order_id) != orders_by_id_.end()) {
        return std::nullopt; // duplicate id
    }

    Order incoming{order_id, side, limit_price, qty, ts};

    return (side == Side::Bid) ? execute_trades(incoming, asks_)
                               : execute_trades(incoming, bids_);
}

std::pair<std::optional<Price>, std::optional<Price>>
OrderBook::best_bid_ask() const {
    std::optional<Price> best_bid = std::nullopt;
    std::optional<Price> best_ask = std::nullopt;

    if (!bids_.empty()) best_bid = bids_.begin()->first;
    if (!asks_.empty()) best_ask = asks_.begin()->first;
    return {best_bid, best_ask};
}

std::vector<DepthLevel>
OrderBook::depth(size_t levels) const {
    std::vector<DepthLevel> result;
    result.reserve(levels * 2);

    size_t bid_count = 0;
    for (const auto& [price, list] : bids_) {
        if (bid_count >= levels) break;
        Quantity total = 0;
        for (const auto& order : list) total += order.qty;
        result.push_back({.price = price, .quantity = total, .side = Side::Bid});
        ++bid_count;
    }

    size_t ask_count = 0;
    for (const auto& [price, list] : asks_) {
        if (ask_count >= levels) break;
        Quantity total = 0;
        for (const auto& order : list) total += order.qty;
        result.push_back({.price = price, .quantity = total, .side = Side::Ask});
        ++ask_count;
    }

    return result;
}

}  // namespace fastbook