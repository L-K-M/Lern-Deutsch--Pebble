#include "lg.h"

// ============================================================================
//  hanzi.c — drawing Chinese on a watch that has no Chinese font.
//
//  Each difficulty group ships a PNG "glyph atlas" (built by tools/gen_assets.py)
//  containing exactly the characters it needs, white-on-transparent, packed into
//  a grid. Alongside it is a sorted table of the codepoints, one per cell. To
//  draw a string we UTF-8 decode it, binary-search each codepoint to find its
//  cell, and blit that cell with a sub-bitmap.
//
//  The atlas imports as a 1-bit palette bitmap (transparent + one opaque slot),
//  so we can recolour the opaque slot on the fly and tint the text any colour.
// ============================================================================

#define MAX_GLYPHS 24   // longest phrase we lay out in one go

// --- UTF-8 ------------------------------------------------------------------
// Decode the next codepoint; *adv receives the bytes consumed (0 at the end).
static uint32_t utf8_next(const char *s, int *adv) {
  const uint8_t *u = (const uint8_t *)s;
  uint8_t c = u[0];
  if (c == 0)            { *adv = 0; return 0; }
  if (c < 0x80)          { *adv = 1; return c; }
  if ((c & 0xE0) == 0xC0){ *adv = 2; return ((c & 0x1F) << 6) | (u[1] & 0x3F); }
  if ((c & 0xF0) == 0xE0){ *adv = 3; return ((c & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); }
  if ((c & 0xF8) == 0xF0){ *adv = 4; return ((c & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); }
  *adv = 1; return c;    // malformed; skip a byte
}

// Index of `cp` in the atlas, or -1. The codepoint table is sorted.
static int glyph_index(const HanAtlas *a, uint32_t cp) {
  int lo = 0, hi = a->cp_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = a->cp[mid];
    if (v == cp) return mid;
    if (v < cp)  lo = mid + 1;
    else         hi = mid - 1;
  }
  return -1;
}

// Decode `utf8` into up to MAX_GLYPHS atlas cell indices; returns the count.
// Characters not in this atlas are skipped (they simply won't draw).
static int decode_glyphs(const HanAtlas *a, const char *utf8, int *out) {
  int n = 0, adv;
  for (uint32_t cp; (cp = utf8_next(utf8, &adv)) && n < MAX_GLYPHS; utf8 += adv) {
    int gi = glyph_index(a, cp);
    if (gi >= 0) out[n++] = gi;
  }
  return n;
}

// --- Atlas load / unload ----------------------------------------------------
static int palette_count(GBitmap *b) {
  switch (gbitmap_get_format(b)) {
    case GBitmapFormat1BitPalette: return 2;
    case GBitmapFormat2BitPalette: return 4;
    case GBitmapFormat4BitPalette: return 16;
    default: return 0;
  }
}

void han_load(HanFont *f, const HanAtlas *def) {
  f->def      = def;
  f->bmp      = gbitmap_create_with_resource(def->res_id);
  f->palette  = f->bmp ? gbitmap_get_palette(f->bmp) : NULL;
  f->fg_index = -1;
  if (f->palette) {
    int n = palette_count(f->bmp);
    for (int i = 0; i < n; i++) {        // the non-transparent slot is the ink
      if (f->palette[i].a != 0) { f->fg_index = i; break; }
    }
  }
}

void han_unload(HanFont *f) {
  if (f->bmp) { gbitmap_destroy(f->bmp); f->bmp = NULL; }
  f->palette = NULL; f->fg_index = -1;
}

// --- Drawing ----------------------------------------------------------------
static void blit_cell(GContext *ctx, HanFont *f, int gi, int x, int y) {
  int cw = f->def->cell_w, ch = f->def->cell_h, cols = f->def->cols;
  GRect src = GRect((gi % cols) * cw, (gi / cols) * ch, cw, ch);
  GBitmap *sub = gbitmap_create_as_sub_bitmap(f->bmp, src);
  if (sub) {
    graphics_draw_bitmap_in_rect(ctx, sub, GRect(x, y, cw, ch));
    gbitmap_destroy(sub);
  }
}

int han_width(const HanFont *f, const char *utf8) {
  int idx[MAX_GLYPHS];
  return decode_glyphs(f->def, utf8, idx) * f->def->cell_w;
}

void han_draw(GContext *ctx, HanFont *f, const char *utf8, GPoint origin, GColor color) {
  if (!f->bmp) return;
  if (f->palette && f->fg_index >= 0) f->palette[f->fg_index] = color;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  int idx[MAX_GLYPHS];
  int n = decode_glyphs(f->def, utf8, idx);
  for (int i = 0; i < n; i++) {
    blit_cell(ctx, f, idx[i], origin.x + i * f->def->cell_w, origin.y);
  }
}

void han_draw_box(GContext *ctx, HanFont *f, const char *utf8, GRect box, GColor color) {
  if (!f->bmp) return;
  if (f->palette && f->fg_index >= 0) f->palette[f->fg_index] = color;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  int idx[MAX_GLYPHS];
  int n = decode_glyphs(f->def, utf8, idx);
  if (n == 0) return;

  int cw = f->def->cell_w, ch = f->def->cell_h;
  int per_line = box.size.w / cw;
  if (per_line < 1) per_line = 1;
  int lines = (n + per_line - 1) / per_line;
  int y = box.origin.y + (box.size.h - lines * ch) / 2;

  for (int li = 0; li < lines; li++) {
    int start = li * per_line;
    int count = (start + per_line <= n) ? per_line : (n - start);
    int x = box.origin.x + (box.size.w - count * cw) / 2;
    for (int i = 0; i < count; i++) {
      blit_cell(ctx, f, idx[start + i], x + i * cw, y + li * ch);
    }
  }
}
