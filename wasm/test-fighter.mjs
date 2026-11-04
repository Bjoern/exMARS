// Tests for the persistent fighter and the assembler module (fighter-client.mjs).
//   node wasm/test-fighter.mjs [dist/wasm]
// Needs the pMARS comparison build (make wasm PMARS_SOURCE=...), the benchmark
// corpora in warriors/benchmark (or $EXMARS_CORPUS) and, for the assembler
// comparison, the native Python binding (built into build/python if missing).
// Missing optional inputs are reported as SKIP; EXMARS_FIGHTER_STRICT=1 fails instead.
import assert from 'node:assert/strict';
import {existsSync, readdirSync, readFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve, join} from 'node:path';
import {pathToFileURL, fileURLToPath} from 'node:url';

const ROOT = resolve(fileURLToPath(import.meta.url), '../..');
const build = resolve(process.argv[2] ?? 'dist/wasm');
const corpusRoot = resolve(process.env.EXMARS_CORPUS ?? join(ROOT, 'warriors/benchmark'));
const strict = process.env.EXMARS_FIGHTER_STRICT === '1';
const {assemble, createFighter} = await import(pathToFileURL(join(build, 'fighter-client.mjs')));
const {runCLI, runMatch} = await import(pathToFileURL(join(build, 'client.mjs')));
const havePmars = existsSync(join(build, 'pmars.mjs'));

function skip(what) {
  if (strict) throw new Error(`Missing for strict run: ${what}`);
  console.log(`SKIP ${what}`);
}

// Deterministic PRNG so failures reproduce.
let state = 0x2545f491;
const rand = n => { state ^= state << 13; state ^= state >>> 17; state ^= state << 5; return (state >>> 0) % n; };
const pick = list => list[rand(list.length)];

const HILLS = {
  nano: {dir: 'nanobm07', hill: {coreSize: 80, cycles: 800, processes: 80, maxLength: 5, minDistance: 5}},
  tiny: {dir: 'tinybm04', hill: {coreSize: 800, cycles: 8000, processes: 800, maxLength: 20, minDistance: 20}},
  standard: {dir: 'psptop50', hill: {coreSize: 8000, cycles: 80000, processes: 8000, maxLength: 100, minDistance: 100}},
  '94nop': {dir: '94top50', hill: {coreSize: 8000, cycles: 80000, processes: 8000, maxLength: 100, minDistance: 100}},
  lp: {dir: 'lp_koenigstuhl', hill: {coreSize: 8000, cycles: 80000, processes: 8, maxLength: 200, minDistance: 200}},
};
const triple = r => [r.wins, r.ties, r.losses];
const legal = h => h.minDistance + rand(h.coreSize - 2 * h.minDistance + 1);

function corpus(name) {
  const dir = join(corpusRoot, HILLS[name].dir);
  if (!existsSync(dir)) return null;
  return readdirSync(dir).filter(f => /\.red$/i.test(f)).sort()
    .map(file => ({file, source: readFileSync(join(dir, file), 'latin1')}));
}
const corpora = Object.fromEntries(Object.keys(HILLS).map(n => [n, corpus(n)]));
const available = Object.keys(HILLS).filter(n => corpora[n]);
if (available.length < Object.keys(HILLS).length)
  skip(`corpora missing under ${corpusRoot}: ${Object.keys(HILLS).filter(n => !corpora[n]).join(', ')}`);

// Assembly cache per (hill, file, rounds): ROUNDS is a predefined constant.
const asmCache = new Map();
async function asm(name, w, rounds = 1) {
  const key = `${name}/${w.file}/${rounds}`;
  if (!asmCache.has(key)) asmCache.set(key, await assemble(w.source, HILLS[name].hill, {rounds}));
  return asmCache.get(key);
}
const fightable = {};
for (const name of available) {
  fightable[name] = [];
  for (const w of corpora[name]) if ((await asm(name, w)).ok) fightable[name].push(w);
  assert.ok(fightable[name].length >= 10, `${name}: too few warriors assemble`);
}

