#!/usr/bin/env bash
# Starter script for local XPilot Classic session
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PORT="${1:-15355}"
MAP="${2:-lib/maps/globe.xp}"
ROBOTS="${3:-2}"

if [ ! -x "src/server/xpilots" ] || [ ! -x "src/client/xpilot" ]; then
    echo "Binaries not found. Building first with 'make'..."
    make
fi

echo "Starting XPilot server on port $PORT with map '$MAP' ($ROBOTS bots)..."
src/server/xpilots -map "$MAP" -port "$PORT" -robots "$ROBOTS" -noQuit &
SERVER_PID=$!

cleanup() {
    echo "Stopping XPilot server (PID $SERVER_PID)..."
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Give the server a moment to bind the socket
sleep 1

echo "Starting XPilot client..."
src/client/xpilot -port "$PORT" localhost
