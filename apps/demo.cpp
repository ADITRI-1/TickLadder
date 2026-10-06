// A tiny walkthrough: add, cancel and match orders and watch the book change.
// Run it with: ./build/debug/lob_demo

#include <iostream>

#include "lob/order_book.hpp"

using namespace lob;

int main() {
  OrderBook book;

  auto show = [&](const char* step) {
    std::cout << "\n== " << step << " ==\n" << book;
    std::cout << "  best bid: ";
    if (auto b = book.best_bid()) std::cout << *b; else std::cout << "none";
    std::cout << "   best ask: ";
    if (auto a = book.best_ask()) std::cout << *a; else std::cout << "none";
    std::cout << '\n';
  };

  auto show_trades = [&]() {
    for (const Trade& t : book.trades()) std::cout << "  >> " << t << '\n';
    if (book.trades().empty()) std::cout << "  >> no trades\n";
    book.clear_trades();
  };

  show("1. Empty book");

  book.add(1, Side::Buy, 10000, 10);   // buy 10 at Rs 100.00
  book.add(2, Side::Buy, 9950, 5);     // buy 5  at Rs 99.50
  book.add(3, Side::Sell, 10100, 8);   // sell 8 at Rs 101.00
  book.add(4, Side::Sell, 10200, 12);  // sell 12 at Rs 102.00
  show("2. Two buyers and two sellers arrive");

  book.add(5, Side::Buy, 10000, 7);    // joins the BACK of the 10000 queue
  show("3. Order 5 joins the queue at 10000, behind order 1");

  book.cancel(1);
  show("4. Order 1 cancels; order 5 is now first in line");

  book.cancel(3);
  show("5. Order 3 cancels; level 10100 is empty and disappears");

  // ---- Layer 4: matching ----
  book.add(6, Side::Buy, 10300, 5);
  show("6. Order 6 BUYS 5 at up to 103.00 -> trades with order 4 at 102.00");
  show_trades();

  book.market(7, Side::Sell, 10);
  show("7. Order 7: MARKET sell 10 -> takes 7 from order 5, then 3 from order 2");
  show_trades();

  book.add(8, Side::Buy, 10200, 10);
  show("8. Order 8 BUYS 10 at 102.00 -> takes the last 7, its 3 left over rest");
  show_trades();

  std::cout << "\nTrying to cancel order 3 again: " << book.cancel(3) << '\n';
  std::cout << "Trying to add duplicate id 2:  "
            << book.add(2, Side::Buy, 9900, 1) << '\n';
  return 0;
}