// --- 1. Same results as the CLI engines ------------------------------------
for (const name of available) {
  const {hill} = HILLS[name];
  const f = await createFighter(hill);
  const pairs = name === 'nano' || name === 'tiny' ? 8 : 5;
  let checked = 0;
  for (let i = 0; i < pairs; i++) {
    const A = pick(fightable[name]), B = pick(fightable[name]);
    for (const rounds of [1, 2, 7, name === 'nano' || name === 'tiny' ? 60 : 16]) {
      const position = legal(hill);
      const a = await asm(name, A, rounds), b = await asm(name, B, rounds);
      if (!a.ok || !b.ok) continue;
      const ha = f.load(a), hb = f.load(b);
      for (const [x, y, hx, hy] of [[A, B, ha, hb], [B, A, hb, ha]]) {
        const got = triple(f.fight(hx, hy, {rounds, position}));
        assert.equal(got[0] + got[1] + got[2], rounds);
        for (const engine of ['exmars', ...(havePmars ? ['pmars'] : [])]) {
          const want = await runMatch([x.source, y.source], {...hill, rounds, position, engine});
          assert.deepEqual(got, triple(want), `${name} ${x.file} vs ${y.file} r=${rounds} F=${position} ${engine}`);
        }
        checked++;
      }
      f.unload(ha); f.unload(hb);
    }
  }
  // Random placement (position 0) still plays every round.
  const h1 = f.load(await asm(name, fightable[name][0])), h2 = f.load(await asm(name, fightable[name][1]));
  const r = f.fight(h1, h2, {rounds: 9});
  assert.equal(r.wins + r.ties + r.losses, 9);
  f.dispose();
  console.log(`ok ${name}: ${checked} matches equal exMARS CLI${havePmars ? ' and pMARS' : ''} round for round`);
}
if (!havePmars) skip('pMARS comparisons (build with PMARS_SOURCE)');

// --- 2. Exact = sum of single rounds = pMARS permutation mode --------------
for (const name of ['nano', 'tiny'].filter(n => corpora[n])) {
  const {hill} = HILLS[name], {coreSize: c, minDistance: d} = hill;
  const rounds = 2 * (c - 2 * d + 1);
  const f = await createFighter(hill);
  const pairs = name === 'nano' ? 10 : 3;
  for (let i = 0; i < pairs; i++) {
    const A = pick(fightable[name]), B = pick(fightable[name]);
    const ha = f.load(await asm(name, A)), hb = f.load(await asm(name, B));
    const exact = f.exact(ha, hb);
    assert.equal(exact.rounds, rounds);
    assert.equal(exact.wins + exact.ties + exact.losses, rounds);
    const sum = [0, 0, 0];
    for (let p = d; p <= c - d; p++) {
      const fwd = triple(f.fight(ha, hb, {rounds: 1, position: p}));
      const back = triple(f.fight(hb, ha, {rounds: 1, position: (c - p) % c}));
      sum[0] += fwd[0] + back[2]; sum[1] += fwd[1] + back[1]; sum[2] += fwd[2] + back[0];
    }
    assert.deepEqual(triple(exact), sum, `${name} exact vs single rounds ${A.file} ${B.file}`);
    if (havePmars) {
      const r = await runCLI('pmars', ['-b', '-k', '-P', '-r', '' + rounds, '-s', '' + c, '-c', '' + hill.cycles,
        '-p', '' + hill.processes, '-l', '' + hill.maxLength, '-d', '' + d, 'a.red', 'b.red'],
        {'a.red': A.source, 'b.red': B.source});
      assert.equal(r.exitCode, 0, r.stderr);
      const [[wa, ta], [wb, tb]] = r.stdout.trim().split('\n').slice(-2).map(s => s.trim().split(/\s+/).map(Number));
      assert.equal(ta, tb);
      assert.deepEqual(triple(exact), [wa, ta, wb], `${name} exact vs pMARS -P ${A.file} ${B.file}`);
    }
    f.unload(ha); f.unload(hb);
  }
  f.dispose();
  console.log(`ok ${name}: exact (${rounds} rounds) = sum of single rounds${havePmars ? ' = pMARS -P' : ''}, ${pairs} pairs`);
}

// --- 3. Reuse: one instance = fresh instance per call ----------------------
const fixture = f => readFileSync(join(ROOT, 'tests/pmars_compat', f), 'utf8');
const idle = ';redcode-94\n;assert 1\njmp 0\nend\n';
// Dies in a match's first round after writing P-space, survives later rounds.
const learner = ';redcode-94\n;name learner\n;assert 1\n        ldp.ab #1, flag\n        jmn.b live, flag\n' +
  '        stp.ab #1, #1\n        dat 0, 0\nlive    jmp 0\nflag    dat 0, 0\nend\n';
