#!/usr/bin/env python3
# coding: utf-8
"""Generate the Lern Deutsch launcher icon.

Pebble renders launcher icons as a monochrome mask (white shape on a
transparent background, tinted by the system). We draw the app's cat mascot so
it is instantly recognisable in the watch's app list.

Output: resources/images/menu_icon.png  (24x24, within the 25x25 limit)
"""
import os
from PIL import Image, ImageDraw

SIZE = 24
OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "images", "menu_icon.png")

img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
W = (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)

cx, cy, r = 12, 13, 8

# Ears (triangles)
d.polygon([(cx - 8, cy - 5), (cx - 2, cy - 6), (cx - 7, cy - 12)], fill=W)
d.polygon([(cx + 8, cy - 5), (cx + 2, cy - 6), (cx + 7, cy - 12)], fill=W)

# Head
d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=W)

# Eyes + nose punched out so the silhouette reads as a face
for ex in (-3, 3):
    d.ellipse([cx + ex - 1, cy - 2, cx + ex + 1, cy + 1], fill=CLEAR)
d.polygon([(cx - 1, cy + 3), (cx + 1, cy + 3), (cx, cy + 5)], fill=CLEAR)

# Whiskers (clear lines)
for s in (-1, 1):
    d.line([(cx + s * 6, cy + 3), (cx + s * 11, cy + 2)], fill=CLEAR)
    d.line([(cx + s * 6, cy + 5), (cx + s * 11, cy + 6)], fill=CLEAR)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
img.save(OUT)
print("wrote", os.path.normpath(OUT), img.size)
