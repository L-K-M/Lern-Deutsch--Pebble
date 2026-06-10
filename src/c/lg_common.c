#include "lg.h"

// ============================================================================
//  lg_common.c — persistence, the mascot, and shared drawing helpers.
// ============================================================================

// --- Persistent state -------------------------------------------------------
#define KEY_DIR      1
#define KEY_LEARNED  2
#define KEY_BEST_BASE 100   // + group index

Direction lg_dir_get(void) {
  return persist_exists(KEY_DIR) ? (Direction)persist_read_int(KEY_DIR) : DIR_DE_ZH;
}
void lg_dir_set(Direction d) { persist_write_int(KEY_DIR, (int)d); }

int  lg_best_stars(int group) {
  uint32_t k = KEY_BEST_BASE + (uint32_t)group;
  return persist_exists(k) ? persist_read_int(k) : 0;
}
void lg_best_submit(int group, int stars) {
  if (stars > lg_best_stars(group)) persist_write_int(KEY_BEST_BASE + (uint32_t)group, stars);
}

int  lg_learned_total(void) {
  return persist_exists(KEY_LEARNED) ? persist_read_int(KEY_LEARNED) : 0;
}
void lg_learned_add(int n) { persist_write_int(KEY_LEARNED, lg_learned_total() + n); }

// --- Text + panels ----------------------------------------------------------
void lg_text(GContext *ctx, const char *s, GFont font, GRect box, GTextAlignment a) {
  graphics_draw_text(ctx, s, font, box, GTextOverflowModeTrailingEllipsis, a, NULL);
}

void lg_panel(GContext *ctx, GRect r, GColor fill, GColor border) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, r, 8, GCornersAll);
  graphics_context_set_stroke_color(ctx, border);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, r, 8);
  graphics_context_set_stroke_width(ctx, 1);
}

GColor lg_gender_color(uint8_t gender, bool on_dark) {
  switch (gender) {
    case 1: return on_dark ? GColorVividCerulean : GColorDukeBlue;      // der
    case 2: return on_dark ? GColorBrilliantRose : GColorFolly;         // die
    case 3: return on_dark ? GColorBrightGreen   : GColorIslamicGreen;  // das
    default: return on_dark ? GColorWhite : GColorBlack;
  }
}

