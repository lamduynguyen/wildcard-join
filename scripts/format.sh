#!/usr/bin/env bash
#
# One canonical file list for formatting, used by both the `format` cmake
# target and the CI lint job. Before this existed the list lived in
# CMakeLists.txt as SOURCE_FILES plus TEST_FILES, which covers ten of the
# twenty six source files. Everything under src/include was never formatted
# by the target that claims to format the project.
#
#   scripts/format.sh           rewrite the files in place
#   scripts/format.sh --check   exit non zero if anything is unformatted
#
# clang-format output changes between releases, so CI pins the version with
# `pipx install clang-format==18.1.8` and you should use the same one. Set
# CLANG_FORMAT to point at a specific binary.

set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

# Everything we wrote. third_party/ is vendored croaring and bin/ is prebuilt
# binaries, so neither is in the search at all.
#
# Three headers under src/include/common are vendored too, and reformatting a
# vendored file is worse than leaving it alone: it makes the next diff against
# upstream unreadable for no benefit. flat_map.h is ankerl::unordered_dense,
# perf_event.h carries a 2018 Viktor Leis copyright, and utf8.h carries an
# Umbra one. None of the three is in THIRD_PARTY_LICENSES.md yet, which is its
# own problem and is filed separately.
#
# A while read loop and not mapfile, because macOS still ships bash 3.2 and
# mapfile is a bash 4 builtin.
VENDORED='src/include/common/(flat_map|perf_event|utf8)\.h'

FILES=()
while IFS= read -r f; do
  FILES+=("$f")
done < <(find src test -type f \( -name '*.cc' -o -name '*.h' \) | grep -Ev "$VENDORED" | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "format.sh: found no files, that is a bug in this script" >&2
  exit 1
fi

if [[ "${1:-}" == "--check" ]]; then
  failed=0
  for f in "${FILES[@]}"; do
    if ! "$CLANG_FORMAT" --style=file:.clang-format "$f" | diff -q - "$f" >/dev/null; then
      echo "not formatted: $f"
      failed=1
    fi
  done
  if [[ $failed -ne 0 ]]; then
    echo
    echo "run scripts/format.sh with $("$CLANG_FORMAT" --version)"
    exit 1
  fi
  echo "all ${#FILES[@]} files are formatted"
  exit 0
fi

"$CLANG_FORMAT" --style=file:.clang-format -i "${FILES[@]}"
echo "formatted ${#FILES[@]} files"
