#!/usr/bin/env python3
"""Render the full Claudeogotchi 128x64 screen as it appears on the Flipper
(black pixels on a light screen), scaled up, for the README. Mirrors the FAP's
main_draw so it's a faithful mockup of the real UI."""
from PIL import Image, ImageDraw, ImageFont
import sys

S = 7                      # upscale factor
W, H = 128, 64
FG = (20, 20, 20)          # Flipper "ink"
BG = (247, 244, 236)       # Flipper screen
img = Image.new("RGB", (W*S, H*S), BG)
d = ImageDraw.Draw(img)

def box(x, y, w, h, color=FG):
    if w <= 0 or h <= 0: return
    d.rectangle([x*S, y*S, (x+w)*S-1, (y+h)*S-1], fill=color)
def line(x1, y1, x2, y2, color=FG):
    d.line([(x1*S+S//2, y1*S+S//2), (x2*S+S//2, y2*S+S//2)], fill=color, width=S)

# font: try a few mono faces, fall back to default
def load_font(px):
    for p in ["/System/Library/Fonts/Supplemental/Andale Mono.ttf",
              "/System/Library/Fonts/Menlo.ttc",
              "/Library/Fonts/Arial.ttf",
              "/System/Library/Fonts/Supplemental/Arial.ttf"]:
        try: return ImageFont.truetype(p, px)
        except Exception: pass
    return ImageFont.load_default()
F = load_font(int(6.5*S))   # ~ FontSecondary
FP = load_font(int(9*S))    # ~ FontPrimary

def text(x, baseline, s, font=F):
    # canvas_draw_str uses baseline; PIL "ls" anchor = left/baseline
    try:
        d.text((x*S, baseline*S), s, fill=FG, font=font, anchor="ls")
    except TypeError:
        d.text((x*S, (baseline-6)*S), s, fill=FG, font=font)

# ---------- creature (working pose) ----------
def creature(cx, cy):
    bs = 5
    G = [".#######."]*4
    ox = cx - (9*bs)//2
    oy = cy - (4*bs)//2
    for r,row in enumerate(G):
        for col,ch in enumerate(row):
            if ch=='#': box(ox+col*bs, oy+r*bs, bs, bs)
    box(ox+0*bs, oy+2*bs, bs, bs)        # tabs
    box(ox+8*bs, oy+2*bs, bs, bs)
    lw, lh = bs-1, bs+1
    total = 9*bs
    l1 = ox+1*bs; l2 = ox+2*bs+2
    r1 = ox+total-(l1-ox)-lw; r2 = ox+total-(l2-ox)-lw
    ly = oy+4*bs
    for lx in (l1,l2,r2,r1): box(lx, ly, lw, lh)
    # eyes (slits), punched as background
    ey = oy+bs
    box(ox+2*bs+1, ey, 2, bs, BG)
    box(ox+6*bs+1, ey, 2, bs, BG)

# ---------- header ----------
text(0, 7, "Opus 4.8")
d.ellipse([(120*S, 2*S),(124*S,6*S)], fill=FG)   # online link dot
line(0,9,127,9)

# ---------- creature ----------
creature(24, 28)

# ---------- bars ----------
def bar(y, label, pct):
    lx, bx, bw, bh = 52, 80, 27, 6
    text(lx, y+6, label)
    d.rectangle([bx*S, y*S, (bx+bw)*S-1, (y+bh)*S-1], outline=FG, width=max(1,S//2))
    f = (bw-2)*pct//100
    if f>0: box(bx+1, y+1, f, bh-2)
    text(bx+bw+3, y+6, str(pct))
bar(13, "SESS", 42)
bar(25, "5H", 28)
bar(37, "WEEK", 18)

# ---------- bottom (working) ----------
line(0,50,127,50)
# spinner spark
import math
cx0, cy0 = 6, 57
for i in range(12):
    a = i*(2*math.pi/12)
    ln = 5 if i%2==0 else 3
    line(cx0, cy0, cx0+round(math.cos(a)*ln), cy0+round(math.sin(a)*ln))
box(cx0-1, cy0-1, 2, 2)
text(14, 61, "Spelunking 1:23 7.3k")

img.save("/tmp/screen.png")
print("saved /tmp/screen.png")
