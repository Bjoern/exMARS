"""Regression tests for the Python binding (Mars module).

    py -3.14 tests/python_bindings/run.py BIN_DIR [--exmars EXE] [--pmars EXE]
           [--hills corewar.dev/warriors_public] [--sample 12] [--rounds 200]

BIN_DIR holds Mars.<abi>.pyd (or .so) and Warrior.py, e.g. bin/ or a bundle's
bin/generic. Checks:
  * every fight() is a fresh match: P-space persists between its rounds only,
    so reusing one instance gives the results of a fresh instance per fight;
  * equal PINs share P-space;
  * fixed_position reproduces the CLI's -F placements (seed 0 included);
  * parsing does not leak, and errors raise exceptions instead of ending the
    process (too many assembly errors, invalid warriors passed to fight());
  * with --hills: a sample of '94nop and P-space hill pairs gives the same
    wins/ties on one reused instance, fresh instances, the CLI (default
    BIN_DIR/exmars-fast.exe) and, with --pmars, pMARS.
"""
import argparse
import gc
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "..", "pmars_compat")

ap = argparse.ArgumentParser()
ap.add_argument("bin_dir")
ap.add_argument("--exmars")
ap.add_argument("--exact", action="store_true", help="Disable safe early termination")
ap.add_argument("--pmars")
ap.add_argument("--hills")
ap.add_argument("--sample", type=int, default=12)
ap.add_argument("--rounds", type=int, default=200)
args = ap.parse_args()
bin_dir = os.path.abspath(args.bin_dir)
sys.path.insert(0, bin_dir)
import Mars  # noqa: E402

exmars = os.path.abspath(args.exmars or os.path.join(bin_dir, "exmars-fast.exe" if os.name == "nt" else "exmars-fast"))
failures = 0


def check(name, got, expected):
    global failures
    if got == expected:
        print(f"ok   {name}")
    else:
        failures += 1
        print(f"FAIL {name}: expected {expected}, got {got}")


def rows(m):
    return m.results_str().split("\n")[:2]


