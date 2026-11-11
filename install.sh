#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Install the device package (app/build/paperlagoon.fwsc) on an FM-1 over USB, with fm1-base's installer.
# Usage: ./install.sh [--build] [fm1_install.py options: --port NAME, --yes, --classic ...]
#   --build   run ./build.sh first
#   --info    only show the identity of the connected FM-1
# Needs: the FM-1 on USB, and python3 with mido and python-rtmidi (python3 -m pip install mido python-rtmidi).
set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"
PKG=app/build/paperlagoon.fwsc

if [ "$1" = "--build" ]; then
    shift
    ./build.sh
fi
if ! "$PY" -c "import mido, rtmidi" 2>/dev/null; then
    echo "install: $PY has no mido / python-rtmidi: $PY -m pip install mido python-rtmidi" >&2
    exit 1
fi
if [ "$1" = "--info" ]; then
    exec "$PY" base/tools/fm1_install.py --info
fi
if [ ! -f "$PKG" ]; then
    echo "install: no $PKG; build it first: ./build.sh (or ./install.sh --build)" >&2
    exit 1
fi
echo "install: $PKG ($(git rev-parse --short HEAD 2>/dev/null || echo '?') on $(git branch --show-current 2>/dev/null || echo '?'), built $(date -r "$PKG" '+%Y-%m-%d %H:%M'))"
exec "$PY" base/tools/fm1_install.py "$PKG" "$@"
