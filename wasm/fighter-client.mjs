/**
 * Persistent exMARS fighter for browsers, module Workers and Node.
 *
 *   const w = await assemble(source, hill);  // fresh throwaway assembler instance per call
 *   const f = await createFighter(hill);     // one per Worker, kept for the whole run
 *   const h = f.load(w);                     // handle
 *   f.fight(h, h2, {rounds: 250, position: 1234});  // {wins, ties, losses}
 *   f.exact(h, h2);                          // {wins, ties, losses, rounds}
 *
 * hill = {coreSize, cycles, processes, maxLength, minDistance}. Instructions are
 * 6-int tuples [opcode, modifier, aMode, aValue, bMode, bValue] numbered as in
 * Warrior.py / insn.h. See README.md, "Persistent fighter".
 */
const HILL_KEYS = ['coreSize', 'cycles', 'processes', 'maxLength', 'minDistance'];
const MAX_SOURCE_BYTES = 65536;
const MAX_ROUNDS = 1000000;
const factories = new Map();

function factory(name) {
  if (!factories.has(name)) factories.set(name, import(`./${name}.mjs`).then(m => m.default));
  return factories.get(name);
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
 * Assembles one Redcode source for a hill in a fresh assembler instance (the
 * pMARS parser can exit(), so instances are never reused).
 * Options: rounds (default 1, as the Python binding) sets the ROUNDS constant.
 */
export async function assemble(source, hill, {rounds = 1} = {}) {
  if (typeof source !== 'string') throw new TypeError('source must be a string');
  if (new TextEncoder().encode(source).length > MAX_SOURCE_BYTES) throw new RangeError('Warrior source exceeds 64 KiB');
  if (source.includes('\0')) throw new RangeError('Warrior source contains a NUL character');
  const h = validateHill(hill);
  if (!Number.isSafeInteger(rounds) || rounds < 1 || rounds > MAX_ROUNDS) throw new RangeError('Invalid rounds');
  const stderr = [];
  const create = await factory('assembler');
  const restore = nodeExitGuard();
  let module, status;
  try {
    module = await create({print: s => stderr.push(s), printErr: s => stderr.push(s),
      quit: (code, toThrow) => { throw toThrow; }});
    const src = writeString(module, source);
    status = module._ex_assemble(src, h.coreSize, h.cycles, h.processes, h.maxLength, h.minDistance, rounds);
  } catch (e) {
    if (e?.name !== 'ExitStatus' && !(e instanceof WebAssembly.RuntimeError)) throw e;
    const d = splitDiagnostics(stderr);
    if (e instanceof WebAssembly.RuntimeError) d.other.push(`Assembler aborted: ${e.message}`);
    return {ok: false, errors: [...d.errors, ...d.other].join('\n'), warnings: d.warnings.join('\n')};
  } finally { restore(); }
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
  #m; #hill; #out; #scratch = 0; #scratchInts = 0; #handles = new Set();

  constructor(module) {
    this.#m = module;
    this.#out = module._malloc(12);
    if (!this.#out) throw new Error('Out of memory');
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

  /** Every legal relative position in both starting orders, one round each. */
  exact(a, b) {
    const m = this.#live();
    this.#handle(a); this.#handle(b);
    this.#check(m._ex_fight_exact(a, b, this.#out));
    const {coreSize, minDistance} = this.#hill;
    return {...this.#result(), rounds: 2 * (coreSize - 2 * minDistance + 1)};
  }

  /** Releases the instance. The object can't be used afterwards. */
  dispose() {
    if (!this.#m) return;
    this.#m._ex_configure(0, 0, 0, 0, 0);   // frees the simulator and all warriors (returns an error by design)
    if (this.#scratch) this.#m._free(this.#scratch);
    this.#m._free(this.#out);
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
