import {runMatch, runTournament} from './client.mjs';
import {benchmark} from './benchmark.mjs';
import {assemble, createFighter} from './fighter-client.mjs';
// A worker owns one job. Terminating it is a hard cancellation, including C loops.
// method 'fighter' (smoke page only): assemble two sources, then fight and exact
// on a persistent fighter kept per hill for this worker.
let busy = false;
let fighter = null, fighterHill = '';
async function fighterJob({warriors, hill, rounds = 2, position = 0}) {
  if (!Array.isArray(warriors) || warriors.length !== 2) throw new TypeError('Supply two Redcode source strings');
  const assembled = await Promise.all(warriors.map(w => assemble(w, hill)));
  const failed = assembled.find(w => !w.ok);
  if (failed) throw new Error(failed.errors);
  if (fighterHill !== JSON.stringify(hill)) {
    fighter?.dispose();
    fighter = await createFighter(hill);
    fighterHill = JSON.stringify(hill);
  }
  const [a, b] = assembled.map(w => fighter.load(w));
  try {
    return {fight: fighter.fight(a, b, {rounds, position}), exact: fighter.exact(a, b),
      names: assembled.map(w => w.name)};
  } finally { fighter.unload(a); fighter.unload(b); }
}
self.onmessage = async ({data: {id, method = 'match', warriors, options, input}}) => {
  if (busy) { self.postMessage({id, error: 'Worker is busy'}); return; }
  busy = true;
  try {
    if (!['match', 'tournament', 'benchmark', 'fighter'].includes(method)) throw new Error('Unknown method');
    const progress = progress => self.postMessage({id, progress});
    const result = method === 'match' ? await runMatch(warriors, options) : method === 'benchmark' ?
      await benchmark(input, {...options, onProgress: progress}) : method === 'fighter' ?
      await fighterJob({warriors, ...options}) :
      await runTournament(warriors, options, progress);
    self.postMessage({id, result});
  } catch (e) { self.postMessage({id, error: String(e.message ?? e)}); }
  finally { busy = false; }
};
