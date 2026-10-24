#!/usr/bin/env node
import {readFile, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import os from 'node:os';
import {createHash} from 'node:crypto';
const [buildDir = 'dist/wasm', passes = '3', repeats = '1', output = 'build/wasm-results.json'] = process.argv.slice(2);
const {benchmark} = await import(pathToFileURL(resolve(buildDir, 'benchmark.mjs')));
const results = {date: new Date().toISOString(), runtime: process.version, platform: `${os.platform()} ${os.arch()}`, cpu: os.cpus()[0]?.model,
  build: JSON.parse(await readFile(resolve(buildDir, 'manifest.json'), 'utf8')), suites: {}};
// Refuse to overwrite an existing measurement.
await writeFile(output, JSON.stringify(results, null, 2), {flag: 'wx'});
for (const suite of ['94top50', 'nanobm07']) {
  const input = await readFile(new URL(`../benchmarks/comparison/${suite}.txt`, import.meta.url), 'utf8');
  results.suites[suite] = {sha256: createHash('sha256').update(input).digest('hex'),
    ...await benchmark(input, {passes: Number(passes), repeats: Number(repeats),
      onWarmup: r => console.error(`${suite} warming up ${r.engine}`),
      onProgress: r => console.error(`${suite} pass ${r.pass+1} ${r.engine}: ${r.seconds.toFixed(3)}s`)})};
  await writeFile(output, JSON.stringify(results, null, 2)+'\n');
}
console.log(output);
