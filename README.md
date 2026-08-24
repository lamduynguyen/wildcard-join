# Reproducibility package: wildcard joins and filters

Artifact for the paper "Teach Your Database to LIKE Strings via Efficient Wildcard Joins and Filters", under submission for VLDB 2027.

This repository is what the Artifact Availability block points at, so it is the
first thing a reviewer touches. Read the state section below before spending
time on it: parts of it work today and parts of it do not.

## Layout

| Path | What is in it |
| - | - |
| `src/` | The wildcard join prototype. Skeleton and segment decomposition of `LIKE` patterns, the positional constraint checks, the ART index and the matcher. |
| `test/` | GoogleTest suites for the matcher and the automaton. |
| `third_party/croaring/` | Vendored CRoaring 1.3.0, single header amalgamation. |
| `cmake/Dependencies.cmake` | Pinned fmt, oneTBB and GoogleTest, with the `USE_SYSTEM_DEPS` escape hatch. |
| `reproducibility/` | `fetch_dataset.sh`, schema, load scripts and the benchmark queries for the HackerNews evaluation. |
| `bin/` | The three system binaries the evaluation compares, plus `SHA256SUMS`. |
| `docs/umbra-binaries.md` | What those binaries are, what they need, and what cannot be rebuilt. |

## Build

```
cmake --preset release
cmake --build --preset release -j"$(nproc)"
ctest --preset release
```

Needs a C++23 compiler and CMake 3.25 or newer. fmt 11.0.2, oneTBB 2021.13.0
and GoogleTest 1.15.2 are fetched at configure time and checked against a
SHA256, so you do not need them installed and you get the same versions we
did. The first configure of a given build directory needs network, and only
that one.

`-DUSE_SYSTEM_DEPS=ON` goes back to `find_package` instead, which is the right
choice for a distro package or a machine with no network. On Ubuntu 24.04 that
wants `libfmt-dev`, `libtbb-dev` and `libgtest-dev`. `THIRD_PARTY_LICENSES.md`
has the versions that resolves to on server3.

`release`, `debug` and `release-asan` presets are in `CMakePresets.json`. Only
`release` turns on `-march=native`, since a build with it on is not comparable
across machines.

On server3, from a clean clone: 17 seconds to configure, 2 minutes to build
with `-j8`, tests in under a second.

## The binaries in `bin/`

Three x86-64 ELF executables are committed here, unstripped, 177.7 MB between
them.

`duckdb` is a stock DuckDB release, `v1.4.4 (Andium) 6ddac802ff`, MIT licensed.
It runs anywhere current and it can be obtained from upstream instead of taken
on trust from here.

`umbraNaive` and `umbraOurs` are Umbra -- the system we use to demonstrate the paper's optimizations.
The former, `umbraNaive`, is a naive implementation without any optimizations.
The latter, `umbraOurs`, contains all the optimizations described in the paper.
Because Umbra is closed source, we can't publish the main source code for it.
Instead, we (1) provide the binaries and (2) publish the prototype implementation of the paper's techniques here in `src/` folder.

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

```
cd reproducibility && ./fetch_dataset.sh
```

Downloads the three CSV files from
https://huggingface.co/datasets/lamduynguyen/hackernews at a pinned revision,
checks each against a SHA256 and counts the rows. About 1.5 GB, 1m22s on
server3. `reproducibility/README.md` has the load instructions and what the
queries do.

## Licence and citation

MIT, see `LICENSE`. Third party components and the licensing of the
committed binaries are in `THIRD_PARTY_LICENSES.md`. `CITATION.cff` has the
citation metadata, with the journal fields still at the VLDB template's
placeholder values until the paper is accepted.
