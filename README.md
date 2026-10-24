# exMARS - Exhaust Memory Array Redcode Simulator

exMARS is a redcode simulator, just like Exhaust and pMARS. In fact, I
have shamelessly taken sourcecode from both  [pMARS](http://www.koth.org/pmars/), [exhaust](http://www.cs.helsinki.fi/u/jpihlaja/exhaust/exhaust.html), some
ideas from [qmars](https://github.com/martinus/qmars) plus further optimizations, a higher level
interface for [Ruby](http://www.ruby-lang.org/en/), and shaked everything very well. 

The resulting programm has the following main features:

* Uses the parser from pMARS, so no previous parsing is neccessary.
* Speed: 50% faster than pmars on a pentiumIII, and about twice as fast
  on a pentium 4 on average than pmars (using gcc 3.3.1).
* Rewritten the C code in a more object oriented way, which allows
  different Mars'es at the same time in the same program. 
* Ruby interface: finally a really fast mars can be used in a high level
  programming language. (see test.rb for an example usage)


Have fun!

Martin

---

## AI-added documentation

The original README by Martin is preserved above, including his sign-off.
Everything below this heading was added by AI (OpenAI Codex) to document
the changes in this fork and the current build and release workflow.

### Changes from the original exMARS

The following documents describe the AI-assisted updates and experiments in
this fork. The [change overview](README-changes.md) explains the original
staging of the correctness, Ruby, Python, performance and benchmark work;
its branch and publication notes describe that preparation stage.

- [Correctness fixes](README-correctness.md): expression evaluation, PIN and
  P-space sharing, placement seeds, instruction side effects, arithmetic
  overflow, and safe early termination with an exact-execution option.
- [Ruby binding updates](tests/ruby_bindings/README.md): Ruby 3.4 API support,
  match isolation, PIN sharing, deterministic placement, input validation,
  and build and regression-test instructions.
- [Python bindings](README-python.md): the added CPython extension,
  Warrior/Instruction helpers, in-memory assembly, API examples, and build
  and test instructions.
- [Performance changes](README-performance.md): MOV.I shortcuts, cached
  two-warrior process queues, DAT-count early termination, and optional
  builds specialized for fixed hill settings.
- [Cross-engine benchmarks](benchmarks/comparison/README.md): comparisons
  with original exMARS and other simulators, including methodology, raw
  results, reproduction instructions, and limits on interpreting the timings.
- [Packed-cell experiments](benchmarks/cell_layout/README.md): Apple M1
  measurements of alternative cell layouts and the death-table allocation
  fix discovered during validation. The experimental layouts did not replace
  the production layout.

The original README's Pentium-era speed claims above are historical. See the
benchmark reports for measurements of the updated engine on specific hardware
and workloads. Automated builds and tagged releases are described below.

### Release version

This fork's version is **0.01-ai.1**, with release tag **`v0.01-ai.1`**:
**AI-assisted fork of exMARS 0.01**. Martin's
[original release page](https://martin.ankerl.com/2006/07/29/corwars-simulators-overview/)
lists `exmars-0.01.tar.gz`. Fork revisions use `0.01-ai.1`, `0.01-ai.2`, and so on;
this is a custom version scheme, not strict semantic versioning.

See [the latest published release](https://github.com/Bjoern/exMARS/releases/latest)
for the current release tag and binary downloads, or browse
[all releases](https://github.com/Bjoern/exMARS/releases). Builds from branch pushes
are development snapshots, identified by their workflow run and commit.

Run `exmars --version` (or `exmars.exe --version` on Windows) to see the CLI
version. It is declared as `EXMARS_VERSION` in `pmars.c`; update it before
tagging each new fork revision. The Python package retains its separate
package version `0.1.0` in `setup.py`.

### Automated builds

**[Download development builds: open successful build runs](https://github.com/Bjoern/exMARS/actions/workflows/build.yml?query=is%3Asuccess)**

1. Sign in to GitHub and open the link above.
2. Click the newest successful run's title to open its **Summary** page.
3. Scroll down to **Artifacts**, below the jobs, and click the download for
   your platform listed in the table below. If you opened an individual job's
   logs, select **Summary** in the left sidebar first.

Artifacts belong to individual workflow runs; they are not listed on the
repository's main **Code** page. Each platform uploads its archive only after
its build and tests pass. If no successful runs are listed, check
[all build runs](https://github.com/Bjoern/exMARS/actions/workflows/build.yml)
for a running or failed build. Artifacts older than 30 days may have expired.

For tagged versions, use **[Release downloads](https://github.com/Bjoern/exMARS/releases)**
and expand **Assets** under the release. These downloads do not expire after
30 days; they appear after a tag has been pushed and all platform jobs pass.

The [Build binaries workflow](https://github.com/Bjoern/exMARS/actions/workflows/build.yml)
builds and tests the standalone command-line simulator on every push to `main` or
`master` (including merged pull requests), on pull requests targeting either branch, on any tag
push, and on manual workflow runs.

Downloads are available under **Artifacts** on each successful workflow run:

| Artifact | Platform |
| --- | --- |
| `exmars-windows-x86_64` | Windows, Intel/AMD 64-bit |
| `exmars-macos-x86_64` | macOS, Intel |
| `exmars-macos-arm64` | macOS, Apple Silicon |
| `exmars-linux-x86_64` | Linux, Intel/AMD 64-bit |
| `exmars-linux-arm64` | Linux, ARM 64-bit |

Artifacts are retained for 30 days. Extract the downloaded artifact, then the
archive inside it. Unix builds use `.tar.gz` to preserve executable permissions;
Windows builds contain `exmars.exe` in a `.zip` and do not require MSYS2.
These are generic optimized builds; Python and Ruby bindings are not included.
Linux binaries are built on Ubuntu 22.04 (glibc 2.35 or newer required); macOS
binaries are built on macOS 15. These builds are not signed or notarized.

Tag pushes also publish a [GitHub Release](https://github.com/Bjoern/exMARS/releases)
with all five archives and automatically generated release notes, after every
platform's build and tests pass. Release downloads do not have the 30-day
artifact expiration. For example, after committing the workflow changes:

```sh
git tag v0.01-ai.1
git push origin v0.01-ai.1
```

The tagged commit must contain the workflow. Re-running a tag's workflow updates
the archive attachments on its existing release. Branch and pull-request builds
only upload temporary workflow artifacts.

## WebAssembly

Browser/Worker and Node modules, tournament API, and comparisons against pMARS
and original exMARS are documented in [wasm/README.md](wasm/README.md).
Build with `make wasm PMARS_SOURCE=../pmars_experiments/src` after activating Emscripten.
