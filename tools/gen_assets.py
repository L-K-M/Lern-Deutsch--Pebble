#!/usr/bin/env python3
# coding: utf-8
"""
Generate the Chinese rendering assets for LearnDeutsch.

Pebble's firmware fonts have no Chinese glyphs, and the SDK's font subsetter is
unreliable for non-ASCII regexes. So instead of a TrueType resource we ship our
own *bitmap glyph atlas*: a PNG sprite-sheet containing exactly the characters
our word list uses, plus a sorted codepoint table the watch binary-searches at
run time to find each glyph's cell.

For every atlas we emit a 1-bit, transparent PNG (white glyph on a transparent
background) so the watch can recolour it on the fly and composite it over any
panel colour.

Outputs:
  resources/images/han_<key>.png   one atlas per difficulty group + a UI atlas
  src/c/vocab_gen.c                 cards, group table, codepoint tables

Run from the repo root:   .venv/bin/python tools/gen_assets.py
"""
import json
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import vocab  # noqa: E402

FONT_PATH = os.path.join(HERE, "fonts", "NotoSansSC-Regular.otf")

# --- atlas geometry ---------------------------------------------------------
# CELL must be a multiple of 8 so 1-bit sub-bitmaps stay byte-aligned in X
# (a hard requirement of gbitmap_create_as_sub_bitmap for <8bpp formats).
COLS = 16
CELL = 32
GLYPH = 30          # rendered glyph size inside the cell
SS = 3              # supersampling factor for cleaner 1-bit edges
THRESHOLD = 116     # 0..255 cut-off when flattening grey -> 1-bit

IMG_DIR = os.path.join(ROOT, "resources", "images")
GEN_C = os.path.join(ROOT, "src", "c", "vocab_gen.c")

GENDER = {"": 0, "m": 1, "f": 2, "n": 3}
ICONS = {"wave": "ICON_WAVE", "num": "ICON_NUM", "people": "ICON_PEOPLE",
         "cup": "ICON_CUP", "sun": "ICON_SUN", "speech": "ICON_SPEECH",
         "palette": "ICON_PALETTE", "paw": "ICON_PAW", "shirt": "ICON_SHIRT",
         "house": "ICON_HOUSE", "clock": "ICON_CLOCK", "cloud": "ICON_CLOUD",
         "plane": "ICON_PLANE", "book": "ICON_BOOK", "bag": "ICON_BAG",
         "fork": "ICON_FORK", "plus": "ICON_PLUS", "heart": "ICON_HEART",
         "leaf": "ICON_LEAF", "ball": "ICON_BALL", "phone": "ICON_PHONE",
         "car": "ICON_CAR", "calendar": "ICON_CALENDAR", "warning": "ICON_WARNING",
         "globe": "ICON_GLOBE", "building": "ICON_BUILDING", "run": "ICON_RUN",
         "box": "ICON_BOX"}

PKG = os.path.join(ROOT, "package.json")


def han_codepoints(s):
    """Codepoints in `s` that need a custom glyph (anything non-ASCII)."""
    return [ord(c) for c in s if ord(c) > 0x7F]


def collect(strings):
    cps = set()
    for s in strings:
        cps.update(han_codepoints(s))
    return sorted(cps)


