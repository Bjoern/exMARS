# Cross-engine speed comparison, 2026-10-18

Native Windows x64, AMD Ryzen 9 5900X, GCC/G++ 16.1.0. Median seconds
from three passes; parentheses show the observed minimum–maximum, not a
confidence interval. Smaller is faster. These are simulator timings, not
Python/Ruby throughput or end-to-end CLI benchmarks.

| Engine | 94nop: `94top50` | Nano: `nanobm07` |
|---|---:|---:|
| pMARS 0.9.6-dev (local patches) | 58.934 (58.927–59.642) | 9.713 (9.679–9.876) |
| Original exMARS | 34.594 (34.537–35.566) | 7.280 (7.266–7.298) |
| Exhaust 1.9.2 | 35.283 (33.449–37.215) | 6.926 (6.904–6.926) |
| Exhaust-MA | 32.413 (32.383–33.696) | 6.679 (6.657–6.687) |
| qMARS (fixed hill constants) | 32.590 (32.260–33.957) | 6.452 (6.424–6.473) |
| fMARS 0.0.207 + diagnostic DIV/MOD fix | 25.927 (25.885–26.995) | 5.853 (5.818–5.873) |
| Updated exMARS, exact | 28.627 (27.779–29.401) | 4.897 (4.890–4.898) |
| Updated exMARS, safe early ties (default) | 28.457 (26.654–28.510) | 3.797 (3.768–3.821) |

Against original exMARS, the updated default uses **17.7% less time** on
this standard workload and **47.8% less time** on nano.
Against the locally patched pMARS reference, reductions are 51.7% and
60.9%, respectively. Exact updated exMARS uses
17.2% and 32.7% less time than original exMARS.
These are direct ratios of medians from this run; no historical gains are multiplied together.

All engines in the table match every pair’s complete win/tie/loss triple
against the pMARS reference in all three passes. This is corpus agreement,
not proof of universal interpreter equivalence. The corrected fMARS variant
is faster than updated exMARS on this standard workload; updated exMARS is
faster on nano. The benchmark does not establish one universally fastest engine.

## Workloads and qualifications

- The existing `94top50` folder actually contains 59 warriors: 1,711 non-self
  pairings, 16 placements and both starting orders, 54,752 rounds per pass.
- Nano has 50 warriors: 1,225 non-self pairings, all 71 legal placements and
  both starting orders, ten sweeps, 1,739,500 rounds per pass.
- Every engine receives the same preassembled instructions and deterministic
  placement schedule. Assembly, process launch and printing are excluded.
  Core clearing, warrior loading, execution and scoring are included.
- pMARS allocates/frees simulation buffers once per pairing inside its public
  batch API; other adapters mostly allocate outside timing. This affects nano.
- qMARS uses compile-time hill constants. Updated exMARS uses the generic
  runtime-configurable build. fMARS is generated for the instruction-mode
  union of these corpora. These implementation differences matter.
- These corpora exclude P-space instructions. Correctness fixes elsewhere
  in the patch series have their own regressions; this comparison cannot
  establish their coverage. Original exMARS still has the known bugs.
- Runs were sequential, with engine order reversed on alternating passes.
  No concurrent builds/tests were run; normal desktop background activity
  was not isolated. Three passes give limited precision.

## Original fMARS diagnostic

Uncorrected fMARS 0.0.207 is excluded from the equivalent-results table:
57/1,711 standard pairs and 68/1,225 nano pairs disagree in every pass.
Its observed times were 25.817 (25.799–26.971) seconds and
5.868 (5.850–5.877) seconds. These are not valid speed wins.
The generator reverses DIV/MOD operands and tests the wrong zero divisor.
[The reduced test](fmars_divmod_test.c), [diagnostic patch](fmars-divmod.patch)
and [test output](fmars-divmod-validation.json) are included. The separately
corrected variant agrees on both corpora; it is not an official fMARS release.

## Sources and reproduction

The reference is the user-supplied pMARS 0.9.6-dev tree with local
security/SDL publication patches, compiled in SERVER mode. It is not claimed
to be a pristine authenticated publisher release. Exact source hashes are
recorded for it and every other engine.

- [Original exMARS](https://github.com/martinus/exMARS/tree/f7dbc5ef962b4b23547b9532f1750dbf1671e8ac)
- [Exhaust-MA](https://github.com/martinus/exhaust-ma/tree/9ec4b9e721ff1d46f974c3a66d03f6fca7788aae)
- [qMARS](https://github.com/martinus/qmars/tree/090bbd4c750bfd4bc9465653142e0dff02ef141a)
- Exhaust 1.9.2 and fMARS 0.0.207 from the FreeBSD distfile mirror, checked
  against [Exhaust](https://raw.githubusercontent.com/freebsd/freebsd-ports/main/games/exhaust/distinfo)
  and [fMARS](https://raw.githubusercontent.com/freebsd/freebsd-ports/main/games/fmars/distinfo) SHA-256 records.

[Method](METHOD.md), [reproduction instructions](REPRODUCE.md),
[all pair scores and raw timings](results.json),
[corpus filenames/hashes](corpora.json), and
[source/build provenance](source-provenance.json) accompany this report.
The source snapshot is supplied beside the exported patches. Warrior sources
are not republished. Benchmark code and reporting were prepared with AI assistance.
No tournament runner was inspected or changed.
