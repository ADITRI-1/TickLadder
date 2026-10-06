# Layer 7 step 4: align Order to a full 64-byte cache line? (REJECTED)

Idea: Order is 48 bytes, so half of all orders straddle two cache lines.
`struct alignas(64) Order` puts every order on its own line, at the cost of
33% more memory (64 instead of 48 bytes per order).

A/B test: both binaries run alternately, 5 rounds each, same machine state.

| Order size | median p50 | median p99 | median p99.9 | median throughput |
|---|---:|---:|---:|---:|
| 48 bytes (kept) | 49 ns | 103 ns | 208 ns | 26.8 M msg/s |
| 64 bytes aligned | 52 ns | 113 ns | 257 ns | 26.4 M msg/s |

The 64-byte version was slower in every round. With ~10,000 resting orders
the whole working set already fits in the L2 cache, and the CPU prefetches
the neighbouring cache line anyway, so straddling is cheap; making every
order 33% bigger just means more cache lines to keep warm.
Lesson: a "known" optimization must still be measured. This one lost.
(Absolute numbers are lower than in other files because the laptop was in a
slower power state during this test; both variants ran under the same state.)
