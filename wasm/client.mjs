/** Browser, Worker and Node API. Each CLI invocation owns a fresh runtime. */
const factories = new Map();
export async function runCLI(engine, args, files = {}) {
  if (!/^(exmars|reference-exmars|pmars|bench-(optimized-exmars|exact-exmars|original-exmars|pmars))$/.test(engine))
    throw new Error(`Unknown engine: ${engine}`);
  if (!Array.isArray(args) || args.some(x => typeof x !== 'string')) throw new TypeError('args must be strings');
  if (!factories.has(engine)) factories.set(engine, import(`./${engine}.mjs`));
  const {default: create} = await factories.get(engine);
  const stdout = [], stderr = [];
  const module = await create({noInitialRun: true, print: s => stdout.push(s), printErr: s => stderr.push(s)});
  for (const [name, data] of Object.entries(files)) {
    if (!/^[a-zA-Z0-9_.-]+$/.test(name) || name === '.' || name === '..') throw new Error('Files must have flat names');
    module.FS.writeFile('/' + name, data);
  }
  module.FS.chdir('/');
  const start = performance.now();
  let exitCode = 0;
  const nodeProcess = globalThis.process?.versions?.node ? globalThis.process : null;
  const previousExitCode = nodeProcess?.exitCode;
  try { exitCode = module.callMain([...args]) ?? 0; }
  catch (e) { if (e.name === 'ExitStatus') exitCode = e.status; else throw e; }
  finally { if (nodeProcess) nodeProcess.exitCode = previousExitCode; }
  return {exitCode, stdout: stdout.join('\n'), stderr: stderr.join('\n'), wallSeconds: (performance.now() - start) / 1000};
}

const defaults = {coreSize: 8000, cycles: 80000, processes: 8000, maxLength: 100, minDistance: 100, rounds: 100, position: 4000};
export async function runMatch(warriors, options = {}) {
  if (!Array.isArray(warriors) || warriors.length !== 2 || warriors.some(w => typeof w !== 'string'))
    throw new TypeError('Supply two Redcode source strings');
  if (warriors.some(w => new TextEncoder().encode(w).length > 65536)) throw new RangeError('Warrior source exceeds 64 KiB');
  for (const key of Object.keys(options))
    if (!(key in defaults) && !['engine', 'earlyExit'].includes(key)) throw new Error(`Unsupported match option: ${key}`);
  if (options.earlyExit !== undefined && typeof options.earlyExit !== 'boolean') throw new TypeError('earlyExit must be boolean');
  const o = {...defaults, ...options};
  for (const k of Object.keys(defaults)) if (!Number.isSafeInteger(o[k]) || o[k] < 1) throw new RangeError(`Invalid ${k}`);
  if (o.coreSize > 65535 || o.processes > 1000000 || o.cycles > 100000000 || o.rounds > 1000000 ||
      o.maxLength > o.minDistance || o.minDistance * 2 > o.coreSize ||
      o.position < o.minDistance || o.position > o.coreSize - o.minDistance) throw new RangeError('Invalid hill settings');
  const engine = o.engine ?? 'exmars';
  if (!['exmars', 'reference-exmars', 'pmars'].includes(engine)) throw new Error('Not a tournament engine');
  const args = ['-b', '-k', '-s', ''+o.coreSize, '-c', ''+o.cycles, '-p', ''+o.processes,
    '-l', ''+o.maxLength, '-d', ''+o.minDistance, '-r', ''+o.rounds, '-F', ''+o.position];
  if (o.earlyExit === false && engine !== 'pmars') args.push('-X');
  const result = await runCLI(engine, [...args, 'a.red', 'b.red'], {'a.red': warriors[0], 'b.red': warriors[1]});
  const rows = result.stdout.trim().split('\n').slice(-2).map(s => s.trim().split(/\s+/).map(Number));
  if (result.exitCode || rows.length !== 2 || rows.some(r => r.length !== 2 || r.some(n => !Number.isSafeInteger(n) || n < 0)) ||
      rows[0][1] !== rows[1][1] || rows[0][0] + rows[1][0] + rows[0][1] !== o.rounds)
    throw new Error(`Engine failed (${result.exitCode}): ${result.stderr}\n${result.stdout}`);
  return {...result, wins: rows[0][0], ties: rows[0][1], losses: rows[1][0], rounds: o.rounds};
}

/** One match per unordered pair, engine alternates first execution each round. */
export async function runTournament(warriors, options = {}, onProgress = () => {}) {
  if (!Array.isArray(warriors) || warriors.length < 2) throw new TypeError('At least two warriors required');
  const standings = warriors.map((_, index) => ({index, wins: 0, ties: 0, losses: 0, points: 0}));
  const matches = [];
  for (let i = 0; i < warriors.length; i++) for (let j = i + 1; j < warriors.length; j++) {
    const r = await runMatch([warriors[i], warriors[j]], options);
    for (const [k, w, l] of [[i, r.wins, r.losses], [j, r.losses, r.wins]]) {
      const s = standings[k]; s.wins += w; s.losses += l; s.ties += r.ties; s.points += 3*w + r.ties;
    }
    matches.push({i, j, ...r}); onProgress({completed: matches.length, total: warriors.length*(warriors.length-1)/2});
  }
  return {standings: standings.sort((a,b) => b.points-a.points || a.index-b.index), matches};
}
