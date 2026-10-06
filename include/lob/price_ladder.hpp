#pragma once

// PriceLadder: all price levels of ONE side of the book, stored in a flat
// array with one slot per possible price ("tick").
//
//   levels_[price - min_price]  is the queue for that price.
//
// Finding a level is a subtraction, not a tree search. To find the NEXT best
// price quickly when the best level empties, a bitmap keeps one bit per
// price: 1 = "orders are waiting here". One 64-bit word covers 64 prices,
// and a single CPU instruction (count trailing/leading zeros) finds the
// first 1 in a word, so empty prices are skipped 64 at a time.
//
// The price range is fixed when the book is created ("price band"), like
// the price bands real exchanges use. Orders outside it are rejected.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "lob/types.hpp"

namespace lob {

template <Side S>  // Side::Buy = bids (best = highest), Side::Sell = asks
class PriceLadder {
 public:
  PriceLadder(Price min_price, Price max_price)
      : min_(min_price),
        levels_(static_cast<std::size_t>(max_price - min_price + 1)),
        bits_((levels_.size() + 63) / 64, 0) {
    assert(min_price <= max_price);
  }

  bool in_band(Price p) const noexcept {
    return p >= min_ && p - min_ < static_cast<Price>(levels_.size());
  }

  // The best non-empty level, or nullptr if this side is empty. O(1).
  PriceLevel* best() noexcept {
    return best_ == kNone ? nullptr : &levels_[best_];
  }
  const PriceLevel* best() const noexcept {
    return best_ == kNone ? nullptr : &levels_[best_];
  }

  // The level for `p`, switched on if it was empty. The caller must add an
  // order to it straight away. O(1).
  PriceLevel& level_for(Price p) noexcept {
    assert(in_band(p));
    const std::size_t i = index(p);
    PriceLevel& lvl = levels_[i];
    if (!test(i)) {
      set(i);
      lvl.price = p;
      ++count_;
      if (best_ == kNone || better(i, best_)) best_ = i;
    }
    return lvl;
  }

  // Switch off a level whose last order just left. O(1), plus a bitmap scan
  // if it was the best level.
  void remove_level(PriceLevel& lvl) noexcept {
    assert(lvl.empty());
    const std::size_t i = index(lvl.price);
    assert(test(i));
    clear(i);
    --count_;
    if (i == best_) best_ = next_worse(i);
  }

  // The level at `p` if orders are waiting there, else nullptr.
  const PriceLevel* find(Price p) const noexcept {
    if (!in_band(p)) return nullptr;
    const std::size_t i = index(p);
    return test(i) ? &levels_[i] : nullptr;
  }

  // The next non-empty level after `lvl`, going from best to worst.
  const PriceLevel* next_worse(const PriceLevel& lvl) const noexcept {
    const std::size_t j = next_worse(index(lvl.price));
    return j == kNone ? nullptr : &levels_[j];
  }

  std::size_t size() const noexcept { return count_; }  // non-empty levels

 private:
  static constexpr std::size_t kNone = SIZE_MAX;

  std::size_t index(Price p) const noexcept {
    return static_cast<std::size_t>(p - min_);
  }

  // Is slot a a better price than slot b for this side?
  static constexpr bool better(std::size_t a, std::size_t b) noexcept {
    return S == Side::Buy ? a > b : a < b;
  }

  std::size_t next_worse(std::size_t i) const noexcept {
    if constexpr (S == Side::Buy) {
      return i == 0 ? kNone : scan_down(i - 1);  // bids: lower is worse
    } else {
      return scan_up(i + 1);  // asks: higher is worse
    }
  }

  // --- the bitmap ----------------------------------------------------------
  bool test(std::size_t i) const noexcept {
    return (bits_[i / 64] >> (i % 64)) & 1u;
  }
  void set(std::size_t i) noexcept { bits_[i / 64] |= std::uint64_t{1} << (i % 64); }
  void clear(std::size_t i) noexcept {
    bits_[i / 64] &= ~(std::uint64_t{1} << (i % 64));
  }

  // First set bit at position >= i, or kNone.
  std::size_t scan_up(std::size_t i) const noexcept {
    if (i >= levels_.size()) return kNone;
    std::size_t w = i / 64;
    std::uint64_t word = bits_[w] & (~std::uint64_t{0} << (i % 64));
    while (word == 0) {
      if (++w == bits_.size()) return kNone;
      word = bits_[w];
    }
    return w * 64 + static_cast<std::size_t>(__builtin_ctzll(word));
  }

  // Last set bit at position <= i, or kNone.
  std::size_t scan_down(std::size_t i) const noexcept {
    std::size_t w = i / 64;
    std::uint64_t word = bits_[w] & (~std::uint64_t{0} >> (63 - i % 64));
    while (word == 0) {
      if (w == 0) return kNone;
      word = bits_[--w];
    }
    return w * 64 + 63 - static_cast<std::size_t>(__builtin_clzll(word));
  }

  Price min_;
  std::vector<PriceLevel> levels_;  // never resized: Order::level stays valid
  std::vector<std::uint64_t> bits_;
  std::size_t best_ = kNone;
  std::size_t count_ = 0;
};

}  // namespace lob
