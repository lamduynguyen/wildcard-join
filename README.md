# Reproducibility package: wildcard joins and filters

Artifact for the paper "Teach Your Database to LIKE Strings via Efficient
Wildcard Joins and Filters", under submission for VLDB 2027.

This repository is what the Artifact Availability block points at, so it is the
first thing a reviewer touches. Read the state section below before spending
time on it: parts of it work today and parts of it do not.

## Layout

| Path | What is in it |
| - | - |
| `src/` | The wildcard join prototype. Skeleton and segment decomposition of `LIKE` patterns, the positional constraint checks, the ART index and the matcher. |
| `test/` | GoogleTest suites for the matcher and the automaton. |
| `third_party/croaring/` | Vendored CRoaring 1.3.0, single header amalgamation. |
| `reproducibility/` | Schema, load scripts and the benchmark queries for the HackerNews evaluation. |
| `bin/` | The three system binaries the evaluation compares, plus `SHA256SUMS`. |
| `docs/umbra-binaries.md` | What those binaries are, what they need, and what cannot be rebuilt. |

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Needs a C++23 compiler, CMake 3.24 or newer, and `fmt` and `oneTBB` findable by
`find_package`. On Ubuntu 24.04 that is `libfmt-dev` and `libtbb-dev`. Add
`-DENABLE_TESTING=ON` for the test suite, which also needs `libgtest-dev`.

There is no pinning yet, so the versions you get are the host's. See
`THIRD_PARTY_LICENSES.md` for what server3 resolves them to.

## The binaries in `bin/`

Three x86-64 ELF executables are committed here, unstripped, 177.7 MB between
them.

`duckdb` is a stock DuckDB release, `v1.4.4 (Andium) 6ddac802ff`, MIT licensed.
It runs anywhere current and it can be obtained from upstream instead of taken
on trust from here.

`umbraOurs` and `umbraNaive` are Umbra. Umbra is closed source. They cannot be
rebuilt by anyone outside the group that produced them, there is no source in
this repository that corresponds to them, and there is no way for a reader to
verify that the binary named `umbraOurs` contains the algorithm the paper
describes. That is a limitation of this artifact and it should be read as one.

They also do not currently run on Ubuntu 24.04. Both link against libraries at
versions the archive does not carry, including `libLLVM.so.21.1`,
`libre2.so.11`, `libboost_context.so.1.90.0`, `libthrift-0.22.0.so` and
`libabsl_synchronization.so.20260107`. Checked on three Ubuntu 24.04.4 hosts.
`docs/umbra-binaries.md` has the full list, the sha256 values, the build ids
and the loader's error message.

To check the files you have are the files that were committed:

```
cd bin && sha256sum -c SHA256SUMS
```

## Dataset

The HackerNews dataset lives at
https://huggingface.co/datasets/lamduynguyen/hackernews. `reproducibility/README.md`
has the current instructions.

Those instructions are not yet reproducible: the URL carries no revision and no
checksums, and two of the three CSV files they ask you to place in `dataset/`
are neither in this repository nor generatable from anything in it. A
`fetch_dataset.sh` with a pinned revision and per file checksums is an open
item.

## State of this artifact

Being direct about it, because a reviewer finding these out one at a time is
worse than reading them here.

Works today:

* The prototype configures and builds from the repository root.
* The test suite builds and passes.
* `bin/duckdb` runs and is a verifiable upstream release.
* `bin/SHA256SUMS` lets you check all three binaries.

Does not work today:

* `bin/umbraOurs` and `bin/umbraNaive` do not run on Ubuntu 24.04.
* The dataset cannot be fetched reproducibly, and two of the three input files
  are not obtainable at all.
* There is no `run_all.sh`, so there is no single command that goes from a
  clean clone to the numbers in the paper, and no statement of how long a full
  run takes.
* Dependencies are not pinned, so two machines build two different things.
* There is no CI, so none of the above is caught automatically.

Each of these is tracked on the issues in this repository. Work lands here
first and goes upstream by hand once it is reviewed.

## Licence and citation

MIT, see `LICENSE`. Third party components and the licensing of the
committed binaries are in `THIRD_PARTY_LICENSES.md`. `CITATION.cff` has the
citation metadata, with the journal fields still at the VLDB template's
placeholder values until the paper is accepted.
