# Cross-engine benchmark method

Measured on this Windows x64 machine with GCC/G++ 16.1.0, an AMD Ryzen 9 5900X,
`-O3 -fomit-frame-pointer`, no target-specific tuning or LTO. fMARS additionally
uses its documented `-fno-strict-aliasing` requirement. All engines run natively
on Windows; Ubuntu/Guile is used only to generate fMARS C sources, outside timing.

## Workloads and timing

- `94top50`: 59 warriors, all 1,711 unordered non-self pairings, core/process
  limit 8000, 80000 cycles per warrior, length/separation 100. Each pairing uses
  16 deterministic distinct legal placements, both starting orders: 54,752
  rounds per timed pass. Positions are `100 + ((k*487+3900) % 7801)`, k=0..15.
- `nanobm07`: 50 warriors, all 1,225 unordered non-self pairings, core/process
  limit 80, 800 cycles per warrior, length/separation 5. Every legal position
  5..75, both starting orders, ten sweeps: 1,739,500 rounds per timed pass.
- Three passes, reversing engine execution order on alternating passes.
  Report the median and range, not the minimum. Sources/builds and validation
  runs precede timing; no competing build/test/benchmark during timed passes.
  The desktop is not otherwise isolated from background activity.

Input is assembled once by the verified exMARS parser into canonical numeric
fields. Each adapter converts explicitly to its engine's representation before
timing; the original Exhaust structure puts its opcode first, unlike exMARS.
Hashes identify original warrior files, assembled inputs, sources and binaries.
P-space opcodes are rejected: qMARS/fMARS are not full P-space comparators.

Timers are native QueryPerformanceCounter calls around the entire batch of
rounds. Core clearing, warrior loading, process queues, simulation and score
accumulation are included; assembly, file I/O, process startup and printing are
excluded. Most adapters allocate/cache outside timing. pMARS's public batch
routine allocates/frees core and queue memory once per pairing, within timing;
this is part of the measured pMARS API cost and can matter on nano workloads.

The exMARS/Exhaust adapters use their unmodified simulator entry points. pMARS
has only its placement expression replaced by the supplied schedule; qMARS has
its round-placement routine replaced and its maximum-round clamp disabled.
qMARS also needs an obsolete constructor qualification removed and a missing
cstdlib include supplied for the modern compiler. Interpreter instruction
logic is unchanged in those adapters. qMARS has compile-time hill constants;
this exMARS comparison uses the generic runtime-configurable engine.

## Correctness gate

Compare **every pair's complete win/tie/loss triple** against pMARS, for every
pass. Scores must also sum to the requested round count. A fast engine with
mismatches is excluded from the equivalent-results ranking.

The pMARS reference is the user's existing **locally patched 0.9.6-dev** tree,
compiled with `EXT94`, `PERMUTATE`, `RWLIMIT`, `SERVER`; it is not claimed to be a
pristine authenticated publisher release. `source-provenance.json` records the
exact source hashes. The same reference underpins the earlier compatibility
investigation. This corpus agreement does not prove universal equivalence.

fMARS 0.0.207 generates a separate handler for each op/modifier/mode combination.
It is generated with the union needed by both corpora (including the zero DAT
cell), with its supplied configuration. Only the removed Guile `ice-9 syncase`
import is removed to run the generator on Guile 3.0.9.

The release generator reverses DIV/MOD source and destination and checks the
wrong operand for zero. A reduced native test gives DIV 0,0 instead of 3,5 and
MOD 6,3 instead of 2,2. `fmars-divmod.patch` corrects this in a separate source
copy; the release and corrected variant are labeled separately throughout.
The diagnostic correction is not represented as an upstream fMARS release.

Earlier development validation found and corrected an adapter error in the
original Exhaust field order. Those exploratory runs are not timing samples
and are not included here. Only `results.json` supplies the final table.

No tournament runner was inspected or modified. These are simulator benchmark
adapters, not changes to scheduling or multicore execution in a runner.
