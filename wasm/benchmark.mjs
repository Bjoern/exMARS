import {runCLI} from './client.mjs';
export const engines = ['pmars', 'original-exmars', 'exact-exmars', 'optimized-exmars'];
/** Simulation-only clock from C; full per-pair score triples retained and checked. */
export async function benchmark(input, {passes = 3, repeats = 1, onProgress = () => {}, onWarmup = () => {}} = {}) {
  if (!Number.isInteger(passes) || passes < 1 || !Number.isInteger(repeats) || repeats < 1 || repeats > 1000)
    throw new RangeError('Invalid passes/repeats');
  // The inherited adapters accept only trusted, preassembled 94nop benchmark corpora.
  const values = input.trim().split(/\s+/).map(Number);
  let at = 0;
  const take = () => { const n = values[at++]; if (!Number.isInteger(n) || n < 0) throw new Error('Invalid corpus'); return n; };
  const [core, cycles, procs, sep, count] = Array.from({length: 5}, take);
  if (![80,8000].includes(core) || cycles < 1 || cycles > 100000000 || procs < 1 || procs > 1000000 ||
      sep !== (core === 80 ? 5 : 100) || count < 2 || count > 64) throw new Error('Unsupported benchmark settings');
  for (let i=0; i<count; i++) {
    const len=take(), start=take();
    if (len < 1 || len > sep || start >= len) throw new Error('Invalid warrior');
    for (let j=0; j<len; j++) {
      const [op,mod,am,a,bm,b] = Array.from({length:6}, take);
      if (op>15 || mod>6 || am>7 || bm>7 || a>=core || b>=core) throw new Error('Invalid instruction / P-space excluded');
    }
  }
  if (at !== values.length) throw new Error('Trailing corpus data');
  const runs = [];
  let expected;
  // Untimed warm-up of each engine (recorded measurements start afterwards).
  for (const engine of engines) { onWarmup({engine}); await measure(engine, true); }
  for (let pass=0; pass<passes; pass++) {
    for (const engine of (pass%2 ? [...engines].reverse() : engines)) {
      const result = await measure(engine, false);
      runs.push({pass, engine, ...result}); onProgress(runs.at(-1));
    }
  }
  return {passes, repeats, runs, allScoresMatch: true};
  async function measure(engine, warmup) {
    const result = await runCLI('bench-'+engine, ['input.txt', String(warmup ? 1 : repeats)], {'input.txt': input});
    if (result.exitCode) throw new Error(`${engine}: ${result.stderr || result.stdout}`);
    const data = JSON.parse(result.stdout);
    const factor = warmup ? 1 : repeats;
    const pairs = count*(count-1)/2, rounds = (core===80 ? 142 : 32)*factor;
    if (data.rounds !== pairs*rounds || !Number.isFinite(data.seconds) || data.seconds < 0 ||
        data.scores.length !== pairs || data.scores.some(row => row.length !== 3 ||
          row.some(n => !Number.isSafeInteger(n) || n < 0) || row.reduce((a,b) => a+b,0) !== rounds))
      throw new Error(`Invalid benchmark result: ${engine}`);
    const normalized = JSON.stringify(data.scores.map(row => row.map(n => n/factor)));
    if (expected === undefined) expected = normalized;
    if (normalized !== expected) throw new Error(`Score mismatch: ${engine}; benchmark is not equivalent`);
    return {...data, wallSeconds: result.wallSeconds};
  }
}
