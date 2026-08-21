# HackerNews benchmark

The evaluation workload. Wildcard filters and wildcard joins over the
HackerNews corpus, run against DuckDB and against Umbra with and without the
technique in the paper.

## Dataset

```
./fetch_dataset.sh
```

Downloads the three CSV files into `dataset/`, checks each one against a
SHA256, and counts the rows. About 1.5 GB, so have 2 GB free. On server3 it
took 1m22s including the row count pass.

```
hackernews.csv: ok, 1514032645 bytes, sha256 matches, expected 3886492 data rows
blocklists.csv: ok, 30470 bytes, sha256 matches, expected 1000 data rows
topics.csv: ok, 15646 bytes, sha256 matches, expected 1000 data rows
```

`--verify` checks files that are already there without downloading anything.
`--force` re-downloads. Both exit non zero if anything does not match.

The script pins the Hugging Face revision, currently
`87f6bf9adb6e6fb41e7591a0d83787487e7e7fb3`. The instruction here used to be a
bare repository URL, which points at a moving branch: if the dataset is
updated, someone reproducing the paper later gets different data and different
numbers with nothing in the output to say so. Anyone updating the dataset has
to update the revision and the three checksums in `fetch_dataset.sh` together,
which is the point.

The row count check needs something that can parse a quoted CSV. It uses
`../bin/duckdb` if that runs on your machine, otherwise a `duckdb` on `PATH`,
otherwise it says so and skips. `wc -l` is not usable here, `hackernews.csv`
has newlines inside quoted comment text. The checksums pin the contents
regardless, the row counts are there so a human can see the shape is right.

## Load

`load.sql` uses bare filenames, so it has to run from inside `dataset/`:

```
cd dataset
../../bin/duckdb hn.duckdb -c ".read ../schema.sql" -c ".read ../load.sql"
```

31 seconds on server3, giving a 1.97 GB database file.

## Benchmark queries

`queries/` has the five main queries. `filter1` and `filter2` are the wildcard
filter workloads, `join1` through `join3` the wildcard joins.

`queries/microbench/` has `filter2` and `join2` reparameterised over 1, 2, 4,
8, 16, 32 and 64 patterns, which is the pattern count sweep.

All five run in stock DuckDB on the loaded database. Wall clock on server3,
single run, warm page cache, no warm up protocol, so treat these as an order
of magnitude and not as a measurement:

| query | wall clock | rows out |
| - | -: | -: |
| filter1 | 1.9s | 9 |
| filter2 | 0.8s | 8 |
| join1 | 4.2s | 50 |
| join2 | 60.6s | 50 |
| join3 | 129.0s | 50 |

Umbra numbers are not here because `bin/umbraOurs` and `bin/umbraNaive` do not
currently run on Ubuntu 24.04. See `../docs/umbra-binaries.md` and issue #3.

## What is still missing

There is no `run_all.sh`, so there is no single command from a clean clone to
the plot data, and no `EXPECTED_RUNTIME.md`. DuckDB does not get the same warm
up protocol Umbra does, which has to be fixed before the two are compared. The
join experiments run at a pattern count the paper reports at n equal to 1 for
memory, hand transcribed. Those are tracked on issue #1.


## The golden workload file, expected/baseline_workloads.tsv

The answers for the workloads in `benchmark/workload.h`, computed once by the
recursive reference matcher and checked in. `test/test_golden.cc` reads this file
and recomputes the same numbers with the Aho-Corasick automaton.

Every other correctness test here compares one of our implementations against
another one of ours. The fuzzer runs the automaton against the recursive matcher,
`test_reuse` runs a reused iterator against a fresh one, the benchmark
cross-checks all three on every run. Those catch a lot and they share one blind
spot, which is that a change moving both sides together looks like agreement.
This file does not move.

Columns:

| column | meaning |
| - | - |
| `name` | the workload, matching `Spec::name` in `benchmark/workload.h` |
| `corpus_digest` | FNV-1a over the patterns then the texts, in generation order |
| `rows` | number of probe side rows |
| `patterns` | number of build side patterns |
| `bytes` | total probe side bytes |
| `pairs` | number of matching row and pattern pairs |
| `result_digest` | FNV-1a over those pairs |

The result digest is defined in `test/test_golden.cc` and the definition is the
contract: row major, within a row in increasing pattern order, each index folded
in as eight little endian bytes. That is a page of code in any language, which is
the bar for a number an artifact reviewer is asked to trust.

## Checking it

Part of the normal test run.

```
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=ON
cmake --build build/release --target TestGolden
./build/release/test/TestGolden
```

Under a second on an idle machine, six seconds on a loaded one. The regeneration
case skips unless you ask for it.

## Reading a failure

The test checks the corpus first and the answers second, because they fail for
different reasons.

`corpus_digest` differs: the input is not the input this file describes, so
nothing below it is a correctness result. Either a spec in
`benchmark/workload.h` changed, or the generator changed, or something in the
generator went back to depending on the standard library rather than on the seed.
That last one is why this file could not exist before the generator was
pinned to the seed.

`corpus_digest` matches and `result_digest` differs: the input is the same and
the answers are not. Either the matcher changed behaviour or the reference
matcher did. `pairs` narrows it down, since a pair count that moved a long way is
usually a whole pattern class dropping out, and a pair count that is identical
with a different digest means the same number of matches landed on different
rows.

Neither of those is automatically a bug. Both of them are a thing that has to be
explained in the pull request that caused it, rather than noticed six months
later.

## Regenerating

Only when a spec changes, or when a behaviour change is intended and understood.

```
AC_GOLDEN_REGENERATE=1 ./build/release/test/TestGolden --gtest_filter='GoldenRegenerate.*'
```

The values come from `NljRecursiveMatch` in `test/matcher.h`, not from the
automaton, so the file is not a recording of whatever the automaton did last
time. That is M patterns times N rows of the slow path, which is why it takes
minutes rather than seconds, and why it is behind an environment variable rather
than a command line flag somebody could set by accident.

Regenerating is a deliberate act. A diff to this file in a pull request should be
read as a claim that the answers changed on purpose.
