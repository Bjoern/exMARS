"""pMARS compatibility regressions for the exMARS command-line engine.

Each case fights two fixtures and expects pMARS's wins/ties rows, in both the
default (safe early ties) and -X (exact) modes. The expectations come from
pMARS 0.9.6 (corewar.dev pmars_experiments, built with -DEXT94 -DPERMUTATE
-DRWLIMIT -DSERVER); every case fails on the exMARS that preceded the fix.

    python tests/pmars_compat/run.py path/to/exmars-fast.exe [--pmars path/to/pmars.exe]

--pmars also checks the expectations against a pMARS binary (run with -b -k).
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (name, fixed bug, options, warriors, expected rows "wins ties")
CASES = [
    ("seed_zero", "-F equal to the minimum separation gave rng seed 0, a fixed point",
     ["-r", "20", "-F", "100"], ["idle.red", "seed_zero.red"], ["0 19", "1 19"]),
    ("expression", "assembler grouped a-(b)/2-c/2 as a-((b)/2-c/2)",
     ["-r", "10", "-F", "4000"], ["expression.red", "idle.red"], ["0 10", "0 10"]),
    ("immediate_b", "immediate B operand was read before A side effects",
     ["-r", "10", "-F", "4000"], ["immediate_b.red", "idle.red"], ["0 10", "0 10"]),
    ("immediate_b_stp", "STP to an immediate B address after A post-increment",
     ["-r", "10", "-F", "4000"], ["immediate_b_stp.red", "idle.red"], ["0 10", "0 10"]),
    ("pin", "PIN was ignored, so equal PINs did not share P-space",
     ["-r", "10", "-F", "4000"], ["pin_writer.red", "pin_reader.red"], ["0 10", "0 10"]),
    ("wide_fields", "ADD/SUB reduced in 16 bits, wrong for cores above 32768",
     ["-s", "55440", "-r", "10", "-F", "4000"], ["wide_fields.red", "idle.red"], ["0 10", "0 10"]),
]
COMMON = ["-c", "1000"]


def rows(cmd):
    run = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE)
    out = run.stdout
    if "specialized build requires" in run.stdout + run.stderr:
        return None  # a fixed-settings build rejects this case's core or processes
    return [" ".join(line.split()[:2]) for line in out.strip().splitlines()[-2:]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exmars")
    ap.add_argument("--pmars")
    a = ap.parse_args()
    exmars = os.path.abspath(a.exmars)
    failures = 0
    for name, bug, options, warriors, expected in CASES:
        runs = [("default", [exmars, *COMMON, *options, *warriors]),
                ("-X", [exmars, "-X", *COMMON, *options, *warriors])]
        if a.pmars:
            runs.append(("pmars", [os.path.abspath(a.pmars), "-b", "-k", *COMMON, *options, *warriors]))
        for mode, cmd in runs:
            got = rows(cmd)
            if got is None:
                print(f"skip {name} ({mode}): settings rejected by this fixed build")
            elif got != expected:
                failures += 1
                print(f"FAIL {name} ({mode}): expected {expected}, got {got}  [{bug}]")
            else:
                print(f"ok   {name} ({mode})")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
