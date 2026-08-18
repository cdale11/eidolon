#!/bin/bash
# Integration test driver for Eidolon (conda env eidolon).
# Runs all python/tests/test_*.py against the built binaries, in parallel
# across all CPU threads (each test file gets its own PORT_BASE).
set -euo pipefail
cd "$(dirname "$0")/../.."

export SIM_BIN="${SIM_BIN:-./build/bin/eidolon-sim}"
export SERVER_BIN="${SERVER_BIN:-./build/bin/eidolon-server}"
if [ ! -x "$SIM_BIN" ]; then
  echo "error: $SIM_BIN not built (run: cmake --build build -j)" >&2
  exit 1
fi
if [ ! -x "$SERVER_BIN" ]; then
  echo "error: $SERVER_BIN not built (run: cmake --build build -j)" >&2
  exit 1
fi

status_file=$(mktemp)
run_file() {
  t="$1"
  pb="$2"
  if PORT_BASE="$pb" python "$t"; then
    echo "PASS $t"
    echo "ok $t" >> "$status_file"
  else
    echo "FAIL $t"
    echo "bad $t" >> "$status_file"
  fi
}
export -f run_file
export status_file

pb=9000
for t in python/tests/test_*.py; do
  echo "$t $pb"
  pb=$((pb + 100))
done | xargs -P"$(nproc)" -L1 bash -c 'run_file "$0" "$1"' || true

nfail=$(grep -c '^bad' "$status_file" || true)
rm -f "$status_file"
if [ "$nfail" -gt 0 ]; then
  echo "integration: $nfail test file(s) failed" >&2
  exit 1
fi
echo "integration: all passed"