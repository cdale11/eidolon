#!/bin/bash
# Integration test driver for Eidolon (conda env eidolon).
# Runs all python/tests/test_*.py against the built binaries.
set -euo pipefail
cd "$(dirname "$0")/../.."

SIM_BIN="${SIM_BIN:-./build/bin/eidolon-sim}"
if [ ! -x "$SIM_BIN" ]; then
  echo "error: $SIM_BIN not built (run: cmake --build build -j)" >&2
  exit 1
fi

failures=0
for t in python/tests/test_*.py; do
  echo "== $t"
  if SIM_BIN="$SIM_BIN" python "$t"; then
    echo "   PASS"
  else
    echo "   FAIL"
    failures=$((failures + 1))
  fi
done

if [ "$failures" -gt 0 ]; then
  echo "integration: $failures test file(s) failed" >&2
  exit 1
fi
echo "integration: all passed"
