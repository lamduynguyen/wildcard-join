# Third party licenses

This project is licensed under MIT, see `LICENSE`. It depends on the
software below, and it ships three binaries produced by other people. Nothing
in the source dependencies is copyleft and nothing there restricts
redistribution of the artifact. The binaries are a separate question and are
covered at the bottom.

## Vendored, source in this repository

| Component | Version | License | Where |
| - | - | - | - |
| [CRoaring](https://github.com/RoaringBitmap/CRoaring) | 1.3.0 | Apache-2.0 or MIT, at your option | `third_party/croaring/`, license text in `third_party/croaring/LICENSE` |

The CRoaring copy is the single header amalgamation, so `roaring.h`,
`roaring.hh` and `roaring.cc` are generated files from the upstream release
and are not edited here. `third_party/croaring/CMakeLists.txt` is ours. The
version is the `ROARING_VERSION` define in `third_party/croaring/roaring.h`.

## Resolved by find_package at configure time

| Component | License | How it is found |
| - | - | - |
| [{fmt}](https://github.com/fmtlib/fmt) | MIT | `find_package(fmt REQUIRED)` |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | Apache-2.0 | `find_package(TBB REQUIRED)` |
| [GoogleTest](https://github.com/google/googletest) | BSD-3-Clause | `find_package(GTest REQUIRED)`, only when `ENABLE_TESTING=ON` |

There is no pinning yet, so the version you get is whatever the host has. On
server3, Ubuntu 24.04.4:

| Component | Package | Version |
| - | - | - |
| fmt | `libfmt-dev` | 9.1.0+ds1-2 |
| oneTBB | `libtbb-dev` | 2021.11.0-2ubuntu2 |
| GoogleTest | `libgtest-dev` | 1.14.0-1 |

Pinning these with `FetchContent` and a SHA256 per release tarball is an open
item on the tracking issue. Until that lands, a build on a machine with
different distro versions is a different build, and this table is a snapshot
rather than a specification.

## Binaries in `bin/`

Three x86-64 ELF executables are committed to this repository. They are not
built from anything here and they are not covered by this project's licence.

| File | What it is | License |
| - | - | - |
| `duckdb` | stock DuckDB release, `v1.4.4 (Andium) 6ddac802ff` | MIT |
| `umbraNaive` | Umbra, closed source | not stated anywhere in this repository |
| `umbraOurs` | Umbra, closed source | not stated anywhere in this repository |

`duckdb` is an unmodified upstream release and can be obtained from
https://github.com/duckdb/duckdb at tag `v1.4.4` instead of being taken on
trust from here.

The two Umbra entries say "not stated" because that is the actual situation
and not a placeholder. Umbra is closed source, these binaries are redistributed
here, and nothing in the repository records the terms that permits it. That
needs a sentence from the Umbra authors before the artifact is submitted, and
it is not something this file can invent.

The two Umbra binaries cannot be rebuilt, and as committed they do not run on
Ubuntu 24.04. `docs/umbra-binaries.md` has the sha256 values, build ids,
library requirements and the reason.
