#!/usr/bin/env bash
# run_eidolon.sh — one-command launcher for the full Eidolon stack.
#
# Starts, in order:
#   1. llama-server (Vulkan backend, Radeon 740M) on 127.0.0.1:8080  — skipped if already
#      healthy there, or if EIDOLON_NO_LLM=1.
#   2. eidolon-server on 0.0.0.0:8081 (chat UI reachable at http://<lan-ip>:8081).
#
# Ctrl+C stops eidolon-server and the llama-server we started (a pre-existing
# llama-server is never touched).
#
# Env overrides (sane defaults; normally change nothing):
#   LLAMA_MODEL   GGUF path        (default ~/llama.cpp/Qwen3-4B-Instruct-Q4_K_M.gguf)
#   LLAMA_BIN     llama-server binary (default ~/llama.cpp/build-vulkan/bin/llama-server)
#   LLAMA_PORT    LLM port          (default 8080)
#   EIDOLON_PORT  eidolon-server port (default 8081)
#   EIDOLON_DATA  run directory     (default data/runs/server)
#   EIDOLON_NO_LLM=1              skip llama-server entirely (offline mode)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

LLAMA_MODEL="${LLAMA_MODEL:-$HOME/llama.cpp/Qwen3-4B-Instruct-Q4_K_M.gguf}"
LLAMA_BIN="${LLAMA_BIN:-$HOME/llama.cpp/build-vulkan/bin/llama-server}"
LLAMA_PORT="${LLAMA_PORT:-8080}"
EIDOLON_PORT="${EIDOLON_PORT:-8081}"
EIDOLON_DATA="${EIDOLON_DATA:-data/runs/server}"
EIDOLON_BIN="${EIDOLON_BIN:-$ROOT/build/bin/eidolon-server}"

mkdir -p data/logs
LLAMA_LOG="data/logs/llama-server.log"
LLAMA_PID=""

llama_healthy() {
  curl -sf --max-time 2 "http://127.0.0.1:${LLAMA_PORT}/health" \
    | grep -q '"status":"ok"' 2>/dev/null
}

cleanup() {
  trap - INT TERM EXIT
  echo ""
  if [ -n "$LLAMA_PID" ] && kill -0 "$LLAMA_PID" 2>/dev/null; then
    echo "stopping llama-server (pid $LLAMA_PID)..."
    kill "$LLAMA_PID" 2>/dev/null || true
    wait "$LLAMA_PID" 2>/dev/null || true
  fi
  exit 0
}
trap cleanup INT TERM EXIT

# --- sanity checks -----------------------------------------------------------
if [ ! -x "$EIDOLON_BIN" ]; then
  echo "error: $EIDOLON_BIN not found. Build first:" >&2
  echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j" >&2
  exit 1
fi

# --- 1. llama-server ---------------------------------------------------------
if [ "${EIDOLON_NO_LLM:-0}" = "1" ]; then
  echo "[1/2] llama-server skipped (EIDOLON_NO_LLM=1) — running offline"
  LLM_ARG=()
elif llama_healthy; then
  echo "[1/2] llama-server already healthy on 127.0.0.1:${LLAMA_PORT} — reusing it"
  LLM_ARG=(--llm "http://127.0.0.1:${LLAMA_PORT}/v1" --llm-timeout 20000)
else
  if [ ! -x "$LLAMA_BIN" ]; then
    echo "error: llama-server not found at $LLAMA_BIN" >&2
    exit 1
  fi
  if [ ! -f "$LLAMA_MODEL" ]; then
    echo "error: model not found at $LLAMA_MODEL" >&2
    exit 1
  fi
  echo "[1/2] starting llama-server (Vulkan0, log: $LLAMA_LOG)..."
  "$LLAMA_BIN" -m "$LLAMA_MODEL" --device Vulkan0 --threads 8 \
    --ctx-size 2048 --port "$LLAMA_PORT" --host 127.0.0.1 \
    --n-gpu-layers 14 --no-kv-offload --cache-ram 0 \
    --cache-type-k q8_0 --cache-type-v q8_0 --no-mmproj \
    >>"$LLAMA_LOG" 2>&1 &
  LLAMA_PID=$!

  echo "      waiting for llama-server to load the model..."
  for _ in $(seq 1 90); do
    if llama_healthy; then break; fi
    if ! kill -0 "$LLAMA_PID" 2>/dev/null; then
      echo "error: llama-server died during startup — see $LLAMA_LOG" >&2
      tail -20 "$LLAMA_LOG" >&2
      exit 1
    fi
    sleep 1
  done
  if ! llama_healthy; then
    echo "error: llama-server did not become healthy in 90s — see $LLAMA_LOG" >&2
    exit 1
  fi
  echo "      llama-server ready (pid $LLAMA_PID)"
  LLM_ARG=(--llm "http://127.0.0.1:${LLAMA_PORT}/v1" --llm-timeout 20000)
fi

# --- 2. eidolon-server -------------------------------------------------------
echo "[2/2] starting eidolon-server on 0.0.0.0:${EIDOLON_PORT} (data: $EIDOLON_DATA)"
echo "      chat UI: http://localhost:${EIDOLON_PORT}  (LAN: http://<this-machine-ip>:${EIDOLON_PORT})"
echo "      stop with Ctrl+C"
exec_cmd="$EIDOLON_BIN --data $EIDOLON_DATA --host 0.0.0.0 --port $EIDOLON_PORT"
if [ "${#LLM_ARG[@]}" -gt 0 ]; then
  exec_cmd="$exec_cmd ${LLM_ARG[*]}"
fi
echo "      $exec_cmd"
# Foreground: Ctrl+C -> trap stops llama-server after eidolon-server exits.
if [ "${#LLM_ARG[@]}" -gt 0 ]; then
  "$EIDOLON_BIN" --data "$EIDOLON_DATA" --host 0.0.0.0 --port "$EIDOLON_PORT" "${LLM_ARG[@]}"
else
  "$EIDOLON_BIN" --data "$EIDOLON_DATA" --host 0.0.0.0 --port "$EIDOLON_PORT"
fi
