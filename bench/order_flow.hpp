#pragma once

// Generates a realistic, repeatable stream of order messages for benchmarks.
//
// What real order flow looks like, and what we copy:
//  * Most messages are passive limit orders placed NEAR the best price,
//    fewer further away (traders care about the top of the book).
//  * Most orders are cancelled, not traded. Adds roughly equal cancels +
//    fills, otherwise the book would grow forever.
//  * A small share of messages are aggressive and actually trade.
//  * The "fair price" drifts randomly, like a real stock.
//
// The whole stream is generated BEFORE timing starts, so the random number
// generator is never part of what we measure. A fixed seed means every run
// replays exactly the same messages.

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "lob/order_book.hpp"

namespace lob::bench {

enum class MsgType : std::uint8_t {
  Add,              // passive limit order: rests in the book
  Cancel,           // cancel a resting order
  AggressiveLimit,  // limit order priced to cross: trades immediately
  Market,           // market order: trades immediately
};

struct Msg {
  MsgType type{};
  Side side{};
  Quantity qty{};
  OrderId id{};
  Price price{};
};

struct FlowConfig {
  std::size_t num_msgs = 10'000'000;
  std::size_t target_orders = 10'000;  // typical number of resting orders
  double add_share = 0.50;
  double cancel_share = 0.45;  // the rest (5%) is aggressive
  std::uint32_t seed = 42;
  Price start_mid = 100'000;  // Rs 1000.00 in paise
};

struct Flow {
  std::vector<Msg> prefill;  // builds the starting book (not timed)
  std::vector<Msg> msgs;     // the timed messages
};

// Sends one message to the book.
inline void apply(OrderBook& book, const Msg& m) {
  switch (m.type) {
    case MsgType::Add:
    case MsgType::AggressiveLimit:
      book.add(m.id, m.side, m.price, m.qty);
      break;
    case MsgType::Cancel:
      book.cancel(m.id);
      break;
    case MsgType::Market:
      book.market(m.id, m.side, m.qty);
      break;
  }
}

namespace detail {

class FlowGenerator {
 public:
  explicit FlowGenerator(const FlowConfig& cfg)
      : cfg_(cfg), rng_(cfg.seed), mid_(cfg.start_mid) {}

  Flow run() {
    Flow flow;
    flow.prefill.reserve(cfg_.target_orders);
    for (std::size_t i = 0; i < cfg_.target_orders; ++i) {
      flow.prefill.push_back(emit(passive()));
    }
    flow.msgs.reserve(cfg_.num_msgs);
    while (flow.msgs.size() < cfg_.num_msgs) {
      flow.msgs.push_back(emit(next()));
    }
    return flow;
  }

 private:
  // Pick the next message, keeping the book size near its target.
  Msg next() {
    drift_mid();
    const double r = uniform_(rng_);
    const std::size_t live = scratch_.order_count();
    const bool too_big = live > cfg_.target_orders * 11 / 10;
    const bool too_small = live < cfg_.target_orders * 9 / 10;

    if (r < cfg_.add_share) {
      return too_big ? cancel() : passive();
    }
    if (r < cfg_.add_share + cfg_.cancel_share) {
      return too_small ? passive() : cancel();
    }
    return aggressive();
  }

  // The fair price takes a small random step now and then.
  void drift_mid() {
    if (uniform_(rng_) < 0.02) {
      mid_ += (rng_() % 2 == 0) ? 1 : -1;
    }
  }

  Side random_side() { return (rng_() % 2 == 0) ? Side::Buy : Side::Sell; }

  // Distance from the best price: usually 0-3 ticks, sometimes far.
  Price passive_offset() {
    return static_cast<Price>(std::min<std::uint32_t>(offset_(rng_), 100));
  }

  Msg passive() {
    Msg m;
    m.type = MsgType::Add;
    m.side = random_side();
    m.qty = static_cast<Quantity>(1 + rng_() % 100);
    m.id = next_id_++;
    // Never cross: a passive buy must sit below the best ask, a passive
    // sell above the best bid.
    if (m.side == Side::Buy) {
      Price p = mid_ - 1 - passive_offset();
      if (auto ask = scratch_.best_ask()) p = std::min(p, *ask - 1);
      m.price = std::max<Price>(p, 1);
    } else {
      Price p = mid_ + 1 + passive_offset();
      if (auto bid = scratch_.best_bid()) p = std::max(p, *bid + 1);
      m.price = p;
    }
    live_.push_back(m.id);
    return m;
  }

  Msg cancel() {
    // Pick random live orders; skip ids that have already traded away.
    while (!live_.empty()) {
      const std::size_t i = rng_() % live_.size();
      const OrderId id = live_[i];
      live_[i] = live_.back();
      live_.pop_back();
      if (scratch_.find(id) != nullptr) {
        Msg m;
        m.type = MsgType::Cancel;
        m.id = id;
        return m;
      }
    }
    return passive();  // nothing left to cancel
  }

  Msg aggressive() {
    Msg m;
    m.side = random_side();
    m.qty = static_cast<Quantity>(1 + rng_() % 200);
    m.id = next_id_++;
    const auto opposite =
        m.side == Side::Buy ? scratch_.best_ask() : scratch_.best_bid();
    if (rng_() % 2 == 0 || !opposite) {
      m.type = MsgType::Market;
    } else {
      // A limit order priced 0-2 ticks THROUGH the best opposite price.
      m.type = MsgType::AggressiveLimit;
      const auto through = static_cast<Price>(rng_() % 3);
      m.price = m.side == Side::Buy ? *opposite + through
                                    : std::max<Price>(*opposite - through, 1);
      live_.push_back(m.id);  // its leftover may rest in the book
    }
    return m;
  }

  // Apply to the scratch book so the next message sees the real state.
  Msg emit(const Msg& m) {
    apply(scratch_, m);
    scratch_.clear_trades();
    return m;
  }

  FlowConfig cfg_;
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> uniform_{0.0, 1.0};
  std::geometric_distribution<std::uint32_t> offset_{0.3};
  OrderBook scratch_;
  std::vector<OrderId> live_;
  OrderId next_id_ = 1;
  Price mid_;
};

}  // namespace detail

inline Flow generate_flow(const FlowConfig& cfg) {
  return detail::FlowGenerator(cfg).run();
}

}  // namespace lob::bench
