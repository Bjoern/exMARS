/**
 * Persistent exMARS fighter for browsers, module Workers and Node.
 *
 *   const w = await assemble(source, hill);  // fresh throwaway assembler instance per call
 *   const f = await createFighter(hill);     // one per Worker, kept for the whole run
 *   const h = f.load(w);                     // handle
 *   f.fight(h, h2, {rounds: 250, position: 1234});  // {wins, ties, losses}
 *   f.exact(h, h2, {position: 1234});        // {wins, ties, losses, rounds}: pMARS -P
 *   f.fightMany([h, h2, h3], {rounds: 200, position: 1234});  // {results: [[dead, alive1..aliveN], ...]}
 *
 * hill = {coreSize, cycles, processes, maxLength, minDistance}. Instructions are
 * 6-int tuples [opcode, modifier, aMode, aValue, bMode, bValue] numbered as in
 * Warrior.py / insn.h. See README.md, "Persistent fighter".
 */
const HILL_KEYS = ['coreSize', 'cycles', 'processes', 'maxLength', 'minDistance'];
const MAX_SOURCE_BYTES = 65536;
const MAX_ROUNDS = 1000000;
const MAX_WARRIORS = 36;                     // pMARS MAXWARRIOR
const MAX_FIXED_POSITION = 2 ** 30;          // pMARS MAXCORESIZE, the largest -F
const factories = new Map();

// This module's own query (a site's ?v=<release>), passed on to the .mjs and .wasm
// files it loads. Each Emscripten .mjs works only with the .wasm built with it, so a
// browser must not combine a cached old one with a new one after an update.
const VERSION = new URL(import.meta.url).search;

function factory(name) {
  if (!factories.has(name)) factories.set(name, import(`./${name}.mjs${VERSION}`).then(m => m.default));
  return factories.get(name);
}

// Wasm modules, fetched and compiled once per realm (page, Worker or Node process),
// so a new instance costs neither a request nor a compile.
const compiledModules = new Map();

async function compileWasm(name) {
  const url = new URL(`./${name}.wasm${VERSION}`, import.meta.url);
  if (url.protocol === 'file:') {
    const {readFile} = await import('node:fs/promises');
    url.search = '';
    return WebAssembly.compile(await readFile(url));
  }
  if (typeof WebAssembly.compileStreaming === 'function') {
    // Falls back to an array buffer when the server sends another MIME type.
    try { return await WebAssembly.compileStreaming(fetch(url, {credentials: 'same-origin'})); } catch {}
  }
  const response = await fetch(url, {credentials: 'same-origin'});
  if (!response.ok) throw new Error(`Failed to load ${url}: HTTP ${response.status}`);
  return WebAssembly.compile(await response.arrayBuffer());
}

function compiledModule(name) {
  if (!compiledModules.has(name)) {
    const loading = compileWasm(name);
    compiledModules.set(name, loading);
    loading.catch(() => compiledModules.delete(name));  // try again on the next call
  }
  return compiledModules.get(name);
}

// A fresh Emscripten instance of `name` from its compiled module. Emscripten's
// instantiateWasm hook can only report success, so a failed instantiation rejects
// through `failed` instead of leaving the call waiting forever.
async function freshInstance(name, options) {
  const [create, wasm] = await Promise.all([factory(name), compiledModule(name)]);
  let fail;
  const failed = new Promise((_, reject) => { fail = reject; });
  return Promise.race([failed, create({...options, instantiateWasm: (imports, done) => {
    WebAssembly.instantiate(wasm, imports).then(instance => done(instance, wasm), fail);
    return {};
  }})]);
}

// The assembler instance assemble() calls, one per realm. The parser returns its
// errors instead of calling exit() and assembler.c frees each call's memory, so one
// instance serves any number of calls. As a safeguard (older builds did both, and
// wasm memory never shrinks) an instance is still retired once its memory has grown
// past ASSEMBLER_MEMORY_LIMIT, and at once after an exit() or a trap. A fresh instance per call, as before, made a new
// wasm memory per warrior: browsers free those only when that realm's garbage
// collector runs, so Workers assembling many warriors (an evolver) ran out of
// memory (Edge: "Cannot allocate Wasm memory for new instance", or a crashed tab).
const ASSEMBLER_MEMORY_LIMIT = 32 * 1024 * 1024;
let assembler = null;  // {ready: Promise<{module, sink, entry}>}

function assemblerInstance() {
  if (!assembler) {
    const entry = {}, sink = {lines: null}, out = s => sink.lines?.push(s);
    entry.ready = freshInstance('assembler', {print: out, printErr: out, quit: (code, toThrow) => { throw toThrow; }})
      .then(module => ({module, sink, entry}));
    entry.ready.catch(() => retireAssembler(entry));
    assembler = entry;
  }
  return assembler.ready;
}

