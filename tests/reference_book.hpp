#pragma once

// A deliberately SLOW but OBVIOUSLY CORRECT order book, used only in tests.
//
// It keeps every order in one plain vector and searches the whole vector
// every time. No maps, no linked lists, no pointers: there is almost nothing
// that can go wrong. The fast OrderBook must always agree with it.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

#include "lob/order_book.hpp"

namespace lob::test {

class ReferenceBook {
 public:
  Status add(OrderId id, Side side, Price price, Quantity qty) {
    if (qty == 0) return Status::InvalidQuantity;
    if (price <= 0) return Status::InvalidPrice;
    if (find(id) != orders_.end()) return Status::DuplicateId;
    qty = match(id, side, price, qty);
    if (qty > 0) orders_.push_back({id, side, price, qty, next_seq_++});
    return Status::Ok;
  }

  Status market(OrderId id, Side side, Quantity qty) {
    if (qty == 0) return Status::InvalidQuantity;
    if (find(id) != orders_.end()) return Status::DuplicateId;
    match(id, side,
          side == Side::Buy ? std::numeric_limits<Price>::max()
                            : std::numeric_limits<Price>::min(),
          qty);
    return Status::Ok;
  }

  Status cancel(OrderId id) {
    auto it = find(id);
    if (it == orders_.end()) return Status::UnknownId;
    orders_.erase(it);
    return Status::Ok;
  }

  std::optional<Price> best(Side side) const {
    std::optional<Price> b;
    for (const auto& o : orders_) {
      if (o.side != side) continue;
      if (!b || (side == Side::Buy ? o.price > *b : o.price < *b)) b = o.price;
    }
    return b;
  }

  // Every level of one side, best first, built by brute force.
  std::vector<LevelInfo> depth(Side side) const {
    std::map<Price, LevelInfo> levels;
    for (const auto& o : orders_) {
      if (o.side != side) continue;
      LevelInfo& l = levels[o.price];
      l.price = o.price;
      l.total_qty += o.qty;
      ++l.count;
    }
    std::vector<LevelInfo> out;
    for (const auto& [p, l] : levels) out.push_back(l);
    if (side == Side::Buy) std::reverse(out.begin(), out.end());
    return out;
  }

  std::size_t order_count() const { return orders_.size(); }
  const std::vector<Trade>& trades() const { return trades_; }
  void clear_trades() { trades_.clear(); }

 private:
  struct Resting {
    OrderId id;
    Side side;
    Price price;
    Quantity qty;
    std::uint64_t seq;  // arrival number: smaller = arrived earlier
  };

  std::vector<Resting>::iterator find(OrderId id) {
    return std::find_if(orders_.begin(), orders_.end(),
                        [&](const Resting& o) { return o.id == id; });
  }

  Quantity match(OrderId id, Side side, Price limit, Quantity qty) {
    while (qty > 0) {
      // Search EVERY order for the best one we are allowed to trade with:
      // best price first; on a tie, the earliest arrival.
      auto best = orders_.end();
      for (auto it = orders_.begin(); it != orders_.end(); ++it) {
        if (it->side == side) continue;  // same side: can't trade
        const bool ok =
            side == Side::Buy ? it->price <= limit : it->price >= limit;
        if (!ok) continue;
        if (best == orders_.end()) { best = it; continue; }
        const bool better_price = side == Side::Buy ? it->price < best->price
                                                    : it->price > best->price;
        if (better_price ||
            (it->price == best->price && it->seq < best->seq)) {
          best = it;
        }
      }
      if (best == orders_.end()) break;  // nobody left to trade with

      const Quantity fill = std::min(qty, best->qty);
      if (side == Side::Buy) trades_.push_back({id, best->id, best->price, fill});
      else trades_.push_back({best->id, id, best->price, fill});
      qty -= fill;
      best->qty -= fill;
      if (best->qty == 0) orders_.erase(best);
    }
    return qty;
  }

  std::vector<Resting> orders_;
  std::vector<Trade> trades_;
  std::uint64_t next_seq_ = 0;
};

}  // namespace lob::test
