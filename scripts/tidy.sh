#!/usr/bin/env bash
#
# Run clang-tidy over the sources we wrote. The check set and the reasoning
# behind every disabled check live in .clang-tidy, not here.
#
#   scripts/tidy.sh             lint everything
#   scripts/tidy.sh path.cc ... lint just those files
#
# clang-tidy needs a compile_commands.json, and it needs one that covers the
# tests as well as the library, otherwise test/ is silently skipped and that is
# where the matcher oracle lives. build/release is configured for whatever the
# last person was doing, so this script keeps its own build directory and
# configures it if it is missing. Set BUILD_DIR to point somewhere else.
#
# The check set changes between clang-tidy releases, so CI pins it with
# `pipx install clang-tidy==18.1.8`. Use the same one locally or you will get
# findings CI does not have and miss findings it does. Set CLANG_TIDY to point
# at a specific binary.

set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
BUILD_DIR="${BUILD_DIR:-build/tidy}"
JOBS="${JOBS:-4}"

if ! command -v "$CLANG_TIDY" >/dev/null 2>&1; then
  echo "tidy.sh: $CLANG_TIDY not found, install clang-tidy 18.1.8 or set CLANG_TIDY" >&2
  exit 1
fi

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
  echo "tidy.sh: configuring $BUILD_DIR"
  cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTING=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
fi

# NATIVE_ARCH is deliberately left off. It puts -march=native in the compile
# commands, and clang-tidy then parses the code with whatever the linting
# machine happens to support. Linting is not benchmarking and the findings
# should not depend on which core ran them.

# The generated headers from the fetched dependencies have to exist before
# clang-tidy parses anything that includes them, and configure alone does not
# create them. Building the library target is enough and it is cheap after the
# first time.
cmake --build "$BUILD_DIR" --target ahocorasick -j "$JOBS" >/dev/null

ARGS=(-p "$BUILD_DIR" --quiet)

# Apple's clang finds the SDK through the driver, but the compile commands
# cmake writes do not carry -isysroot, so a clang-tidy from pip cannot find
# cassert and reports every standard header as missing. Put the sysroot back.
if [[ "$(uname -s)" == "Darwin" ]]; then
  ARGS+=(--extra-arg=-isysroot "--extra-arg=$(xcrun --show-sdk-path)")
fi

if [[ $# -gt 0 ]]; then
  FILES=("$@")
else
  # Same rule as scripts/format.sh: everything we wrote. third_party/ is
  # vendored croaring and bin/ is prebuilt binaries, so neither is in the
  # search at all. Headers are not listed because clang-tidy reaches them
  # through whichever translation unit includes them, gated by the
  # HeaderFilterRegex in .clang-tidy, which is where the three vendored
  # headers under src/include/common are excluded.
  #
  # A while read loop and not mapfile, because macOS still ships bash 3.2 and
  # mapfile is a bash 4 builtin.
  FILES=()
  while IFS= read -r f; do
    FILES+=("$f")
  done < <(find src test -type f -name '*.cc' | sort)
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "tidy.sh: found no files, that is a bug in this script" >&2
  exit 1
fi

# A file with no entry in the compile database gets linted with a guessed
# command line and produces garbage, so treat that as a hard error rather than
# quietly lowering the coverage of the job.
missing=0
for f in "${FILES[@]}"; do
  if ! grep -q "/${f}\"" "$BUILD_DIR/compile_commands.json"; then
    echo "no compile command for $f, is $BUILD_DIR stale?" >&2
    missing=1
  fi
done
if [[ $missing -ne 0 ]]; then
  echo "delete $BUILD_DIR and run this again" >&2
  exit 1
fi

echo "clang-tidy: ${#FILES[@]} files, $("$CLANG_TIDY" --version | sed -n 's/.*version /version /p')"

# clang-tidy counts every warning the frontend raised anywhere, including the
# ones it then suppressed because they came out of a system or a vendored
# header. On a clean run that prints a five figure "warnings generated." line
# and nothing else, which reads like a disaster and is not one. Drop that line
# and keep the exit status, which is what actually says whether anything fired.
failed=0
for f in "${FILES[@]}"; do
  set +e
  "$CLANG_TIDY" "${ARGS[@]}" "$f" 2>&1 | grep -Ev '^[0-9]+ warnings? generated\.$'
  status=${PIPESTATUS[0]}
  set -e
  if [[ $status -ne 0 ]]; then
    failed=1
  fi
done

if [[ $failed -ne 0 ]]; then
  echo
  echo "clang-tidy found problems, see above"
  exit 1
fi

echo "clean"
