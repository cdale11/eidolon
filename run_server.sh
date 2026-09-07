#!/usr/bin/env bash
# run_server.sh — failsafe, reusable launcher for eidolon-server in a named screen.
#
# Guarantees a single, reattachable server regardless of how you invoke it:
#   ./run_server.sh              # (re)start in screen session "eidolon"
#   ./run_server.sh stop         # stop the server (leave screen)
#   ./run_server.sh status       # print running state / status JSON
#   ./run_server.sh log          # tail the server log (also: screen -r eidolon)
#
# Uses `screen` so the server keeps running after you disconnect, and you can always
# reattach with `screen -r eidolon`. Falls back to tmux if screen is missing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

SESSION="eidolon"
BIN="${EIDOLON_BIN:-$ROOT/build/bin/eidolon-server}"
DATA="${EIDOLON_DATA:-data/runs/server}"
HOST="${EIDOLON_HOST:-0.0.0.0}"
PORT="${EIDOLON_PORT:-8081}"
SEED="${EIDOLON_SEED:-42}"
DETERMINISTIC="${EIDOLON_DETERMINISTIC:-1}"
LOG="data/logs/eidolon-server.log"

mkdir -p data/logs

# Build the argument list.
ARGS=(--data "$DATA" --host "$HOST" --port "$PORT" --seed "$SEED")
[ "$DETERMINISTIC" = "1" ] && ARGS+=(--deterministic)
# Optionally attach an LLM endpoint if one is healthy on 8080.
if [ "${EIDOLON_NO_LLM:-0}" = "1" ]; then
  :
elif curl -sf --max-time 2 http://127.0.0.1:8080/health >/dev/null 2>&1; then
  ARGS+=(--llm http://127.0.0.1:8080/v1 --llm-timeout 20000)
fi

die() { echo "error: $*" >&2; exit 1; }

have_screen() { command -v screen >/dev/null 2>&1; }
have_tmux()  { command -v tmux  >/dev/null 2>&1; }
session_exists() {
  if have_screen; then screen -ls "$SESSION" 2>/dev/null | grep -q "$SESSION";
  elif have_tmux; then tmux has-session -t "$SESSION" 2>/dev/null;
  else return 1; fi
}

run_in_session() {
  if have_screen; then
    screen -dmS "$SESSION" bash -c "$BIN ${ARGS[*]} >> \"$LOG\" 2>&1"
  elif have_tmux; then
    tmux new-session -d -s "$SESSION" "$BIN ${ARGS[*]} >> \"$LOG\" 2>&1"
  else
    die "neither screen nor tmux is installed"
  fi
}

stop() {
  if session_exists; then
    echo "stopping server (screen session $SESSION)..."
    if have_screen; then screen -S "$SESSION" -X quit;
    else tmux kill-session -t "$SESSION"; fi
    # Also kill the server binary in case it outlived the session shell.
    pkill -f "$BIN --data $DATA" 2>/dev/null || true
  else
    echo "not running (no screen session $SESSION)"
  fi
}

status() {
  if session_exists; then
    echo "screen session '$SESSION': RUNNING"
  else
    echo "screen session '$SESSION': not running"
  fi
  local s
  s="$(curl -sf --max-time 2 "http://127.0.0.1:$PORT/api/status" 2>/dev/null || true)"
  if [ -n "$s" ]; then
    echo "HTTP $HOST:$PORT: $(printf '%s' "$s" | python3 -c 'import sys,json; d=json.load(sys.stdin); print("alive=%s day=%s hour=%.1f health=%.0f energy=%.0f" % (d["alive"], d["day"], d["hour"], d["health"], d["energy"]))' 2>/dev/null || echo "$s")"
  else
    echo "HTTP $HOST:$PORT: NOT RESPONDING"
  fi
}

case "${1:-start}" in
  start|restart)
    # Idempotent: if already in a live session with a responding server, do nothing.
    if session_exists && curl -sf --max-time 2 "http://127.0.0.1:$PORT/api/status" >/dev/null 2>&1; then
      echo "already running (session $SESSION): http://localhost:$PORT"
      exit 0
    fi
    stop
    [ -x "$BIN" ] || die "server binary not found at $BIN (build first: cmake --build build -j)"
    run_in_session
    echo "started eidolon-server in screen session '$SESSION'"
    echo "  chat UI:  http://localhost:$PORT  (LAN: http://<this-machine-ip>:$PORT)"
    echo "  reattach: $(have_screen && echo "screen -r $SESSION" || echo "tmux attach -t $SESSION")"
    echo "  log:      tail -f $LOG"
    for _ in $(seq 1 30); do
      curl -sf --max-time 2 "http://127.0.0.1:$PORT/api/status" >/dev/null 2>&1 && break
      sleep 1
    done
    status
    ;;
  stop) stop ;;
  status) status ;;
  log) tail -f "$LOG" ;;
  *) echo "usage: $0 [start|stop|status|log]" >&2; exit 2 ;;
esac