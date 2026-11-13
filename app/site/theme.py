# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Puts PaperLagoon's colours (theme.css: the screen's black, its pale type, the great ring's blue) last in the <head> of
# every page of a site made by fm1-base's tools/make_site.py, over its paper; and the Barlow the screen is drawn in.
#   python3 app/site/theme.py SITE_DIR          (as ./site.sh does)
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARK = "<!-- paperlagoon theme -->"
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600'
         '&family=Barlow+Semi+Condensed:wght@600;700&display=swap">\n')


def main(site):
    css = (HERE / "theme.css").read_text(encoding="utf-8")
    pages = sorted(Path(site).rglob("*.html"))
    for p in pages:
        html = p.read_text(encoding="utf-8")
        if MARK in html:
            continue
        i = html.find("</head>")
        if i < 0:
            raise SystemExit(f"theme.py: {p}: no </head>")
        fonts = "" if "fonts.googleapis.com/css2" in html[:i] else FONTS
        html = html[:i] + f"{MARK}\n{fonts}<style>\n{css}</style>\n" + html[i:]
        p.write_text(html, encoding="utf-8")
        print(f"theme: {p}")
    if not pages:
        raise SystemExit(f"theme.py: no pages in {site}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "app/build/site")
