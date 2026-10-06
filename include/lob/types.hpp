#pragma once

#include <cassert>
#include <cstdint>
#include <iosfwd>

namespace lob {

// ---------------------------------------------------------------------------
// Basic units. Prices are integer ticks (1 tick = 1 paisa, so Rs 100.05 is
// 10005). Never use floating point for money: 0.1 + 0.2 != 0.3.
// ---------------------------------------------------------------------------
using OrderId  = std::uint64_t;
using Price    = std::int64_t;
using Quantity = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };

struct PriceLevel;  // forward declaration: Order points to its level

// ---------------------------------------------------------------------------
// One resting order. It is also a node in its price level's queue
// ("intrusive" list): prev/next live inside the order itself, so joining or
// leaving a queue never allocates memory.
// ---------------------------------------------------------------------------
struct Order {
  OrderId id{};
  Price price{};
  Quantity qty{};            // quantity still open (shrinks on partial fills)
  Side side{Side::Buy};

  Order* prev{nullptr};
  Order* next{nullptr};
  PriceLevel* level{nullptr};  // which queue we are in, for O(1) cancel
};

// The whole order should fit in one 64-byte CPU cache line.
static_assert(sizeof(Order) <= 64, "Order must fit in one cache line");

// ---------------------------------------------------------------------------
// All orders waiting at one price, oldest first (FIFO = time priority).
// ---------------------------------------------------------------------------
struct PriceLevel {
  Price price{};
  std::uint64_t total_qty{};  // sum of qty of all orders here
  std::uint32_t count{};      // number of orders here
  Order* head{nullptr};       // oldest order: matched first
  Order* tail{nullptr};       // newest order: new arrivals join here

  bool empty() const noexcept { return head == nullptr; }

  // Join the back of the queue. O(1).
  void push_back(Order* o) noexcept {
    assert(o->level == nullptr && "order is already in a queue");
    o->prev = tail;
    o->next = nullptr;
    o->level = this;
    if (tail != nullptr) {
      tail->next = o;
    } else {
      head = o;  // queue was empty
    }
    tail = o;
    total_qty += o->qty;
    ++count;
  }

  // Unhook an order from anywhere in the queue. O(1): we already hold a
  // pointer to it, and it knows its neighbours.
  void remove(Order* o) noexcept {
    assert(o->level == this && "order is not in this queue");
    if (o->prev != nullptr) {
      o->prev->next = o->next;
    } else {
      head = o->next;  // o was the head
    }
    if (o->next != nullptr) {
      o->next->prev = o->prev;
    } else {
      tail = o->prev;  // o was the tail
    }
    total_qty -= o->qty;
    --count;
    o->prev = o->next = nullptr;  // no dangling links
    o->level = nullptr;
  }

  // Partial fill: the order trades part of its quantity but stays in the
  // queue, KEEPING its place in line. O(1).
  void reduce(Order* o, Quantity filled) noexcept {
    assert(o->level == this && "order is not in this queue");
    assert(filled < o->qty && "use remove() for a full fill");
    o->qty -= filled;
    total_qty -= filled;
  }
};

// ---------------------------------------------------------------------------
// One execution between a buyer and a seller.
// ---------------------------------------------------------------------------
struct Trade {
  OrderId buy_id{};
  OrderId sell_id{};
  Price price{};
  Quantity qty{};

  // Two trades are equal if every field is equal (the compiler writes it).
  bool operator==(const Trade&) const = default;
};

// Printing, for debugging and tests. Defined in types.cpp so the heavy
// <ostream> header stays out of this hot-path header.
std::ostream& operator<<(std::ostream& os, Side s);
std::ostream& operator<<(std::ostream& os, const Order& o);
std::ostream& operator<<(std::ostream& os, const PriceLevel& l);
std::ostream& operator<<(std::ostream& os, const Trade& t);

}  // namespace lob
