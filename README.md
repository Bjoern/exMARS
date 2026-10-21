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


## Automated builds

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
git tag v1.0.0
git push origin v1.0.0
```

The tagged commit must contain the workflow. Re-running a tag's workflow updates
the archive attachments on its existing release. Branch and pull-request builds
only upload temporary workflow artifacts.

Have fun!

Martin