const standardHill = HILLS.standard.hill;
{
  const f = await createFighter(standardHill);
  const [hl, hi] = [f.load(await assemble(learner, standardHill)), f.load(await assemble(idle, standardHill))];
  for (let k = 0; k < 3; k++) assert.deepEqual(triple(f.fight(hl, hi, {rounds: 3, position: 4000})), [0, 2, 1], 'P-space within/between matches');
  assert.deepEqual(triple(f.fight(hi, hl, {rounds: 3, position: 4000})), [1, 2, 0]);
  const cli = await runMatch([learner, idle], {...standardHill, rounds: 3, position: 4000});
  assert.deepEqual(triple(cli), [0, 2, 1]);
  const [hw, hr] = [f.load(await assemble(fixture('pin_writer.red'), standardHill)), f.load(await assemble(fixture('pin_reader.red'), standardHill))];
  assert.deepEqual(triple(f.fight(hw, hr, {rounds: 10, position: 4000})), [0, 10, 0], 'PIN sharing');
  f.dispose();
  console.log('ok P-space persists within a match, resets between matches; PIN shares P-space');
}
{
  const plan = [];
  const nanoW = corpora.nano ? fightable.nano.slice(0, 12) : [];
  for (const w of nanoW) w.asm = await asm('nano', w);
  const stdSources = [
    learner, idle, fixture('pin_writer.red'), fixture('pin_reader.red'), fixture('immediate_b_stp.red'),
    ...(corpora.standard ? fightable.standard.slice(0, 8).map(w => w.source) : []),
  ];
  const stdW = [];
  for (const s of stdSources) stdW.push({asm: await assemble(s, standardHill)});
  for (let i = 0; i < 1000; i++) {
    const nano = nanoW.length && i < 500;
    const ws = nano ? nanoW : stdW, hill = nano ? HILLS.nano.hill : standardHill;
    const a = rand(ws.length), b = rand(ws.length);
    const roll = rand(20);
    if (roll < 3) {   // melee: 3..6 warriors (repeats allowed), mixed into the same instance
      const list = Array.from({length: 3 + rand(4)}, () => rand(ws.length));
      plan.push({nano, a, b, many: list, rounds: 1 + rand(nano ? 12 : 3), position: rand(4) ? hill.minDistance + rand(4 * hill.coreSize) : null});
    } else if (nano && roll < 9) plan.push({nano, a, b, exact: true});
    else plan.push({nano, a, b, rounds: 1 + rand(nano ? 12 : 5), position: rand(4) ? legal(hill) : null});
  }
  // Random placements (position 0) differ per instance, so compare those by round count only.
  const f = await createFighter(nanoW.length ? HILLS.nano.hill : standardHill);
  let handles = (nanoW.length ? nanoW : stdW).map(w => f.load(w.asm));
  let phase = nanoW.length ? 'nano' : 'standard';
  let compared = 0, manyCompared = 0;
  for (const [i, step] of plan.entries()) {
    if (!step.nano && phase === 'nano') {
      f.configure(standardHill);
      assert.throws(() => f.fight(handles[0], handles[1], {rounds: 1}), /Unknown warrior handle/);
      handles = stdW.map(w => f.load(w.asm));
      phase = 'standard';
    }
    if (step.nano && !nanoW.length) continue;
    const ws = step.nano ? nanoW : stdW, hill = step.nano ? HILLS.nano.hill : standardHill;
    if (i % 50 === 7) {
      assert.throws(() => f.load({instructions: [[99, 0, 0, 0, 0, 0]], start: 0}), /opcode|out of range/);
      assert.throws(() => f.load({instructions: [[0, 0, 0, 0, 0, 0]], start: 3}), /start/);
      assert.throws(() => f.load({instructions: Array(hill.maxLength + 1).fill([0, 0, 0, 0, 0, 0]), start: 0}), /instructions/);
    }
    if (i % 97 === 13) {   // unload and reload: the old handle must stop working
      const k = step.a, old = handles[k];
      f.unload(old);
      assert.throws(() => f.fight(old, old, {rounds: 1}), /Unknown warrior handle/);
      handles[k] = f.load(ws[k].asm);
    }
    if (step.many) {
      const got = f.fightMany(step.many.map(k => handles[k]), {rounds: step.rounds, position: step.position ?? 0});
      assert.ok(got.results.every(r => r.reduce((x, y) => x + y) === step.rounds));
      if (step.position === null) continue;
      const fresh = await createFighter(hill);
      const fh = ws.map(w => fresh.load(w.asm));
      const want = fresh.fightMany(step.many.map(k => fh[k]), {rounds: step.rounds, position: step.position});
      fresh.dispose();
      assert.deepEqual(got, want, `reuse step ${i} ${JSON.stringify(step)}`);
      compared++; manyCompared++;
      continue;
    }
    const got = step.exact ? f.exact(handles[step.a], handles[step.b])
      : f.fight(handles[step.a], handles[step.b], {rounds: step.rounds, position: step.position ?? 0});
    if (step.position === null && !step.exact) { assert.equal(got.wins + got.ties + got.losses, step.rounds); continue; }
    const fresh = await createFighter(hill);
    const fa = fresh.load(ws[step.a].asm), fb = fresh.load(ws[step.b].asm);
    const want = step.exact ? fresh.exact(fa, fb) : fresh.fight(fa, fb, {rounds: step.rounds, position: step.position});
    fresh.dispose();
    assert.deepEqual(got, want, `reuse step ${i} ${JSON.stringify(step)}`);
    compared++;
  }
  // Settings changes and a failed configure leave a usable instance.
  assert.throws(() => f.configure({...standardHill, maxLength: 101}), /Invalid hill/);
  assert.deepEqual(triple(f.fight(handles[0], handles[1], {rounds: 3, position: 4000})), [0, 2, 1], 'rejected settings keep the old ones');
  f.configure(standardHill);
  assert.throws(() => f.fight(handles[0], handles[1], {rounds: 1}), /Unknown warrior handle/);
  const x = f.load(stdW[0].asm), y = f.load(stdW[1].asm);
  assert.deepEqual(triple(f.fight(x, y, {rounds: 3, position: 4000})), [0, 2, 1]);
  f.dispose();
  assert.throws(() => f.load(stdW[0].asm), /disposed/);
  console.log(`ok reuse: 1000 mixed fight/exact/fightMany calls on one instance (${compared} compared with fresh instances, ${manyCompared} of them melee), incl. P-space/PIN, failed loads, unload, configure`);
}

