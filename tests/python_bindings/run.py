"""Regression tests for the Python binding (Mars module).

    py -3.14 tests/python_bindings/run.py BIN_DIR [--exmars EXE] [--pmars EXE]
           [--hills corewar.dev/warriors_public] [--sample 12] [--rounds 200]

BIN_DIR holds Mars.<abi>.pyd (or .so) and Warrior.py, e.g. bin/ or a bundle's
bin/generic. Checks:
  * every fight() is a fresh match: P-space persists between its rounds only,
    so reusing one instance gives the results of a fresh instance per fight;
  * equal PINs share P-space;
  * fixed_position reproduces the CLI's -F placements (seed 0 included);
  * with --hills: a sample of '94nop and P-space hill pairs gives the same
    wins/ties on one reused instance, fresh instances, the CLI (default
    BIN_DIR/exmars-fast.exe) and, with --pmars, pMARS.
"""
import argparse
import hashlib
import json
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "..", "pmars_compat")

ap = argparse.ArgumentParser()
ap.add_argument("bin_dir")
ap.add_argument("--exmars")
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

if args.hills:
    for suite in ("94top50", "psptop50"):
        folder = os.path.join(args.hills, suite)
        d = json.load(open(os.path.join(folder, "suite.benchmark.json"), encoding="utf-8-sig"))
        p = d["parameters"]
        sep = p.get("distance") or p["maxLength"]
        ws = d["warriors"]
        pairs = random.Random(suite).sample([(i, j) for i in range(len(ws)) for j in range(i, len(ws))], args.sample)
        shared = Mars.Mars(2, p["coreSize"], p["cycles"], p["tasks"], p["maxLength"], sep)
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
            fresh.rounds, fresh.fixed_position = args.rounds, seed
            new = rows(fresh.fight(*(fresh.parse_file(f) for f in files)))
            common = ["-s", str(p["coreSize"]), "-c", str(p["cycles"]), "-p", str(p["tasks"]), "-l", str(p["maxLength"]),
                      "-d", str(sep), "-r", str(args.rounds), "-F", str(seed)]
            label = f"{suite} {ws[i]} vs {ws[j]} -F {seed}"
            check(f"{label}: reused instance = fresh", reused, new)
            check(f"{label}: Python = CLI", reused, cli([exmars, *common, *files]))
            if args.pmars:
                check(f"{label}: Python = pMARS", reused, cli([args.pmars, "-b", "-k", "-S", str(p["pspace"]), *common, *files]))

print(f"{failures} failure(s)")
sys.exit(1 if failures else 0)
