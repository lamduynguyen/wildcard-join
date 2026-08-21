# The binaries in `bin/`

Three x86-64 ELF executables are committed to this repository. They are the
three systems the evaluation compares. This file records what they are, where
they came from, and what a machine needs to run them, because two of the three
cannot be rebuilt and one of those two does not currently run on the platform
this artifact targets.

Everything below was read out of the files as committed and checked on
server1, server2 and server3, all Ubuntu 24.04.4 LTS.

## What is there

| | `duckdb` | `umbraNaive` | `umbraOurs` |
| - | - | - | - |
| bytes | 56734064 | 60413288 | 60571280 |
| sha256 | `6d20aaae...a72c46ed` | `45f269dd...16cddcb3` | `a5181ef9...fc005b13` |
| GNU build id | `7ac478aa591c0b0a8c3faa06ede4c641fb1d2c34` | `137fe44358136616386a1809b089f840093d5611` | `ef35e013c8963f5d00e7e4338e86cd01e544987d` |
| ELF type | `ET_EXEC` | `ET_DYN`, position independent | `ET_DYN`, position independent |
| stripped | no | no | no |
| highest glibc symbol required | `GLIBC_2.25` | `GLIBC_2.39` | `GLIBC_2.39` |
| highest libstdc++ symbol required | `GLIBCXX_3.4.22` | `GLIBCXX_3.4.32` | `GLIBCXX_3.4.32` |
| `DT_RUNPATH` | none | `/usr/lib/llvm-21/lib` | `/usr/lib/llvm-21/lib` |
| shared libraries needed | 7 | 18 | 18 |
| rebuildable from this repo | no, but it is a stock release | no, closed source | no, closed source |

Full sha256 values are in `bin/SHA256SUMS`. Verify with:

```
cd bin && sha256sum -c SHA256SUMS
```

None of the three is stripped, which is why they are as large as they are.

## `duckdb`

A stock DuckDB release build, not something produced for this paper.

```
$ ./bin/duckdb --version
v1.4.4 (Andium) 6ddac802ff
```

It is `ET_EXEC`, links only `libdl`, `libpthread`, `libstdc++`, `libm`,
`libgcc_s`, `libc` and the loader, and its highest glibc symbol requirement is
`GLIBC_2.25`, which shipped in 2017. It runs on anything current. Confirmed on
server3:

```
$ ldd bin/duckdb | grep -c "not found"
0
$ ./bin/duckdb -c "select 1 as ok, version() as v;"
┌───────┬─────────┐
│  ok   │    v    │
│ int32 │ varchar │
├───────┼─────────┤
│     1 │ v1.4.4  │
└───────┴─────────┘
```

DuckDB is MIT licensed. Since this is an unmodified upstream release, the same
binary can be obtained from https://github.com/duckdb/duckdb at tag `v1.4.4`,
build `6ddac802ff`, rather than taken on trust from this repository.

## `umbraOurs` and `umbraNaive`

Umbra is closed source. These two binaries cannot be rebuilt by anyone outside
the group that produced them, there is no source in this repository that
corresponds to them, and there is no way for a reader to verify that the binary
named `umbraOurs` contains the algorithm the paper describes. That is a
limitation of the artifact and it should be read as one.

They also do not currently run on Ubuntu 24.04. Both link against eighteen
shared libraries, and several of those are at versions that the Ubuntu 24.04
archive does not carry at all:

```
libre2.so.11
libboost_context.so.1.90.0
libthrift-0.22.0.so
libLLVM.so.21.1
libabsl_synchronization.so.20260107
liburing.so.2
libjemalloc.so.2
libsnappy.so.1
libssl.so.3
libcrypto.so.3
libzstd.so.1
liblz4.so.1
libbrotlidec.so.1
libz.so.1
```

| the binary needs | what Ubuntu 24.04 offers | obtainable from the archive |
| - | - | - |
| `libre2.so.11` | `libre2-10` | no |
| `libboost_context.so.1.90.0` | `libboost-context1.74.0`, `libboost-context1.83.0` | no |
| `libthrift-0.22.0.so` | `libthrift-0.19.0t64` | no |
| `libLLVM.so.21.1` | `libllvm14t64` through `libllvm20` | no, needs apt.llvm.org |
| `libabsl_synchronization.so.20260107` | `libabsl20220623t64` | no |
| `liburing.so.2` | `liburing2` | yes |
| `libjemalloc.so.2` | `libjemalloc2` | yes |
| `libsnappy.so.1` | `libsnappy1v5` | yes |

The runtime floor itself is fine. Ubuntu 24.04 ships glibc 2.39 and a
`libstdc++` carrying `GLIBCXX_3.4.33`, and the binaries ask for `GLIBC_2.39`
and `GLIBCXX_3.4.32`. It is the five bleeding edge libraries above that stop
them. `libabsl_synchronization.so.20260107` is an Abseil release from January
2026 and `libboost_context.so.1.90.0` is Boost 1.90, so whatever machine built
these was tracking upstream closely. The `DT_RUNPATH` of `/usr/lib/llvm-21/lib`
is a path on that machine and means nothing anywhere else.

What it looks like on server3:

```
$ ldd bin/umbraOurs | grep "not found"
	libre2.so.11 => not found
	libboost_context.so.1.90.0 => not found
	libsnappy.so.1 => not found
	libthrift-0.22.0.so => not found
	liburing.so.2 => not found
	libLLVM.so.21.1 => not found
	libabsl_synchronization.so.20260107 => not found

$ ./bin/umbraOurs --version
./bin/umbraOurs: error while loading shared libraries: libre2.so.11: cannot open shared object file: No such file or directory
```

`umbraNaive` has an identical `DT_NEEDED` list and fails the same way.

## What is missing from this file

Two things that only the people who built these binaries can supply, and both
are needed before this artifact is complete:

* The Umbra commit or internal revision each binary was built from, and the
  compiler and flags used. Without it the build ids above identify the files
  but not what is in them.
* A way to run them. The options, best first, are a container image or
  `Dockerfile` with the right library set, or bundling the missing `.so` files
  next to the binaries with an `$ORIGIN` runpath, or an exact recipe naming the
  suite or source tarball for each of the five libraries the archive does not
  have.

Tracked as https://github.com/tamnd/wildcard-join/issues/3.