def render_atlas(codepoints, out_path):
    """Render `codepoints` (already sorted) into a 1-bit transparent PNG."""
    n = max(1, len(codepoints))
    rows = (n + COLS - 1) // COLS
    W, H = COLS * CELL, rows * CELL
    atlas = Image.new("P", (W, H), 0)
    atlas.putpalette([0, 0, 0,        # index 0 -> transparent (see tRNS below)
                      255, 255, 255])  # index 1 -> glyph (recoloured on watch)

    font = ImageFont.truetype(FONT_PATH, GLYPH * SS)
    for i, cp in enumerate(codepoints):
        ch = chr(cp)
        big = Image.new("L", (CELL * SS, CELL * SS), 0)
        d = ImageDraw.Draw(big)
        l, t, r, b = font.getbbox(ch)
        x = (CELL * SS - (r - l)) // 2 - l
        y = (CELL * SS - (b - t)) // 2 - t
        d.text((x, y), ch, fill=255, font=font)
        small = big.resize((CELL, CELL), Image.LANCZOS)
        mask = small.point(lambda p: 255 if p >= THRESHOLD else 0).convert("1")
        atlas.paste(1, ((i % COLS) * CELL, (i // COLS) * CELL), mask)

    atlas.save(out_path, transparency=0, bits=1, optimize=True)
    return W, H


def c_string(s):
    """A C UTF-8 string literal, robust against \\x escapes eating the next char.

    Each escaped byte becomes its own adjacent literal ("\\xc3" "\\x9f" "e"), so a
    following ASCII hex digit (the 'e' in "heisse", the 'd' in "muede") can never
    be swallowed into the escape. Adjacent C literals are concatenated.
    """
    parts, run = [], ""
    for byte in s.encode("utf-8"):
        if 0x20 <= byte < 0x7F and byte not in (0x22, 0x5C):  # safe printable
            run += chr(byte)
        else:
            if run:
                parts.append('"%s"' % run)
                run = ""
            parts.append('"\\x%02x"' % byte)
    if run:
        parts.append('"%s"' % run)
    return " ".join(parts) if parts else '""'


def cp_array(name, cps):
    body = ", ".join("0x%04x" % c for c in cps) if cps else "0"
    return "static const uint16_t %s[] = { %s };\n" % (name, body)


def write_package_json(atlases):
    """Rewrite package.json's resource list to match the current atlases."""
    with open(PKG, encoding="utf-8") as f:
        pkg = json.load(f)
    media = [{"type": "png", "name": "IMAGE_MENU_ICON",
              "file": "images/menu_icon.png", "menuIcon": True}]
    for key, res, _ in atlases:
        media.append({"type": "bitmap", "name": res, "file": "images/han_%s.png" % key})
    pkg["pebble"]["resources"]["media"] = media
    with open(PKG, "w", encoding="utf-8") as f:
        json.dump(pkg, f, indent=2)
        f.write("\n")


def main():
    if not os.path.exists(FONT_PATH):
        sys.exit("Missing %s — see tools/fonts/README" % FONT_PATH)
    os.makedirs(IMG_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(GEN_C), exist_ok=True)

    # ---- UI atlas: every non-card Chinese string the C code draws ----------
    ui_strings = list(vocab.UI_STRINGS.values()) + vocab.UI_PRAISE + vocab.UI_NUDGE
    ui_strings += [g["zh"] for g in vocab.GROUPS]          # deck names (menu)
    ui_strings += [t["zh"] for t in vocab.TIERS]           # tier names (menu)
    ui_cps = collect(ui_strings)

    atlases = [("ui", "HAN_UI", ui_cps)]
    for g in vocab.GROUPS:
        cps = collect([c[1] for c in g["cards"]])
        atlases.append((g["key"], g["res"], cps))

    # ---- render every atlas PNG -------------------------------------------
    report = []
    for key, res, cps in atlases:
        path = os.path.join(IMG_DIR, "han_%s.png" % key)
        w, h = render_atlas(cps, path)
        report.append((key, res, len(cps), w, h, path))

    # ---- emit the generated C ---------------------------------------------
    L = []
    L.append("// Generated by tools/gen_assets.py — DO NOT EDIT BY HAND.\n")
    L.append("// Re-run `.venv/bin/python tools/gen_assets.py` after editing tools/vocab.py.\n")
    L.append('#include "lg.h"\n\n')

    # codepoint tables
    for key, res, cps in atlases:
        L.append(cp_array("CP_%s" % key.upper(), cps))
    L.append("\n")

    # atlas definitions
    for key, res, cps in atlases:
        L.append(
            "const HanAtlas ATLAS_%s = { RESOURCE_ID_%s, CP_%s, %d, %d, %d, %d };\n"
            % (key.upper(), res, key.upper(), len(cps), COLS, CELL, CELL))
    L.append("\n")

    # cards + group table
    for g in vocab.GROUPS:
        L.append("static const Card CARDS_%s[] = {\n" % g["key"].upper())
        for de, zh, en, gen in g["cards"]:
            L.append("  { %s, %s, %s, %d },\n"
                     % (c_string(de), c_string(zh), c_string(en), GENDER[gen]))
        L.append("};\n")
    L.append("\n")

    L.append("const Group g_groups[] = {\n")
    for g in vocab.GROUPS:
        L.append(
            "  { %s, %s, %s, GColor%s, %s, CARDS_%s, %d, &ATLAS_%s },\n"
            % (c_string(g["de"]), c_string(g["zh"]), c_string(g["en"]),
               g["accent"], ICONS[g["icon"]], g["key"].upper(),
               len(g["cards"]), g["key"].upper()))
    L.append("};\n")
    L.append("const int g_group_count = %d;\n\n" % len(vocab.GROUPS))

    # tiers: each lists the indices of the decks it contains
    tier_decks = {t["id"]: [] for t in vocab.TIERS}
    for i, g in enumerate(vocab.GROUPS):
        tier_decks[g["tier"]].append(i)
    for ti, t in enumerate(vocab.TIERS):
        L.append("static const int TIER_DECKS_%d[] = { %s };\n"
                 % (ti, ", ".join(str(x) for x in tier_decks[t["id"]])))
    L.append("const Tier g_tiers[] = {\n")
    for ti, t in enumerate(vocab.TIERS):
        L.append("  { %s, %s, %s, GColor%s, TIER_DECKS_%d, %d },\n"
                 % (c_string(t["de"]), c_string(t["zh"]), c_string(t["en"]),
                    t["accent"], ti, len(tier_decks[t["id"]])))
    L.append("};\n")
    L.append("const int g_tier_count = %d;\n\n" % len(vocab.TIERS))

    # UI strings
    for k, v in vocab.UI_STRINGS.items():
        L.append("const char UIZH_%s[] = %s;\n" % (k, c_string(v)))
    L.append("\n")
    for arr_name, arr in (("PRAISE", vocab.UI_PRAISE), ("NUDGE", vocab.UI_NUDGE)):
        items = ", ".join(c_string(s) for s in arr)
        L.append("const char *const UIZH_%s[] = { %s };\n" % (arr_name, items))
        L.append("const int UIZH_%s_N = %d;\n" % (arr_name, len(arr)))
    L.append("\n")

    with open(GEN_C, "w", encoding="utf-8") as f:
        f.write("".join(L))

    write_package_json(atlases)

    # ---- report ------------------------------------------------------------
    total_cards = sum(len(g["cards"]) for g in vocab.GROUPS)
    print("LearnDeutsch assets generated")
    print("  decks: %d   cards: %d" % (len(vocab.GROUPS), total_cards))
    for key, res, ncp, w, h, path in report:
        print("  atlas %-11s %3d glyphs  %3dx%-4d  %s"
              % (key, ncp, w, h, os.path.relpath(path, ROOT)))
    print("  wrote", os.path.relpath(GEN_C, ROOT))
    print("  updated", os.path.relpath(PKG, ROOT), "(%d atlases)" % len(atlases))


if __name__ == "__main__":
    main()
