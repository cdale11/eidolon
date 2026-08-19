#!/bin/bash
# Parallel C++ unit test driver for Eidolon.
# Runs each test in its own process via --name, across all CPU threads.
# The simulation core is single-threaded per process (determinism invariant);
# this parallelizes around it, exactly like run_integration.sh.
set -euo pipefail
cd "$(dirname "$0")/../.."

TEST_BIN="${TEST_BIN:-./build/tests/eidolon_tests}"
if [ ! -x "$TEST_BIN" ]; then
  echo "error: $TEST_BIN not built (run: cmake --build build -j)" >&2
  exit 1
fi

status_file=$(mktemp)
run_test() {
  n="$1"
  if "$TEST_BIN" --name "$n" >/dev/null 2>&1; then
    echo "ok   $n"
    echo "ok $n" >> "$status_file"
  else
    echo "FAIL $n"
    echo "bad $n" >> "$status_file"
  fi
}
export -f run_test
export status_file
export TEST_BIN

"$TEST_BIN" --list | xargs -P"$(nproc)" -L1 bash -c 'run_test "$0"' || true

nfail=$(grep -c '^bad' "$status_file" || true)
rm -f "$status_file"
if [ "$nfail" -gt 0 ]; then
  echo "unit: $nfail test(s) failed" >&2
  exit 1
fi
echo "unit: all passed"