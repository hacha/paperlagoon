#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# PaperLagoon's site: fm1-base's (tools/make_site.py: the front page, the installer, the manual, the emulator), in
# PaperLagoon's colours (app/site/theme.py), into app/build/site; with --push, to the gh-pages branch (replacing it).
#   ./site.sh VERSION PACKAGE [--push]
# PACKAGE: the release's attested package (gh release download vX.Y -p '*.fwsc'). Needs Emscripten (the emulator).
set -e
cd "$(dirname "$0")"
V="${1:?usage: ./site.sh VERSION PACKAGE [--push]}"
PKG="${2:?usage: ./site.sh VERSION PACKAGE [--push]}"
[ -f "$PKG" ] || { echo "site.sh: no package $PKG" >&2; exit 1; }
PY="${PYTHON:-python3}"
"$PY" base/tools/build.py app --web --release "$V"
"$PY" base/tools/make_site.py app "$PKG" "$V" app/build/site
"$PY" app/site/theme.py app/build/site
[ "${3:-}" = "--push" ] || { echo "site in app/build/site (./site.sh $V $PKG --push to publish it)"; exit 0; }
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd app/build/site
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for paperlagoon $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
echo "pushed the site for paperlagoon $V to gh-pages"
