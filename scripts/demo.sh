#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bin="${ATLAS_BIN:-build/debug}"
port="${ATLAS_PORT:-0}"
startup_log="$(mktemp)"
"$bin/atlas-node" --memory-demo --port "$port" >"$startup_log" &
node_pid=$!
trap 'kill -TERM "$node_pid" 2>/dev/null || true; wait "$node_pid" 2>/dev/null || true; rm -f -- "$startup_log"' EXIT
ready=0
for _ in {1..50}; do
  if ! kill -0 "$node_pid" 2>/dev/null; then echo "Node failed to start (possibly port in use)." >&2; exit 1; fi
  if grep -q '^ATLAS_READY ' "$startup_log"; then
    port="$(sed -n 's/^ATLAS_READY [^:]*:\([0-9]*\) .*/\1/p' "$startup_log")"
    ready=1
    break
  fi
  sleep 0.1
done
if [[ "$ready" != 1 ]]; then echo "Node did not become ready." >&2; exit 1; fi
cat "$startup_log"
"$bin/atlas-cli" --port "$port" ping
"$bin/atlas-cli" --port "$port" put atlas hello
"$bin/atlas-cli" --port "$port" get atlas
printf '\n'
"$bin/atlas-cli" --port "$port" batch put a first put b second delete atlas
"$bin/atlas-cli" --port "$port" stats
echo 'Demo complete. These values exist in memory only.'
