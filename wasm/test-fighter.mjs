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
    if (nano && rand(10) < 3) plan.push({nano, a, b, exact: true});
    else plan.push({nano, a, b, rounds: 1 + rand(nano ? 12 : 5), position: rand(4) ? legal(hill) : null});
  }
  // Random placements (position 0) differ per instance, so compare those by round count only.
  const f = await createFighter(nanoW.length ? HILLS.nano.hill : standardHill);
  let handles = (nanoW.length ? nanoW : stdW).map(w => f.load(w.asm));
  let phase = nanoW.length ? 'nano' : 'standard';
  let compared = 0;
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
  console.log(`ok reuse: 1000 mixed fight/exact calls on one instance (${compared} compared with fresh instances), incl. P-space/PIN, failed loads, unload, configure`);
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
console.log('ok fighter tests');
