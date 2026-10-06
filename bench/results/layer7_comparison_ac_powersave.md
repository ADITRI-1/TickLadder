# Layer 7: before/after (median of 5 interleaved runs)

Conditions: AC power, governor powersave.
CPU: 12th Gen Intel(R) Core(TM) i5-12500H, pinned to one P-core. 10M messages per run, same seed.
All latencies in ns and include ~11 ns of timer overhead.

| step | mean | p50 | p90 | p99 | p99.9 | p99.99 | max | throughput (M msg/s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 baseline (std::map + unordered_map) | 61 | 57 | 77 | 133 | 219 | 2306 | 79096 | 22.3 |
| 1 + object pool | 54 | 51 | 69 | 104 | 187 | 2279 | 81415 | 29.7 |
| 2 + open-addressing hash map | 45 | 42 | 55 | 90 | 212 | 2249 | 69915 | 38.5 |
| 3 + flat price ladder | 42 | 38 | 52 | 84 | 194 | 2140 | 73700 | 46.8 |
