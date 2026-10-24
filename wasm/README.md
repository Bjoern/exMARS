# exMARS WebAssembly

Browser ES modules, module Workers and Node.js; no server execution, threads,
SharedArrayBuffer or COOP/COEP requirement. The production API runs two-warrior
matches and round-robin tournaments from Redcode source strings. P-space/PIN
persists across rounds within a match and resets between matches.

## Build

Use Emscripten 4.0.23 (the tested version) with its environment activated:

```sh
source /path/to/emsdk/emsdk_env.sh
make wasm PMARS_SOURCE=../pmars_experiments/src
make wasm-test
```

Omit `PMARS_SOURCE` to build exMARS alone. The full test and comparative benchmark
require pMARS. Outputs go to `dist/wasm`, isolated from native builds. The builder
extracts original exMARS at `f7dbc5ef962b4b23547b9532f1750dbf1671e8ac` from local
Git history. For a shallow/source-only checkout, pass `--original-source PATH`
to `python3 wasm/build.py`. Supply that revision's source, not the optimized fork.
`manifest.json` records compiler, commands, source hashes and artifact hashes.

| Module | Purpose |
|---|---|
| `exmars.mjs` | Current optimized assembler and simulator; safe early ties default |
| `reference-exmars.mjs` | Current correctness fixes, reference interpreter (`EXMARS_REFERENCE`) |
| `pmars.mjs` | Local pMARS SERVER build, including assembler |
| `bench-optimized-exmars.mjs` | Optimized simulation with safe early ties |
| `bench-exact-exmars.mjs` | Optimized simulation with early ties disabled |
| `bench-original-exmars.mjs` | Actual original upstream simulator, including known bugs |
| `bench-pmars.mjs` | pMARS simulator with deterministic benchmark placements |

Each generated module has a same-named `.wasm` companion. The reference module
is a corrected baseline, not original upstream. `earlyExit: false` disables early
ties but keeps queue/MOV optimizations. Use the original benchmark module for the
historical comparison. Original exMARS is not offered as a production tournament
engine because its known correctness defects remain.

## corewar.dev integration

Copy `dist/wasm/` under a static path, e.g. `/engines/exmars/`. Serve `.mjs` with
JavaScript MIME type and `.wasm` with `application/wasm`. Keep companions beside
the generated modules; their URLs resolve relative to `import.meta.url`. This
package does not modify or publish the corewar.dev site.

```js
const worker = new Worker('/engines/exmars/worker.mjs', {type: 'module'});
worker.onmessage = ({data}) => {
  if (data.error) console.error(data.error);
  else if (data.progress) console.log(data.progress);
  else console.log(data.result.standings, data.result.matches);
};
worker.postMessage({
  id: 'hill-1', method: 'tournament',
  warriors: [impSource, dwarfSource, paperSource], // Redcode strings
  options: {rounds: 100, coreSize: 8000, cycles: 80000,
    processes: 8000, maxLength: 100, minDistance: 100, position: 4000}
});
// Stop immediately, even inside the C simulator or assembler:
// worker.terminate();
```

Use `method: 'match'` with exactly two sources for `{wins, ties, losses, rounds}`.
Optional `engine` is `exmars` (default), `reference-exmars`, or `pmars`.
`position` maps to CLI `-F`: it seeds deterministic placements, rather than fixing
one position for every round. The engine alternates first execution each round;
even round counts balance starting order. Standings score 3 per win, 1 per tie.
All unordered non-self pairings are played once, in input order. This is a
round-robin runner, not a persistent hill database or ranking policy.

Node or existing Workers can import `runMatch`/`runTournament` from `client.mjs`
directly. `runCLI(engine, args, files)` exposes the full CLI with an in-memory,
flat filename map; it returns stdout, stderr, exitCode and wallSeconds. The
low-level CLI leaves argument validation to the original engines. Prefer the
validated match API for site inputs (64 KiB per source), and enforce application
limits on rounds and job duration. A worker handles one job at a time. Terminate it
for cancellation/timeouts; progress is emitted between matches, not per cycle.

Fresh runtimes avoid retained parser globals and match-to-match P-space. This
costs instantiation/assembly time per pairing; tournament throughput therefore
differs from the simulator-only benchmark below. `wallSeconds` includes assembly
and simulation but excludes module initialization and virtual-file setup.

## Fair simulator benchmarks

Prepare identical preassembled inputs with the existing native Python binding:

```sh
python3 benchmarks/comparison/assemble.py --corpus-root warriors/benchmark --module-dir .
node wasm/run-benchmarks.mjs dist/wasm 3 1 build/wasm-results.json
```

The arguments are build directory, measured passes, repetitions per pairing,
and a **new** output filename. The script verifies every score triple against
pMARS, warms up every engine, reverses engine order on alternate passes, and
records all scores, raw timings, runtime/CPU, input hashes and build manifest.
A disagreement fails the run instead of declaring the faster engine a winner.
For a browser measurement, send `{id, method: 'benchmark', input: corpusText,
options: {passes: 3, repeats: 1}}` to the same Worker. Results/progress have the
same format. Load corpus text from your own hosted assets; warrior corpora are
not included in the deployment package.

C timers cover clearing, loading, simulation and scoring. They exclude assembly,
WASM initialization and output formatting. pMARS allocates per pair; exMARS
allocates once. Inputs are restricted to the original 8000/80-cell 94nop suites;
P-space is excluded. Standard uses 16 placements in both orders; nano uses all
71 legal placements in both orders. See `benchmarks/comparison/METHOD.md` for
the inherited method. Cross-corpus agreement does not prove universal engine
correctness; the separate CLI regressions cover PIN/P-space and arithmetic.

Benchmark results depend on browser/JIT/device and warm-up. Node numbers are
Node numbers, not measurements on corewar.dev. Run in the target browser before
choosing a production engine. Deployment of pMARS must retain its COPYING and
source obligations; the generated package includes source snapshots/notices.

### Existing site API boundary

Inspected the public `engine.mjs`, `settings.mjs` and `tournament-worker.mjs` on
2026-10-23. The site currently calls custom `_web_*` exports and transfers pMARS
instruction banks. Those banks and stepping/debug APIs are not compatible with
this CLI-backed module. Keep the existing visual engine; route headless match
jobs to this Worker using Redcode sources. Map `tasks` → `processes`,
`distance || maxLength` → `minDistance`, and numeric `fixedPosition` → `position`.
Map result counts back to the site's two `warriors` rows, swapping wins/losses
for the second row and computing `score = 3*wins + ties`.

The high-level API supports ICWS '94, two warriors, automatic P-space size and
3/1/0 scoring. It does not implement the site's '88 mode, permutation toggle,
custom P-space size, arbitrary score formulas, no-P-space submission enforcement,
or the site's random seed string convention. Do not silently ignore those
settings when integrating. The benchmark's exhaustive nano schedule is separate
from the production match API. The pMARS comparison here is rebuilt from the
local `pmars_experiments/src` tree, not the site's deployed custom WASM binary.

To verify actual browser loading, serve `dist/wasm` over HTTP and visit
`browser-smoke.html`. It runs all three match engines and all four benchmark
engines inside a module Worker. Run full browser benchmarks separately from
performance-sensitive work.
