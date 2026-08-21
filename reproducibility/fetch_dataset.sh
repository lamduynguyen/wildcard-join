#!/usr/bin/env bash
#
# Fetch the HackerNews dataset into reproducibility/dataset/.
#
# The README used to say "download the dataset here" and give a bare Hugging
# Face URL with no revision and no checksums. That URL is a moving branch. If
# the authors push a new commit to it, a reviewer reproducing the paper next
# year silently gets different data and different numbers, with nothing in the
# output to say so. HF_REVISION below is the commit the numbers in the paper
# were produced against, and every file is checked against a SHA256 after
# download.
#
# Usage:
#   ./fetch_dataset.sh              fetch what is missing, then verify
#   ./fetch_dataset.sh --verify     verify what is already there, fetch nothing
#   ./fetch_dataset.sh --force      re-download everything
#
# Needs curl and one of sha256sum or shasum. Downloads about 1.5 GB, so make
# sure there is 2 GB free before starting.

set -euo pipefail

HF_REPO="lamduynguyen/hackernews"
HF_REVISION="87f6bf9adb6e6fb41e7591a0d83787487e7e7fb3"

# sha256, expected byte size, expected data rows, filename. The row counts are
# data rows and do not include the header line. They are not a substitute for
# the checksum, they are there so that a human reading the output can see that
# the file is the shape they expected.
FILES=(
  "c371f8abde30a066dddf36f85bf6d906a90d1cf2f555f4c08560411ada32fead 1514032645 3886492 hackernews.csv"
  "ce088fe82f9c15fcd9c539af476679b903d1f53f9a17eb1479175f0fb5d66a37      30470       1000 blocklists.csv"
  "04386e3e69a6c846e87aa7f14f9f55a0496cadd8d3f77f7617ae75108c8f57be      15646       1000 topics.csv"
)

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$HERE/dataset"
MODE="fetch"

for arg in "$@"; do
  case "$arg" in
    --verify) MODE="verify" ;;
    --force)  MODE="force" ;;
    -h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# macOS has shasum and no sha256sum, Linux is usually the other way round.
if command -v sha256sum >/dev/null 2>&1; then
  sha256_of() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
  sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
  echo "need either sha256sum or shasum on PATH" >&2
  exit 1
fi

size_of() {
  # stat is one of the least portable programs there is.
  wc -c < "$1" | tr -d ' '
}

mkdir -p "$DEST"

echo "repo     $HF_REPO"
echo "revision $HF_REVISION"
echo "dest     $DEST"
echo

failed=0

for entry in "${FILES[@]}"; do
  # shellcheck disable=SC2086
  set -- $entry
  want_sha="$1"; want_size="$2"; want_rows="$3"; name="$4"
  path="$DEST/$name"

  if [ "$MODE" = "force" ] && [ -e "$path" ]; then
    rm -f "$path"
  fi

  if [ ! -e "$path" ]; then
    if [ "$MODE" = "verify" ]; then
      echo "$name: missing"
      failed=1
      continue
    fi
    url="https://huggingface.co/datasets/$HF_REPO/resolve/$HF_REVISION/$name"
    echo "$name: downloading"
    # --fail so an HTML error page does not land on disk pretending to be a
    # CSV, -L because the LFS backed file redirects to a CDN.
    curl -sSL --fail -o "$path.part" "$url"
    mv "$path.part" "$path"
  fi

  got_size="$(size_of "$path")"
  if [ "$got_size" != "$want_size" ]; then
    echo "$name: WRONG SIZE, want $want_size bytes, have $got_size"
    failed=1
    continue
  fi

  got_sha="$(sha256_of "$path")"
  if [ "$got_sha" != "$want_sha" ]; then
    echo "$name: WRONG SHA256"
    echo "  want $want_sha"
    echo "  have $got_sha"
    failed=1
    continue
  fi

  echo "$name: ok, $want_size bytes, sha256 matches, expected $want_rows data rows"
done

echo

if [ "$failed" -ne 0 ]; then
  echo "FAILED. Re-run with --force to download again."
  exit 1
fi

# Row counts, when there is something around that can parse a CSV properly.
# wc -l is wrong here: hackernews.csv has quoted fields with embedded newlines
# in the comment text, so it over counts by a lot. bin/duckdb is a Linux
# x86-64 binary, so this is a best effort check and not a gate. The checksums
# above already determine the contents completely.
duckdb_bin=""
if [ -x "$HERE/../bin/duckdb" ] && "$HERE/../bin/duckdb" --version >/dev/null 2>&1; then
  duckdb_bin="$HERE/../bin/duckdb"
elif command -v duckdb >/dev/null 2>&1; then
  duckdb_bin="duckdb"
fi

if [ -n "$duckdb_bin" ]; then
  echo "counting rows with $duckdb_bin, this reads 1.5 GB and takes a minute"
  for entry in "${FILES[@]}"; do
    # shellcheck disable=SC2086
    set -- $entry
    want_rows="$3"; name="$4"
    got_rows="$("$duckdb_bin" -noheader -list -c \
      "select count(*) from read_csv('$DEST/$name', header=true, quote='\"', escape='\"', delim=',');")"
    if [ "$got_rows" = "$want_rows" ]; then
      echo "$name: $got_rows rows, as expected"
    else
      echo "$name: ROW COUNT MISMATCH, want $want_rows, have $got_rows"
      failed=1
    fi
  done
  echo
else
  echo "no usable duckdb found, skipping the row count check"
  echo "the checksums above already pin the contents, so this is informational"
  echo
fi

if [ "$failed" -ne 0 ]; then
  exit 1
fi

echo "dataset ready in $DEST"
