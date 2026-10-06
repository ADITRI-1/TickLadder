#pragma once

// A pool of reusable objects: memory is requested from the system in big
// chunks, up front, and individual objects are then handed out and taken
// back with a couple of pointer operations. No malloc/free per object.
//
// Analogy: a restaurant that owns 4096 plates and washes them, instead of
// buying a new plate for every customer and throwing it away afterwards.

#include <cstddef>
#include <memory>
#include <vector>

namespace lob {

template <typename T, std::size_t ChunkSize = 4096>
class ObjectPool {
 public:
  // Grab enough chunks for `expected` objects right away, so the hot path
  // never has to ask the system for memory.
  explicit ObjectPool(std::size_t expected = 0) {
    while (capacity() < expected) {
      grow();
    }
  }

  // Hand out a fresh, default-initialized object. O(1).
  T* acquire() {
    if (free_.empty()) {
      grow();  // rare: only when more objects are live than ever before
    }
    T* p = free_.back();
    free_.pop_back();
    *p = T{};  // a reused slot must not keep the previous order's data
    return p;
  }

  // Give an object back for reuse. O(1).
  void release(T* p) noexcept { free_.push_back(p); }

  std::size_t capacity() const noexcept { return chunks_.size() * ChunkSize; }
  std::size_t in_use() const noexcept { return capacity() - free_.size(); }

 private:
  void grow() {
    // make_unique<T[]> value-initializes the chunk, which also touches every
    // page now, so the OS's page faults happen here and not mid-trading.
    chunks_.push_back(std::make_unique<T[]>(ChunkSize));
    T* chunk = chunks_.back().get();
    free_.reserve(capacity());
    // Push in reverse so acquire() hands out low addresses first.
    for (std::size_t i = ChunkSize; i-- > 0;) {
      free_.push_back(&chunk[i]);
    }
  }

  // Chunks are never moved or freed while the pool lives, so every T*
  // handed out stays valid.
  std::vector<std::unique_ptr<T[]>> chunks_;
  std::vector<T*> free_;  // stack of slots ready for reuse
};

}  // namespace lob
