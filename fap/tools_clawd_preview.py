#!/usr/bin/env python3
"""Render the Clawd sprite the same way the FAP draws it, scaled up, so we can
compare to the reference and tune before flashing. Orange-on-black to match."""
from PIL import Image, ImageDraw
import sys

SCALE = 16
BS = 5  # block size in "device pixels"
ORANGE = (200, 120, 90)
BLACK = (0, 0, 0)

# canvas in device pixels
W, H = 60, 50
img = Image.new("RGB", (W * SCALE, H * SCALE), BLACK)
d = ImageDraw.Draw(img)

def box(x, y, w, h, color=ORANGE):
    d.rectangle([x*SCALE, y*SCALE, (x+w)*SCALE-1, (y+h)*SCALE-1], fill=color)

# ---- creature params (tune these) ----
ox, oy = 8, 8
# body grid: '#' filled. cols 0..8 (col0/col8 are side tabs)
# 4-row body: head (2 blocks) -> square tab row -> gap below (1 block)
G = [
    ".#######.",
    ".#######.",
    ".#######.",
    ".#######.",
]
for r, row in enumerate(G):
    for col, ch in enumerate(row):
        if ch == '#':
            box(ox + col*BS, oy + r*BS, BS, BS)

# square side tabs (BS x BS) at row2: head above = 2 blocks = 2x tab height,
# gap below = 1 block = tab height
box(ox + 0*BS, oy + 2*BS, BS, BS)
box(ox + 8*BS, oy + 2*BS, BS, BS)

# legs: 4 legs in two pairs, symmetric (mirror left pair to the right)
EYE_W = 2
LEG_W = 4          # ~2x eye
total = 9*BS
l1 = ox + 1*BS        # outer leg flush with body edge
l2 = ox + 2*BS + 2
r1 = ox + total - (l1 - ox) - LEG_W
r2 = ox + total - (l2 - ox) - LEG_W
ly = oy + 4*BS
LEG_H = BS + 1
for lx in [l1, l2, r2, r1]:
    box(lx, ly, LEG_W, LEG_H)

# eyes: thin slits, col2 & col6, sitting just above the tab row (row1)
ey = oy + 1*BS
EYE_H = BS
box(ox + 2*BS + 1, ey, EYE_W, EYE_H, BLACK)
box(ox + 6*BS + 1, ey, EYE_W, EYE_H, BLACK)

img.save("/tmp/clawd.png")
print("saved /tmp/clawd.png")
