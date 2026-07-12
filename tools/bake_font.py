#!/usr/bin/env python3
"""Bake assets/minecraft.otf into bitmap atlases (ASCII 32..126) + JSON metrics.
Usage: python3 tools/bake_font.py   (writes assets/fonts/baked/{small,button,splash,load}.{png,json})"""
import json, os
from PIL import Image, ImageDraw, ImageFont

SIZES = {"small": 32, "button": 43, "splash": 48, "load": 35}
FONT = "assets/minecraft.otf"
OUT = "assets/fonts/baked"
os.makedirs(OUT, exist_ok=True)
for name, px in SIZES.items():
    font = ImageFont.truetype(FONT, px)
    chars = [chr(c) for c in range(32, 127)]
    pad = 2
    cell_w = max(int(font.getlength(c)) for c in chars) + pad * 2
    asc, desc = font.getmetrics()
    cell_h = asc + desc + pad * 2
    cols = 16
    rows = (len(chars) + cols - 1) // cols
    atlas = Image.new("RGBA", (cols * cell_w, rows * cell_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(atlas)
    glyphs = {}
    for i, ch in enumerate(chars):
        cx, cy = (i % cols) * cell_w + pad, (i // cols) * cell_h + pad
        d.text((cx, cy), ch, font=font, fill=(255, 255, 255, 255))
        glyphs[ch] = {"x": cx - pad, "y": cy - pad, "w": cell_w, "h": cell_h,
                      "adv": int(font.getlength(ch))}
    atlas.save(f"{OUT}/{name}.png")
    with open(f"{OUT}/{name}.json", "w") as jf:
        json.dump({"size": px, "cell_h": cell_h, "glyphs": glyphs}, jf)
    print(f"{OUT}/{name}.png  {atlas.size}")
