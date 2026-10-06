#include "lob/types.hpp"

#include <ostream>

namespace lob {

std::ostream& operator<<(std::ostream& os, Side s) {
  return os << (s == Side::Buy ? "BUY" : "SELL");
}

std::ostream& operator<<(std::ostream& os, const Order& o) {
  return os << "Order{id=" << o.id << ", " << o.side << ", price=" << o.price
            << ", qty=" << o.qty << "}";
}

std::ostream& operator<<(std::ostream& os, const PriceLevel& l) {
  os << "Level{price=" << l.price << ", total=" << l.total_qty
     << ", count=" << l.count << ", queue=[";
  for (const Order* o = l.head; o != nullptr; o = o->next) {
    os << o->id << (o->next != nullptr ? " -> " : "");
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const Trade& t) {
  return os << "Trade{buy=" << t.buy_id << ", sell=" << t.sell_id
            << ", price=" << t.price << ", qty=" << t.qty << "}";
}

}  // namespace lob