// --- 3b. Melee: fightMany = pMARS (and exMARS CLI) round for round ----------
// pMARS's score formula, as its eval.c evaluates it: integer arithmetic with
// C truncation for / and %, unary + - !, comparisons and && || (1 or 0),
// registers W (warriors) and S (survivors); others are 0.
function pmarsFormula(formula, W, S) {
  let i = 0;
  const peek = () => { while (/\s/.test(formula[i] ?? '')) i++; return formula[i]; };
  const trunc = x => Math.trunc(x);
  const ops = {'*': 5, '/': 5, '%': 5, '+': 4, '-': 4, '<': 3, '>': 3, '<=': 3, '>=': 3, '==': 3, '!=': 3, '&&': 2, '||': 1};
  function op() {
    peek();
    const two = formula.slice(i, i + 2), one = formula[i];
    if (['<=', '>=', '==', '!=', '&&', '||'].includes(two)) return two;
    return '*/%+-<>'.includes(one) && one ? one : null;
  }
  function primary() {
    const c = peek();
    if (c === '(') { i++; const v = expr(0); assert.equal(peek(), ')'); i++; return v; }
    if (c === '+' || c === '-' || c === '!') { i++; const v = primary(); return c === '-' ? -v : c === '!' ? +!v : v; }
    if (/\d/.test(c)) { let n = ''; while (/\d/.test(formula[i] ?? '')) n += formula[i++]; return Number(n); }
    if (/[A-Za-z]/.test(c)) { i++; const r = c.toUpperCase(); return r === 'W' ? W : r === 'S' ? S : 0; }
    throw new Error(`bad formula at ${i}`);
  }
  function apply(o, x, y) {
    switch (o) {
      case '*': return x * y; case '/': return trunc(x / y); case '%': return x % y;
      case '+': return x + y; case '-': return x - y;
      case '<': return +(x < y); case '>': return +(x > y); case '<=': return +(x <= y); case '>=': return +(x >= y);
      case '==': return +(x === y); case '!=': return +(x !== y); case '&&': return +(x && y); case '||': return +(x || y);
    }
  }
  function expr(min) {
    let left = primary();
    for (let o; (o = op()) && ops[o] >= min;) { i += o.length; left = apply(o, left, expr(ops[o] + 1)); }
    return left;
  }
  const v = expr(0);
  assert.equal(peek(), undefined, 'formula fully parsed');
  return v;
}
const pmarsScore = (formula, hist, n) => {
  let accu = 0;
  for (let s = 1; s <= n; s++) accu += hist[s] * pmarsFormula(formula, n, s);
  return accu;
};
// pMARS -k prints, per warrior: score, alive-with-1..n-survivors counts, deaths.
async function cliMelee(engine, hill, sources, rounds, position, formula) {
  const files = Object.fromEntries(sources.map((s, k) => [`w${k}.red`, s]));
  const args = ['-b', '-k', '-r', '' + rounds, ...(position ? ['-F', '' + position] : []), '-s', '' + hill.coreSize,
    '-c', '' + hill.cycles, '-p', '' + hill.processes, '-l', '' + hill.maxLength, '-d', '' + hill.minDistance];
  if (formula) args.push('-=', formula);
  const r = await runCLI(engine, [...args, ...Object.keys(files)], files);
  assert.equal(r.exitCode, 0, `${engine}: ${r.stderr}`);
  const rows = r.stdout.trim().split('\n').slice(-sources.length).map(l => l.trim().split(/\s+/).map(Number));
  const n = sources.length;
  if (engine === 'pmars') return rows.map(row => ({score: row[0], hist: [row[n + 1], ...row.slice(1, n + 1)]}));
  return rows.map(row => ({hist: [row[n], ...row.slice(0, n)]}));   // exMARS CLI: alive 1..n, dead
}
const FORMULAS = ['(W*W-1)/S', '10*W-S', '(S==1)*W*W+(S>1&&S<W)*(W-S)%3-!S'];
{
  const MELEE_HILLS = ['nano', 'tiny', 'standard', 'lp'].filter(n => corpora[n]);
  let compared = 0;
  for (const name of MELEE_HILLS) {
    const {hill} = HILLS[name];
    const small = name === 'nano' || name === 'tiny';
    const f = await createFighter(hill);
    for (const n of [3, 4, 6, 10]) {
      for (let rep = 0; rep < 2; rep++) {
        const ws = Array.from({length: n}, () => pick(fightable[name]));
        const rounds = small ? 10 + rand(40) : 2 + rand(5);
        // -F for n > 2 is a seed: pMARS accepts minDistance..MAXCORESIZE.
        const position = rep === 0 ? legal(hill) : hill.minDistance + rand(10 * hill.coreSize);
        const handles = ws.map(w => f.load(asmCache.get(`${name}/${w.file}/1`)));
        const got = f.fightMany(handles, {rounds, position}).results;
        for (const row of got) assert.equal(row.length, n + 1);
        const label = `${name} n=${n} r=${rounds} F=${position} ${ws.map(w => w.file).join(',')}`;
        if (havePmars) {
          const want = await cliMelee('pmars', hill, ws.map(w => w.source), rounds, position);
          assert.deepEqual(got, want.map(x => x.hist), `pMARS histogram ${label}`);
          want.forEach((x, k) => assert.equal(pmarsScore(FORMULAS[0], got[k], n), x.score, `default score ${label}`));
          if (rep === 0) for (const formula of FORMULAS.slice(1)) {
            const alt = await cliMelee('pmars', hill, ws.map(w => w.source), rounds, position, formula);
            alt.forEach((x, k) => assert.equal(pmarsScore(formula, got[k], n), x.score, `score ${formula} ${label}`));
          }
        }
        const ex = await cliMelee('exmars', hill, ws.map(w => w.source), rounds, position);
        assert.deepEqual(got, ex.map(x => x.hist), `exMARS CLI histogram ${label}`);
        f.setEarlyExit(false);
        assert.deepEqual(f.fightMany(handles, {rounds, position}).results, got, `early exit off ${label}`);
        f.setEarlyExit(true);
        // Random placements: every round is counted once per warrior.
        const random = f.fightMany(handles, {rounds}).results;
        assert.ok(random.every(r => r.reduce((x, y) => x + y) === rounds));
        handles.forEach(h => f.unload(h));
        compared++;
      }
    }
    f.dispose();
  }
  // Warriors exactly fill the core (n * minDistance = coreSize): posit fails, npos places them.
  const fills = [];
  if (corpora.nano) fills.push(['nano 16 warriors', HILLS.nano.hill, Array.from({length: 16}, () => pick(fightable.nano).source)]);
  const synthetic = [';assert 1\nmov 0, 1\n', ';assert 1\nadd #4, 3\nmov 2, @2\njmp -2\ndat #0, #0\n',
    ';assert 1\nspl 0\nmov 0, 1\n', ';assert 1\njmp 0\n', ';assert 1\nspl 0, <-1\nmov.i #1, -2\n', ';assert 1\ndat 0, 0\n'];
  fills.push(['36 warriors, coreSize 360', {coreSize: 360, cycles: 3600, processes: 360, maxLength: 10, minDistance: 10},
    Array.from({length: 36}, (_, k) => synthetic[k % synthetic.length])]);
  for (const [label, hill, sources] of fills) {
    const f = await createFighter(hill);
    const handles = [];
    for (const s of sources) handles.push(f.load(await assemble(s, hill)));
    for (const position of [hill.minDistance, hill.minDistance + 123]) {
      const rounds = 12;
      const got = f.fightMany(handles, {rounds, position}).results;
      if (havePmars) assert.deepEqual(got, (await cliMelee('pmars', hill, sources, rounds, position)).map(x => x.hist), `fill ${label}`);
      assert.deepEqual(got, (await cliMelee('exmars', hill, sources, rounds, position)).map(x => x.hist), `fill exMARS ${label}`);
    }
    assert.throws(() => f.fightMany([...handles, handles[0]], {rounds: 1}), /exceeds coreSize|2\.\.36/);
    f.dispose();
  }
  // P-space and PIN across n warriors: writer/reader pairs share by PIN, learner keeps
  // its P-space across the rounds of a match only.
  {
    const hill = standardHill;
    const sources = [fixture('pin_writer.red'), learner, fixture('pin_reader.red'), idle, fixture('immediate_b_stp.red'),
      ...(corpora.standard ? fightable.standard.filter(w => /^\s*pin\b/im.test(w.source)).slice(0, 3).map(w => w.source) : [])];
    const f = await createFighter(hill);
    const handles = [];
    for (const s of sources) handles.push(f.load(await assemble(s, hill)));
    for (const [position, rounds] of [[4000, 6], [137, 4], [100, 9]]) {
      const got = f.fightMany(handles, {rounds, position}).results;
      if (havePmars) assert.deepEqual(got, (await cliMelee('pmars', hill, sources, rounds, position)).map(x => x.hist), `P-space melee F=${position}`);
      assert.deepEqual(got, (await cliMelee('exmars', hill, sources, rounds, position)).map(x => x.hist), `P-space melee exMARS F=${position}`);
      assert.deepEqual(f.fightMany(handles, {rounds, position}).results, got, 'P-space resets between matches');
    }
    // The learner dies only in a match's first round (unless killed by others): check alone with idles.
    const lone = [learner, idle, idle].map(s => s);
    const lh = [];
    for (const s of lone) lh.push(f.load(await assemble(s, hill)));
    assert.deepEqual(f.fightMany(lh, {rounds: 4, position: 4000}).results[0], [1, 0, 0, 3]);
    assert.deepEqual(f.fightMany(lh, {rounds: 4, position: 4000}).results[0], [1, 0, 0, 3]);
    f.dispose();
  }
  // Two warriors: fightMany equals fight; argument checks.
  if (corpora.nano) {
    const {hill} = HILLS.nano;
    const f = await createFighter(hill);
    for (let k = 0; k < 10; k++) {
      const a = f.load(asmCache.get(`nano/${pick(fightable.nano).file}/1`)), b = f.load(asmCache.get(`nano/${pick(fightable.nano).file}/1`));
      const rounds = 1 + rand(30), position = legal(hill);
      const pair = f.fight(a, b, {rounds, position}), many = f.fightMany([a, b], {rounds, position}).results;
      assert.deepEqual([many[0][1], many[0][2], many[0][0]], triple(pair));
      assert.deepEqual([many[1][1], many[1][2], many[1][0]], [pair.losses, pair.ties, pair.wins]);
    }
    const h = f.load(asmCache.get(`nano/${fightable.nano[0].file}/1`));
    assert.throws(() => f.fightMany([h], {rounds: 1}), /2\.\.36/);
    assert.throws(() => f.fightMany(Array(37).fill(h), {rounds: 1}), /2\.\.36/);
    assert.throws(() => f.fightMany(Array(17).fill(h), {rounds: 1}), /exceeds coreSize/);
    assert.throws(() => f.fightMany([h, h, 99999], {rounds: 1}), /Unknown warrior handle/);
    assert.throws(() => f.fightMany([h, h, h], {rounds: 0}), /rounds/);
    assert.throws(() => f.fightMany([h, h, h], {rounds: 1, position: 4}), /position/);
    assert.throws(() => f.fightMany([h, h, h], {rounds: 1, position: 2 ** 30 + 1}), /position/);
    assert.throws(() => f.fightMany([h, h], {rounds: 1, position: 76}), /position/);
    assert.throws(() => f.fightMany([h, h, h], {rounds: 1, seed: 3}), /Unsupported fight option/);
    assert.equal(f.fightMany(Array(16).fill(h), {rounds: 2, position: 2 ** 30}).results.length, 16);
    f.dispose();
  }
  console.log(`ok melee: fightMany = pMARS${havePmars ? '' : ' (skipped)'} and exMARS CLI histograms on ${compared} matches ` +
    `(3/4/6/10 warriors, nano/tiny/standard/lp), scores under ${FORMULAS.length} formulas, early exit on = off, ` +
    'full-core placement (16 and 36 warriors), P-space/PIN melee, 2-warrior fightMany = fight');
}

