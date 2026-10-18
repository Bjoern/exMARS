"""Validate the complete timed run and regenerate its Markdown report."""
from pathlib import Path
import json
import statistics

here = Path(__file__).resolve().parent
data = json.loads((here / 'results.json').read_text())
for run in data['runs']:
    if 'score_set' in run:
        run['scores'] = data['score_sets'][run['score_set']]
engines = ['pmars', 'original-exmars', 'exhaust', 'exhaust-ma', 'qmars',
           'fmars', 'fmars-corrected', 'exact-exmars', 'optimized-exmars']
labels = ['pMARS 0.9.6-dev (local patches)', 'Original exMARS', 'Exhaust 1.9.2',
          'Exhaust-MA', 'qMARS (fixed hill constants)', 'fMARS 0.0.207 (incorrect results)',
          'fMARS 0.0.207 + diagnostic DIV/MOD fix', 'Updated exMARS, exact',
          'Updated exMARS, safe early ties (default)']
assert data['passes'] == 3 and data['nano_repeats'] == 10
assert len(data['runs']) == 54, 'Wait for all timed runs to finish'
stats = {}
for suite, pairs, rounds in [('94top50', 1711, 54752), ('nanobm07', 1225, 1739500)]:
    reference = next(r['scores'] for r in data['runs'] if r['suite'] == suite and r['engine'] == 'pmars')
    for engine in engines:
        rows = [r for r in data['runs'] if r['suite'] == suite and r['engine'] == engine]
        assert len(rows) == 3 and sorted(r['trial'] for r in rows) == [0, 1, 2]
        for r in rows:
            assert 'error' not in r and r.get('exit_code', 0) == 0
            assert r['seconds'] > 0 and r['rounds'] == rounds and len(r['scores']) == pairs
            assert all(sum(s) == rounds // pairs for s in r['scores'])
            mismatches = [i for i, (x, y) in enumerate(zip(reference, r['scores'])) if x != y]
            assert mismatches == r['mismatching_pairs']
            assert len(mismatches) == ((57 if suite == '94top50' else 68) if engine == 'fmars' else 0)
        times = [r['seconds'] for r in rows]
        stats[suite, engine] = statistics.median(times), min(times), max(times)

def cell(suite, engine):
    median, low, high = stats[suite, engine]
    return f'{median:.3f} ({low:.3f}–{high:.3f})'

def reduction(suite, baseline, updated='optimized-exmars'):
    return 100 * (1 - stats[suite, updated][0] / stats[suite, baseline][0])

lines = ['# Cross-engine speed comparison, 2026-10-18', '',
    'Native Windows x64, AMD Ryzen 9 5900X, GCC/G++ 16.1.0. Median seconds',
    'from three passes; parentheses show the observed minimum–maximum, not a',
    'confidence interval. Smaller is faster. These are simulator timings, not',
    'Python/Ruby throughput or end-to-end CLI benchmarks.', '',
    '| Engine | 94nop: `94top50` | Nano: `nanobm07` |',
    '|---|---:|---:|']
for engine, label in zip(engines, labels):
    if engine != 'fmars':
        lines.append(f'| {label} | {cell("94top50", engine)} | {cell("nanobm07", engine)} |')
lines += ['',
    f'Against original exMARS, the updated default uses **{reduction("94top50", "original-exmars"):.1f}% less time** on',
    f'this standard workload and **{reduction("nanobm07", "original-exmars"):.1f}% less time** on nano.',
    f'Against the locally patched pMARS reference, reductions are {reduction("94top50", "pmars"):.1f}% and',
    f'{reduction("nanobm07", "pmars"):.1f}%, respectively. Exact updated exMARS uses',
    f'{reduction("94top50", "original-exmars", "exact-exmars"):.1f}% and {reduction("nanobm07", "original-exmars", "exact-exmars"):.1f}% less time than original exMARS.',
    'These are direct ratios of medians from this run; no historical gains are multiplied together.', '',
    'All engines in the table match every pair’s complete win/tie/loss triple',
    'against the pMARS reference in all three passes. This is corpus agreement,',
    'not proof of universal interpreter equivalence. The corrected fMARS variant',
    'is faster than updated exMARS on this standard workload; updated exMARS is',
    'faster on nano. The benchmark does not establish one universally fastest engine.', '',
    '## Workloads and qualifications', '',
    '- The existing `94top50` folder actually contains 59 warriors: 1,711 non-self',
    '  pairings, 16 placements and both starting orders, 54,752 rounds per pass.',
    '- Nano has 50 warriors: 1,225 non-self pairings, all 71 legal placements and',
    '  both starting orders, ten sweeps, 1,739,500 rounds per pass.',
    '- Every engine receives the same preassembled instructions and deterministic',
    '  placement schedule. Assembly, process launch and printing are excluded.',
    '  Core clearing, warrior loading, execution and scoring are included.',
    '- pMARS allocates/frees simulation buffers once per pairing inside its public',
    '  batch API; other adapters mostly allocate outside timing. This affects nano.',
    '- qMARS uses compile-time hill constants. Updated exMARS uses the generic',
    '  runtime-configurable build. fMARS is generated for the instruction-mode',
    '  union of these corpora. These implementation differences matter.',
    '- These corpora exclude P-space instructions. Correctness fixes elsewhere',
    '  in the patch series have their own regressions; this comparison cannot',
    '  establish their coverage. Original exMARS still has the known bugs.',
    '- Runs were sequential, with engine order reversed on alternating passes.',
    '  No concurrent builds/tests were run; normal desktop background activity',
    '  was not isolated. Three passes give limited precision.', '',
    '## Original fMARS diagnostic', '',
    'Uncorrected fMARS 0.0.207 is excluded from the equivalent-results table:',
    '57/1,711 standard pairs and 68/1,225 nano pairs disagree in every pass.',
    f'Its observed times were {cell("94top50", "fmars")} seconds and',
    f'{cell("nanobm07", "fmars")} seconds. These are not valid speed wins.',
    'The generator reverses DIV/MOD operands and tests the wrong zero divisor.',
    '[The reduced test](fmars_divmod_test.c), [diagnostic patch](fmars-divmod.patch)',
    'and [test output](fmars-divmod-validation.json) are included. The separately',
    'corrected variant agrees on both corpora; it is not an official fMARS release.', '',
    '## Sources and reproduction', '',
    'The reference is the user-supplied pMARS 0.9.6-dev tree with local',
    'security/SDL publication patches, compiled in SERVER mode. It is not claimed',
    'to be a pristine authenticated publisher release. Exact source hashes are',
    'recorded for it and every other engine.', '',
    '- [Original exMARS](https://github.com/martinus/exMARS/tree/f7dbc5ef962b4b23547b9532f1750dbf1671e8ac)',
    '- [Exhaust-MA](https://github.com/martinus/exhaust-ma/tree/9ec4b9e721ff1d46f974c3a66d03f6fca7788aae)',
    '- [qMARS](https://github.com/martinus/qmars/tree/090bbd4c750bfd4bc9465653142e0dff02ef141a)',
    '- Exhaust 1.9.2 and fMARS 0.0.207 from the FreeBSD distfile mirror, checked',
    '  against [Exhaust](https://raw.githubusercontent.com/freebsd/freebsd-ports/main/games/exhaust/distinfo)',
    '  and [fMARS](https://raw.githubusercontent.com/freebsd/freebsd-ports/main/games/fmars/distinfo) SHA-256 records.', '',
    '[Method](METHOD.md), [reproduction instructions](REPRODUCE.md),',
    '[all pair scores and raw timings](results.json),',
    '[corpus filenames/hashes](corpora.json), and',
    '[source/build provenance](source-provenance.json) accompany this report.',
    'The source snapshot is supplied beside the exported patches. Warrior sources',
    'are not republished. Benchmark code and reporting were prepared with AI assistance.',
    'No tournament runner was inspected or changed.', '']
(here / 'README.md').write_text('\n'.join(lines), encoding='utf-8')
print('Validated 54 runs; wrote', here / 'README.md')
