#!/usr/bin/env bash
# run_eidolon.sh — one-command launcher for the full Eidolon stack.
#
# Starts, in order:
#   0. Build: configure (if needed) and incrementally build the native game
#      targets (eidolon-server, eidolon-sim). Skipped with EIDOLON_NO_BUILD=1.
#      With EIDOLON_WASM=1 (default), also ensures the browser worker assets
#      (build-wasm-simd/bin/eidolon-worker.js/.wasm) exist for client offload.
#   1. llama-server (Vulkan backend, Radeon 740M) on 127.0.0.1:8080  — skipped if already
#      healthy there, or if EIDOLON_NO_LLM=1. Built from local sources when missing
#      (EIDOLON_BUILD_LLAMA=1, default) — set 0 to only explain instead.
#   2. eidolon-server on 0.0.0.0:8081 (chat UI reachable at http://<lan-ip>:8081).
#
# Ctrl+C stops eidolon-server and the llama-server we started (a pre-existing
# llama-server is never touched).
#
# Env overrides (sane defaults; normally change nothing):
#   EIDOLON_BUILD_DIR  cmake build dir   (default <repo>/build)
#   EIDOLON_BUILD_TYPE cmake build type  (default Release)
#   EIDOLON_NO_BUILD=1                  skip the game build entirely
#   EIDOLON_WASM=0                      skip the browser worker-asset check/build
#   LLAMA_SRC      llama.cpp checkout   (default ~/llama.cpp)
#   EIDOLON_BUILD_LLAMA=0               don't auto-build llama-server; just explain
#   LLAMA_MODEL   GGUF path        (default ~/llama.cpp/Qwen3-4B-Instruct-Q4_K_M.gguf)
#   LLAMA_BIN     llama-server binary (default ~/llama.cpp/build-vulkan/bin/llama-server)
#   LLAMA_PORT    LLM port          (default 8080)
#   EIDOLON_PORT  eidolon-server port (default 8081)
#   EIDOLON_DATA  run directory     (default data/runs/server)
#   EIDOLON_NO_LLM=1              skip llama-server entirely (offline mode)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

EIDOLON_BUILD_DIR="${EIDOLON_BUILD_DIR:-$ROOT/build}"
EIDOLON_BUILD_TYPE="${EIDOLON_BUILD_TYPE:-Release}"
EIDOLON_BIN="${EIDOLON_BIN:-$EIDOLON_BUILD_DIR/bin/eidolon-server}"
EIDOLON_WASM="${EIDOLON_WASM:-1}"
LLAMA_SRC="${LLAMA_SRC:-$HOME/llama.cpp}"
EIDOLON_BUILD_LLAMA="${EIDOLON_BUILD_LLAMA:-1}"
LLAMA_MODEL="${LLAMA_MODEL:-$HOME/llama.cpp/Qwen3-4B-Instruct-Q4_K_M.gguf}"
LLAMA_BIN="${LLAMA_BIN:-$HOME/llama.cpp/build-vulkan/bin/llama-server}"
LLAMA_PORT="${LLAMA_PORT:-8080}"
EIDOLON_PORT="${EIDOLON_PORT:-8081}"
EIDOLON_DATA="${EIDOLON_DATA:-data/runs/server}"

mkdir -p data/logs
LLAMA_LOG="data/logs/llama-server.log"
LLAMA_PID=""

llama_healthy() {
  curl -sf --max-time 2 "http://127.0.0.1:${LLAMA_PORT}/health" \
    | grep -q '"status":"ok"' 2>/dev/null
}

cleanup() {
  local rc=$? # preserve the triggering exit status (failures must stay nonzero)
  trap - INT TERM EXIT
  echo ""
  if [ -n "$LLAMA_PID" ] && kill -0 "$LLAMA_PID" 2>/dev/null; then
    echo "stopping llama-server (pid $LLAMA_PID)..."
    kill "$LLAMA_PID" 2>/dev/null || true
    wait "$LLAMA_PID" 2>/dev/null || true
  fi
  exit "$rc"
}
trap cleanup INT TERM EXIT

# --- sanity checks -----------------------------------------------------------
need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required tool '$1' not found in PATH." >&2
    echo "  install it first (Fedora: sudo dnf install $2)" >&2
    exit 1
  fi
}

# --- 0. build the game -------------------------------------------------------
if [ "${EIDOLON_NO_BUILD:-0}" = "1" ]; then
  echo "[0/3] game build skipped (EIDOLON_NO_BUILD=1)"
