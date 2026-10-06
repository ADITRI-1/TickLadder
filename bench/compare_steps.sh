#!/usr/bin/env bash
# Fair before/after comparison of the Layer 7 optimization steps.
#
# For each step it checks out THAT step's engine from git history but builds
# it with TODAY's benchmark program, so every step is measured the same way.
# Steps are run in rotating order over several rounds (0123, 1230, 2301...)
# so background noise hits all of them equally, and the MEDIAN of each
# number is reported.
#
# Usage: bench/compare_steps.sh            (ROUNDS=5 by default)
#        ROUNDS=9 bench/compare_steps.sh

set -euo pipefail
ROOT=$(git rev-parse --show-toplevel)
WORK="$ROOT/build/compare"
ROUNDS=${ROUNDS:-5}
CPU=${CPU:-2}
# Power source and CPU governor change the numbers a lot, so they are part
# of the file name: runs under different conditions never overwrite each other.
POWER=battery
for s in /sys/class/power_supply/*; do
  if [[ "$(cat "$s/type" 2>/dev/null)" == "Mains" && "$(cat "$s/online")" == "1" ]]; then POWER=ac; fi
done
GOV=$(cat "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor" 2>/dev/null || echo unknown)
OUT="$ROOT/bench/results/layer7_comparison_${POWER}_${GOV}.md"

NAMES=("0 baseline (std::map + unordered_map)" "1 + object pool"
       "2 + open-addressing hash map" "3 + flat price ladder")
COMMITS=()
for n in 0 1 2 3; do
  COMMITS+=("$(git -C "$ROOT" log --grep="Layer 7 step $n:" --format=%h -1)")
done

# Reuse the already-downloaded GoogleTest/Benchmark sources.
DEPS="$ROOT/build/release/_deps"

mkdir -p "$WORK"
for i in "${!COMMITS[@]}"; do
  dir="$WORK/step$i"
  rm -rf "$dir" && mkdir -p "$dir"
  git -C "$ROOT" archive "${COMMITS[$i]}" | tar x -C "$dir"
  cp "$ROOT"/bench/{latency_main.cpp,order_flow.hpp,latency_stats.hpp,tsc_clock.hpp} "$dir/bench/"
  echo "building step $i (${COMMITS[$i]})..."
  cmake -S "$dir" -B "$dir/build" -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$DEPS/googletest-src" \
    -DFETCHCONTENT_SOURCE_DIR_BENCHMARK="$DEPS/benchmark-src" >/dev/null
  cmake --build "$dir/build" -j --target lob_latency >/dev/null
done

RAW="$WORK/raw.txt"
: > "$RAW"
n=${#COMMITS[@]}
for ((r = 0; r < ROUNDS; r++)); do
  for ((k = 0; k < n; k++)); do
    i=$(( (r + k) % n ))  # rotate the order every round
    echo "round $((r + 1))/$ROUNDS: step $i"
    "$WORK/step$i/build/lob_latency" --cpu "$CPU" |
      awk -v s="$i" '/^\| ALL/ {print s, "lat", $0} /^Throughput/ {print s, "thr", $2}' >> "$RAW"
  done
done

python3 - "$RAW" "$OUT" "$ROUNDS" "$POWER" "$GOV" "${NAMES[@]}" <<'PY'
import statistics, sys, subprocess
raw, out, rounds, power, gov = sys.argv[1:6]
names = sys.argv[6:]
lat = {i: [] for i in range(len(names))}
thr = {i: [] for i in range(len(names))}
for line in open(raw):
    step, kind, rest = line.split(" ", 2)
    if kind == "thr":
        thr[int(step)].append(float(rest))
    else:
        cells = [c.strip() for c in rest.strip().strip("|").split("|")]
        lat[int(step)].append([float(c) for c in cells[2:]])  # mean..max
cols = ["mean", "p50", "p90", "p99", "p99.9", "p99.99", "max"]
med = lambda xs: statistics.median(xs)
env = subprocess.run(["bash", "-c", "grep -m1 'model name' /proc/cpuinfo | cut -d: -f2"],
                     capture_output=True, text=True).stdout.strip()
lines = [f"# Layer 7: before/after (median of {rounds} interleaved runs)", "",
         f"Conditions: {'AC' if power == 'ac' else 'battery'} power, governor {gov}.",
         f"CPU: {env}, pinned to one P-core. 10M messages per run, same seed.",
         "All latencies in ns and include ~11 ns of timer overhead.", "",
         "| step | " + " | ".join(cols) + " | throughput (M msg/s) |",
         "|---|" + "---:|" * (len(cols) + 1)]
for i, name in enumerate(names):
    m = [med([run[c] for run in lat[i]]) for c in range(len(cols))]
    lines.append(f"| {name} | " + " | ".join(f"{x:.0f}" for x in m) +
                 f" | {med(thr[i]):.1f} |")
open(out, "w").write("\n".join(lines) + "\n")
print("\n".join(lines))
PY
echo "saved to $OUT"
