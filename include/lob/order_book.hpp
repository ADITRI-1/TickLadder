#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <vector>

#include "lob/object_pool.hpp"
#include "lob/order_index.hpp"
#include "lob/types.hpp"

namespace lob {

// Result of an add or cancel. We return a code instead of throwing an
// exception: exceptions are slow and unpredictable on the hot path.
enum class Status : std::uint8_t {
  Ok,
  DuplicateId,      // an order with this id is already in the book
  InvalidQuantity,  // quantity was 0
  InvalidPrice,     // price was 0 or negative
  UnknownId,        // cancel of an id that is not in the book
};

// One row of the depth view: "at this price, this much is waiting".
struct LevelInfo {
  Price price{};
  std::uint64_t total_qty{};
  std::uint32_t count{};

  bool operator==(const LevelInfo&) const = default;
};

// ---------------------------------------------------------------------------
// The limit order book and matching engine.
//
// Every incoming order first trades against the opposite side while prices
// cross (best price first, then oldest order first = price-time priority).
// Whatever is left of a LIMIT order then rests in the book; whatever is left
// of a MARKET order is dropped. Every execution is appended to trades().
// ---------------------------------------------------------------------------
class OrderBook {
 public:
  // `expected_orders`: how many orders are usually resting at once. Memory
  // for that many is reserved up front; more still works, just slower.
  explicit OrderBook(std::size_t expected_orders = 1 << 16);

  // Limit order: "buy/sell up to qty at price or better".
  Status add(OrderId id, Side side, Price price, Quantity qty);

  // Market order: "buy/sell up to qty right now at any price". Never rests.
  Status market(OrderId id, Side side, Quantity qty);

  Status cancel(OrderId id);

  std::optional<Price> best_bid() const;  // highest buy price, if any
  std::optional<Price> best_ask() const;  // lowest sell price, if any

  // Total quantity waiting at one price on one side (0 if none).
  std::uint64_t volume_at(Side side, Price price) const;

  // The best `max_levels` price levels of one side, best price first.
  std::vector<LevelInfo> depth(Side side, std::size_t max_levels) const;

  const Order* find(OrderId id) const;  // nullptr if not in the book
  std::size_t order_count() const { return orders_.size(); }
  std::size_t level_count(Side side) const;

  // Receipt book: every trade since the last clear_trades(), oldest first.
  const std::vector<Trade>& trades() const { return trades_; }
  void clear_trades() { trades_.clear(); }

  friend std::ostream& operator<<(std::ostream& os, const OrderBook& book);

 private:
  // Trade an incoming order against the opposite side for as long as prices
  // cross `limit`. Returns the quantity that is still unfilled.
  Quantity match(OrderId id, Side side, Price limit, Quantity qty);

  // The matching loop, written once for both sides. `book_side` is the
  // OPPOSITE side of the incoming order (a buyer trades against asks).
  template <typename Map>
  Quantity match_against(Map& book_side, OrderId id, Side side, Price limit,
                         Quantity qty);

  // Remove a finished order from the id index and return its memory.
  void forget(Order* o);

  // Put a (remaining) limit order into its price level queue.
  void rest(OrderId id, Side side, Price price, Quantity qty);

  // Bids: highest price first, so begin() is the best bid.
  // Asks: lowest price first, so begin() is the best ask.
  using BidMap = std::map<Price, PriceLevel, std::greater<>>;
  using AskMap = std::map<Price, PriceLevel, std::less<>>;

  BidMap bids_;
  AskMap asks_;

  // Every resting order lives in the pool; the index finds it by id in O(1).
  ObjectPool<Order> pool_;
  OrderIndex orders_;

  std::vector<Trade> trades_;
};

std::ostream& operator<<(std::ostream& os, Status s);
std::ostream& operator<<(std::ostream& os, const LevelInfo& l);

}  // namespace lob