// --- 4. Assembler ------------------------------------------------------------
function pythonModuleDir() {
  const dir = resolve(process.env.EXMARS_PYTHON_MODULE_DIR ?? join(ROOT, 'build/python'));
  const probe = () => spawnSync('python3', ['-c', 'import sys; sys.path[:0]=sys.argv[1:]; import Mars', dir, ROOT], {encoding: 'utf8'}).status === 0;
  if (probe()) return dir;
  spawnSync('python3', ['setup.py', '-q', 'build_ext', '--build-lib', dir], {cwd: ROOT, encoding: 'utf8'});
  return probe() ? dir : null;
}
const pyDir = pythonModuleDir();
if (!pyDir) skip('assembler comparison with the Python binding (python3 setup.py build_ext failed)');
else {
  const script = `
import sys, json
sys.path[:0] = [sys.argv[1], sys.argv[2]]
import Mars
jobs = json.load(sys.stdin)
out = []
for hill, source in jobs:
    m = Mars.Mars(2, hill['coreSize'], hill['cycles'], hill['processes'], hill['maxLength'], hill['minDistance'])
    try:
        w = m.parse_string(source)
        out.append({'ok': True, 'instructions': [list(i.data) for i in w.instructions], 'start': w.startPos,
                    'pin': w.pin, 'name': w.name, 'author': w.authorName})
    except Exception as e:
        out.append({'ok': False, 'error': str(e)})
print(json.dumps(out))
`;
  const jobs = [];
  for (const name of available) for (const w of corpora[name]) jobs.push([name, w]);
  const py = spawnSync('python3', ['-c', script, pyDir, ROOT], {
    input: JSON.stringify(jobs.map(([n, w]) => [HILLS[n].hill, w.source])), encoding: 'utf8', maxBuffer: 1 << 28});
  assert.equal(py.status, 0, py.stderr);
  const want = JSON.parse(py.stdout);
  let same = 0;
  for (const [i, [name, w]] of jobs.entries()) {
    const got = await asm(name, w), p = want[i];
    if (!p.ok) { assert.equal(got.ok, false, `${name}/${w.file}: Python rejects, Wasm accepts`); same++; continue; }
    if (!got.ok) {
      // The only intended difference: warriors the fighter can't run.
      assert.match(got.errors, /No instructions|Too many instructions|outside of the program/, `${name}/${w.file}: ${got.errors}`);
      continue;
    }
    assert.deepEqual({instructions: got.instructions, start: got.start, pin: got.pin, name: got.name, author: got.author},
      {instructions: p.instructions, start: p.start, pin: p.pin, name: p.name, author: p.author}, `${name}/${w.file}`);
    same++;
  }
  console.log(`ok assembler: ${same}/${jobs.length} corpus sources give the Python binding's tuples, start, PIN, name and author`);
}
{
  const nano = HILLS.nano.hill, std = standardHill;
  let many = ';redcode-94\n;assert 1\n';
  for (let i = 0; i < 20; i++) many += `bogus${i} label${i} what${i}\n`;
  for (const bad of [many, '', 'garbage', ';assert 1\nrof\ndat 0,0\n', ';assert 1\ndat 99999999999999999999, 0\n', ';assert 1\ndat 1/0, 0\n', ';assert 1\nmov 0,\n',
    ';assert 1\nstart dat 0,0\nend nowhere\n', ';assert 1\n' + 'dat 0,0\n'.repeat(6), 'ééé ☃\n']) {
    const r = await assemble(bad, nano);
    assert.equal(r.ok, false, JSON.stringify(bad).slice(0, 60));
    assert.equal(typeof r.errors, 'string');
    assert.ok(r.errors.length > 0);
    assert.equal(typeof r.warnings, 'string');
  }
  const r = await assemble(many, nano);
  assert.match(r.errors, /Too many errors/);
  const good = await assemble(';redcode-94\n;assert CORESIZE==80\n;name ok\njmp 0\n', nano);
  assert.equal(good.ok, true);
  assert.deepEqual(good.instructions, [[11, 2, 0, 0, 0, 0]]);
  assert.deepEqual(good.warnings, []);
  const failsStd = await assemble(';redcode-94\n;assert CORESIZE==80\njmp 0\n', std);
  assert.equal(failsStd.ok, false);
  assert.match(failsStd.errors, /Assertion/);
  const warned = await assemble('jmp 0\n', nano);
  assert.equal(warned.ok, true);
  assert.ok(warned.warnings.some(s => /assert/.test(s)));
  const pin = await assemble(fixture('pin_reader.red'), std);
  assert.equal(pin.pin, 123);
  assert.equal((await assemble(';assert 1\ndat #ROUNDS, #0\n', nano, {rounds: 7})).instructions[0][3], 7);
  await assert.rejects(assemble('jmp 0', {...nano, rounds: 3}), /Unsupported hill setting/);
  await assert.rejects(assemble('x'.repeat(65537), nano), /64 KiB/);
  await assert.rejects(assemble('jmp 0', {...nano, minDistance: 41}), /Invalid hill/);
  await assert.rejects(createFighter({...nano, maxLength: 6}), /Invalid hill/);
  const f = await createFighter(nano);
  assert.throws(() => f.load(r), /failed to assemble/);
  const h = f.load(good);
  assert.throws(() => f.fight(h, h, {rounds: 1, position: 4}), /position/);
  assert.throws(() => f.fight(h, h, {rounds: 0}), /rounds/);
  assert.throws(() => f.fight(h, h, {rounds: 1, permutate: true}), /Unsupported fight option/);
  assert.deepEqual(f.fight(h, h, {rounds: 3, position: 5}), {wins: 0, ties: 3, losses: 0});
  f.dispose();
  console.log('ok assembler: broken input and > ERRMAX errors give {ok:false}, later calls work, ;assert follows the hill');
}