function retireAssembler(entry) {
  if (assembler === entry) assembler = null;
}

/** Returns a frozen copy of the five hill settings; throws on anything else. */
export function validateHill(hill) {
  if (!hill || typeof hill !== 'object') throw new TypeError('hill must be an object');
  for (const key of Object.keys(hill))
    if (!HILL_KEYS.includes(key)) throw new Error(`Unsupported hill setting: ${key}`);
  const h = {};
  for (const key of HILL_KEYS) {
    if (!Number.isSafeInteger(hill[key]) || hill[key] < 1) throw new RangeError(`Invalid ${key}`);
    h[key] = hill[key];
  }
  if (h.coreSize > 65535 || h.processes > 1000000 || h.cycles > 100000000 ||
      h.maxLength > h.minDistance || h.minDistance * 2 > h.coreSize)
    throw new RangeError('Invalid hill settings');
  return Object.freeze(h);
}

function nodeExitGuard() {
  const p = globalThis.process?.versions?.node ? globalThis.process : null;
  const prior = p?.exitCode;
  return () => { if (p) p.exitCode = prior; };
}

function writeString(module, text) {
  const bytes = new TextEncoder().encode(text);
  const ptr = module._malloc(bytes.length + 1);
  if (!ptr) throw new Error('Out of memory');
  module.HEAPU8.set(bytes, ptr);
  module.HEAPU8[ptr + bytes.length] = 0;
  return ptr;
}

/** Splits pMARS diagnostics into "Warning…"/"Error…" blocks and other lines. */
function splitDiagnostics(lines) {
  const warnings = [], errors = [], other = [];
  let current = null;
  for (const line of lines) {
    if (/^(Warning|Error)\b/.test(line)) {
      current = {list: line.startsWith('Error') ? errors : warnings, lines: [line]};
      current.list.push(current);
    } else if (current && /^\s/.test(line) && line.trim()) current.lines.push(line);
    else { current = null; if (line.trim() && !/^Number of warnings/.test(line)) other.push(line.trim()); }
  }
  const text = list => list.map(b => b.lines.join('\n'));
  return {warnings: text(warnings), errors: text(errors), other};
}

/**
 * Assembles one Redcode source for a hill. The assembler instance is shared by
 * the calls of a realm and replaced only after a trap (or an exit() of an older
 * build) or when its memory has grown (see ASSEMBLER_MEMORY_LIMIT); everything a call
 * returns is read before the next call can run.
 * Options: rounds (default 1, as the Python binding) sets the ROUNDS constant;
 * icws88 (default false) enforces ICWS'88 rules, as pMARS -8.
 */
export async function assemble(source, hill, {rounds = 1, icws88 = false} = {}) {
  if (typeof source !== 'string') throw new TypeError('source must be a string');
  if (new TextEncoder().encode(source).length > MAX_SOURCE_BYTES) throw new RangeError('Warrior source exceeds 64 KiB');
  if (source.includes('\0')) throw new RangeError('Warrior source contains a NUL character');
  const h = validateHill(hill);
  if (!Number.isSafeInteger(rounds) || rounds < 1 || rounds > MAX_ROUNDS) throw new RangeError('Invalid rounds');
  if (typeof icws88 !== 'boolean') throw new TypeError('icws88 must be boolean');
  const stderr = [];
  const {module, sink, entry} = await assemblerInstance();
  if (icws88 && !module._ex_set_icws88) throw new Error('This assembler build has no ICWS\'88 mode');
  // From here to the return nothing awaits, so no other call uses the instance meanwhile.
  const restore = nodeExitGuard();
  let status;
  sink.lines = stderr;
  try {
    const src = writeString(module, source);
    module._ex_set_icws88?.(icws88 ? 1 : 0);
    status = module._ex_assemble(src, h.coreSize, h.cycles, h.processes, h.maxLength, h.minDistance, rounds);
    module._free(src);
  } catch (e) {
    retireAssembler(entry);
    if (e?.name !== 'ExitStatus' && !(e instanceof WebAssembly.RuntimeError)) throw e;
    const d = splitDiagnostics(stderr);
    if (e instanceof WebAssembly.RuntimeError) d.other.push(`Assembler aborted: ${e.message}`);
    return {ok: false, errors: [...d.errors, ...d.other].join('\n'), warnings: d.warnings.join('\n')};
  } finally { sink.lines = null; restore(); }
  if (module.HEAPU8.length > ASSEMBLER_MEMORY_LIMIT) retireAssembler(entry);  // still readable below
  const d = splitDiagnostics(stderr);
  if (status < 0) throw new Error(`Assembler rejected settings: ${module.UTF8ToString(module._ex_message())}`);
  if (status !== 0) {
    const message = module.UTF8ToString(module._ex_message());
    const errors = d.errors.length ? [...d.errors, ...d.other] : [`Error:\n        ${message}`, ...d.other];
    return {ok: false, errors: errors.join('\n'), warnings: d.warnings.join('\n')};
  }
  const base = module._ex_result() >> 2;
  const heap = module.HEAP32;
  const count = heap[base], instructions = [];
  for (let i = 0; i < count; i++) instructions.push(Array.from(heap.subarray(base + 6 + 6 * i, base + 12 + 6 * i)));
  return {
    ok: true, instructions, start: heap[base + 1], pin: heap[base + 2] ? heap[base + 3] : null,
    name: module.UTF8ToString(module._ex_name()), author: module.UTF8ToString(module._ex_author()),
    warnings: d.warnings,
  };
}

