import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const build = resolve(process.argv[2] ?? 'dist/wasm');
const {runCLI, runMatch, runTournament} = await import(pathToFileURL(resolve(build, 'client.mjs')));
const {benchmark} = await import(pathToFileURL(resolve(build, 'benchmark.mjs')));
const imported = spawnSync('python3', ['-c', 'import sys,json; sys.path.insert(0,"tests/pmars_compat"); from run import CASES,COMMON; print(json.dumps([CASES, COMMON]))'], {encoding:'utf8'});
assert.equal(imported.status, 0, imported.stderr);
const [cases, common] = JSON.parse(imported.stdout);
for (const [name, , options, warriors, expected] of cases) {
  const files = Object.fromEntries(await Promise.all(warriors.map(async f => [f, await readFile(new URL('../tests/pmars_compat/'+f, import.meta.url), 'utf8')])));
  for (const [engine, flags] of [['exmars',[]],['exmars',['-X']],['reference-exmars',[]],['pmars',[]]]) {
    const r = await runCLI(engine, ['-b','-k',...flags,...common,...options,...warriors], files);
    assert.equal(r.exitCode, 0, r.stderr);
    assert.deepEqual(r.stdout.trim().split('\n').slice(-2), expected, `${name} ${engine} ${flags}`);
  }
  console.log(`ok ${name}: optimized, exact, reference, pMARS`);
}
const live = ';redcode-94\n;assert 1\njmp 0\nend\n';
const dead = ';redcode-94\n;assert 1\ndat 0,0\nend\n';
for (const engine of ['exmars','reference-exmars','pmars']) {
  const r = await runMatch([live, dead], {engine, rounds:4, cycles:100});
  assert.deepEqual([r.wins,r.ties,r.losses], [4,0,0]);
}
const t = await runTournament([live,dead,live], {rounds:4, cycles:100});
assert.equal(t.matches.length,3);
assert.deepEqual(t.standings.map(s => [s.index,s.points]), [[0,16],[2,16],[1,0]]);
await assert.rejects(runMatch([live,dead], {coreSize:80}));
await assert.rejects(runMatch([live,dead], {permutate:true}), /Unsupported match option/);
await assert.rejects(runMatch([live,dead], {earlyExit:'false'}));
await assert.rejects(runMatch([live,'this is invalid redcode'], {rounds:1}));
const priorExit = process.exitCode;
const badArgs = ['--not-an-option'];
assert.notEqual((await runCLI('exmars', badArgs)).exitCode, 0);
assert.deepEqual(badArgs, ['--not-an-option']);
assert.equal(process.exitCode, priorExit, 'library must not change Node exit status');
assert.equal((await runMatch([live,dead], {rounds:2})).wins,2, 'fresh runtime after failure');
const tiny = '80 800 80 5 2\n1 0\n11 2 0 0 0 0\n1 0\n0 0 0 0 0 0\n';
const b = await benchmark(tiny, {passes:2, repeats:2});
assert.equal(b.runs.length,8);
assert.ok(b.allScoresMatch);
await assert.rejects(benchmark(tiny.replace('11 2','16 2')));
console.log('ok match scoring, tournament, isolation, input validation, all four benchmark engines');
