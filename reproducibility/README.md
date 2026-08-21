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
