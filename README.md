# TickLadder

**A C++20 limit order book matching engine: 51M msgs/sec, p99 81 ns.**

A single-threaded, single-instrument limit order book and matching engine,
built for low and predictable latency, and verified for correctness with
differential testing against a reference implementation.

*Why the name:* each side of the book is a **ladder with one rung per price
tick**, a flat array indexed by price plus a bitmap of non-empty rungs.
That structure is what makes level lookup O(1) (see
[Architecture](#architecture)).

**Measured on one core of an Intel i5-12500H laptop** (pinned P-core,
`performance` governor, 10 million realistic messages, timer overhead of
~11 ns included):

| p50 | p90 | p99 | p99.9 | throughput | heap allocations on the hot path |
|---:|---:|---:|---:|---:|---:|
| **39 ns** | 53 ns | **81 ns** | 152 ns | **51.4 M msgs/s** | **0** |

The optimized engine is **2.1× faster than the `std::map` /
`std::unordered_map` baseline** (median of interleaved runs, see
[Performance](#performance)). Every number here was measured, and the raw
output is in [`bench/results/`](bench/results/).

---

## Features

- **Limit orders and market orders**, buy and sell, plus **cancel by id**
- **Price-time priority**: best price first, then first-come-first-served
- **Partial fills** on both the incoming and the resting order
- Trades execute at the **resting (maker) order's price**
- Market-order leftovers are dropped; limit-order leftovers rest in the book
- Integer prices (ticks) and quantities, **no floating point**
- Configurable **price band**; orders outside it are rejected
- Errors are returned as status codes: no exceptions, no virtual calls and no
  heap allocation on the hot path

## Architecture

```
          add(id, side, price, qty)   market(id, side, qty)   cancel(id)
                                    │
                                    ▼
┌─────────────────────────────── OrderBook ────────────────────────────────┐
│                                                                          │
│  OrderIndex  (open addressing)              ObjectPool<Order>            │
│  id ─────────────► Order*                   pre-allocated 48-byte slots, │
│  flat array, linear probing,                free-list reuse, no malloc   │
│  backward-shift delete                      while trading                │
│                                                                          │
│  PriceLadder<Sell>  asks                    best ask = lowest set bit    │
│   ...                                                                    │
│   price 10102 │ [ ]                                                      │
│   price 10101 │ [#7] ─ [#12]                ◄── FIFO queue per price     │
│   price 10100 │ [#3]                        ◄── best ask                 │
│  ─────────────┼──────── spread ─────────                                 │
│   price 10000 │ [#5] ─ [#9] ─ [#11]         ◄── best bid                 │
│   price  9999 │ [ ]                                                      │
│   ...                                                                    │
│  PriceLadder<Buy>   bids                    best bid = highest set bit   │
│                                                                          │
│  bitmap: 1 bit per price tick ("orders waiting here?"); one 64-bit word  │
│  covers 64 ticks, and ctz/clz finds the next non-empty level             │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                       trades(): {buy_id, sell_id, price, qty}
```

| Component | File | Idea |
|---|---|---|
| `Order`, `PriceLevel`, `Trade` | [`types.hpp`](include/lob/types.hpp) | `Order` is a node of an **intrusive doubly linked list**: `prev`/`next` live inside the order, so joining or leaving a queue is a few pointer writes. 48 bytes, so it fits in one cache line (enforced by `static_assert`). |
| `PriceLadder<Side>` | [`price_ladder.hpp`](include/lob/price_ladder.hpp) | A flat array with one `PriceLevel` per tick in the price band, plus an occupancy bitmap. Level lookup is `levels[price - min]`. |
| `OrderIndex` | [`order_index.hpp`](include/lob/order_index.hpp) | An open-addressing hash map from id to `Order*`, kept at most 50% full, using the MurmurHash3 finalizer. |
| `ObjectPool<T>` | [`object_pool.hpp`](include/lob/object_pool.hpp) | Memory is allocated in chunks up front (and pre-faulted), then objects are handed out and returned via a free list. |
| `OrderBook` | [`order_book.hpp`](include/lob/order_book.hpp), [`order_book.cpp`](src/order_book.cpp) | Validation, the matching loop (one template for both sides), resting and cancelling. |

### Complexity

| Operation | Cost |
|---|---|
| Add a passive limit order | O(1) |
| Cancel | O(1), plus a bitmap scan (64 ticks per step) if it empties the best level |
| Match an aggressive order | O(levels crossed + orders filled) |
| Best bid / best ask | O(1) |
| Depth of the top k levels | O(k), plus bitmap scans over empty ticks |

### Matching rules

1. An incoming order trades against the **best opposite price** while prices
   cross (a buy at limit L accepts asks ≤ L; a sell accepts bids ≥ L).
2. Within a price level, the **oldest order fills first**. Queue position *is*
   time priority, so no timestamps are stored.
3. Each fill is `min(incoming remaining, resting remaining)` at the **resting
   order's price**.
4. A limit order's remainder rests at its limit price. A market order's
   remainder is discarded.

---

## Performance

### Methodology

- **Timer:** `rdtsc` / `rdtscp` with `lfence` (to stop out-of-order execution
  from moving work across the measurement), calibrated against
  `steady_clock`. The timer's own cost (~11 ns) is measured and *included* in
  every number reported.
- **Order flow** ([`order_flow.hpp`](bench/order_flow.hpp)): 10M messages
  generated **before** timing, with a fixed seed: 52% passive adds clustered
  near the touch (geometric distance), 43% cancels of random live orders, 5%
  aggressive (half market orders, half limits priced through the touch). The
  fair price follows a random walk, and the book is held near 10,000 resting
  orders, because in real markets adds ≈ cancels + fills.
- **Every message is timed individually**, and results are reported as
  percentiles, because the average hides exactly the outliers that matter.
- Pinned to one **P-core** (on this hybrid CPU, an E-core measured ~30%
  slower with a 10× worse max).
- The benchmark reports **heap allocations, page faults and interrupts** on
  the pinned core during the timed pass, plus the power source and governor.
- Before/after comparisons rebuild every version with the *same* benchmark
  program and run them **interleaved** over several rounds, reporting medians
  ([`compare_steps.sh`](bench/compare_steps.sh)).

### Optimization journey

Median of 5 interleaved runs, AC power, `powersave` governor, ns per message
([full table](bench/results/layer7_comparison_ac_powersave.md)):

| Version | p50 | p99 | p99.9 | Throughput |
|---|---:|---:|---:|---:|
| Baseline: `std::map` levels + `std::unordered_map<id, Order>` | 57 | 133 | 219 | 22.3 M/s |
| + Object pool for orders | 51 | 104 | 187 | 29.7 M/s |
| + Open-addressing `OrderIndex` (heap allocations: 0.53 → **0.00** per msg) | 42 | 90 | 212 | 38.5 M/s |
| + Flat `PriceLadder` with bitmap | **38** | **84** | 194 | **46.8 M/s** |
| `alignas(64)` on `Order` | *lower throughput in 5/5 rounds and worse p99: rejected* ([why](bench/results/layer7_step4_alignment_experiment.md)) | | | |

The same comparison on battery power was ~2× slower in absolute terms but
showed the same **2.2× speed-up** ([table](bench/results/layer7_comparison_battery_powersave.md)).
Relative results are stable even when absolute ones are not.

### Findings along the way

- **The power source doubled latency.** The first baseline was measured on
  battery. Re-measured on AC, the same code was 2× faster, so the benchmark
  now prints the power source and governor, and result files are named by
  them.
- **A pre-filled results buffer still page-faulted.** `std::vector<T>(n, 0)`
  can be lowered to `calloc`, which skips writing zeros to fresh OS pages, so
  the pages were first touched *during* the timed loop (38k faults). Filling
  with a non-zero value fixed it, and page faults in the timed pass are now 0.
- **The remaining tail is the OS, not the engine.** p99.99 (~2 µs) and max
  stayed flat across all optimizations. The count of messages slower than
  1 µs tracks the count of interrupts on the pinned core (`/proc/interrupts`,
  `CONFIG_HZ=1000`). A production deployment would use `isolcpus`,
  `nohz_full`, IRQ affinity, the `performance` governor and disabled deep
  C-states.

### Micro-benchmarks (Google Benchmark)

| Benchmark | Time |
|---|---:|
| `PriceLevel` push + remove | 3.2 ns |
| Add + cancel on a 2,000-order book | 35 ns |
| Add a resting sell + crossing buy (one trade) | 50 ns |

---

## Correctness

**83 tests**, run under **AddressSanitizer + UndefinedBehaviorSanitizer** in
Debug and again in Release.

| Kind | What it covers |
|---|---|
| Unit tests | Every branch of the intrusive list (head, middle, tail, only element), pool reuse, hash map deletion inside long probe runs, bitmap scans across word boundaries |
| Scenario tests | Price-time priority, price improvement, partial fills on both sides, multi-level sweeps, market orders, cancels after partial fills |
| Edge cases | `UINT32_MAX` quantities (64-bit level totals), price-band edges, 10,000-order FIFO queues, 1,000-level sweeps, rejected orders leaving no side effects, id reuse |
| **Differential tests** | 4 × 20,000 random messages (including invalid ones) run through the engine **and** a deliberately naive [`ReferenceBook`](tests/reference_book.hpp) (one vector, brute-force search). Statuses, trades and full depth must match after every message. The same idea checks `OrderIndex` against `std::unordered_map` and `PriceLadder` against `std::set`. |
| Invariants | 20,000 random messages: the book is never crossed, and quantity is conserved (in = traded + cancelled + dropped + resting) |

**The tests were tested.** Five bugs were planted on purpose (`<=` → `<` in
the crossing check, trading at the taker's price, not erasing an empty level,
not updating a level total on a partial fill, no duplicate check for market
orders). All five were caught. The last one was caught by only one test at
first, so the differential test was extended until it caught it too.

---

## Build and run

Requires a C++20 compiler (GCC 13 tested) and CMake ≥ 3.21. GoogleTest and
Google Benchmark are downloaded automatically at pinned versions.

```bash
# Debug: tests with sanitizers
cmake --preset debug
cmake --build --preset debug -j
ctest --preset debug

# Release: benchmarks
cmake --preset release
cmake --build --preset release -j
./build/release/lob_latency --cpu 2       # latency percentiles (pick a fast core)
./build/release/lob_bench                 # micro-benchmarks
./build/release/lob_demo                  # walk through a book step by step
./bench/compare_steps.sh                  # fair before/after of all optimization steps
```

`lob_latency` options: `--msgs N`, `--orders N` (target book size), `--cpu N`,
`--seed N`, `--diagnose 1` (classifies slow messages by page fault or
allocation).

## Project layout

```
include/lob/   types, order book, price ladder, order index, object pool
src/           order book implementation
tests/         GoogleTest suites + the reference book for differential tests
bench/         latency benchmark, order flow generator, TSC clock, statistics,
               Google Benchmark micro-benchmarks, comparison script
bench/results/ raw output of every measurement quoted in this README
apps/          step-by-step demo
```

## Design trade-offs and limitations

- **Single-threaded, single instrument**, deliberately. A real venue shards by
  symbol and feeds each book from a queue.
- **The price band costs memory**: ~40 bytes per tick per side (~21 MB for the
  default band of 262,143 ticks). A sparse or unbounded price range would
  need a different level index, for example a hash of levels plus a heap.
- **Trades are appended to a vector** that the caller reads and clears.
  Production engines usually publish each execution through a callback or a
  ring buffer.
- **Not implemented:** IOC/FOK/post-only order types, order modification,
  self-trade prevention, tick-size validation, persistence and networking.

## Roadmap

- Replay real exchange data (NASDAQ TotalView-ITCH sample files)
- Multi-threaded pipeline: gateway thread → lock-free SPSC ring buffer →
  matching thread, measuring end-to-end latency
- Hardware-counter profiling (cache misses, branch mispredictions) with `perf`
- Latency histograms in this README

The engine was built in stages (types → add/cancel → matching → edge cases →
benchmark → optimizations), and each stage and each optimization step is a
separate commit, so any number above can be reproduced from history.