else
  echo "[0/3] ensuring the game is built (dir: $EIDOLON_BUILD_DIR, type: $EIDOLON_BUILD_TYPE)..."
  need_tool cmake cmake
  need_tool ninja ninja-build
  need_tool g++ gcc-c++
  if [ ! -f /usr/include/sqlite3.h ]; then
    echo "error: sqlite3 dev headers not found (/usr/include/sqlite3.h)." >&2
    echo "  install them first (Fedora: sudo dnf install sqlite-devel)" >&2
    exit 1
  fi
  if [ ! -f "$ROOT/CMakeLists.txt" ]; then
    echo "error: $ROOT/CMakeLists.txt not found — run this script from the eidolon repo." >&2
    exit 1
  fi
  if [ ! -f "$EIDOLON_BUILD_DIR/CMakeCache.txt" ]; then
    echo "      configuring..."
    cmake -S "$ROOT" -B "$EIDOLON_BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE="$EIDOLON_BUILD_TYPE" || {
      echo "error: cmake configure failed for $EIDOLON_BUILD_DIR" >&2
      exit 1
    }
  fi
  echo "      building (incremental)..."
  cmake --build "$EIDOLON_BUILD_DIR" -j"$(nproc)" || {
    echo "error: game build failed in $EIDOLON_BUILD_DIR" >&2
    exit 1
  }
  echo "      game build ok"

  # Browser worker assets for client offload: the server arms offload only when
  # build-wasm-simd (or build-wasm) worker files are servable. Build them when
  # missing so a fresh checkout gets offload support from this one command.
  if [ "$EIDOLON_WASM" = "1" ]; then
    if [ -f "$ROOT/build-wasm-simd/bin/eidolon-worker.js" ] || \
       [ -f "$ROOT/build-wasm/bin/eidolon-worker.js" ]; then
      echo "      wasm worker assets present — skipping wasm build"
    elif [ ! -f "$HOME/emsdk/emsdk_env.sh" ]; then
      echo "      warning: Emscripten SDK not found at ~/emsdk — skipping wasm worker build." >&2
      echo "      the server will run fine; browser client-offload just stays disarmed." >&2
    else
      # shellcheck disable=SC1091
      source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1 || true
      echo "      building wasm-simd worker assets (one-time cost)..."
      cmake -S "$ROOT" -B "$ROOT/build-wasm-simd" \
        -DCMAKE_TOOLCHAIN_FILE="$HOME/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DEIDOLON_WASM_BUILD=1 \
        -DCMAKE_CXX_FLAGS="-msimd128" || {
        echo "error: wasm configure failed" >&2
        exit 1
      }
      cmake --build "$ROOT/build-wasm-simd" -j"$(nproc)" || {
        echo "error: wasm worker build failed" >&2
        exit 1
      }
      echo "      wasm worker assets ok"
    fi
  else
    echo "      wasm worker check skipped (EIDOLON_WASM=0)"
  fi
fi

if [ ! -x "$EIDOLON_BIN" ]; then
  echo "error: $EIDOLON_BIN not found or not executable." >&2
  if [ "${EIDOLON_NO_BUILD:-0}" = "1" ]; then
    echo "  unset EIDOLON_NO_BUILD so this script builds it, or set EIDOLON_BIN/EIDOLON_BUILD_DIR correctly." >&2
  else
    echo "  the build above should have produced it — check for build errors." >&2
  fi
  exit 1
fi

# --- 1. llama-server ---------------------------------------------------------
if [ "${EIDOLON_NO_LLM:-0}" = "1" ]; then
  echo "[1/3] llama-server skipped (EIDOLON_NO_LLM=1) — running offline"
  LLM_ARG=()
elif llama_healthy; then
  echo "[1/3] llama-server already healthy on 127.0.0.1:${LLAMA_PORT} — reusing it"
  LLM_ARG=(--llm "http://127.0.0.1:${LLAMA_PORT}/v1" --llm-timeout 20000)
else
  if [ ! -x "$LLAMA_BIN" ]; then
    if [ "$EIDOLON_BUILD_LLAMA" = "1" ] && [ -f "$LLAMA_SRC/CMakeLists.txt" ]; then
      echo "[1/3] llama-server not found at $LLAMA_BIN — building Vulkan target from $LLAMA_SRC..."
      need_tool cmake cmake
      cmake -S "$LLAMA_SRC" -B "$LLAMA_SRC/build-vulkan" \
        -DGGML_VULKAN=ON -DGGML_CUDA=OFF || {
        echo "error: llama.cpp configure failed" >&2
        exit 1
      }
      cmake --build "$LLAMA_SRC/build-vulkan" --target llama-server -j"$(nproc)" || {
        echo "error: llama-server build failed" >&2
        exit 1
      }
    else
      echo "error: llama-server not found at $LLAMA_BIN" >&2
      if [ ! -f "$LLAMA_SRC/CMakeLists.txt" ]; then
        echo "  no llama.cpp sources at $LLAMA_SRC either. Install them first:" >&2
        echo "  git clone https://github.com/ggerganov/llama.cpp.git ~/llama.cpp" >&2
      else
        echo "  build it (or rerun with EIDOLON_BUILD_LLAMA=1 to build automatically):" >&2
        echo "  cmake -S $LLAMA_SRC -B $LLAMA_SRC/build-vulkan -DGGML_VULKAN=ON -DGGML_CUDA=OFF" >&2
        echo "  cmake --build $LLAMA_SRC/build-vulkan --target llama-server -j\$(nproc)" >&2
      fi
      echo "  or run offline with EIDOLON_NO_LLM=1." >&2
      exit 1
    fi
  fi
  if [ ! -x "$LLAMA_BIN" ]; then
    echo "error: llama-server build did not produce $LLAMA_BIN" >&2
    exit 1
  fi
  if [ ! -f "$LLAMA_MODEL" ]; then
    echo "error: model not found at $LLAMA_MODEL" >&2
    exit 1
  fi
  echo "[1/3] starting llama-server (Vulkan0, log: $LLAMA_LOG)..."
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
echo "[2/3] starting eidolon-server on 0.0.0.0:${EIDOLON_PORT} (data: $EIDOLON_DATA)"
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
