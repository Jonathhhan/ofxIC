#!/usr/bin/env sh
set -eu

HOST="127.0.0.1"
PORT="8080"
MODEL="${OFXIC_MODEL:-}"
SERVER="${OFXIC_LLAMA_SERVER:-llama-server}"
CTX="4096"
STARTUP_TIMEOUT="${OFXIC_SERVER_STARTUP_TIMEOUT:-120}"
DETACHED=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: sh scripts/start-llama-server.sh [options]

Options:
  --model PATH       GGUF model path (or OFXIC_MODEL)
  --server PATH      llama-server executable (or OFXIC_LLAMA_SERVER)
  --host HOST        bind host (default 127.0.0.1)
  --port PORT        bind port (default 8080)
  --ctx N            context size (default 4096)
  --startup-timeout N seconds to wait for /health (default 120)
  --detached         start in background
  --dry-run          print command only
  -h, --help         show this help
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --model) MODEL=$2; shift 2 ;;
    --server) SERVER=$2; shift 2 ;;
    --host) HOST=$2; shift 2 ;;
    --port) PORT=$2; shift 2 ;;
    --ctx) CTX=$2; shift 2 ;;
    --startup-timeout) STARTUP_TIMEOUT=$2; shift 2 ;;
    --detached) DETACHED=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [ -z "$MODEL" ]; then
  echo "No GGUF model configured. Pass --model /path/to/model.gguf or set OFXIC_MODEL." >&2
  exit 1
fi

if [ ! -f "$MODEL" ]; then
  echo "Model file not found: $MODEL" >&2
  exit 1
fi

if [ -x "$SERVER" ]; then
  SERVER_PATH=$SERVER
elif command -v "$SERVER" >/dev/null 2>&1; then
  SERVER_PATH=$(command -v "$SERVER")
else
  echo "Could not find llama-server: $SERVER" >&2
  echo "Install/build llama.cpp separately, put llama-server on PATH, or pass --server /path/to/llama-server." >&2
  exit 1
fi

URL="http://$HOST:$PORT"
if command -v curl >/dev/null 2>&1 && curl -fsS --max-time 2 "$URL/health" >/dev/null 2>&1; then
  echo "llama-server is already ready at $URL"
  echo "export OFXIC_ENDPOINT_URL=$URL"
  exit 0
fi

set -- "$SERVER_PATH" -m "$MODEL" --host "$HOST" --port "$PORT" -c "$CTX"

printf 'Starting llama-server\n  exe:   %s\n  model: %s\n  url:   %s\n' "$SERVER_PATH" "$MODEL" "$URL"
printf 'Command:'
printf ' %s' "$@"
printf '\n'

if [ "$DRY_RUN" -eq 1 ]; then
  exit 0
fi

if [ "$DETACHED" -eq 1 ]; then
  LOG_DIR="${TMPDIR:-/tmp}/ofxIC"
  mkdir -p "$LOG_DIR"
  LOG_FILE="$LOG_DIR/llama-server-$PORT.log"
  "$@" >"$LOG_FILE" 2>&1 &
  PID=$!
  echo "llama-server started in background (PID $PID)"
  echo "log: $LOG_FILE"
  if command -v curl >/dev/null 2>&1; then
    i=0
    max_tries=$((STARTUP_TIMEOUT * 2))
    while [ "$i" -lt "$max_tries" ]; do
      if curl -fsS --max-time 2 "$URL/health" >/dev/null 2>&1; then
        echo "llama-server is ready at $URL"
        echo "export OFXIC_ENDPOINT_URL=$URL"
        exit 0
      fi
      if ! kill -0 "$PID" 2>/dev/null; then
        echo "llama-server exited before becoming ready. See $LOG_FILE" >&2
        tail -n 30 "$LOG_FILE" >&2 2>/dev/null || true
        exit 1
      fi
      sleep 0.5
      i=$((i + 1))
    done
    echo "llama-server did not become ready within ${STARTUP_TIMEOUT}s. See $LOG_FILE" >&2
    tail -n 30 "$LOG_FILE" >&2 2>/dev/null || true
    exit 1
  fi
  echo "curl is required to verify llama-server readiness." >&2
  exit 1
fi

echo "llama-server is running in this terminal. Press Ctrl+C to stop it."
echo "Use OFXIC_ENDPOINT_URL=$URL in the example."
exec "$@"
