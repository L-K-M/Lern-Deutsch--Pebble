#include "lg.h"

// ============================================================================
//  Lern Deutsch — two-level menu + stats
//
//  HOME lists a direction toggle, a stats entry, and the difficulty tiers.
//  Opening a tier lists its decks; opening a deck starts a study session.
//  UP/DOWN move, SELECT acts on the highlighted row, BACK steps back out
//  (and exits to the watchface from HOME).
// ============================================================================

#define BANNER_H   46
#define HEADER_H   30
#define ROW_H      50

typedef enum { VIEW_HOME, VIEW_DECKS, VIEW_STATS } View;

#define HOME_DIR   0
#define HOME_STATS 1
#define HOME_TIER0 2     // first tier row

static Window   *s_window;
static Layer    *s_layer;
static AppTimer *s_timer;
static HanFont   s_ui;

static View s_view   = VIEW_HOME;
static int  s_tier   = 0;
static int  s_sel    = HOME_TIER0;   // start on the first tier
static int  s_anim   = 0;
static int  s_scroll = 0;
static time_t s_pet_until = 0;       // while now < this, the stats cat is being petted
static int    s_pets = 0;            // consecutive pets; enough summons Elfie

static int home_count(void) { return HOME_TIER0 + g_tier_count; }
static int list_count(void) { return s_view == VIEW_DECKS ? g_tiers[s_tier].deck_count : home_count(); }
static int list_top(void)   { return (s_view == VIEW_DECKS ? HEADER_H : BANNER_H) + 4; }

// ---- progress ----
static bool deck_done(int gi) { return lg_best_stars(gi) > 0; }
static void tier_progress(int ti, int *done, int *total) {
  const Tier *t = &g_tiers[ti];
  *total = t->deck_count; *done = 0;
  for (int i = 0; i < t->deck_count; i++) if (deck_done(t->decks[i])) (*done)++;
}

// ---- little icons drawn inline ----
static void draw_swap(GContext *ctx, GRect box, GColor fg) {
  int cx = box.origin.x + box.size.w / 2, cy = box.origin.y + box.size.h / 2;
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(cx - 8, cy - 4), GPoint(cx + 8, cy - 4));
  graphics_draw_line(ctx, GPoint(cx + 8, cy + 4), GPoint(cx - 8, cy + 4));
  graphics_draw_line(ctx, GPoint(cx + 8, cy - 4), GPoint(cx + 4, cy - 7));
  graphics_draw_line(ctx, GPoint(cx + 8, cy - 4), GPoint(cx + 4, cy - 1));
  graphics_draw_line(ctx, GPoint(cx - 8, cy + 4), GPoint(cx - 4, cy + 1));
  graphics_draw_line(ctx, GPoint(cx - 8, cy + 4), GPoint(cx - 4, cy + 7));
  graphics_context_set_stroke_width(ctx, 1);
}

// A "level meter": `filled` of `total` ascending bars — the tier's difficulty.
static void draw_bars(GContext *ctx, GRect box, int filled, int total, GColor fg) {
  int bw = 4, gap = 2, tw = total * bw + (total - 1) * gap;
  int x0 = box.origin.x + (box.size.w - tw) / 2;
  int base = box.origin.y + box.size.h / 2 + 9;
  for (int i = 0; i < total; i++) {
    int h = 5 + i * 4;
    graphics_context_set_fill_color(ctx, i < filled ? fg : GColorDarkGray);
    graphics_fill_rect(ctx, GRect(x0 + i * (bw + gap), base - h, bw, h), 1, GCornersTop);
  }
}

static void draw_dir_chip(GContext *ctx, int x, int y, GColor ink) {
  Direction d = lg_dir_get();
  han_draw(ctx, &s_ui, d == DIR_DE_ZH ? UIZH_DE : UIZH_ZH, GPoint(x, y), ink);
  graphics_context_set_fill_color(ctx, ink);
  int ax = x + 36;
  for (int i = 0; i <= 5; i++)
    graphics_draw_line(ctx, GPoint(ax + i, y + 11 - (5 - i)), GPoint(ax + i, y + 21 + (5 - i)));
  han_draw(ctx, &s_ui, d == DIR_DE_ZH ? UIZH_ZH : UIZH_DE, GPoint(x + 52, y), ink);
}

