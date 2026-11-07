# Optional CPython binding

AI-assisted addition; the C CLI and Ruby extension do not require Python.

Build with `python -m pip install .`. On Windows with MinGW GCC and CPython's
import library, `python build_fast.py --python-extension` writes into `bin`.
The extension must match the running Python version/ABI and architecture.
Tested configurations: CPython 3.14 and 3.15 x64 with MinGW GCC 16.1.0. The setuptools
route selects C17 for MSVC or GNU C17 elsewhere; those toolchains need their own
validation. For a helper build, set `PYTHONPATH` to the absolute `bin` directory
before running the example (PowerShell: `$env:PYTHONPATH = (Resolve-Path bin).Path`).

```python
import Mars
m = Mars.Mars(2, 8000, 80000, 8000, 100, 100)
a = m.parse_file("first.red")
b = m.parse_string(";redcode-94\n;assert 1\nmov.i 0,1\nend\n")
m.rounds = 200
m.fixed_position = 4000
print(m.fight(a, b).results_str())
```

Constructor arguments: warriors, coresize, cycles, max_processes, max_length,
min_distance. `Mars.standard(2)` and `Mars.tiny(2)` are convenience constructors.
`rounds` and `fixed_position` are writable. Each fight is a fresh match; P-space
persists across rounds within that call. `warrior.pin = None` disables sharing;
equal integer PINs share, including zero. Warrior objects expose instructions,
startPos, name, authorName, fileName and pin. Comment text that is not UTF-8
(old Latin-1 warriors) is decoded with U+FFFD replacement characters. `scores()`,
`wins(i, survivor_count)`, `losses(i)` and `results_str()` expose results.

Errors raise exceptions and never end the process. `parse_string` and
`parse_file` raise `ValueError` for code that does not assemble, including
pMARS's limit of 9 distinct errors or warnings (the message starts with
"too many errors or warnings"), `MemoryError` when memory runs out and
`RuntimeError` for an internal assembler error. `fight()` raises `ValueError`
for a warrior with no instructions or more than max_length, a start position
outside it, or instruction data outside Warrior.py's numbering. The assembler
still prints its errors and warnings to stdout/stderr.

Run `python tests/python_bindings/run.py bin` for binding regressions.
Safe early ties are enabled by default. Set `m.early_exit = False` for exact
final core/queue state; scores and P-space are preserved in both modes.
Do not call a simulator instance concurrently from different threads.

Safe process-count termination defaults to `True` on this branch. Set
`m.early_exit = False` for full execution; both modes preserve results/P-space.
