#!/usr/bin/env python3
"""Load the HackerNews dataset from Hugging Face with the datasets library.

fetch_dataset.sh is the supported path and this is not a replacement for it.
This exists because the obvious thing to type does not work:

    load_dataset("lamduynguyen/hackernews")

    DatasetGenerationCastError: An error occurred while generating the dataset
    All the data files must have the same columns, but at some point there are
    13 new columns ({'by', 'id', 'title', ...}) and 3 missing columns
    ({'block_id', 'host_pattern', 'category'}).

The repository holds hackernews.csv, blocklists.csv and topics.csv at the top
level with three different schemas. With no configuration in the dataset card,
the csv builder globs all three into one train split, generates the first, and
fails casting the second to the first one's schema. The dataset viewer on the
hub fails the same way and for the same reason, which is the first thing anyone
opening the dataset page sees.

reproducibility/hf/README.md is the dataset card that fixes it at the source, by
declaring one configuration per file. Until that is uploaded, naming the file
explicitly through data_files works and needs nothing on the hub side. That is
what this script does, and it keeps working unchanged after the card lands.

Usage:
  ./hf_load.py            check the columns, and the row counts of the two
                          small files; streams hackernews.csv, a few seconds
  ./hf_load.py --full     materialise all three, including 3,886,492 rows of
                          hackernews.csv; downloads 1.5 GB and takes minutes
  ./hf_load.py --revision REV   use another commit instead of the pinned one

Needs `pip install datasets`. Exits non zero if anything does not match.
"""

import argparse
import sys

REPO = "lamduynguyen/hackernews"

# The commit the numbers in the paper were produced against, the same value
# fetch_dataset.sh pins. A bare repository name refers to a moving branch and
# does not identify the data anyone else measured.
REVISION = "87f6bf9adb6e6fb41e7591a0d83787487e7e7fb3"

# filename, expected data rows, expected columns. Same shape the checksums in
# fetch_dataset.sh pin, restated here so a human can see what is expected.
FILES = [
    (
        "hackernews.csv",
        3886492,
        ["id", "deleted", "type", "by", "time", "text", "dead", "parent",
         "poll", "url", "score", "title", "descendants"],
    ),
    ("blocklists.csv", 1000, ["block_id", "host_pattern", "category"]),
    ("topics.csv", 1000, ["topic_id", "pattern"]),
]


def load(name, revision, streaming):
    """Load one file as its own single split dataset.

    data_files is the whole point: it overrides the builder's glob, so the
    other two CSVs are never considered and their schemas cannot collide with
    this one's.
    """
    from datasets import load_dataset

    return load_dataset(
        REPO,
        revision=revision,
        data_files={"train": name},
        split="train",
        streaming=streaming,
    )


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--full", action="store_true",
                    help="materialise every row rather than streaming the big file")
    ap.add_argument("--revision", default=REVISION,
                    help=f"hub revision to read (default {REVISION})")
    args = ap.parse_args()

    try:
        import datasets
    except ImportError:
        print("needs the datasets library: pip install datasets", file=sys.stderr)
        return 1

    print(f"repo     {REPO}")
    print(f"revision {args.revision}")
    print(f"datasets {datasets.__version__}")
    print()

    failed = 0

    for name, want_rows, want_cols in FILES:
        # Streaming reads the header and stops, which is enough to check the
        # columns. Row counts need the whole file, so under --full only, and
        # the two small files are cheap enough to count either way.
        big = name == "hackernews.csv"
        streaming = big and not args.full

        ds = load(name, args.revision, streaming)

        if streaming:
            first = next(iter(ds), None)
            if first is None:
                print(f"{name}: EMPTY")
                failed = 1
                continue
            got_cols = list(first.keys())
            got_rows = None
        else:
            got_cols = list(ds.column_names)
            got_rows = ds.num_rows

        if got_cols != want_cols:
            print(f"{name}: WRONG COLUMNS")
            print(f"  want {want_cols}")
            print(f"  have {got_cols}")
            failed = 1
            continue

        if got_rows is None:
            print(f"{name}: ok, {len(got_cols)} columns, rows not counted (streamed)")
        elif got_rows != want_rows:
            print(f"{name}: ROW COUNT MISMATCH, want {want_rows}, have {got_rows}")
            failed = 1
        else:
            print(f"{name}: ok, {got_rows} rows, {len(got_cols)} columns")

    print()

    if failed:
        print("FAILED")
        return 1

    if not args.full:
        print("columns match; re-run with --full to count every row")
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
