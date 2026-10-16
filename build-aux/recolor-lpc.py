#!/usr/bin/env python3
"""Make the parade's exclusive items (excl_*.png) from the LPC layers already
in data/texuguito/web/assets/lpc/: a gradient-map recolor, where each opaque
pixel's luminance picks a colour on a dark->light ramp, so the shading
survives the new colour. Recolors of CC-BY-SA/GPL sprites keep their
credits (CREDITS.md). Needs Pillow.

    python3 build-aux/recolor-lpc.py

A new item: a line in ITEMS here, then its key in manifest.json and in
the catalog of src/tools/texuguito/items.cpp.
"""
import os
from PIL import Image

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "texuguito", "web", "assets", "lpc")
OUT = SRC

def ramp(colors, t):
    t = max(0.0, min(1.0, t))
    pos = t * (len(colors) - 1)
    i = min(int(pos), len(colors) - 2)
    f = pos - i
    a, b = colors[i], colors[i + 1]
    return tuple(round(a[k] + (b[k] - a[k]) * f) for k in range(3))

def hexes(*hs):
    return [tuple(int(h[i:i + 2], 16) for i in (1, 3, 5)) for h in hs]

def recolor(name, out, colors, gamma=1.0):
    im = Image.open(os.path.join(SRC, name)).convert("RGBA")
    px = im.load()
    lums = [0.299 * r + 0.587 * g + 0.114 * b for r, g, b, a in im.getdata() if a > 0]
    lo, hi = min(lums), max(lums)
    span = max(1.0, hi - lo)
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            t = ((0.299 * r + 0.587 * g + 0.114 * b) - lo) / span
            px[x, y] = ramp(colors, t ** gamma) + (a,)
    im.save(os.path.join(OUT, out), optimize=True)

GOLD = hexes("#5a3a06", "#a8740e", "#e8b828", "#fff1a0")
ITEMS = [
    ("capa_walk.png", "excl_capareal.png", hexes("#1e0a33", "#4a1a7a", "#7b3fb8", "#e0b84a"), 1.0),
    ("hat_coroa_walk.png", "excl_coroarubi.png", hexes("#3a0610", "#8e1028", "#e0304e", "#ffd0d8"), 1.0),
    ("chifres_walk.png", "excl_chifresdourados.png", GOLD, 1.0),
    ("asas_bg_walk.png", "excl_asasdouradas_bg.png", GOLD, 1.0),
    ("asas_fg_walk.png", "excl_asasdouradas_fg.png", GOLD, 1.0),
    ("hat_mago.png", "excl_magoestelar.png", hexes("#04061a", "#0b1548", "#1a2f86", "#5a86e8"), 1.4),
    ("hat_elmo.png", "excl_elmosombrio.png", hexes("#0c0614", "#2a1640", "#5a3a80", "#c0a0e8"), 1.0),
    ("asas_morcego_bg.png", "excl_asaschefao_bg.png", hexes("#12051f", "#3a0f5a", "#7a2aa0", "#ff6ad0"), 1.0),
    ("asas_morcego_fg.png", "excl_asaschefao_fg.png", hexes("#12051f", "#3a0f5a", "#7a2aa0", "#ff6ad0"), 1.0),
]
for src, out, colors, gamma in ITEMS:
    recolor(src, out, colors, gamma)
print("ok", len(ITEMS))
