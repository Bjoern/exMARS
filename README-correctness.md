# Correctness changes

The CLI now preserves assembled PIN sharing, uses the bounded pMARS 0.9.6
expression evaluator, handles the zero placement seed, reads immediate B fields
after A side effects, and reduces ADD/SUB/MUL without narrow/signed overflow.
The unsafe SPL early tie is removed: remaining STP writes must execute.
Safe process-count termination is enabled by default: every surviving warrior
must have strictly more processes than its remaining execution turns, and no
STP may exist anywhere in core. Results and all P-space are preserved; final
transient core/queues can differ. `-X` executes fully; `-E` enables the shortcut
(last option wins). Direct C callers set `mars_t.earlyExit` explicitly.
Queue caching, the immediate/direct MOV.I fast path, DAT-count termination and
fixed-hill builds belong only to the optional performance stage.

Run `python tests/engine/check_early.py` for positive early exits, strict process
boundaries, pending/dormant STP, and 4,160 results/P-space cases in both modes.

Run `make test` (Python 3 is needed only for the test driver). Native checks:

```
cc -std=gnu17 -O2 -ftrapv -I. tests/engine/mul.c sim.c pspace.c -o test-mul
./test-mul
cc -std=gnu17 -O2 -I. tests/engine/pspace_endgame.c sim.c pspace.c -o test-pspace
./test-pspace
```

Validated with GCC 16.1.0 on Windows x64: all 12 CLI cases, independent MUL oracle
at four core sizes, and the two-turn SPL/STP P-space regression pass.
The evaluator is adapted from pMARS 0.9.6 (corewar.dev/pmars_experiments);
original source notices remain. No Python binding or optimized scheduler is
required. Changes and tests were prepared with AI assistance and need upstream
review like any other contribution.
