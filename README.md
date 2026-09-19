# Reproducibility package: wildcard joins and filters

Artifact for the paper "Teach Your Database to LIKE Strings via Efficient Wildcard Joins and Filters".

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

## The binaries in `bin/`

Three x86-64 ELF executables are committed here, unstripped, 177.7 MB between
them.

`duckdb` is a stock DuckDB release, `v1.4.4 (Andium) 6ddac802ff`, MIT licensed.
`umbraNaive` and `umbraOurs` are Umbra -- the system we use to demonstrate the paper's optimizations.
The former, `umbraNaive`, is a naive implementation without any optimizations.
The latter, `umbraOurs`, contains all the optimizations described in the paper.
Because Umbra is closed source, we can't publish the main source code for it.
Instead, we (1) provide the binaries and (2) publish the prototype implementation of the paper's techniques here in `src/` folder.

## Dataset

```
cd reproducibility && ./fetch_dataset.sh
```

Downloads the three CSV files from
https://huggingface.co/datasets/lamduynguyen/hackernews at a pinned revision, checks each against a SHA256 and counts the rows. 
`reproducibility/README.md` has the load instructions and what the queries do.

## Licence and citation

MIT, see `LICENSE`. Third party components and the licensing of the committed binaries are in `THIRD_PARTY_LICENSES.md`.
