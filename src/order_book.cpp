#include "lob/order_book.hpp"

#include <algorithm>
#include <limits>
#include <ostream>

namespace lob {

namespace {

// Walk one side from best to worst level and copy out up to max_levels.
template <typename Ladder>
std::vector<LevelInfo> depth_of(const Ladder& side, std::size_t max_levels) {
  std::vector<LevelInfo> out;
  for (const PriceLevel* lvl = side.best();
       lvl != nullptr && out.size() < max_levels; lvl = side.next_worse(*lvl)) {
    out.push_back({lvl->price, lvl->total_qty, lvl->count});
  }
  return out;
}

template <typename Ladder>
std::uint64_t volume_in(const Ladder& side, Price price) {
  const PriceLevel* lvl = side.find(price);
  return lvl == nullptr ? 0 : lvl->total_qty;
}

// Does a resting order at `level_price` cross an incoming order on `side`
// with limit `limit`? A buyer accepts asks at or BELOW its limit; a seller
// accepts bids at or ABOVE its limit.
bool crosses(Side side, Price limit, Price level_price) {
  return side == Side::Buy ? level_price <= limit : level_price >= limit;
}

}  // namespace

OrderBook::OrderBook(const BookConfig& cfg)
    : min_price_(cfg.min_price),
      max_price_(cfg.max_price),
      bids_(cfg.min_price, cfg.max_price),
      asks_(cfg.min_price, cfg.max_price),
      pool_(cfg.expected_orders),
      orders_(cfg.expected_orders) {}

template <typename Ladder>
Quantity OrderBook::match_against(Ladder& book_side, OrderId id, Side side,
                                  Price limit, Quantity qty) {
  // Outer loop: best price level first.
  while (qty > 0) {
    PriceLevel* best = book_side.best();
    if (best == nullptr) {
      break;  // nothing left on the other side
    }
    PriceLevel& lvl = *best;
    if (!crosses(side, limit, lvl.price)) {
      break;  // best remaining price is too expensive: stop
    }

    // Inner loop: oldest order at this price first.
    while (qty > 0 && !lvl.empty()) {
      Order* maker = lvl.head;
      const Quantity fill = std::min(qty, maker->qty);

      // Trade at the RESTING order's price: it was there first.
      if (side == Side::Buy) {
        trades_.push_back({id, maker->id, lvl.price, fill});
      } else {
        trades_.push_back({maker->id, id, lvl.price, fill});
      }
      qty -= fill;

      if (fill == maker->qty) {
        lvl.remove(maker);  // fully filled: leaves the queue...
        forget(maker);      // ...and the book (maker is now gone)
      } else {
        lvl.reduce(maker, fill);  // partly filled: keeps its place
      }
    }

    if (lvl.empty()) {
      book_side.remove_level(lvl);  // no one left at this price
    }
  }
  return qty;
}

void OrderBook::forget(Order* o) {
  orders_.erase(o->id);
  pool_.release(o);
}

Status OrderBook::add(OrderId id, Side side, Price price, Quantity qty) {
  if (qty == 0) {
    return Status::InvalidQuantity;
  }
  if (price < min_price_ || price > max_price_) {
    return Status::InvalidPrice;  // outside the price band (includes <= 0)
  }
  if (orders_.contains(id)) {
    return Status::DuplicateId;
  }

  const Quantity left = match(id, side, price, qty);
  if (left > 0) {
    rest(id, side, price, left);  // the unfilled part waits in the book
  }
  return Status::Ok;
}

Status OrderBook::market(OrderId id, Side side, Quantity qty) {
  if (qty == 0) {
    return Status::InvalidQuantity;
  }
  if (orders_.contains(id)) {
    return Status::DuplicateId;
  }

  // "Any price": a buyer accepts up to +infinity, a seller down to -infinity.
  const Price any = side == Side::Buy ? std::numeric_limits<Price>::max()
                                      : std::numeric_limits<Price>::min();
  match(id, side, any, qty);  // whatever is left is simply dropped
  return Status::Ok;
}

Quantity OrderBook::match(OrderId id, Side side, Price limit, Quantity qty) {
  return side == Side::Buy ? match_against(asks_, id, side, limit, qty)
                           : match_against(bids_, id, side, limit, qty);
}

void OrderBook::rest(OrderId id, Side side, Price price, Quantity qty) {
  Order& o = *pool_.acquire();
  orders_.insert(id, &o);
  o.id = id;
  o.side = side;
  o.price = price;
  o.qty = qty;

  if (side == Side::Buy) {
    bids_.level_for(price).push_back(&o);
  } else {
    asks_.level_for(price).push_back(&o);
  }
}

Status OrderBook::cancel(OrderId id) {
  Order* found = orders_.find(id);  // O(1): no searching through queues
  if (found == nullptr) {
    return Status::UnknownId;
  }

  Order& o = *found;
  PriceLevel* lvl = o.level;
  lvl->remove(&o);  // O(1): the order knows its neighbours

  // An empty level must disappear, or best_bid/best_ask would point at a
  // price where nothing is waiting.
  if (lvl->empty()) {
    if (o.side == Side::Buy) {
      bids_.remove_level(*lvl);
    } else {
      asks_.remove_level(*lvl);
    }
  }

  orders_.erase(id);
  pool_.release(&o);  // last step: o may be reused from here on
  return Status::Ok;
}

std::optional<Price> OrderBook::best_bid() const {
  const PriceLevel* lvl = bids_.best();
  if (lvl == nullptr) {
    return std::nullopt;
  }
  return lvl->price;
}

std::optional<Price> OrderBook::best_ask() const {
  const PriceLevel* lvl = asks_.best();
  if (lvl == nullptr) {
    return std::nullopt;
  }
  return lvl->price;
}

std::uint64_t OrderBook::volume_at(Side side, Price price) const {
  return side == Side::Buy ? volume_in(bids_, price) : volume_in(asks_, price);
}

std::vector<LevelInfo> OrderBook::depth(Side side,
                                        std::size_t max_levels) const {
  return side == Side::Buy ? depth_of(bids_, max_levels)
                           : depth_of(asks_, max_levels);
}

const Order* OrderBook::find(OrderId id) const {
  return orders_.find(id);
}

std::size_t OrderBook::level_count(Side side) const {
  return side == Side::Buy ? bids_.size() : asks_.size();
}

// Prints the book like a trading screen: asks on top (highest first),
// then the spread line, then bids (highest first).
std::ostream& operator<<(std::ostream& os, const OrderBook& book) {
  std::vector<const PriceLevel*> asks;  // collected best-first...
  for (const PriceLevel* l = book.asks_.best(); l != nullptr;
       l = book.asks_.next_worse(*l)) {
    asks.push_back(l);
  }
  for (auto it = asks.rbegin(); it != asks.rend(); ++it) {  // ...printed reversed
    os << "  ASK " << **it << '\n';
  }
  os << "  ------------------------------------\n";
  for (const PriceLevel* l = book.bids_.best(); l != nullptr;
       l = book.bids_.next_worse(*l)) {
    os << "  BID " << *l << '\n';
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, Status s) {
  switch (s) {
    case Status::Ok: return os << "Ok";
    case Status::DuplicateId: return os << "DuplicateId";
    case Status::InvalidQuantity: return os << "InvalidQuantity";
    case Status::InvalidPrice: return os << "InvalidPrice";
    case Status::UnknownId: return os << "UnknownId";
  }
  return os << "Status(?)";
}

std::ostream& operator<<(std::ostream& os, const LevelInfo& l) {
  return os << "{price=" << l.price << ", qty=" << l.total_qty
            << ", count=" << l.count << "}";
}

}  // namespace lob