def cli(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    return [" ".join(line.split()[:2]) for line in out.strip().splitlines()[-2:]]


def standard(rounds, position=0):
    m = Mars.Mars(2, 8000, 80000, 8000, 100, 100)
    assert m.early_exit is True
    m.early_exit = not args.exact
    assert m.early_exit == (not args.exact)
    m.rounds = rounds
    m.fixed_position = position
    return m


# P[0] starts at CORESIZE-1 in a fresh match and is the last result afterwards.
FIRST_ROUND_ONLY = """;redcode-94
;name first round only
;assert CORESIZE == 8000
        ldp.ab #0, r
        seq.ab #7999, r
        dat    0, 0
        jmp    0
r       dat    0, 0
end
"""
# Stores its marker in round 1 and checks it in every later round of the match.
def marker(n):
    return f""";redcode-94
;name marker {n}
;assert CORESIZE == 8000
        ldp.ab #0, r
        sne.ab #7999, r
        stp.ab #{n}, #1
        ldp.ab #1, r
        seq.ab #{n}, r
        dat    0, 0
        jmp    0
r       dat    0, 0
end
"""

m = standard(1, 4000)
first, idle = m.parse_string(FIRST_ROUND_ONLY), m.parse_file(os.path.join(FIXTURES, "idle.red"))
check("fresh P-space: first fight", rows(m.fight(first, idle)), ["0 1", "0 1"])
check("fresh P-space: second fight on the same instance", rows(m.fight(first, idle)), ["0 1", "0 1"])

# Ownership: after an even-round fight each warrior must still find its own marker.
m = standard(2, 4000)
a, b = m.parse_string(marker(11)), m.parse_string(marker(22))
for rounds in (2, 3, 2, 4, 1):
    m.rounds = rounds
    check(f"P-space ownership, {rounds}-round fight after reuse", rows(m.fight(a, b)), [f"0 {rounds}", f"0 {rounds}"])

m = standard(10, 4000)
writer, reader = (m.parse_file(os.path.join(FIXTURES, f)) for f in ("pin_writer.red", "pin_reader.red"))
check("parsed PIN", (writer.pin, reader.pin), (123, 123))
check("PIN sharing", rows(m.fight(writer, reader)), ["0 10", "0 10"])
writer.pin = None
check("PIN removed in Python", rows(m.fight(writer, reader)), ["10 0", "0 0"])

m = standard(20, 100)
check("fixed_position = minimum separation (seed 0)",
      rows(m.fight(m.parse_file(os.path.join(FIXTURES, "idle.red")), m.parse_file(os.path.join(FIXTURES, "seed_zero.red")))),
      ["0 19", "1 19"])
try:
    m.fixed_position = 99
    check("fixed_position below minimum separation rejected", "accepted", "ValueError")
except ValueError:
    check("fixed_position below minimum separation rejected", "ValueError", "ValueError")

# Parsing must not leak Python objects: parse_string once leaked name, authorName,
# startPos and fileName (four objects per warrior), which adds up in an evolver.
m = Mars.Mars(2, 80, 800, 80, 5, 5)
source = ";redcode-94\n;name leak\n;author test\n;assert 1\nspl.b #3, }1\nmov.i 2, }1\ndjn.f -1, {-2\nend\n"
for _ in range(200):
    m.parse_string(source)
gc.collect()
before = sys.getallocatedblocks()
for _ in range(5000):
    m.fight(m.parse_string(source), m.parse_string(source))
gc.collect()
growth = sys.getallocatedblocks() - before
check("parse_string and fight do not leak (allocated blocks after 10000 parses)",
      "flat" if growth < 500 else f"+{growth}", "flat")

# Comment text that is not UTF-8 (many old warriors are Latin-1) must not fail the parse.
with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, "latin1.red")
    with open(path, "wb") as f:
        f.write(";redcode-94\n;name Caf\xe9\n;assert 1\nmov.i 0, 1\nend\n".encode("latin-1"))
    try:
        check("non-UTF-8 name parses", m.parse_file(path).name, "Caf�")
    except Exception as error:
        check("non-UTF-8 name parses", repr(error), "Caf�")


def outcome(call):
    try:
        call()
        return "accepted"
    except Exception as error:
        return type(error).__name__


# pMARS gives up after 9 distinct errors or warnings. The assembler used to exit()
# there, which killed the Python process; now it raises ValueError and the same
# instance keeps working.
TOO_MANY = "".join(f"xyz{i} {i}\n" for i in range(12))
m = Mars.Mars(2, 8000, 80000, 8000, 100, 100)
devnull, saved = os.open(os.devnull, os.O_WRONLY), (os.dup(1), os.dup(2))
sys.stdout.flush(); sys.stderr.flush()
os.dup2(devnull, 1); os.dup2(devnull, 2)          # the assembler prints each error
try:
    too_many = outcome(lambda: m.parse_string(TOO_MANY))
    try:
        m.parse_string(TOO_MANY)
    except ValueError as error:
        message = str(error)
    with tempfile.TemporaryDirectory() as tmp:     # removing it fails if the file stayed open
        path = os.path.join(tmp, "too_many.red")
        with open(path, "w") as f:
            f.write(TOO_MANY)
        too_many_file = outcome(lambda: m.parse_file(path))
    gc.collect()
    before = sys.getallocatedblocks()
    for _ in range(300):
        outcome(lambda: m.parse_string(TOO_MANY))
    gc.collect()
    abort_growth = sys.getallocatedblocks() - before
    after = m.parse_string(";assert 1\nmov.i 0, 1\n")
finally:
    sys.stdout.flush(); sys.stderr.flush()
    os.dup2(saved[0], 1); os.dup2(saved[1], 2)
    for fd in (*saved, devnull):
        os.close(fd)
