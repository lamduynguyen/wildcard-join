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
| [csv-parser](https://github.com/vincentlaucsb/csv-parser) | 2.3.0 | MIT | `third_party/csv-parser/csv.h`, single header, license text in the file header |

The CRoaring copy is the single header amalgamation, so `roaring.h`,
`roaring.hh` and `roaring.cc` are generated files from the upstream release
and are not edited here. `third_party/croaring/CMakeLists.txt` is ours. The
version is the `ROARING_VERSION` define in `third_party/croaring/roaring.h`.

## Fetched at configure time, pinned

`cmake/Dependencies.cmake` fetches these at the versions below and checks each
tarball against a SHA256. They are the same versions tamnd/prototype-string
pins, so the two repos build against the same code.

| Component | Version | License | SHA256 of the release tarball |
| - | - | - | - |
| [{fmt}](https://github.com/fmtlib/fmt) | 11.0.2 | MIT | `6cb1e6d3...3f7c027f` |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | 2021.13.0 | Apache-2.0 | `3ad5dd08...c94133e1` |
| [GoogleTest](https://github.com/google/googletest) | 1.15.2 | BSD-3-Clause | `7b42b4d6...27f02926` |

GoogleTest is only fetched when `ENABLE_TESTING=ON`. A library only build has
to work on a machine that has no gtest at all.

`USE_SYSTEM_DEPS=ON` switches all three back to `find_package`, which is what
the build did before and is the right choice for a distro package or a machine
with no network. Taking that path means the versions are the host's. On
server3, Ubuntu 24.04.4, that resolves to:

| Component | Package | Version |
| - | - | - |
| fmt | `libfmt-dev` | 9.1.0+ds1-2 |
| oneTBB | `libtbb-dev` | 2021.11.0-2ubuntu2 |
| GoogleTest | `libgtest-dev` | 1.14.0-1 |

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
