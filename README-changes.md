# Proposed changes for exMARS

These changes were prepared with AI assistance. They are separated so upstream
can review correctness and Ruby maintenance without accepting Python or speed
work. The original project README remains intact.

1. **Correctness:** parser expressions, PIN sharing, zero RNG seed, immediate-B
   side effects, arithmetic width and safe process-count termination (`-X` for
   full execution). See
   [README-correctness.md](README-correctness.md).
2. **Ruby maintenance:** Ruby 3.4 native APIs, P-space match isolation and PINs,
   deterministic placement, validation, reproducible Windows build and tests.
3. **Optional Python:** CPython extension, Warrior/Instruction helpers, in-memory
   assembly, build instructions and API: [README-python.md](README-python.md).
4. **Optional performance:** MOV.I shortcut, two-warrior queue caching, DAT-count
   early termination and checked fixed-hill builds: [README-performance.md](README-performance.md).

Patches are ordered against upstream f7dbc5e. Correctness alone is usable;
correctness + Ruby is the upstream maintenance proposal. Python and performance
are separate later commits. The exported series documents bases for PRs.
The original five branch tips were already published to Bjoern/exMARS, and
fork master points to the original benchmark tip (03c2602). This revision adds
commits and merges to the stage branches without rewriting those published
commits or changing master. The revised branches have not been pushed.
The exported five-patch series is a linear projection of stage trees, not a
replacement for the published branch history.

Validation: `make test`, `python tests/engine/run.py`, and the Ruby/Python
binding suites. Native differential tests compare complete state in exact mode
and survivors/death order/P-space in early mode. The reference is useful for
optimization equivalence; the independent pMARS regression fixtures detect
shared inherited bugs that differential tests alone cannot find.

Benchmark methodology and new results are in
[benchmarks/comparison/README.md](benchmarks/comparison/README.md). Historical timings describe particular corpora and compiler settings;
they are not promises for other hardware, workloads, or Python API throughput.