class Fighter {
  #m; #hill; #out; #manyIn; #manyOut; #scratch = 0; #scratchInts = 0; #handles = new Set();

  constructor(module) {
    this.#m = module;
    this.#out = module._malloc(12);
    this.#manyIn = module._malloc(4 * MAX_WARRIORS);
    this.#manyOut = module._malloc(4 * MAX_WARRIORS * (MAX_WARRIORS + 1));
    if (!this.#out || !this.#manyIn || !this.#manyOut) throw new Error('Out of memory');
  }

  #live() { if (!this.#m) throw new Error('Fighter disposed'); return this.#m; }

  #check(status) {
    if (status < 0) throw new Error(`exMARS fighter: ${this.#m.UTF8ToString(this.#m._ex_error())}`);
    return status;
  }

  #handle(h) {
    if (!Number.isSafeInteger(h) || !this.#handles.has(h)) throw new RangeError('Unknown warrior handle');
    return h;
  }

  /** The current hill settings (frozen). */
  get hill() { return this.#hill; }

  /** Applies new hill settings; drops all loaded warriors. */
  configure(hill) {
    const m = this.#live(), h = validateHill(hill);
    this.#handles.clear();
    this.#hill = undefined;
    this.#check(m._ex_configure(h.coreSize, h.cycles, h.processes, h.maxLength, h.minDistance));
    this.#hill = h;
  }

  /** Safe early ties (default on), as the Python binding's early_exit. */
  setEarlyExit(on) {
    if (typeof on !== 'boolean') throw new TypeError('earlyExit must be boolean');
    this.#live()._ex_set_early_exit(on ? 1 : 0);
  }

  /** Loads an assemble() result ({instructions, start, pin}); returns a handle. */
  load(warrior) {
    const m = this.#live();
    if (!this.#hill) throw new Error('Fighter not configured');
    if (!warrior || typeof warrior !== 'object') throw new TypeError('warrior must be an object');
    if (warrior.ok === false) throw new Error('Cannot load a warrior that failed to assemble');
    const {instructions, start, pin = null} = warrior;
    if (!Array.isArray(instructions) || instructions.length < 1 || instructions.length > this.#hill.maxLength)
      throw new RangeError('instructions must hold 1..maxLength entries');
    if (!Number.isSafeInteger(start) || start < 0 || start >= instructions.length) throw new RangeError('Invalid start');
    if (pin !== null && (!Number.isSafeInteger(pin) || pin < -0x80000000 || pin > 0x7fffffff)) throw new RangeError('Invalid pin');
    const ints = instructions.length * 6;
    if (ints > this.#scratchInts) {
      if (this.#scratch) m._free(this.#scratch);
      this.#scratch = m._malloc(ints * 4);
      this.#scratchInts = this.#scratch ? ints : 0;
      if (!this.#scratch) throw new Error('Out of memory');
    }
    const heap = m.HEAP32, base = this.#scratch >> 2;
    instructions.forEach((t, i) => {
      if (!Array.isArray(t) || t.length !== 6 || t.some(n => !Number.isSafeInteger(n) || n < -0x80000000 || n > 0x7fffffff))
        throw new RangeError(`Instruction ${i} must be 6 32-bit integers`);
      heap.set(t, base + 6 * i);
    });
    const h = this.#check(m._ex_load(this.#scratch, instructions.length, start, pin === null ? 0 : 1, pin ?? 0));
    this.#handles.add(h);
    return h;
  }

  unload(handle) {
    const m = this.#live();
    this.#check(m._ex_unload(this.#handle(handle)));
    this.#handles.delete(handle);
  }

  #result() { const b = this.#out >> 2, heap = this.#m.HEAP32; return {wins: heap[b], ties: heap[b + 1], losses: heap[b + 2]}; }

  /**
   * One match of `rounds` rounds, P-space fresh for the match. `position` has the
   * meaning of the CLI's -F: a placement seed (0 = random), same results as pMARS.
   * Returns warrior a's {wins, ties, losses}.
   */
  fight(a, b, {rounds, position = 0, ...rest} = {}) {
    const m = this.#live();
    for (const key of Object.keys(rest)) throw new Error(`Unsupported fight option: ${key}`);
    this.#handle(a); this.#handle(b);
    if (!Number.isSafeInteger(rounds) || rounds < 1 || rounds > MAX_ROUNDS) throw new RangeError('Invalid rounds');
    const {coreSize, minDistance} = this.#hill;
    if (!Number.isSafeInteger(position) || (position !== 0 && (position < minDistance || position > coreSize - minDistance)))
      throw new RangeError('position must be 0 or minDistance..coreSize-minDistance');
    this.#check(m._ex_fight(a, b, rounds, position, this.#out));
    return this.#result();
  }

  /**
   * One match of 2..36 warriors (handles may repeat), as pMARS runs a multi-warrior
   * battle: placements from `position` as the CLI's -F (0 = random), starting order
   * rotated each round, P-space fresh per match and shared by equal PINs.
   * Returns {results}: results[i][0] = rounds warrior i ended dead, results[i][k] =
   * rounds it ended alive with k survivors (k = 1..n), in input order.
   */
  fightMany(handles, {rounds, position = 0, ...rest} = {}) {
    const m = this.#live();
    for (const key of Object.keys(rest)) throw new Error(`Unsupported fight option: ${key}`);
    if (!Array.isArray(handles) || handles.length < 2 || handles.length > MAX_WARRIORS)
      throw new RangeError(`handles must list 2..${MAX_WARRIORS} warriors`);
    handles.forEach(h => this.#handle(h));
    const n = handles.length, {coreSize, minDistance} = this.#hill;
    if (n * minDistance > coreSize) throw new RangeError('warriors * minDistance exceeds coreSize');
    if (!Number.isSafeInteger(rounds) || rounds < 1 || rounds > MAX_ROUNDS) throw new RangeError('Invalid rounds');
    const top = n === 2 ? coreSize - minDistance : MAX_FIXED_POSITION;
    if (!Number.isSafeInteger(position) || (position !== 0 && (position < minDistance || position > top)))
      throw new RangeError(`position must be 0 or ${minDistance}..${top}`);
    m.HEAP32.set(handles, this.#manyIn >> 2);
    this.#check(m._ex_fight_many(this.#manyIn, n, rounds, position, this.#manyOut));
    const base = this.#manyOut >> 2, heap = this.#m.HEAP32, results = [];
    for (let i = 0; i < n; i++) results.push(Array.from(heap.subarray(base + i * (n + 1), base + (i + 1) * (n + 1))));
    return {results};
  }

  /**
   * pMARS -P: one match playing every legal relative position in both starting
   * orders once, P-space kept between its rounds. `position` seeds the order
   * as -F does (0 = random); the order matters only to warriors using P-space.
   */
  exact(a, b, {position = 0, ...rest} = {}) {
    const m = this.#live();
    const {coreSize, minDistance} = this.#hill;
    if (Object.keys(rest).length) throw new TypeError('unknown option ' + Object.keys(rest)[0]);
    if (!Number.isSafeInteger(position) || (position !== 0 && (position < minDistance || position > coreSize - minDistance)))
      throw new RangeError('position must be 0 or minDistance..coreSize-minDistance');
    this.#handle(a); this.#handle(b);
    this.#check(m._ex_fight_exact(a, b, position, this.#out));
    return {...this.#result(), rounds: 2 * (coreSize - 2 * minDistance + 1)};
  }

  /** Releases the instance. The object can't be used afterwards. */
  dispose() {
    if (!this.#m) return;
    this.#m._ex_configure(0, 0, 0, 0, 0);   // frees the simulator and all warriors (returns an error by design)
    if (this.#scratch) this.#m._free(this.#scratch);
    this.#m._free(this.#out);
    this.#m._free(this.#manyIn);
    this.#m._free(this.#manyOut);
    this.#handles.clear();
    this.#m = null;
  }
}

/** Creates a fighter with its own Wasm instance, configured for `hill`. */
export async function createFighter(hill) {
  const h = validateHill(hill);
  const create = await factory('fighter');
  const f = new Fighter(await create({print: () => {}, printErr: () => {}}));
  f.configure(h);
  return f;
}
