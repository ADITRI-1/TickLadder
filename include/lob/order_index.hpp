#pragma once

// OrderIndex: finds an Order* by its id. A hash map built for speed.
//
// std::unordered_map allocates a separate little node for EVERY entry and
// links them in lists ("chaining"). This map instead keeps all entries in
// ONE flat array ("open addressing"):
//
//   * hash(id) picks a starting slot;
//   * if that slot is taken by another id, try the next slot, and the next
//     ("linear probing") until we find our id or an empty slot.
//
// Neighbouring slots sit next to each other in memory, so a probe usually
// costs a single cache line. Inserting never allocates (until the table has
// to grow), and the table is kept at most half full so probes stay short.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "lob/types.hpp"

namespace lob {

class OrderIndex {
 public:
  explicit OrderIndex(std::size_t expected = 0) { reserve(expected); }

  // Make room for `n` entries without growing later.
  void reserve(std::size_t n) {
    std::size_t cap = 16;
    while (cap < 2 * n) cap *= 2;  // at most 50% full
    if (cap > slots_.size()) rehash(cap);
  }

  Order* find(OrderId id) const noexcept {
    for (std::size_t i = home(id);; i = (i + 1) & mask_) {
      const Slot& s = slots_[i];
      if (s.value == nullptr) return nullptr;  // empty slot: id not present
      if (s.key == id) return s.value;
    }
  }

  bool contains(OrderId id) const noexcept { return find(id) != nullptr; }

  // Returns false (and changes nothing) if `id` is already present.
  bool insert(OrderId id, Order* value) {
    assert(value != nullptr && "nullptr marks an empty slot");
    if (2 * (size_ + 1) > slots_.size()) rehash(2 * slots_.size());
    for (std::size_t i = home(id);; i = (i + 1) & mask_) {
      Slot& s = slots_[i];
      if (s.value == nullptr) {
        s = {id, value};
        ++size_;
        return true;
      }
      if (s.key == id) return false;
    }
  }

  // Returns false if `id` was not present.
  bool erase(OrderId id) noexcept {
    std::size_t i = home(id);
    while (true) {
      if (slots_[i].value == nullptr) return false;
      if (slots_[i].key == id) break;
      i = (i + 1) & mask_;
    }
    // "Backward shift" deletion. We can't just empty slot i: an entry
    // further along may have probed PAST i to reach its place, and an empty
    // hole would make find() stop early and miss it. So walk the run of
    // entries after i and move back any entry that is allowed to sit at i.
    for (std::size_t j = (i + 1) & mask_; slots_[j].value != nullptr;
         j = (j + 1) & mask_) {
      const std::size_t h = home(slots_[j].key);
      // Entry j may move to i only if its home is NOT in the circular
      // range (i, j]; otherwise moving it would put it before its home.
      const bool home_in_between =
          (i <= j) ? (i < h && h <= j) : (i < h || h <= j);
      if (!home_in_between) {
        slots_[i] = slots_[j];
        i = j;
      }
    }
    slots_[i] = {};
    --size_;
    return true;
  }

  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return slots_.size(); }

 private:
  struct Slot {
    OrderId key{};
    Order* value{nullptr};  // nullptr = empty slot
  };

  // Mix the bits of the id so that ids like 1, 2, 3 ... (or multiples of
  // 1024) still spread evenly over the table. (MurmurHash3's finalizer.)
  static std::uint64_t mix(std::uint64_t x) noexcept {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
  }

  // The slot where `id` would ideally live. Capacity is a power of two, so
  // "& mask_" is a cheap way to say "% capacity".
  std::size_t home(OrderId id) const noexcept {
    return static_cast<std::size_t>(mix(id)) & mask_;
  }

  void rehash(std::size_t new_cap) {
    std::vector<Slot> old;
    old.swap(slots_);
    slots_.assign(new_cap, Slot{});
    mask_ = new_cap - 1;
    size_ = 0;
    for (const Slot& s : old) {
      if (s.value != nullptr) insert(s.key, s.value);
    }
  }

  std::vector<Slot> slots_;
  std::size_t mask_ = 0;
  std::size_t size_ = 0;
};

}  // namespace lob
