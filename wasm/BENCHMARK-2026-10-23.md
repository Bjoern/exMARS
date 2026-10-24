# WebAssembly comparison on Apple M1, 2026-10-23

All four engines agree on every pair's complete win/tie/loss triple in all
24 measured runs and eight warm-ups. The full comparison ran in Node v22.22.2
on macOS arm64 using Emscripten 4.0.23, generic runtime hill settings, `-O3`.
These are not browser timings or measurements of the deployed corewar.dev
pMARS binary. That site has a separate custom stepping interface.

Median seconds across three measured passes; parentheses give observed
minimum–maximum. No outliers have been removed.

| Engine | 94top50 seconds | Nano seconds |
|---|---:|---:|
| pMARS (local source) | 84.698 (63.368–86.333) | 1.162 (1.144–1.533) |
| Original exMARS | 41.095 (40.899–191.861) | 0.720 (0.717–0.777) |
| Optimized exMARS, exact | 37.970 (36.927–220.471) | 0.637 (0.628–0.640) |
| Optimized exMARS, early ties | 39.289 (30.563–40.853) | 0.535 (0.395–0.546) |

**Standard timing is provisional:** original and exact exMARS have extreme
third-pass outliers. The cause was not isolated. The medians suggest approximately
2.16× throughput versus pMARS and 1.05× versus original for default exMARS, but
this run cannot establish a small optimization gain reliably. Exact mode's
median is lower than default early-tie mode on standard.

Nano medians give default exMARS approximately **2.17×** the throughput of pMARS
and **1.35×** original exMARS. There is still timing variation; this is a small,
single-machine sample. Do not extrapolate it to every browser or workload.

## Workload and accounting

- Standard: 59 warriors, 1,711 unordered non-self pairs, 16 deterministic
  placements and both execution orders: 54,752 rounds per engine/pass.
- Nano: 50 warriors, 1,225 pairs, all 71 legal placements and both execution
  orders: 173,950 rounds per engine/pass. This is **one** sweep, whereas the
  previous native report used ten; do not compare those seconds directly.
- Same preassembled inputs as the existing comparison, verified by its source
  and serialized-input hashes. No P-space instructions in these corpora.
- A full warm-up for every engine/corpus precedes measurements. Engine order
  reverses on alternate measured passes. Runs execute sequentially.
- C monotonic timers include clearing, loading, simulation and scoring, but not
  assembly, module initialization, parsing benchmark input or printing results.
  pMARS allocates simulation buffers per pair; exMARS allocates once.
- No other builds or tests were run during these measurements. Ordinary desktop
  background activity was not controlled. Outlier cause remains unknown.
- The original source is upstream commit
  `f7dbc5ef962b4b23547b9532f1750dbf1671e8ac`, retaining known correctness bugs.
  The wasm32 ABI has 32-bit unsigned long, so the LP64 allocation repair used
  in the native M1 comparison is unnecessary here.
- Only pMARS's benchmark placement assignment is changed; its interpreter is
  retained. The production pMARS CLI module has no placement patch.

## Validation and artifacts

The six existing CLI regressions pass in optimized, exact, corrected-reference
and pMARS WASM: seed zero, expression parsing, immediate-B side effects,
immediate-B STP, shared PIN and wide-field arithmetic. Additional checks cover
round-robin standings, invalid inputs, fresh runtime after failure, argument
immutability and preserving Node exit status. The shared benchmark adapter now
reads a death-table entry only when a warrior has died, avoiding an uninitialized
read on tied rounds.

A localhost browser module-Worker smoke test passed for all three production
match modules and all four comparison modules. Full performance measurements
were only run in Node; browser-specific benchmarking remains a deployment step.

[Raw results](results-node-m1-2026-10-23.json) retain all measured pair scores,
source and input hashes, exact build commands and artifact hashes. That manifest
captures the measured build; the packaged JS wrapper subsequently received
input-validation and Node-exit-status fixes. Simulator binaries are unchanged.

[Build/integration guide](README.md) and `run-benchmarks.mjs` reproduce the run.
Use `python3 wasm/summarize.py PATH_TO_RESULTS` to print the timing table.
