#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Serve the browser emulator (app/build/emu, made by ./build.sh --web) on http://localhost:PORT/ (default 8000).
# Usage: ./serve.sh [port]
set -e
cd "$(dirname "$0")"
PORT="${1:-8000}"
DIR=app/build/emu
if [ ! -f "$DIR/index.html" ]; then
    echo "serve: no $DIR/index.html; build it first: ./build.sh --web" >&2
    exit 1
fi
echo "serving $DIR on http://localhost:$PORT/ (Ctrl-C to stop)"
exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$DIR"
