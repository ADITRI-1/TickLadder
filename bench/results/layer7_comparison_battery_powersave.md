# Layer 7: before/after (median of 5 interleaved runs)

Conditions: battery power, governor powersave.
CPU: 12th Gen Intel(R) Core(TM) i5-12500H, pinned to one P-core. 10M messages per run, same seed.
All latencies in ns and include ~11 ns of timer overhead.

| step | mean | p50 | p90 | p99 | p99.9 | p99.99 | max | throughput (M msg/s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 baseline (std::map + unordered_map) | 124 | 118 | 159 | 270 | 430 | 3014 | 72703 | 12.3 |
| 1 + object pool | 110 | 105 | 146 | 213 | 361 | 3136 | 52056 | 16.2 |
| 2 + open-addressing hash map | 90 | 86 | 114 | 173 | 329 | 3092 | 72920 | 22.0 |
| 3 + flat price ladder | 83 | 79 | 107 | 162 | 269 | 2527 | 67059 | 26.8 |
