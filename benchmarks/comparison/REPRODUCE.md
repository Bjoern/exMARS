# Reproduce this Windows comparison

The scripts use Windows QueryPerformanceCounter and native GCC/G++ executables.
Use the same compiler for every engine; measured here with GCC/G++ 16.1.0.
Paths below assume this directory is `benchmarks/comparison` in the patched fork.
In the development workspace it is `perf/comparison`, with the same layout depth.

1. Build the fork's Python module: `python build_fast.py --python-extension`.
2. Prepare inputs with `python benchmarks/comparison/assemble.py --corpus-root
   PATH_TO_CORPORA --module-dir bin`. The corpus directory must contain the
   original `94top50` and `nanobm07` folders. The recorded `corpora.json` supplies
   the exact file order; source and assembled-input hashes are checked.
   Warrior sources are not republished in these patches; `corpora.json` identifies
   the exact original filenames/hashes used. Four nano sources lack assert
   directives and produce harmless parser warnings.
3. Extract the supplied source snapshot:
   `python benchmarks/comparison/prepare_sources.py --snapshot
   PATH_TO/benchmark-sources.zip`. It contains the exact source trees and generated
   fMARS C files used here, with source notices/licenses. It changes no installed
   software. The snapshot is supplied beside the patch files, not committed into
   the fork. Check its SHA-256 against the series artifact manifest.
4. Build with `python benchmarks/comparison/build.py`, then
   `python benchmarks/comparison/build_fmars.py` and the same command with
   `--corrected`. Override `--exmars-source` or `--pmars-source` if needed; changing
   source/version requires fresh validation and must be disclosed.
5. Run `python benchmarks/comparison/run.py --passes 3 --nano-repeats 10
   --output results-new.json`. Output files must be new; results are written after
   each completed engine. This can take many minutes. Do not run other CPU-heavy
   work concurrently. Use `--passes 1 --nano-repeats 1` for a short validation
   (still all pairings), with a different output filename.

The published `results.json` deduplicates repeated score arrays through each
run's `score_set` key into the top-level `score_sets` dictionary. This preserves
every pair score and timing. `compact_results.py INPUT OUTPUT` performs and
checks that lossless conversion; `summarize.py` supports both forms and validates
all 54 runs before producing the table in `README.md`.

The snapshot avoids a Guile dependency. To regenerate from downloaded upstream
sources, `prepare_sources.py --pmars-source PATH_TO_EXACT_PMARS_SRC` retrieves
Exhaust/fMARS using official FreeBSD archive SHA-256 values, and qMARS/Exhaust-MA/
original exMARS at pinned GitHub commits. The pMARS tree is supplied locally and
checked against the recorded source hashes. Then run `prepare_fmars_set.py`.

For fMARS generation, Guile 3 works after removing the obsolete syncase import.
The supplied `generate_fmars.sh` expects workspace-local Guile libraries at
`dist/guile/root/usr`. Here Ubuntu 24.04's `apt-get download guile-3.0
 guile-3.0-libs libgc1` and `dpkg-deb -x` unpacked those packages into that folder;
no system package installation was needed. Run the shell script in WSL with
argument `fmars-0.0.207`, then `fmars-corrected`. The generated files compile with
native Windows GCC. See `METHOD.md` for the semantic/timing differences.

`fmars_divmod_test.c` can be compiled against each fMARS directory's `fmars.c`,
`fm_sim.c` and `fm_asm.c`, using `-I DIRECTORY -O3 -fno-strict-aliasing`, to reproduce
the reduced arithmetic defect. The corrected generator patch is diagnostic and
is not part of any exMARS engine/binding patch.
