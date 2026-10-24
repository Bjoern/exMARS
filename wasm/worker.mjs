import {runMatch, runTournament} from './client.mjs';
import {benchmark} from './benchmark.mjs';
// A worker owns one job. Terminating it is a hard cancellation, including C loops.
let busy = false;
self.onmessage = async ({data: {id, method = 'match', warriors, options, input}}) => {
  if (busy) { self.postMessage({id, error: 'Worker is busy'}); return; }
  busy = true;
  try {
    if (!['match', 'tournament', 'benchmark'].includes(method)) throw new Error('Unknown method');
    const progress = progress => self.postMessage({id, progress});
    const result = method === 'match' ? await runMatch(warriors, options) : method === 'benchmark' ?
      await benchmark(input, {...options, onProgress: progress}) :
      await runTournament(warriors, options, progress);
    self.postMessage({id, result});
  } catch (e) { self.postMessage({id, error: String(e.message ?? e)}); }
  finally { busy = false; }
};