// --- The mascot: a little cat that reacts -----------------------------------
void lg_draw_cat(GContext *ctx, GPoint c, int r, int mood, int frame, GColor fur) {
  GColor pink = GColorBrilliantRose;
  bool blink = (mood == 0) && ((frame % 60) < 4);   // occasional idle blink
  int wag = ((frame / 8) % 2) ? 1 : -1;             // tiny ear twitch

  // ears (triangles), with pink inners
  graphics_context_set_fill_color(ctx, fur);
  for (int s = -1; s <= 1; s += 2) {
    int ex = c.x + s * (r - 2);
    GPoint a = GPoint(ex - 5, c.y - r + 3);
    GPoint b = GPoint(ex + 5, c.y - r + 3);
    GPoint t = GPoint(ex + s * wag, c.y - r - 6);
    graphics_fill_circle(ctx, a, 0);
    // draw a filled triangle via three edges + fill: approximate with lines
    for (int yy = 0; yy <= 9; yy++) {
      int xl = a.x + (t.x - a.x) * yy / 9;
      int xr = b.x + (t.x - b.x) * yy / 9;
      graphics_draw_line(ctx, GPoint(xl, a.y - yy), GPoint(xr, a.y - yy));
    }
  }
  graphics_context_set_fill_color(ctx, pink);
  for (int s = -1; s <= 1; s += 2) {
    int ex = c.x + s * (r - 2);
    graphics_fill_circle(ctx, GPoint(ex, c.y - r - 1), 2);
  }

  // head
  graphics_context_set_fill_color(ctx, fur);
  graphics_fill_circle(ctx, c, r);

  // eyes
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  int ex = r / 2, ey = -r / 6;
  if (blink || mood == 2) {                 // closed / sad: little arcs or lines
    for (int s = -1; s <= 1; s += 2) {
      GPoint e = GPoint(c.x + s * ex, c.y + ey);
      if (mood == 2)  // sad: downward arc
        graphics_draw_line(ctx, GPoint(e.x - 3, e.y + 2), GPoint(e.x + 3, e.y - 1));
      else            // blink: flat line
        graphics_draw_line(ctx, GPoint(e.x - 3, e.y), GPoint(e.x + 3, e.y));
    }
  } else if (mood == 1) {                    // happy: upward ^ ^ eyes
    for (int s = -1; s <= 1; s += 2) {
      GPoint e = GPoint(c.x + s * ex, c.y + ey + 1);
      graphics_draw_line(ctx, GPoint(e.x - 3, e.y + 2), GPoint(e.x, e.y - 2));
      graphics_draw_line(ctx, GPoint(e.x, e.y - 2), GPoint(e.x + 3, e.y + 2));
    }
  } else {                                   // neutral: round eyes
    for (int s = -1; s <= 1; s += 2)
      graphics_fill_circle(ctx, GPoint(c.x + s * ex, c.y + ey), 2);
  }
  graphics_context_set_stroke_width(ctx, 1);

  // blush on happy
  if (mood == 1) {
    graphics_context_set_fill_color(ctx, pink);
    for (int s = -1; s <= 1; s += 2)
      graphics_fill_circle(ctx, GPoint(c.x + s * (r - 2), c.y + r / 3), 2);
  }

  // nose + mouth
  graphics_context_set_fill_color(ctx, pink);
  GPoint nose = GPoint(c.x, c.y + r / 4);
  graphics_fill_circle(ctx, nose, 2);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  if (mood == 2) {
    graphics_draw_line(ctx, GPoint(nose.x - 3, nose.y + 5), GPoint(nose.x, nose.y + 3));
    graphics_draw_line(ctx, GPoint(nose.x, nose.y + 3), GPoint(nose.x + 3, nose.y + 5));
  } else {
    graphics_draw_line(ctx, GPoint(nose.x, nose.y), GPoint(nose.x, nose.y + 3));
    graphics_draw_line(ctx, GPoint(nose.x, nose.y + 3), GPoint(nose.x - 3, nose.y + 5));
    graphics_draw_line(ctx, GPoint(nose.x, nose.y + 3), GPoint(nose.x + 3, nose.y + 5));
  }

  // whiskers
  for (int s = -1; s <= 1; s += 2) {
    int wx = c.x + s * (r - 1);
    graphics_draw_line(ctx, GPoint(wx, c.y + r / 4), GPoint(wx + s * 8, c.y + r / 4 - 2));
    graphics_draw_line(ctx, GPoint(wx, c.y + r / 4 + 3), GPoint(wx + s * 8, c.y + r / 4 + 3));
  }
}

