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

`make wasm-release` then copies the persistent fighter's files (`fighter.mjs`,
`fighter.wasm`, `assembler.mjs`, `assembler.wasm`, `fighter-client.mjs`), a
source zip and a `manifest.json` with their SHA-256 hashes into the tracked
`wasm/release/` folder, for sites that vendor built files with pinned hashes.

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
| `fighter.mjs` | Persistent two-warrior fighter (`wasm/fighter.c` + `sim.c` + `pspace.c`, no parser) |
| `assembler.mjs` | Redcode to instruction tuples (`wasm/assembler.c` + pMARS parser), one instance per call |

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

## Persistent fighter

For callers that fight the same warriors many times, such as an optimizer:
one Wasm instance stays loaded, warriors are assembled once and loaded as
handles, and each `fight` starts a fresh match in C. No MEMFS, no stdout
parsing, no runtime per match. It is the Python binding's API (`parse_string`,
`rounds`, `fixed_position`, `fight()`), in `fighter-client.mjs`:

```js
import {assemble, createFighter} from './fighter-client.mjs';

const hill = {coreSize: 80, cycles: 800, processes: 80, maxLength: 5, minDistance: 5};
const w = await assemble(source, hill);
// -> {ok: true, instructions: [[op, mod, am, a, bm, b], ...], start, pin|null, name, author, warnings: [...]}
//  | {ok: false, errors: '...', warnings: '...'}

const f = await createFighter(hill);   // one per Web Worker, kept for the whole run
const h = f.load(w);                     // handle; throws on invalid input
f.fight(h, h2, {rounds: 250, position: 1234});  // -> {wins, ties, losses} of h
f.exact(h, h2);                          // -> {wins, ties, losses, rounds}
f.unload(h);
f.configure(otherHill);                  // drops all handles
f.setEarlyExit(false);                   // optional; safe early ties are on by default
f.dispose();
```

- `hill` takes exactly these five integers. Rules as `runMatch`: `maxLength ≤
  minDistance`, `2·minDistance ≤ coreSize`, `coreSize ≤ 65535`, `processes ≤
  10^6`, `cycles ≤ 10^8`. Sources are limited to 64 KiB.
- `fight`: one match of `rounds` (1..10^6) rounds, exactly as the Python
  binding's `fight()`: P-space and PIN sharing persist between the rounds of a
  match and reset between matches; the starting order alternates each round.
  `position` has the meaning of the CLI's `-F` and the binding's
  `fixed_position`: a placement seed (0 = random), not one fixed position. A given
  (position, rounds) gives pMARS's placements and starting orders round for
  round. Non-zero values must lie in `minDistance..coreSize−minDistance`.
- `exact`: every legal offset `p` of `h2` from `minDistance` to
  `coreSize−minDistance`, one round with `h` starting (`h` at 0, `h2` at `p`)
  and one with `h2` starting (`h2` at 0, `h` at `coreSize−p`), each a fresh
  match; `rounds = 2·(coreSize−2·minDistance+1)` (142 on nano). Totals equal
  pMARS's permutation mode (`-P -r rounds`) for warriors without P-space.
- `assemble(source, hill, {rounds})` runs the pMARS parser in a fresh,
  throwaway assembler instance, because the parser can `exit()` (more than
  `ERRMAX` = 9 errors, out of memory). `CORESIZE`, `MAXLENGTH`, `PSPACESIZE`
  and the other predefined constants and `;assert` follow `hill`. `ROUNDS`
  is the optional `rounds` (default 1, as the Python binding). `warnings` is an
  array of pMARS warning texts on success; on failure `errors` and `warnings`
  are texts. Besides parser errors, it fails for warriors the fighter can't run:
  no instructions, more than `maxLength`, or a start outside the code (pMARS
  only warns about the last one). An assembly takes about 3 ms.
- Errors (invalid handle, settings or tuples) throw `Error`/`RangeError`; the
  instance stays usable. A rejected `configure` keeps the previous settings.

### Instruction encoding

