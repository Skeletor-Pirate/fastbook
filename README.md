# FastBook ⚡

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-orange.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CI](https://github.com/Skeletor-Pirate/fastbook/actions/workflows/ci.yml/badge.svg)](https://github.com/Skeletor-Pirate/fastbook/actions)
[![Tests Passing](https://img.shields.io/badge/Tests-147%20Passed-brightgreen.svg)]()

**FastBook** is an ultra-fast, deterministic, high-performance Limit Order Book (LOB) and Matching Engine written in modern C++20. Designed for algorithmic trading systems, quantitative research, and exchange simulations requiring strict price-time priority (FIFO) execution.

---

## ✨ Features

- **Price-Time Priority (FIFO)**: Strict price-time order matching semantics with deterministic execution ordering.
- **Multiple Order Types**:
  - `Limit`: Resting orders placed in depth queues; matches crossable opposing liquidity immediately.
  - `Market`: Immediate liquidity extraction against opposing book levels.
  - `Immediate-Or-Cancel (IOC)`: Fills available liquidity at or better than limit price; remaining quantity is immediately cancelled.
- **$O(1)$ Order Cancellation**: Constant-time cancel operations indexed by unique `OrderId`.
- **Priority-Preserving Order Modification**:
  - Quantity downsizing retains existing price-time queue priority.
  - Quantity increases or price changes cancel and re-enter at the back of the queue.
- **Deterministic Event-Driven Engine**: Emits strongly typed events (`Ack`, `Trade`, `CancelAck`, `Reject`) with byte-for-byte reproducible streams.
- **Market Data Inspection**: Real-time Level 2 (L2) book depth snapshots and Best Bid/Offer (BBO) queries.
- **Zero Heavy Dependencies**: Pure C++20 standard library baseline with automatic Catch2 test integration via CMake FetchContent.

---

## 🏛️ Architecture & Functional Specification

FastBook adheres to the rigorous functional requirements expected of high-frequency trading (HFT) reference engines:

| Spec ID | Requirement | Implementation Status |
| :--- | :--- | :--- |
| **FR-1** | Add limit order that does not cross rests at price level | ✅ Verified |
| **FR-2** | Incoming order matches best opposite price first (FIFO) | ✅ Verified |
| **FR-3** | Trade price equals resting order price (price-time maker price) | ✅ Verified |
| **FR-4** | Aggressor with residual quantity rests in book at limit price | ✅ Verified |
| **FR-5** | Market order matches across price levels | ✅ Verified |
| **FR-6** | Immediate-Or-Cancel (IOC) fills what it can and cancels rest | ✅ Verified |
| **FR-7** | Cancel by order ID removes resting order in $O(1)$ time | ✅ Verified |
| **FR-8** | Deterministic event stream: `Ack` before `Trade` | ✅ Verified |
| **FR-9** | Reject duplicate order ID on entry | ✅ Verified |
| **FR-10**| Quantity downsize maintains queue position; upsize loses priority | ✅ Verified |
| **FR-11**| Depth snapshot aggregates total quantity per price level | ✅ Verified |

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

# Configure build with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build executables
cmake --build build --config Release

# Run automated tests (Catch2)
ctest --test-dir build --output-on-failure

# Run the demo engine
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

    // Place a resting ask: 10 units at price 100
    fastbook::AddOrder ask{
        .order_id = 1,
        .side = fastbook::Side::Ask,
        .price = 100,
        .quantity = 10,
        .type = fastbook::OrderType::Limit
    };
    auto events1 = engine.process(ask);

    // Place an incoming aggressive bid: 5 units at price 100
    fastbook::AddOrder bid{
        .order_id = 2,
        .side = fastbook::Side::Bid,
        .price = 100,
        .quantity = 5,
        .type = fastbook::OrderType::Limit
    };
    auto events2 = engine.process(bid);

    // Inspect BBO
    auto [best_bid, best_ask] = engine.get_order_book().best_bid_ask();
    std::cout << "Best Ask: " << best_ask << " | Resting Qty: 5\n";

    return 0;
}
```

---

## 🧪 Test Suite & Verification

The test suite validates every functional requirement with **147 assertions across 11 Catch2 test cases**:

```text
Randomness seeded to: 166735577
===============================================================================
All tests passed (147 assertions in 11 test cases)
```

To run individual tests with verbose outputs:
```bash
./build/fastbook_tests "[FR-2]" -v high
```

---

## 🗺️ Roadmap

- [x] **v0 Reference Model**: Idiomatic C++20 reference implementation for behavioral correctness and differential testing.
- [ ] **v1 Flat Array / Intrusive Node**: Contiguous pool allocators to minimize cache misses and branch mispredictions.
- [ ] **v2 Zero-Allocation Core**: Lock-free single-producer single-consumer (SPSC) ring buffer architecture with sub-microsecond tick-to-trade latency.
- [ ] **v3 Market Data Feed**: Direct ITCH/OUCH and SBE binary feed handlers.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE) - see the LICENSE file for details.