// --- 5. Throughput (reported, not asserted) --------------------------------
const ms = t => (performance.now() - t);
if (corpora.nano) {
  const {hill} = HILLS.nano;
  const opponents = fightable.nano.slice(0, 50);
  const f = await createFighter(hill);
  const handles = opponents.map(w => f.load(asmCache.get(`nano/${w.file}/1`)));
  const cand = asmCache.get(`nano/${opponents[0].file}/1`);
  for (const h of handles) f.exact(f.load(cand), h);   // warm-up
  const reps = 20;
  let t = performance.now();
  for (let k = 0; k < reps; k++) { const c = f.load(cand); for (const h of handles) f.exact(c, h); f.unload(c); }
  const perCandidate = ms(t) / reps;
  t = performance.now();
  for (let k = 0; k < 20; k++) await runMatch([opponents[0].source, opponents[1].source], {...hill, rounds: 1, position: 5});
  const runMatch1 = ms(t) / 20;
  t = performance.now();
  for (let k = 0; k < 20; k++) await assemble(opponents[k].source, hill);
  const asmMs = ms(t) / 20;
  console.log(`throughput nano exact: ${perCandidate.toFixed(1)} ms per candidate vs ${handles.length} opponents ` +
    `(${handles.length * 142} rounds); runMatch 1-round match ${runMatch1.toFixed(2)} ms ` +
    `-> ~${(runMatch1 * 142 * handles.length / 1000).toFixed(1)} s per candidate with a runtime per round; assemble ${asmMs.toFixed(2)} ms`);
  f.dispose();
}
if (corpora['94nop']) {
  const {hill} = HILLS['94nop'];
  const opponents = fightable['94nop'].slice(0, 21);
  const f = await createFighter(hill);
  const cand = f.load(await asm('94nop', opponents[0]));
  const handles = [];
  for (const w of opponents.slice(1)) handles.push(f.load(await asm('94nop', w)));
  f.fight(cand, handles[0], {rounds: 10, position: 1234});
  let t = performance.now();
  for (const h of handles) f.fight(cand, h, {rounds: 250, position: 1234});
  const perCandidate = ms(t);
  t = performance.now();
  for (const h of handles.slice(0, 5)) f.fight(cand, h, {rounds: 250, position: 1234});
  const fighter5 = ms(t) / 5;
  t = performance.now();
  for (const w of opponents.slice(1, 6)) await runMatch([opponents[0].source, w.source], {...hill, rounds: 250, position: 1234});
  const runMatch5 = ms(t) / 5;
  console.log(`throughput standard (94top50): ${perCandidate.toFixed(0)} ms per candidate vs ${handles.length} opponents at 250 rounds ` +
    `(${(perCandidate / handles.length).toFixed(1)} ms per match); first 5 opponents: fighter ${fighter5.toFixed(1)} ms, ` +
    `runMatch ${runMatch5.toFixed(1)} ms per 250-round match`);
  f.dispose();
}
if (corpora['94nop']) {
  const {hill} = HILLS['94nop'];
  const ws = fightable['94nop'].slice(0, 10);
  const f = await createFighter(hill);
  const handles = [];
  for (const w of ws) handles.push(f.load(await asm('94nop', w)));
  f.fightMany(handles, {rounds: 5, position: 1234});
  let t = performance.now();
  const got = f.fightMany(handles, {rounds: 200, position: 1234}).results;
  const fighterMs = ms(t);
  t = performance.now();
  const ex = await cliMelee('exmars', hill, ws.map(w => w.source), 200, 1234);
  const exMs = ms(t);
  assert.deepEqual(got, ex.map(x => x.hist));
  let line = `throughput melee (10 x 94top50, 200 rounds): fighter ${fighterMs.toFixed(0)} ms, exMARS CLI (runCLI) ${exMs.toFixed(0)} ms`;
  if (havePmars) {
    t = performance.now();
    const pm = await cliMelee('pmars', hill, ws.map(w => w.source), 200, 1234);
    line += `, pMARS CLI (runCLI) ${ms(t).toFixed(0)} ms`;
    assert.deepEqual(got, pm.map(x => x.hist));
  }
  console.log(line);
  f.dispose();
}
console.log('ok fighter tests');