Instructions are `[opcode, modifier, aMode, aValue, bMode, bValue]`, the
Python binding's tuples (`Warrior.py`, `insn.h` enums, the `OP()` macro).
`load` reduces values modulo `coreSize`; `assemble` returns them as the parser
produced them (0..coreSize−1).

| Field | Values |
|---|---|
| opcode | 0 DAT, 1 SPL, 2 MOV, 3 DJN, 4 ADD, 5 JMZ, 6 SUB, 7 SEQ (= CMP), 8 SNE, 9 SLT, 10 JMN, 11 JMP, 12 NOP, 13 MUL, 14 MOD, 15 DIV, 16 LDP, 17 STP |
| modifier | 0 .F, 1 .A, 2 .B, 3 .AB, 4 .BA, 5 .X, 6 .I |
| mode | 0 `$`, 1 `#`, 2 `@`, 3 `<`, 4 `>`, 5 `*`, 6 `{`, 7 `}` |

### Limits

The fighter takes only the five settings above, so these can't be set by
accident; use pMARS for hills that need them: no ICWS '88 mode, no free
permutation toggle (`exact` covers exhaustive scoring), P-space size fixed at
`coreSize/16`, 3/1/0 scoring only (the API returns counts), exactly two
warriors.

### C API

`fighter.mjs` exports `_ex_configure(coreSize, cycles, processes, maxLength,
minDistance)`, `_ex_load(int32 *insns, count, start, hasPin, pin)` → handle,
`_ex_unload(handle)`, `_ex_fight(a, b, rounds, fixedPosition, int32 out[3])`,
`_ex_fight_exact(a, b, int32 out[3])`, `_ex_set_early_exit(on)` and
`_ex_error()`, plus `_malloc`/`_free` and `HEAPU8`/`HEAP32`/`UTF8ToString`.
Status < 0 means an error; nothing on this path calls `exit()`. The handle
table grows as needed. `assembler.mjs` exports `_ex_assemble(source, coreSize,
cycles, processes, maxLength, minDistance, rounds)` with `_ex_result()`,
`_ex_name()`, `_ex_author()` and `_ex_message()`; see `wasm/assembler.c`.

### Tests and throughput

`node wasm/test-fighter.mjs dist/wasm` (part of `make wasm-test`) needs the
pMARS comparison build, the corpora under `warriors/benchmark/` (`nanobm07`,
`tinybm04`, `psptop50`, `94top50`, `lp_koenigstuhl`; not committed, override
with `EXMARS_CORPUS`) and the native Python binding, which it builds into
`build/python` if needed. Missing inputs are reported as `SKIP`;
`EXMARS_FIGHTER_STRICT=1` turns them into failures. It checks `fight` against
`runMatch` on exMARS and pMARS for random pairs, seeds and round counts in both
orders; `exact` against single rounds and pMARS `-P`; 1000 mixed calls on one
instance against fresh instances, including P-space/PIN warriors, failed
loads, unload and reconfiguration; and `assemble` against the Python binding on
all 336 corpus sources.

Throughput, 2026-11-04, Linux x86_64 cloud VM, Node 22.23 (headless Chromium
gave the same nano and standard numbers). The `runMatch` columns are this
machine; the handoff's M1 figures were 1.7–1.8 ms per 1-round nano match and
0.39 ms per standard round, so this VM is several times slower:

| Workload | Fighter | `runMatch` |
|---|---|---|
| Nano exact, 1 candidate vs 50 `nanobm07` opponents (7100 rounds) | 21–25 ms | ≈ 8–10 ms per 1-round match, ≈ 60 s with a runtime per round |
| Standard, 1 candidate vs 20 `94top50` opponents, 250 rounds | 2.9–3.5 s (≈ 150 ms per match) | 160–220 ms per 250-round match |
| `assemble` | ≈ 3 ms per source | |

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
`browser-smoke.html`. It runs all three match engines, all four benchmark
engines and the persistent fighter (`assemble`, `fight`, `exact`) inside a
module Worker (`worker.mjs` accepts `method: 'fighter'` for this). Run full browser benchmarks separately from
performance-sensitive work.