// --- Group icons (tiny vector glyphs) ---------------------------------------
void lg_draw_icon(GContext *ctx, IconId id, GRect box, GColor fg) {
  int cx = box.origin.x + box.size.w / 2;
  int cy = box.origin.y + box.size.h / 2;
  graphics_context_set_fill_color(ctx, fg);
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 2);
  switch (id) {
    case ICON_WAVE: {            // a waving hand: palm + fingers
      graphics_fill_rect(ctx, GRect(cx - 6, cy - 4, 12, 12), 3, GCornersAll);
      for (int i = 0; i < 4; i++)
        graphics_fill_rect(ctx, GRect(cx - 6 + i * 4, cy - 10, 3, 8), 1, GCornersAll);
      graphics_fill_circle(ctx, GPoint(cx - 8, cy + 2), 2);   // thumb
    } break;
    case ICON_NUM: {             // "123" blocks
      for (int i = 0; i < 3; i++)
        graphics_draw_round_rect(ctx, GRect(cx - 10 + i * 7, cy - 6, 5, 12), 1);
    } break;
    case ICON_PEOPLE: {          // two little heads + shoulders
      for (int s = -1; s <= 1; s += 2) {
        graphics_fill_circle(ctx, GPoint(cx + s * 5, cy - 4), 3);
        graphics_fill_rect(ctx, GRect(cx + s * 5 - 4, cy, 8, 7), 3, GCornersTop);
      }
    } break;
    case ICON_CUP: {             // a steaming cup
      graphics_fill_rect(ctx, GRect(cx - 7, cy - 2, 12, 9), 2, GCornersBottom);
      graphics_draw_arc(ctx, GRect(cx + 3, cy - 2, 8, 9), GOvalScaleModeFitCircle,
                        0, TRIG_MAX_ANGLE / 2);
      graphics_draw_line(ctx, GPoint(cx - 3, cy - 8), GPoint(cx - 3, cy - 5));
      graphics_draw_line(ctx, GPoint(cx + 1, cy - 8), GPoint(cx + 1, cy - 5));
    } break;
    case ICON_SUN: {             // sun with rays
      graphics_fill_circle(ctx, GPoint(cx, cy), 5);
      for (int a = 0; a < 360; a += 45) {
        int32_t t = DEG_TO_TRIGANGLE(a);
        int dx = sin_lookup(t) * 9 / TRIG_MAX_RATIO;
        int dy = -cos_lookup(t) * 9 / TRIG_MAX_RATIO;
        graphics_draw_line(ctx, GPoint(cx + dx, cy + dy), GPoint(cx + dx * 6 / 5, cy + dy * 6 / 5));
      }
    } break;
    case ICON_SPEECH: {          // a speech bubble with a heart
      graphics_fill_rect(ctx, GRect(cx - 9, cy - 7, 18, 12), 4, GCornersAll);
      graphics_fill_circle(ctx, GPoint(cx - 4, cy + 8), 2);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(cx - 2, cy - 2), 2);
      graphics_fill_circle(ctx, GPoint(cx + 2, cy - 2), 2);
      graphics_fill_rect(ctx, GRect(cx - 3, cy - 1, 6, 3), 0, GCornerNone);
    } break;
    case ICON_PALETTE: {         // an artist's palette with a paint blob
      graphics_fill_circle(ctx, GPoint(cx, cy), 9);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(cx + 4, cy + 3), 3);   // thumb hole
      graphics_context_set_fill_color(ctx, fg);
      for (int i = 0; i < 3; i++)
        graphics_fill_circle(ctx, GPoint(cx - 5 + i * 4, cy - 4), 1);
    } break;
    case ICON_PAW: {             // a paw print
      graphics_fill_circle(ctx, GPoint(cx, cy + 3), 5);
      for (int i = -1; i <= 1; i++)
        graphics_fill_circle(ctx, GPoint(cx + i * 5, cy - 4), 2);
      graphics_fill_circle(ctx, GPoint(cx + 8, cy - 1), 2);
      graphics_fill_circle(ctx, GPoint(cx - 8, cy - 1), 2);
    } break;
    case ICON_SHIRT: {           // a t-shirt
      graphics_fill_rect(ctx, GRect(cx - 7, cy - 4, 14, 13), 2, GCornersBottom);
      graphics_fill_rect(ctx, GRect(cx - 11, cy - 5, 5, 6), 1, GCornersAll);  // sleeves
      graphics_fill_rect(ctx, GRect(cx + 6, cy - 5, 5, 6), 1, GCornersAll);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(cx, cy - 5), 3);                       // collar
    } break;
    case ICON_HOUSE: {           // a little house
      graphics_fill_rect(ctx, GRect(cx - 7, cy - 1, 14, 10), 1, GCornersBottom);
      for (int i = 0; i <= 8; i++)                                           // roof
        graphics_draw_line(ctx, GPoint(cx - 9 + i, cy - 1 - i), GPoint(cx + 9 - i, cy - 1 - i));
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(cx - 2, cy + 3, 4, 6), 0, GCornerNone);   // door
    } break;
    case ICON_CLOCK: {           // a clock face
      graphics_draw_circle(ctx, GPoint(cx, cy), 9);
      graphics_draw_line(ctx, GPoint(cx, cy), GPoint(cx, cy - 6));
      graphics_draw_line(ctx, GPoint(cx, cy), GPoint(cx + 4, cy + 2));
    } break;
    case ICON_CLOUD: {           // a cloud
      graphics_fill_circle(ctx, GPoint(cx - 5, cy + 1), 5);
      graphics_fill_circle(ctx, GPoint(cx + 4, cy + 1), 5);
      graphics_fill_circle(ctx, GPoint(cx, cy - 3), 6);
      graphics_fill_rect(ctx, GRect(cx - 9, cy + 2, 18, 5), 2, GCornersBottom);
    } break;
    case ICON_PLANE: {           // a paper plane
      graphics_draw_line(ctx, GPoint(cx - 9, cy + 7), GPoint(cx + 9, cy - 8));
      graphics_draw_line(ctx, GPoint(cx + 9, cy - 8), GPoint(cx - 2, cy + 8));
      graphics_draw_line(ctx, GPoint(cx + 9, cy - 8), GPoint(cx - 9, cy + 7));
      graphics_draw_line(ctx, GPoint(cx - 2, cy + 8), GPoint(cx + 1, cy + 1));
    } break;
    case ICON_BOOK: {            // an open book
      graphics_draw_line(ctx, GPoint(cx, cy - 6), GPoint(cx, cy + 7));
      for (int i = 0; i < 5; i++) {
        graphics_draw_line(ctx, GPoint(cx - 1, cy - 6 + i * 3), GPoint(cx - 9, cy - 3 + i * 3));
        graphics_draw_line(ctx, GPoint(cx + 1, cy - 6 + i * 3), GPoint(cx + 9, cy - 3 + i * 3));
      }
    } break;
    case ICON_BAG: {             // a shopping bag
      graphics_fill_rect(ctx, GRect(cx - 7, cy - 3, 14, 12), 2, GCornersAll);
      graphics_context_set_stroke_color(ctx, fg);
      graphics_draw_arc(ctx, GRect(cx - 5, cy - 9, 10, 12), GOvalScaleModeFitCircle,
                        -TRIG_MAX_ANGLE / 4, TRIG_MAX_ANGLE / 4);             // handle
    } break;
    case ICON_FORK: {            // fork + knife
      graphics_draw_line(ctx, GPoint(cx - 5, cy - 8), GPoint(cx - 5, cy + 8));
      graphics_draw_line(ctx, GPoint(cx - 8, cy - 8), GPoint(cx - 8, cy - 3));
      graphics_draw_line(ctx, GPoint(cx - 2, cy - 8), GPoint(cx - 2, cy - 3));
      graphics_draw_line(ctx, GPoint(cx - 8, cy - 3), GPoint(cx - 2, cy - 3));
      graphics_fill_rect(ctx, GRect(cx + 4, cy - 8, 4, 8), 1, GCornersTop);   // knife
      graphics_draw_line(ctx, GPoint(cx + 6, cy), GPoint(cx + 6, cy + 8));
    } break;
    case ICON_PLUS: {            // a medical cross
      graphics_fill_rect(ctx, GRect(cx - 3, cy - 8, 6, 16), 1, GCornersAll);
      graphics_fill_rect(ctx, GRect(cx - 8, cy - 3, 16, 6), 1, GCornersAll);
    } break;
    case ICON_HEART: {           // a heart
      graphics_fill_circle(ctx, GPoint(cx - 4, cy - 3), 4);
      graphics_fill_circle(ctx, GPoint(cx + 4, cy - 3), 4);
      for (int i = 0; i <= 8; i++)
        graphics_draw_line(ctx, GPoint(cx - 8 + i, cy - 1 + i), GPoint(cx + 8 - i, cy - 1 + i));
    } break;
    case ICON_LEAF: {            // a leaf with a vein
      graphics_fill_radial(ctx, GRect(cx - 8, cy - 8, 16, 16), GOvalScaleModeFitCircle, 8,
                           DEG_TO_TRIGANGLE(135), DEG_TO_TRIGANGLE(315));
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_line(ctx, GPoint(cx - 5, cy + 5), GPoint(cx + 5, cy - 5));
    } break;
    case ICON_BALL: {            // a ball with seams
      graphics_draw_circle(ctx, GPoint(cx, cy), 9);
      graphics_fill_circle(ctx, GPoint(cx, cy), 2);
      for (int a = 0; a < 360; a += 72) {
        int32_t t = DEG_TO_TRIGANGLE(a);
        int dx = sin_lookup(t) * 9 / TRIG_MAX_RATIO, dy = -cos_lookup(t) * 9 / TRIG_MAX_RATIO;
        graphics_draw_line(ctx, GPoint(cx, cy), GPoint(cx + dx, cy + dy));
      }
    } break;
    case ICON_PHONE: {           // a smartphone
      graphics_draw_round_rect(ctx, GRect(cx - 6, cy - 9, 12, 18), 2);
      graphics_fill_rect(ctx, GRect(cx - 4, cy - 6, 8, 10), 0, GCornerNone);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(cx, cy + 6), 1);
    } break;
    case ICON_CAR: {             // a little car
      graphics_fill_rect(ctx, GRect(cx - 9, cy - 1, 18, 6), 2, GCornersBottom);
      graphics_fill_rect(ctx, GRect(cx - 5, cy - 6, 10, 6), 2, GCornersTop);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(cx - 5, cy + 5), 2);
      graphics_fill_circle(ctx, GPoint(cx + 5, cy + 5), 2);
    } break;
    case ICON_CALENDAR: {        // a calendar page
      graphics_fill_rect(ctx, GRect(cx - 8, cy - 6, 16, 14), 2, GCornersAll);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(cx - 8, cy - 6, 16, 4), 0, GCornerNone);   // header
      graphics_context_set_fill_color(ctx, fg);
      graphics_fill_rect(ctx, GRect(cx - 5, cy - 9, 2, 4), 0, GCornerNone);    // rings
      graphics_fill_rect(ctx, GRect(cx + 3, cy - 9, 2, 4), 0, GCornerNone);
    } break;
    case ICON_WARNING: {         // a warning triangle with !
      graphics_draw_line(ctx, GPoint(cx, cy - 9), GPoint(cx - 9, cy + 7));
      graphics_draw_line(ctx, GPoint(cx, cy - 9), GPoint(cx + 9, cy + 7));
      graphics_draw_line(ctx, GPoint(cx - 9, cy + 7), GPoint(cx + 9, cy + 7));
      graphics_context_set_fill_color(ctx, fg);
      graphics_fill_rect(ctx, GRect(cx - 1, cy - 4, 2, 6), 0, GCornerNone);   // ! stem
      graphics_fill_rect(ctx, GRect(cx - 1, cy + 4, 2, 2), 0, GCornerNone);   // ! dot
    } break;
    case ICON_GLOBE: {           // a globe: circle + meridians
      graphics_draw_circle(ctx, GPoint(cx, cy), 9);
      graphics_draw_line(ctx, GPoint(cx, cy - 9), GPoint(cx, cy + 9));
      graphics_draw_line(ctx, GPoint(cx - 9, cy), GPoint(cx + 9, cy));
      graphics_draw_arc(ctx, GRect(cx - 4, cy - 9, 8, 18), GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);
    } break;
    case ICON_BUILDING: {        // a tall building with windows
      graphics_fill_rect(ctx, GRect(cx - 7, cy - 9, 14, 18), 1, GCornersTop);
      graphics_context_set_fill_color(ctx, GColorBlack);
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 2; c++)
          graphics_fill_rect(ctx, GRect(cx - 4 + c * 6, cy - 6 + r * 5, 2, 3), 0, GCornerNone);
    } break;
    case ICON_RUN: {             // a running figure
      graphics_fill_circle(ctx, GPoint(cx + 2, cy - 6), 3);                 // head
      graphics_draw_line(ctx, GPoint(cx + 1, cy - 3), GPoint(cx - 2, cy + 2));
      graphics_draw_line(ctx, GPoint(cx - 2, cy + 2), GPoint(cx - 6, cy + 4));   // back arm
      graphics_draw_line(ctx, GPoint(cx - 2, cy + 2), GPoint(cx + 5, cy));       // front arm
      graphics_draw_line(ctx, GPoint(cx - 2, cy + 2), GPoint(cx + 4, cy + 8));   // front leg
      graphics_draw_line(ctx, GPoint(cx - 2, cy + 2), GPoint(cx - 6, cy + 8));   // back leg
    } break;
    case ICON_BOX: {             // a cardboard box
      graphics_draw_rect(ctx, GRect(cx - 8, cy - 4, 16, 12));
      graphics_draw_line(ctx, GPoint(cx - 8, cy - 4), GPoint(cx - 5, cy - 8));
      graphics_draw_line(ctx, GPoint(cx + 8, cy - 4), GPoint(cx + 5, cy - 8));
      graphics_draw_line(ctx, GPoint(cx - 5, cy - 8), GPoint(cx + 5, cy - 8));
      graphics_draw_line(ctx, GPoint(cx, cy - 8), GPoint(cx, cy + 8));        // tape seam
    } break;
  }
  graphics_context_set_stroke_width(ctx, 1);
}