// ---- shared row chrome: highlight + return where the icon and text go ----
static void row_chrome(GContext *ctx, int y, int w, bool sel, GColor accent,
                       GRect *tile, int *textx) {
  GRect row = GRect(4, y + 3, w - 8, ROW_H - 6);
  if (sel) {
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, row, 8, GCornersAll);
  }
  *tile = GRect(row.origin.x + 4, y + (ROW_H - 30) / 2, 30, 30);
  *textx = tile->origin.x + 38;
}

static void subtitle(GContext *ctx, int x, int y, int w, const char *de, const char *en, bool sel) {
  char sub[44];
  snprintf(sub, sizeof(sub), "%s · %s", de, en);
  graphics_context_set_text_color(ctx, sel ? GColorBlack : GColorLightGray);
  lg_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(x, y, w - x - 6, 16), GTextAlignmentLeft);
}

// ---- HOME rows ----
static void draw_home_row(GContext *ctx, int i, int y, int w, bool sel) {
  GColor accent = (i == HOME_DIR) ? GColorPictonBlue
                : (i == HOME_STATS) ? GColorPurple
                : g_tiers[i - HOME_TIER0].accent;
  GColor ink = sel ? GColorBlack : GColorWhite;
  GRect tile; int textx;
  row_chrome(ctx, y, w, sel, accent, &tile, &textx);

  if (i == HOME_DIR) {
    draw_swap(ctx, tile, sel ? GColorBlack : GColorPictonBlue);
    draw_dir_chip(ctx, textx, y + 4, ink);
    subtitle(ctx, textx, y + 32, w, "Richtung", "Direction", sel);
  } else if (i == HOME_STATS) {
    draw_bars(ctx, tile, 3, 3, sel ? GColorBlack : GColorPurple);
    han_draw(ctx, &s_ui, UIZH_STATS, GPoint(textx, y + 4), ink);     // 统计
    subtitle(ctx, textx, y + 32, w, "Statistik", "Stats", sel);
  } else {
    int ti = i - HOME_TIER0;
    const Tier *t = &g_tiers[ti];
    draw_bars(ctx, tile, ti + 1, g_tier_count, sel ? GColorBlack : t->accent);
    han_draw(ctx, &s_ui, t->zh, GPoint(textx, y + 4), ink);          // tier name
    subtitle(ctx, textx, y + 32, w, t->de, t->en, sel);
    int done, total; tier_progress(ti, &done, &total);
    char prog[16]; snprintf(prog, sizeof(prog), "%d/%d", done, total);
    graphics_context_set_text_color(ctx, sel ? GColorBlack : GColorYellow);
    lg_text(ctx, prog, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(w - 50, y + 12, 44, 22), GTextAlignmentRight);
  }
}

// ---- DECK rows ----
static void draw_deck_row(GContext *ctx, int i, int y, int w, bool sel) {
  int gi = g_tiers[s_tier].decks[i];
  const Group *g = &g_groups[gi];
  GColor ink = sel ? GColorBlack : GColorWhite;
  GRect tile; int textx;
  row_chrome(ctx, y, w, sel, g->accent, &tile, &textx);

  lg_draw_icon(ctx, g->icon, tile, sel ? GColorBlack : g->accent);
  han_draw(ctx, &s_ui, g->zh, GPoint(textx, y + 4), ink);
  subtitle(ctx, textx, y + 32, w, g->de, g->en, sel);

  // best-score rating: ★★☆ — shows at a glance if the deck is done and how well
  lg_draw_rating(ctx, w - 41, y + 14, lg_best_stars(gi), 3, sel ? GColorBlack : GColorDarkGray);
}

// ---- HOME banner / DECK header ----
static void draw_banner(GContext *ctx, int w) {
  graphics_context_set_fill_color(ctx, GColorIndigo);
  graphics_fill_rect(ctx, GRect(0, 0, w, BANNER_H), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorPurple);
  graphics_fill_rect(ctx, GRect(0, BANNER_H - 4, w, 4), 0, GCornerNone);
  lg_draw_cat(ctx, GPoint(24, BANNER_H / 2 + 2), 15, (s_anim % 70 < 8) ? 1 : 0, s_anim, GColorWhite);
  han_draw(ctx, &s_ui, UIZH_TITLE, GPoint(50, 7), GColorWhite);
}

