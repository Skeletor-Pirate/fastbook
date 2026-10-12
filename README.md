# FastBook ⚡

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-orange.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CI](https://github.com/Skeletor-Pirate/fastbook/actions/workflows/ci.yml/badge.svg)](https://github.com/Skeletor-Pirate/fastbook/actions)
[![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20UBSan-brightgreen.svg)]()
[![Differential Tests](https://img.shields.io/badge/Fuzzing-Differential%20Verified-blueviolet.svg)]()

**FastBook** is a clean, deterministic C++20 Limit Order Book (LOB) and Matching Engine **reference implementation**. It is designed as an executable specification for algorithmic trading systems, quantitative research, and exchange simulations requiring strict price-time priority (FIFO) execution and formal correctness.

---

## 🏛️ Design & Reference Architecture

FastBook v0 implements the canonical reference model:
- **Price Levels**: `std::map<Price, std::list<Order>>` providing naturally ordered depth (descending for bids, ascending for asks).
- **FIFO Queues**: Doubly linked lists (`std::list<Order>`) guaranteeing strict queue-order matching per price level.
- **Order Indexing**: `std::unordered_map<OrderId, iterator>` for order lookups.
- **Time Complexities**:
  - Insert / Aggressive Cross: $O(\log(\text{levels}) + \text{fills})$
  - Order Cancel: $O(1)$ iterator lookup + $O(\log(\text{levels}))$ tree cleanup when an entire price level is emptied.
  - Modify Downsize: $O(1)$ in-place quantity reduction preserving queue position.
  - Modify Upsize / Price Change: $O(\log(\text{levels}))$ cancel + re-insert at the back of the queue.

*Note: As a v0 reference implementation, FastBook prioritizes formal correctness, invariant preservation, and differential equivalence over raw micro-optimizations. Contiguous pool allocators and lock-free ring buffers are reserved for subsequent milestone models.*

---

## ✨ Features

- **Price-Time Priority (FIFO)**: Strict queue-order execution with deterministic trade sequence numbering.
- **Order Types**:
  - `Limit`: Resting orders placed in depth queues; matches crossable opposing liquidity immediately.
  - `Market`: Immediate liquidity extraction across price levels; trades carry taker `incoming_id`.
  - `Immediate-Or-Cancel (IOC)`: Executes immediately at limit or better; unexecuted quantity is cancelled immediately.
- **Priority-Preserving Order Modification**:
  - Quantity downsizing retains existing price-time queue priority.
  - Quantity increases or price modifications cancel and re-enter at the back of the queue.
  - Modification to quantity zero is safely rejected without book corruption.
- **Deterministic Event-Driven Stream**: Strongly typed events (`Ack`, `Trade`, `CancelAck`, `Reject`) with byte-for-byte reproducible orderings.
- **Robust Market Inspection**:
  - Best Bid / Offer (BBO) queries returning `std::optional<Price>` (eliminates magic zero-sentinel ambiguities).
  - Level 2 (L2) book depth snapshots explicitly labeled with `Side` (`DepthLevel`).
- **Zero Heavy Dependencies**: Pure C++20 standard library core. Only Catch2 is utilized for the test harnesses.

---

## 📋 Functional Requirements & Verification

| Spec ID | Requirement | Status |
| :--- | :--- | :--- |
| **FR-1** | Add limit order that does not cross rests at price level | ✅ Verified |
| **FR-2** | Incoming order matches best opposite price first (FIFO within price) | ✅ Verified |
| **FR-3** | Partially filled limit order rests with remaining quantity | ✅ Verified |
| **FR-4** | Market order matches until filled or book empty, never rests, reports taker ID | ✅ Verified |
| **FR-5** | Immediate-Or-Cancel (IOC) fills what it can at limit or better, cancels rest | ✅ Verified |
| **FR-6** | Cancel by order ID removes resting order; unknown ID rejected | ✅ Verified |
| **FR-7** | Modify: downsize preserves priority; upsize / price change cancels and re-adds at back | ✅ Verified |
| **FR-8** | Event emission ordering: `Ack` before `Trade`, `CancelAck`, `Reject` | ✅ Verified |
| **FR-9** | Query BBO (`std::optional<Price>`) and depth snapshot of $N$ levels with explicit `Side` | ✅ Verified |
| **FR-10**| Determinism: identical input sequences produce byte-identical output events | ✅ Verified |
| **FR-11**| Reject zero quantity (add/modify) and duplicate IDs (limit/market/IOC) | ✅ Verified *(Price-range check planned for bounded v2)* |

---

## 🧪 Testing & Differential Fuzzing

FastBook is verified with multiple layers of automated testing under **AddressSanitizer (ASan)** and **UndefinedBehaviorSanitizer (UBSan)**:

1. **Unit & Functional Suites**: Handcrafted test suites verifying all functional requirements (FR-1 through FR-11) along with explicit regression probes for use-after-free conditions on full fills, price-level erasures, and order modifications.
2. **Randomized Differential Fuzzing (`test_differential.cpp`)**: A dual-engine property test feeding tens of thousands of randomized messages to both FastBook and a second, naive vector-based reference engine (`NaiveEngine`), verifying identical event streams, order counts, BBOs, and depth states at every transition.

```bash
# Run test suite with ASan/UBSan enabled
ctest --test-dir build --output-on-failure
```

```text
Randomness seeded to: 4076003426
===============================================================================
All tests passed (27357 assertions in 14 test cases)
```

---

## 🚀 Quick Start

### Prerequisites
- Modern C++20 compiler (`clang++` $\ge$ 16, `g++` $\ge$ 12, or `MSVC` $\ge$ 19.34)
- CMake $\ge$ 3.16

### Build & Run Tests

```bash
# Clone the repository
git clone https://github.com/Skeletor-Pirate/fastbook.git
cd fastbook

# Configure with sanitizers enabled (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_SAN=ON

# Build
cmake --build build --config Debug

# Run all unit and differential tests
ctest --test-dir build --output-on-failure -C Debug

# Run the demo binary
./build/fastbook_demo
```

---

## 💻 Example Usage

```cpp
#include <fastbook/matching_engine.hpp>
#include <fastbook/messages.hpp>
#include <iostream>

int main() {
    fastbook::MatchingEngine engine;

    // Resting ask: 10 units at price 100
    fastbook::AddOrder ask{
        .order_id = 1,
        .side = fastbook::Side::Ask,
        .price = 100,
        .quantity = 10,
        .type = fastbook::OrderType::Limit
    };
    engine.process(ask);

    // Incoming aggressive bid: 5 units at price 100
    fastbook::AddOrder bid{
        .order_id = 2,
        .side = fastbook::Side::Bid,
        .price = 100,
        .quantity = 5,
        .type = fastbook::OrderType::Limit
    };
    auto events = engine.process(bid);

    // Inspect BBO with std::optional
    auto [best_bid, best_ask] = engine.get_order_book().best_bid_ask();
    if (best_ask.has_value()) {
        std::cout << "Best Ask: " << *best_ask << "\n";
    }

    // Inspect L2 depth with explicit sides
    auto depth = engine.get_order_book().depth(5);
    for (const auto& level : depth) {
        std::cout << (level.side == fastbook::Side::Bid ? "BID" : "ASK")
                  << " " << level.price << " : " << level.quantity << "\n";
    }

    return 0;
}
```

---

## 🗺️ Roadmap

- [x] **v0 Reference Implementation**: Canonical, verified C++20 behavioral reference engine with differential testing and memory sanitizer verification.
- [ ] **v1 Contiguous Memory Model**: Flat array and intrusive node pool allocations to minimize pointer indirection and cache misses.
- [ ] **v2 Zero-Allocation Engine**: Preallocated circular ring buffers with fixed price bounds and sub-microsecond latency benchmarks.
- [ ] **v3 Market Data Gateway**: Binary ITCH/OUCH market data encoders and decoders.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE) - see the LICENSE file for details.