check("too many errors: parse_string raises", too_many, "ValueError")
check("too many errors: message", message.startswith("too many errors or warnings"), True)
check("too many errors: parse_file raises and closes the file", too_many_file, "ValueError")
check("too many errors: repeated aborts do not leak", "flat" if abort_growth < 100 else f"+{abort_growth}", "flat")
check("too many errors: the instance still assembles", len(after.instructions), 1)

# fight() checks what Python hands it: warrior->code holds maxWarriorLength
# instructions, and an empty warrior used to reach check_sanity()'s exit().
m = Mars.Mars(2, 80, 800, 80, 5, 5)
good = m.parse_string(";assert 1\nmov.i 0, 1\n")


def warrior(instructions, start=0):
    w = m.parse_string(";assert 1\nmov.i 0, 1\n")
    w.instructions, w.startPos = instructions, start
    return w


ins = good.instructions[0]
bad_opcode = m.parse_string(";assert 1\nmov.i 0, 1\n")
bad_opcode.instructions[0].data = [99, 0, 0, 0, 0, 1]
check("fight: no instructions", outcome(lambda: m.fight(warrior([]), good)), "ValueError")
check("fight: more than max length", outcome(lambda: m.fight(warrior([ins] * 6), good)), "ValueError")
check("fight: startPos out of range", outcome(lambda: m.fight(warrior([ins], 1), good)), "ValueError")
check("fight: opcode out of range", outcome(lambda: m.fight(bad_opcode, good)), "ValueError")
check("fight: still works afterwards", outcome(lambda: m.fight(warrior([ins] * 5), good)), "accepted")

if args.hills:
    for suite in ("94top50", "psptop50"):
        folder = os.path.join(args.hills, suite)
        d = json.load(open(os.path.join(folder, "suite.benchmark.json"), encoding="utf-8-sig"))
        p = d["parameters"]
        sep = p.get("distance") or p["maxLength"]
        ws = d["warriors"]
        pairs = random.Random(suite).sample([(i, j) for i in range(len(ws)) for j in range(i, len(ws))], args.sample)
        shared = Mars.Mars(2, p["coreSize"], p["cycles"], p["tasks"], p["maxLength"], sep)
        shared.early_exit = not args.exact
        shared.rounds = args.rounds
        parsed = {}
        for i, j in pairs:
            files = [os.path.join(folder, ws[k]) for k in (i, j)]
            seed = sep + 1 + int(hashlib.sha256(f"{ws[i]}:{ws[j]}".encode()).hexdigest(), 16) % (p["coreSize"] - 2 * sep)
            for f in files:
                parsed.setdefault(f, shared.parse_file(f))
            shared.fixed_position = seed
            reused = rows(shared.fight(*(parsed[f] for f in files)))
            fresh = Mars.Mars(2, p["coreSize"], p["cycles"], p["tasks"], p["maxLength"], sep)
            fresh.early_exit = not args.exact
            fresh.rounds, fresh.fixed_position = args.rounds, seed
            new = rows(fresh.fight(*(fresh.parse_file(f) for f in files)))
            common = ["-s", str(p["coreSize"]), "-c", str(p["cycles"]), "-p", str(p["tasks"]), "-l", str(p["maxLength"]),
                      "-d", str(sep), "-r", str(args.rounds), "-F", str(seed)]
            label = f"{suite} {ws[i]} vs {ws[j]} -F {seed}"
            check(f"{label}: reused instance = fresh", reused, new)
            check(f"{label}: Python = CLI", reused, cli([exmars, *(["-X"] if args.exact else []), *common, *files]))
            if args.pmars:
                check(f"{label}: Python = pMARS", reused, cli([args.pmars, "-b", "-k", "-S", str(p["pspace"]), *common, *files]))

print(f"{failures} failure(s)")
sys.exit(1 if failures else 0)