static void draw_header(GContext *ctx, int w) {
  const Tier *t = &g_tiers[s_tier];
  graphics_context_set_fill_color(ctx, t->accent);
  graphics_fill_rect(ctx, GRect(0, 0, w, HEADER_H), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorBlack);            // ◀ back chevron (points left)
  for (int i = 0; i <= 5; i++)
    graphics_draw_line(ctx, GPoint(8 + i, HEADER_H / 2 - i), GPoint(8 + i, HEADER_H / 2 + i));
  graphics_context_set_text_color(ctx, GColorBlack);
  char title[40]; snprintf(title, sizeof(title), "%s · %s", t->de, t->en);
  lg_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
          GRect(20, 4, w - 26, 22), GTextAlignmentLeft);
}

// ---- STATS screen ----
static void stat_line(GContext *ctx, int y, int w, const char *label, const char *value, GColor vc) {
  graphics_context_set_text_color(ctx, GColorLightGray);
  lg_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(14, y, w - 90, 24), GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, vc);
  lg_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(w - 86, y - 3, 72, 26), GTextAlignmentRight);
}

// A tiny heart for the petting ring.
static void draw_mini_heart(GContext *ctx, GPoint c, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(c.x - 2, c.y - 1), 2);
  graphics_fill_circle(ctx, GPoint(c.x + 2, c.y - 1), 2);
  graphics_context_set_stroke_color(ctx, col);
  for (int i = 0; i <= 3; i++)
    graphics_draw_line(ctx, GPoint(c.x - 4 + i, c.y + i), GPoint(c.x + 4 - i, c.y + i));
}

static void draw_stats(GContext *ctx, GRect b) {
  int learned = lg_learned_total();
  int done = 0, total = g_group_count, stars = 0, smax = g_group_count * 3;
  for (int i = 0; i < g_group_count; i++) { int s = lg_best_stars(i); stars += s; if (s > 0) done++; }

  // pet the mascot three times in a row and Elfie — a real cat — pads in to
  // claim the rest of the pets; she leaves again when the petting stops
  bool petting = time(NULL) < s_pet_until;
  if (petting && s_pets >= 3)
    lg_draw_elfie(ctx, GPoint(b.size.w / 2, 34), 17, 1, s_anim, GColorPurple);
  else
    lg_draw_cat(ctx, GPoint(b.size.w / 2, 34), 17, 1, s_anim, GColorChromeYellow);
  if (petting) {                        // being petted: a slow orbit of hearts
    for (int a = 0; a < 360; a += 60) {
      int32_t t = DEG_TO_TRIGANGLE((a + s_anim * 4) % 360);
      int hx = b.size.w / 2 + sin_lookup(t) * 32 / TRIG_MAX_RATIO;
      int hy = 34 - cos_lookup(t) * 24 / TRIG_MAX_RATIO;
      draw_mini_heart(ctx, GPoint(hx, hy), GColorBrilliantRose);
    }
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  lg_text(ctx, "Statistik", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
          GRect(0, 56, b.size.w, 26), GTextAlignmentCenter);
  han_draw_box(ctx, &s_ui, UIZH_STATS, GRect(0, 84, b.size.w, 32), GColorChromeYellow);

  char v[16];
  snprintf(v, sizeof(v), "%d", learned);
  stat_line(ctx, 124, b.size.w, "Gelernt", v, GColorMintGreen);
  snprintf(v, sizeof(v), "%d/%d", done, total);
  stat_line(ctx, 152, b.size.w, "Decks", v, GColorPictonBlue);
  // deck progress bar
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(14, 178, b.size.w - 28, 6), 2, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  graphics_fill_rect(ctx, GRect(14, 178, (b.size.w - 28) * done / (total ? total : 1), 6), 2, GCornersAll);
  snprintf(v, sizeof(v), "%d/%d", stars, smax);
  stat_line(ctx, 190, b.size.w, "Sterne", v, GColorYellow);
  lg_draw_stars(ctx, GRect(64, 192, 22, 20), 1, 1);     // one gold star as a cue

  int by = b.size.h - 10;
  graphics_context_set_fill_color(ctx, GColorLightGray);
  for (int i = 0; i <= 4; i++)                              // ◀ vector chevron (points left)
    graphics_draw_line(ctx, GPoint(10 + i, by - i), GPoint(10 + i, by + i));
  graphics_context_set_text_color(ctx, GColorLightGray);
  lg_text(ctx, "BACK  Menu", fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(20, b.size.h - 18, 110, 16), GTextAlignmentLeft);
}

// ---- the layer update proc ----
static void menu_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, LG_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (s_view == VIEW_STATS) { draw_stats(ctx, b); return; }

  int top = list_top();
  for (int i = 0; i < list_count(); i++) {
    int y = top + i * ROW_H - s_scroll;
    if (y + ROW_H < top || y > b.size.h) continue;
    if (s_view == VIEW_DECKS) draw_deck_row(ctx, i, y, b.size.w, i == s_sel);
    else                      draw_home_row(ctx, i, y, b.size.w, i == s_sel);
  }
  if (s_view == VIEW_DECKS) draw_header(ctx, b.size.w);
  else                      draw_banner(ctx, b.size.w);
}

