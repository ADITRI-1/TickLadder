#include "lob/order_book.hpp"

#include <ostream>

namespace lob {

namespace {

// Find the level for `price`, creating an empty one if it does not exist.
// A template, because bids and asks are different map types.
template <typename Map>
PriceLevel& level_for(Map& side, Price price) {
  auto [it, inserted] = side.try_emplace(price);
  if (inserted) {
    it->second.price = price;
  }
  return it->second;
}

template <typename Map>
std::vector<LevelInfo> depth_of(const Map& side, std::size_t max_levels) {
  std::vector<LevelInfo> out;
  for (const auto& [price, lvl] : side) {
    if (out.size() == max_levels) {
      break;
    }
    out.push_back({price, lvl.total_qty, lvl.count});
  }
  return out;
}

template <typename Map>
std::uint64_t volume_in(const Map& side, Price price) {
  auto it = side.find(price);
  return it == side.end() ? 0 : it->second.total_qty;
}

}  // namespace

Status OrderBook::add(OrderId id, Side side, Price price, Quantity qty) {
  if (qty == 0) {
    return Status::InvalidQuantity;
  }
  if (price <= 0) {
    return Status::InvalidPrice;
  }

  // try_emplace inserts only if the id is new, in a single hash lookup.
  auto [it, inserted] = orders_.try_emplace(id);
  if (!inserted) {
    return Status::DuplicateId;
  }

  Order& o = it->second;
  o.id = id;
  o.side = side;
  o.price = price;
  o.qty = qty;

  if (side == Side::Buy) {
    level_for(bids_, price).push_back(&o);
  } else {
    level_for(asks_, price).push_back(&o);
  }
  return Status::Ok;
}

Status OrderBook::cancel(OrderId id) {
  auto it = orders_.find(id);  // O(1): no searching through queues
  if (it == orders_.end()) {
    return Status::UnknownId;
  }

  Order& o = it->second;
  PriceLevel* lvl = o.level;
  lvl->remove(&o);  // O(1): the order knows its neighbours

  // An empty level must disappear, or best_bid/best_ask would point at a
  // price where nothing is waiting.
  if (lvl->empty()) {
    if (o.side == Side::Buy) {
      bids_.erase(o.price);
    } else {
      asks_.erase(o.price);
    }
  }

  orders_.erase(it);  // last step: o is destroyed here, so use it before
  return Status::Ok;
}

std::optional<Price> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.begin()->first;
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
  auto it = orders_.find(id);
  return it == orders_.end() ? nullptr : &it->second;
}

std::size_t OrderBook::level_count(Side side) const {
  return side == Side::Buy ? bids_.size() : asks_.size();
}

// Prints the book like a trading screen: asks on top (highest first),
// then the spread line, then bids (highest first).
std::ostream& operator<<(std::ostream& os, const OrderBook& book) {
  for (auto it = book.asks_.rbegin(); it != book.asks_.rend(); ++it) {
    os << "  ASK " << it->second << '\n';
  }
  os << "  ------------------------------------\n";
  for (const auto& [price, lvl] : book.bids_) {
    os << "  BID " << lvl << '\n';
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
