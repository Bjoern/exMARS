# Optional simulator optimizations

AI-assisted changes, kept separate from the correctness/Ruby proposals.
The generic engine retains runtime hill settings. It adds a MOV.I immediate/direct
shortcut, cached local process queues for two warriors, and conservative early
ties. Early ties preserve results and P-space; use CLI `-X` or binding
`early_exit = false` (Python `False`) when final core/queue state matters.

Build: `make` or `python build_fast.py`. The latter also builds a reference
interpreter; `--python-extension` builds Windows CPython with MinGW. Ruby:
`ruby build_ruby.rb`. Build outputs are isolated in `bin/`.

Optional specialization: `make specialized` fixes core/process limits at 8000
but keeps cycles dynamic. Or use `python build_fast.py --fixed-coresize 80
--fixed-processes 80 --max-length 5 --min-distance 5`. Specialized builds reject
incompatible runtime settings. Defaults are 8000 core/processes, 80000 cycles,
100 length/separation. CLI `-l` selects maximum warrior length.

No speed percentage should be inferred by multiplying gains from separate
experiments. See the [benchmark report](benchmarks/comparison/README.md)
and its raw data for new cross-engine measurements and their workload/semantic limits.

The original README's historical Pentium-era claims remain historical.