// --- Stars ------------------------------------------------------------------
static GPath *s_star = NULL;
static const GPathInfo STAR_INFO = {
  .num_points = 10,
  .points = (GPoint[]) {
    {0, -8}, {2, -3}, {8, -2}, {3, 1}, {5, 6},
    {0, 3}, {-5, 6}, {-3, 1}, {-8, -2}, {-2, -3}
  }
};

void lg_draw_stars(GContext *ctx, GRect box, int filled, int total) {
  if (!s_star) s_star = gpath_create(&STAR_INFO);
  int gap = 22;
  int x0 = box.origin.x + box.size.w / 2 - (total - 1) * gap / 2;
  int y  = box.origin.y + box.size.h / 2;
  for (int i = 0; i < total; i++) {
    gpath_move_to(s_star, GPoint(x0 + i * gap, y));
    if (i < filled) {
      graphics_context_set_fill_color(ctx, GColorYellow);
      gpath_draw_filled(ctx, s_star);
      graphics_context_set_stroke_color(ctx, GColorWindsorTan);
      gpath_draw_outline(ctx, s_star);
    } else {
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      gpath_draw_outline(ctx, s_star);
    }
  }
}

// A small star, ~0.6× the size above, for inline deck ratings.
static GPath *s_mini = NULL;
static const GPathInfo MINI_STAR = {
  .num_points = 10,
  .points = (GPoint[]) {
    {0, -5}, {1, -2}, {5, -1}, {2, 1}, {3, 4},
    {0, 2}, {-3, 4}, {-2, 1}, {-5, -1}, {-1, -2}
  }
};

void lg_draw_rating(GContext *ctx, int x, int y, int filled, int total, GColor empty) {
  if (!s_mini) s_mini = gpath_create(&MINI_STAR);
  for (int i = 0; i < total; i++) {
    gpath_move_to(s_mini, GPoint(x + i * 11, y));
    if (i < filled) {
      graphics_context_set_fill_color(ctx, GColorYellow);
      gpath_draw_filled(ctx, s_mini);
      graphics_context_set_stroke_color(ctx, GColorBlack);   // outline reads on any bg
      gpath_draw_outline(ctx, s_mini);
    } else {
      graphics_context_set_stroke_color(ctx, empty);
      gpath_draw_outline(ctx, s_mini);
    }
  }
}