// ---- animation / smooth scroll ----
static void tick(void *data) {
  s_anim++;
  if (s_view != VIEW_STATS) {
    GRect b = layer_get_bounds(s_layer);
    int top = list_top(), list_h = b.size.h - top;
    int target = s_sel * ROW_H - (list_h - ROW_H) / 2;
    int max_scroll = list_count() * ROW_H - list_h;
    if (max_scroll < 0) max_scroll = 0;
    if (target < 0) target = 0;
    if (target > max_scroll) target = max_scroll;
    int diff = target - s_scroll;
    if (diff > -3 && diff < 3) s_scroll = target; else s_scroll += diff / 3;
  }
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(60, tick, NULL);
}

// ---- input ----
static void up_click(ClickRecognizerRef r, void *c) {
  if (s_view == VIEW_STATS) return;
  int n = list_count();
  s_sel = (s_sel - 1 + n) % n;
  layer_mark_dirty(s_layer);
}
static void down_click(ClickRecognizerRef r, void *c) {
  if (s_view == VIEW_STATS) return;
  int n = list_count();
  s_sel = (s_sel + 1) % n;
  layer_mark_dirty(s_layer);
}
static void select_click(ClickRecognizerRef r, void *c) {
  if (s_view == VIEW_HOME) {
    if (s_sel == HOME_DIR) {
      lg_dir_set(lg_dir_get() == DIR_DE_ZH ? DIR_ZH_DE : DIR_DE_ZH);
      vibes_short_pulse();
    } else if (s_sel == HOME_STATS) {
      s_view = VIEW_STATS;
    } else {
      s_tier = s_sel - HOME_TIER0; s_view = VIEW_DECKS; s_sel = 0; s_scroll = 0;
    }
  } else if (s_view == VIEW_DECKS) {
    study_push(g_tiers[s_tier].decks[s_sel]);
  } else {                       // STATS: SELECT pets the cat (BACK still exits)
    s_pets = (time(NULL) < s_pet_until) ? s_pets + 1 : 1;   // keep petting...
    s_pet_until = time(NULL) + 2;
    static const uint32_t purr[] = { 30, 60, 30, 60, 30 };
    vibes_enqueue_custom_pattern((VibePattern){ .durations = purr, .num_segments = 5 });
  }
  layer_mark_dirty(s_layer);
}
static void back_click(ClickRecognizerRef r, void *c) {
  if (s_view == VIEW_DECKS) { s_view = VIEW_HOME; s_sel = HOME_TIER0 + s_tier; s_scroll = 0; }
  else if (s_view == VIEW_STATS) { s_view = VIEW_HOME; s_sel = HOME_STATS; s_scroll = 0; }
  else { window_stack_pop(true); }    // HOME → exit to watchface
  layer_mark_dirty(s_layer);
}
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ---- window ----
static void win_load(Window *window) {
  s_layer = window_get_root_layer(window);
  layer_set_update_proc(s_layer, menu_update);
  han_load(&s_ui, &ATLAS_UI);
}
static void win_appear(Window *window) {
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(60, tick, NULL);
}
static void win_disappear(Window *window) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}
static void win_unload(Window *window) {
  han_unload(&s_ui);
}

static void init(void) {
  srand((unsigned)time(NULL));
  s_window = window_create();
  window_set_background_color(s_window, LG_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = win_load, .appear = win_appear, .disappear = win_disappear, .unload = win_unload,
  });
  window_set_click_config_provider(s_window, click_config);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
