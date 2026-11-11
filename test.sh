#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Build the host simulator and run every script in app/tests (or the ones named), judged by the exit code: 0 passed,
# 1 an expectation failed, 2 the script itself is wrong (an unknown command or name: the run stops there).
# Usage: ./test.sh [name ...]      e.g. ./test.sh basic rec
set -e
cd "$(dirname "$0")"
./build.sh --host >/dev/null
set +e
OUT=app/build/host
fails=0
if [ $# -eq 0 ]; then
    set -- $(ls app/tests/*.txt | sed 's|.*/||; s|\.txt$||')
fi
for t in "$@"; do
    log=$("$OUT/paperlagoon_host" "app/tests/$t.txt" "$OUT" 2>&1)
    rc=$?
    oks=$(printf '%s\n' "$log" | grep -c '^  ok')
    case $rc in
    0) echo "ok    $t ($oks checks)" ;;
    1) echo "FAIL  $t"; printf '%s\n' "$log" | grep FAIL; fails=$((fails + 1)) ;;
    *) echo "ERROR $t (exit $rc)"; printf '%s\n' "$log" | tail -3; fails=$((fails + 1)) ;;
    esac
done
[ $fails -eq 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
