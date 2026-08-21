#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ADDON_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
EXAMPLE_ROOT="$ADDON_ROOT/ofxICChatExample"
ENDPOINT_URL="${OFXIC_ENDPOINT_URL:-http://127.0.0.1:8080}"
MODEL="${OFXIC_MODEL:-}"
SERVER="${OFXIC_LLAMA_SERVER:-llama-server}"
STARTUP_TIMEOUT="${OFXIC_SERVER_STARTUP_TIMEOUT:-120}"
NO_AUTO_SERVER=0
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: sh scripts/run-chat-example.sh [options]

Options:
  --endpoint-url URL endpoint (default http://127.0.0.1:8080)
  --model PATH       local GGUF model used when auto-starting llama-server
  --server PATH      llama-server executable
  --startup-timeout N seconds to wait for local server readiness (default 120)
  --no-auto-server   do not try to start a local server
  --dry-run          print resolved paths/settings without launching
  -h, --help         show this help
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --endpoint-url) ENDPOINT_URL=$2; shift 2 ;;
    --model) MODEL=$2; shift 2 ;;
    --server) SERVER=$2; shift 2 ;;
    --startup-timeout) STARTUP_TIMEOUT=$2; shift 2 ;;
    --no-auto-server) NO_AUTO_SERVER=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

find_example() {
  for candidate in \
    "$EXAMPLE_ROOT/bin/ofxICChatExample" \
    "$EXAMPLE_ROOT/bin/ofxICChatExample.app/Contents/MacOS/ofxICChatExample"; do
    if [ -x "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

endpoint_ready() {
  command -v curl >/dev/null 2>&1 || return 1

  case "$ENDPOINT_URL" in
    http://127.0.0.1:*|http://localhost:*)
      curl -fsS --max-time 2 "$ENDPOINT_URL/health" >/dev/null 2>&1
      ;;
    *)
      API_KEY="${OFXIC_API_KEY:-${HF_TOKEN:-}}"
      if [ -n "$API_KEY" ]; then
        curl -fsS --max-time 5 \
          -H "Authorization: Bearer $API_KEY" \
          "$ENDPOINT_URL/models" >/dev/null 2>&1
      else
        curl -fsS --max-time 5 "$ENDPOINT_URL/models" >/dev/null 2>&1
      fi
      ;;
  esac
}

EXAMPLE_BIN=$(find_example || true)

export OFXIC_ENDPOINT_URL="$ENDPOINT_URL"
if [ -n "$MODEL" ]; then
  export OFXIC_MODEL="$MODEL"
fi
if [ -z "${OFXIC_API_KEY:-}" ] && [ -n "${HF_TOKEN:-}" ]; then
  export OFXIC_API_KEY="$HF_TOKEN"
fi

READY=0
if endpoint_ready; then
  READY=1
fi

if [ "$READY" -eq 0 ] && [ "$NO_AUTO_SERVER" -eq 0 ]; then
  case "$ENDPOINT_URL" in
    http://127.0.0.1:*|http://localhost:*)
      if [ -n "$MODEL" ]; then
        PORT=$(printf '%s' "$ENDPOINT_URL" | sed -n 's#^http://[^:]*:\([0-9][0-9]*\).*$#\1#p')
        [ -n "$PORT" ] || PORT=8080
        echo "llama-server is not responding at $ENDPOINT_URL; starting local server"
        sh "$SCRIPT_DIR/start-llama-server.sh" \
          --server "$SERVER" \
          --model "$MODEL" \
          --port "$PORT" \
          --startup-timeout "$STARTUP_TIMEOUT" \
          --detached
        if endpoint_ready; then
          READY=1
        else
          echo "llama-server was started but is still not healthy at $ENDPOINT_URL/health." >&2
          exit 1
        fi
      else
        echo "llama-server is not reachable at $ENDPOINT_URL." >&2
        echo "Pass --model /path/to/model.gguf to auto-start it, or start an OpenAI-compatible endpoint separately." >&2
      fi
      ;;
    *)
      echo "Endpoint is not reachable at $ENDPOINT_URL/models." >&2
      echo "Hosted endpoints are never auto-started by this script." >&2
      ;;
  esac
fi

if [ "$DRY_RUN" -eq 1 ]; then
  echo "Example: ${EXAMPLE_BIN:-not built}"
  echo "Endpoint URL: $ENDPOINT_URL"
  echo "Model: ${MODEL:-auto/endpoint-advertised}"
  echo "Endpoint reachable: $READY"
  exit 0
fi

if [ -z "$EXAMPLE_BIN" ]; then
  echo "ofxICChatExample executable was not found." >&2
  echo "Generate/build ofxICChatExample first; on macOS the expected path is:" >&2
  echo "  $EXAMPLE_ROOT/bin/ofxICChatExample.app/Contents/MacOS/ofxICChatExample" >&2
  exit 1
fi

if [ "$READY" -eq 0 ]; then
  echo "Endpoint is not reachable; refusing to launch the example with a known-bad endpoint." >&2
  exit 1
fi

echo "Starting ofxICChatExample"
exec "$EXAMPLE_BIN"
