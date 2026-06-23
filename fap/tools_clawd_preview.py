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
G = [
    ".#######.",   # head
    ".#######.",   # head (eyes)
    "#########",   # tabs (2 rows tall, centered)
    "#########",   # tabs
    ".#######.",   # lower body
]
for r, row in enumerate(G):
    for col, ch in enumerate(row):
        if ch == '#':
            box(ox + col*BS, oy + r*BS, BS, BS)

# legs: 4 fat legs, width ~2.5x eye width
EYE_W = 2
LEG_W = 5          # ~2.5x eye
leg_cols_px = [ox+1*BS+1, ox+2*BS+2, ox+5*BS+1, ox+6*BS+2]  # two pairs
ly = oy + 5*BS
LEG_H = BS + 1
for lx in leg_cols_px:
    box(lx, ly, LEG_W, LEG_H)

# eyes: punch black, tall thin, col2 & col6
ey = oy + BS + 1
EYE_H = BS + 2
box(ox + 2*BS + 1, ey, EYE_W, EYE_H, BLACK)
box(ox + 6*BS + 1, ey, EYE_W, EYE_H, BLACK)

img.save("/tmp/clawd.png")
print("saved /tmp/clawd.png")
