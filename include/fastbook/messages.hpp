#pragma once
#include <cstdint>
#include <string>
#include <variant>

namespace fastbook {

// Basic types
using OrderId = uint64_t;
using Price   = int32_t;  // ticks
using Quantity = uint32_t;
using Timestamp = uint64_t;

// Sides
enum class Side : uint8_t { Bid = 0, Ask = 1 };

// Order types
enum class OrderType : uint8_t { Limit = 0, Market = 1, IOC = 2 };

// Events the engine emits
struct Ack {
    OrderId order_id;
    bool    accepted;
    bool operator==(const Ack&) const = default;
};

struct Trade {
    Price       price;        // resting order's price
    Quantity    quantity;
    OrderId     resting_id;   // maker
    OrderId     incoming_id;  // taker
    Timestamp   ts;
    bool operator==(const Trade&) const = default;
};

struct CancelAck {
    OrderId order_id;
    bool    cancelled;
    bool operator==(const CancelAck&) const = default;
};

struct Reject {
    OrderId order_id;
    uint16_t reason;  // 0=qty, 1=dup-id, 2=price-oor
    bool operator==(const Reject&) const = default;
};

using Event = std::variant<Ack, Trade, CancelAck, Reject>;

// Explicitly labeled depth level
struct DepthLevel {
    Price    price;
    Quantity quantity;
    Side     side;
    bool operator==(const DepthLevel&) const = default;
};

// Incoming messages from gateway
struct AddOrder {
    OrderId    order_id;
    Side       side;
    Price      price;
    Quantity   quantity;
    OrderType  type;  // Limit, Market, IOC
};

struct CancelOrder {
    OrderId order_id;
};

// Modify semantics: reduce qty keeps price/time; change price/raise qty = cancel+new
struct ModifyOrder {
    OrderId order_id;
    Quantity new_quantity;  // if < resting qty -> reduce; else -> cancel+re-add
    Price    new_price;     // ignored if reducing quantity
};

using Message = std::variant<AddOrder, CancelOrder, ModifyOrder>;

}  // namespace fastbook