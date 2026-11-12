# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# PaperLagoon's title image, drawn as the screen draws the lagoon: the water's line, a drop over it, its rings, the
# great ring, a little shimmer under the line, on the screen's black.
#   python3 app/site/title.py [OUT_DIR]       (default app/site/img; needs Pillow and numpy)
# Writes title.png at 1280 x 640 (GitHub's social preview size): the README's, the site's and the social preview's.
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "..", "..", "base", "assets", "fonts")
W, H, SS = 1280, 640, 3                      # size, supersampling

INK, SUB, LINE = (230, 234, 230), (148, 149, 156), (131, 137, 139)
RING, RING2, DROP = (57, 182, 238), (148, 226, 255), (255, 255, 255)
SUBTITLE = "GENERATIVE ENVIRONMENTAL MUSIC FOR THE FM-1"


def S(v):
    return int(round(v * SS))


def col(c, a):
    return c + (int(255 * max(0.0, min(1.0, a))),)


def font(name, px):
    return ImageFont.truetype(os.path.join(FONTS, name), S(px))


class Canvas:
    def __init__(self):
        self.lay = Image.new("RGBA", (S(W), S(H)), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.lay)

    def water(self, wy, cx, amp=2.2, x0=0, x1=W, alpha=0.75):
        pts = []
        for i in range(int(x0), int(x1) + 1, 2):
            far = min(1.0, abs(i - cx) / (0.5 * W))
            y = wy + (1.0 - 0.6 * far) * amp * math.sin(i * 0.018) + 0.55 * amp * math.sin(i * 0.051 + 1.3)
            pts.append((S(i), S(y)))
        self.d.line(pts, fill=col(LINE, alpha), width=S(1.4))

    def circle(self, cx, cy, r, c=RING, a=0.95, w=1.6):
        self.d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], outline=col(c, a), width=S(w))

    def dot(self, x, y, r, c, a):
        self.d.ellipse([S(x - r), S(y - r), S(x + r), S(y + r)], fill=col(c, a))

    def rings(self, cx, wy, n=9, size=1.0, flat=0.17, a0=0.95, dotted=2):
        for k in range(n):
            rx = size * (22 + k * 30 + k * k * 2.2)
            ry = rx * flat
            a = a0 * (1.0 - k / (n + 0.5)) ** 1.4
            if k < dotted:
                m = int(rx * 0.55)
                for j in range(m):
                    th = 2 * math.pi * j / m
                    self.dot(cx + rx * math.cos(th), wy + ry * math.sin(th), 1.1, RING2, a)
            else:
                self.d.ellipse([S(cx - rx), S(wy - ry), S(cx + rx), S(wy + ry)], outline=col(RING, a * 0.8),
                               width=S(1.1))

    def drop(self, cx, dy, r=4.2, trail=70):
        for j in range(40):
            self.dot(cx, dy - trail + j * trail / 44, 0.8, DROP, 0.25 * j / 40)
        self.dot(cx, dy, r, DROP, 1.0)

    def shimmer(self, cx, wy, n=110, width=0.18, seed=7, a0=0.6, depth=None):
        rng = np.random.default_rng(seed)
        depth = depth or (H - wy - 30)
        for _ in range(n):
            u = rng.random() ** 1.6
            y = wy + 14 + u * depth
            spread = width * W + (y - wy) * 1.1
            x = cx + rng.normal(0, 0.45) * spread
            ln = 6 + rng.random() * 34 * (1 - u)
            self.d.line([(S(x - ln / 2), S(y)), (S(x + ln / 2), S(y))], fill=col(LINE, a0 * (1 - u) ** 1.2),
                        width=S(1))

    def title(self, x, y, px=92, sub=True, sub_px=21, anchor="l", name="PaperLagoon", track=0.0, color=INK):
        f1 = font("BarlowSemiCondensed-SemiBold.ttf", px)
        f2 = font("BarlowSemiCondensed-Medium.ttf", sub_px)
        def spaced(s, f, t):
            return sum(f.getlength(ch) / SS + t for ch in s) - t
        def put(s, f, t, x, y, c):
            for ch in s:
                self.d.text((S(x), S(y)), ch, font=f, fill=col(c, 1.0))
                x += f.getlength(ch) / SS + t
        wt = spaced(name, f1, track)
        x1 = x - (wt / 2 if anchor == "c" else wt if anchor == "r" else 0)
        put(name, f1, track, x1, y, color)
        if sub:
            ws = spaced(SUBTITLE, f2, sub_px * 0.15)
            x2 = x - (ws / 2 if anchor == "c" else ws if anchor == "r" else -4)
            put(SUBTITLE, f2, sub_px * 0.15, x2, y + px * 1.28, SUB)

    def done(self):
        img = Image.new("RGBA", self.lay.size, (0, 0, 0, 255))
        img = Image.alpha_composite(img, self.lay).convert("RGB").resize((W, H), Image.LANCZOS)
        a = np.asarray(img).astype(np.float32)
        g = np.random.default_rng(3).normal(0, 10, (H, W, 1)) * (a.mean(axis=2, keepdims=True) > 8)
        return Image.fromarray(np.clip(a + g, 0, 255).astype(np.uint8))


def title_image():                           # the screen's lagoon, the name beside it
    c = Canvas(); wy, cx = 0.60 * H, 0.70 * W
    c.water(wy, cx); c.rings(cx, wy); c.shimmer(cx, wy)
    c.circle(cx, wy, 0.33 * H)                                 # the great ring in front of all
    c.drop(cx, wy - 0.6 * 0.33 * H)                            # inside the great ring, a fifth down from its top
    c.title(0.075 * W, 0.255 * H)
    return c.done()


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "img")
    os.makedirs(out, exist_ok=True)
    p = os.path.join(out, "title.png")
    title_image().save(p, optimize=True)
    print(p)
