#include "lg.h"
#include <string.h>

// ============================================================================
//  study.c — a flashcard session for one difficulty group.
//
//  Loop: the deck is shuffled into a queue. Each card is shown front-side; a
//  press flips it; you self-grade ✓/✗. Correct cards leave the queue; missed
//  ones go to the back to come round again. The round ends when every card has
//  been mastered once. Stars are awarded on first-try accuracy.
// ============================================================================

#define MAXQ      256
#define FLIP_N    8      // frames in the flip animation
#define FB_FRAMES 26     // frames the correct/wrong feedback lingers (~1.2s)
#define TICK_MS   45
#define STRIP     52     // width of the right-edge button-hint rail

typedef enum { ST_FRONT, ST_BACK, ST_FEEDBACK, ST_SUMMARY } Phase;

static Window   *s_window;
static Layer    *s_layer;
static AppTimer *s_timer;

static const Group *s_g;
static int          s_group;
static Direction    s_dir;

static HanFont s_ui;     // praise / nudge / flip glyphs
static HanFont s_grp;    // this group's answer glyphs

#define MAXCARDS  64     // largest a single deck may grow to
static int   s_queue[MAXQ];
static int   s_qhead, s_qtail;
static int   s_attempts[MAXCARDS];
static int   s_total, s_mastered, s_first_correct;
static int   s_cur;

static Phase s_phase;
static int   s_anim;
static int   s_flip;             // 0 = not flipping, else 1..FLIP_N
static bool  s_show_back_face;   // which face the flip currently reveals
static int   s_fb_kind;          // 1 correct, 2 wrong
static int   s_fb_frame;
static int   s_msg_i;            // chosen praise/nudge index

// ---- queue helpers ---------------------------------------------------------
static int  q_size(void) { return s_qtail - s_qhead; }
static void q_push(int v) { s_queue[s_qtail % MAXQ] = v; s_qtail++; }
static int  q_front(void) { return s_queue[s_qhead % MAXQ]; }
static void q_pop(void)   { s_qhead++; }

// ---- session lifecycle -----------------------------------------------------
static void start_session(void) {
  s_total = s_g->card_count;
  if (s_total > MAXCARDS) s_total = MAXCARDS;
  int order[MAXCARDS];
  for (int i = 0; i < s_total; i++) order[i] = i;
  for (int i = s_total - 1; i > 0; i--) {        // Fisher–Yates shuffle
    int j = rand() % (i + 1);
    int t = order[i]; order[i] = order[j]; order[j] = t;
  }
  s_qhead = s_qtail = 0;
  for (int i = 0; i < s_total; i++) { q_push(order[i]); s_attempts[i] = 0; }
  s_mastered = 0; s_first_correct = 0;
  s_cur = q_front();
  s_phase = ST_FRONT; s_flip = 0; s_show_back_face = false;
}

static void grade(bool correct) {
  s_attempts[s_cur]++;
  if (correct) {
    if (s_attempts[s_cur] == 1) s_first_correct++;
    q_pop();
    s_mastered++;
    s_fb_kind = 1;
    s_msg_i = rand() % UIZH_PRAISE_N;
    vibes_short_pulse();
  } else {
    q_pop();
    q_push(s_cur);            // see it again later
    s_fb_kind = 2;
    s_msg_i = rand() % UIZH_NUDGE_N;
  }
  s_phase = ST_FEEDBACK; s_fb_frame = 0;
}

static int compute_stars(void) {
  if (s_total == 0) return 3;
  int pct = s_first_correct * 100 / s_total;
  if (pct >= 90) return 3;
  if (pct >= 60) return 2;
  return 1;                    // finishing always earns at least one
}

static void advance(void) {
  if (q_size() == 0) {
    int stars = compute_stars();
    lg_best_submit(s_group, stars);
    lg_learned_add(s_total);
    s_phase = ST_SUMMARY;
    vibes_double_pulse();
  } else {
    s_cur = q_front();
    s_phase = ST_FRONT; s_flip = 0; s_show_back_face = false;
  }
}

// ---- small vector marks ----------------------------------------------------
static void draw_check(GContext *ctx, GPoint p, GColor c) {
  graphics_context_set_stroke_color(ctx, c);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(p.x - 5, p.y), GPoint(p.x - 1, p.y + 4));
  graphics_draw_line(ctx, GPoint(p.x - 1, p.y + 4), GPoint(p.x + 6, p.y - 5));
  graphics_context_set_stroke_width(ctx, 1);
}
static void draw_cross(GContext *ctx, GPoint p, GColor c) {
  graphics_context_set_stroke_color(ctx, c);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, GPoint(p.x - 5, p.y - 5), GPoint(p.x + 5, p.y + 5));
  graphics_draw_line(ctx, GPoint(p.x + 5, p.y - 5), GPoint(p.x - 5, p.y + 5));
  graphics_context_set_stroke_width(ctx, 1);
}
static void draw_flip(GContext *ctx, GPoint p, GColor c) {  // ⇄ "turn the card over"
  graphics_context_set_stroke_color(ctx, c);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(p.x - 5, p.y - 3), GPoint(p.x + 5, p.y - 3));
  graphics_draw_line(ctx, GPoint(p.x + 5, p.y - 3), GPoint(p.x + 2, p.y - 6));
  graphics_draw_line(ctx, GPoint(p.x + 5, p.y - 3), GPoint(p.x + 2, p.y));
  graphics_draw_line(ctx, GPoint(p.x + 5, p.y + 3), GPoint(p.x - 5, p.y + 3));
  graphics_draw_line(ctx, GPoint(p.x - 5, p.y + 3), GPoint(p.x - 2, p.y));
  graphics_draw_line(ctx, GPoint(p.x - 5, p.y + 3), GPoint(p.x - 2, p.y + 6));
  graphics_context_set_stroke_width(ctx, 1);
}

// A button hint in the right-edge rail, centred at the button's height `cy`.
// The arrow sits flush to the screen edge, pointing at the physical button.
// kind: 0 = flip (SELECT), 1 = correct (UP), 2 = missed (DOWN).
static void draw_btn_hint(GContext *ctx, GRect b, int cy, const char *word, GColor col, int kind) {
  int rx = b.size.w - STRIP;              // left edge of the rail
  int ex = b.size.w - 3;                  // screen edge (arrow tip)
  graphics_context_set_fill_color(ctx, col);
  for (int i = 0; i <= 6; i++)            // ▶ pointing at the button
    graphics_draw_line(ctx, GPoint(ex - 6 + i, cy - (6 - i)), GPoint(ex - 6 + i, cy + (6 - i)));

  GPoint ic = GPoint(rx + 13, cy);
  if (kind == 1)      draw_check(ctx, ic, col);
  else if (kind == 2) draw_cross(ctx, ic, col);
  else                draw_flip(ctx, ic, col);

  graphics_context_set_text_color(ctx, col);
  lg_text(ctx, word, fonts_get_system_font(FONT_KEY_GOTHIC_14),
          GRect(rx, cy + 9, STRIP - 2, 16), GTextAlignmentCenter);
}

// ---- German with a colour-coded article chip -------------------------------
// Width of the single widest word in `s` at font `f`, measured unwrapped.
static int widest_word_px(const char *s, GFont f) {
  char buf[48];
  int best = 0, n = 0;
  for (const char *p = s; ; p++) {
    if (*p == ' ' || *p == '\0') {
      if (n > 0) {
        buf[n] = '\0';
        GSize sz = graphics_text_layout_get_content_size(
            buf, f, GRect(0, 0, 2000, 60), GTextOverflowModeWordWrap, GTextAlignmentLeft);
        if (sz.w > best) best = sz.w;
        n = 0;
      }
      if (*p == '\0') break;
    } else if (n < (int)sizeof(buf) - 1) {
      buf[n++] = *p;
    }
  }
  return best;
}

// The largest system font at which `s` fits `box` AND no single word has to be
// hyphen-broken mid-word (it may still wrap at spaces). Falls back to the
// smallest font, which fits every word in our vocabulary on one line.
static GFont fit_font(const char *s, GRect box) {
  static const char *const keys[] = {
    FONT_KEY_BITHAM_30_BLACK, FONT_KEY_GOTHIC_28_BOLD,
    FONT_KEY_GOTHIC_24_BOLD,  FONT_KEY_GOTHIC_18_BOLD,
  };
  for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    GFont f = fonts_get_system_font(keys[i]);
    if (widest_word_px(s, f) > box.size.w) continue;     // a word would break — too big
    GSize sz = graphics_text_layout_get_content_size(
        s, f, GRect(0, 0, box.size.w, 1000), GTextOverflowModeWordWrap, GTextAlignmentCenter);
    if (sz.h <= box.size.h) return f;
  }
  return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
}

// Draw `s` word-wrapped and vertically centred inside `box`.
static void text_block(GContext *ctx, const char *s, GFont f, GRect box, GColor col) {
  GSize sz = graphics_text_layout_get_content_size(s, f, box, GTextOverflowModeWordWrap, GTextAlignmentCenter);
  int y = box.origin.y + (box.size.h - sz.h) / 2;
  if (y < box.origin.y) y = box.origin.y;
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, f, GRect(box.origin.x, y, box.size.w, box.size.h - (y - box.origin.y)),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// Draw the German side: article (if any) as a coloured chip, then the word.
static void draw_german(GContext *ctx, const Card *c, GRect box, bool on_dark) {
  char art[8] = "", noun[40];
  if (c->gender) {
    const char *sp = strchr(c->de, ' ');
    if (sp) {
      int al = sp - c->de; if (al > 7) al = 7;
      memcpy(art, c->de, al); art[al] = 0;
      strncpy(noun, sp + 1, sizeof(noun) - 1); noun[sizeof(noun) - 1] = 0;
    } else { strncpy(noun, c->de, sizeof(noun) - 1); noun[sizeof(noun) - 1] = 0; }
  } else {
    strncpy(noun, c->de, sizeof(noun) - 1); noun[sizeof(noun) - 1] = 0;
  }

  GRect word_box = box;
  if (art[0]) {                                // article chip across the top
    GFont cf = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    GSize cs = graphics_text_layout_get_content_size(art, cf, box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
    int cw = cs.w + 14, ch = 22;
    GRect chip = GRect(box.origin.x + (box.size.w - cw) / 2, box.origin.y + 4, cw, ch);
    graphics_context_set_fill_color(ctx, lg_gender_color(c->gender, on_dark));
    graphics_fill_rect(ctx, chip, 6, GCornersAll);
    graphics_context_set_text_color(ctx, on_dark ? GColorBlack : GColorWhite);
    lg_text(ctx, art, cf, GRect(chip.origin.x, chip.origin.y + 1, chip.size.w, ch), GTextAlignmentCenter);
    word_box = GRect(box.origin.x, box.origin.y + ch + 4, box.size.w, box.size.h - ch - 4);
  }
  text_block(ctx, noun, fit_font(noun, word_box), word_box, on_dark ? GColorWhite : GColorBlack);
}

// One face of the card. `german` selects language; `box` is the content area.
static void draw_face(GContext *ctx, const Card *c, bool german, GRect box, bool on_dark) {
  if (german) draw_german(ctx, c, box, on_dark);
  else        han_draw_box(ctx, &s_grp, c->zh, box, GColorWhite);
}

// ---- the HUD ---------------------------------------------------------------
static int draw_hud(GContext *ctx, GRect b) {
  const int h = 24;
  graphics_context_set_fill_color(ctx, s_g->accent);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, h), 0, GCornerNone);

  char left[16];
  snprintf(left, sizeof(left), "%d/%d", s_mastered, s_total);
  graphics_context_set_text_color(ctx, GColorBlack);
  lg_text(ctx, left, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
          GRect(6, 1, 70, 22), GTextAlignmentLeft);
  lg_text(ctx, s_g->de, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
          GRect(b.size.w / 2, 3, b.size.w / 2 - 6, 20), GTextAlignmentRight);

  // progress bar
  int y = h, bw = b.size.w;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(0, y, bw, 4), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_rect(ctx, GRect(0, y, bw * s_mastered / (s_total ? s_total : 1), 4), 0, GCornerNone);
  return h + 4;
}

// ---- confetti / sparkles ---------------------------------------------------
static void sparkles(GContext *ctx, GRect b, int frame) {
  GColor cols[] = { GColorYellow, GColorBrilliantRose, GColorCyan, GColorMintGreen };
  for (int i = 0; i < 14; i++) {
    int x = (i * 53 + 17) % b.size.w;
    int y = (i * 29 + frame * 6) % (b.size.h + 20) - 10;
    graphics_context_set_fill_color(ctx, cols[i % 4]);
    graphics_fill_rect(ctx, GRect(x, y, 3, 3), 1, GCornersAll);
  }
}

// ---- render ----------------------------------------------------------------
static void render(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, LG_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (s_phase == ST_SUMMARY) {
    if (s_fb_kind == 1) sparkles(ctx, b, s_anim);
    int stars = compute_stars();
    lg_draw_cat(ctx, GPoint(b.size.w / 2, 52), 22, 1, s_anim, s_g->accent);

    graphics_context_set_text_color(ctx, GColorWhite);
    lg_text(ctx, "Fertig!", fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK),
            GRect(0, 80, b.size.w, 34), GTextAlignmentCenter);
    han_draw_box(ctx, &s_ui, UIZH_DONE, GRect(0, 112, b.size.w, 32), s_g->accent);

    lg_draw_stars(ctx, GRect(0, 146, b.size.w, 24), stars, 3);

    char line[28];
    snprintf(line, sizeof(line), "%d/%d beim 1. Mal", s_first_correct, s_total);
    graphics_context_set_text_color(ctx, GColorLightGray);
    lg_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(0, 170, b.size.w, 20), GTextAlignmentCenter);

    // hints: BACK is on the left, SELECT on the right — point at each
    int hy = b.size.h - 20;
    GFont hf = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_context_set_text_color(ctx, GColorWhite);
    for (int i = 0; i <= 4; i++)                                  // ◀ BACK
      graphics_draw_line(ctx, GPoint(8 + (4 - i), hy + 9 - i), GPoint(8 + (4 - i), hy + 9 + i));
    lg_text(ctx, "Menu", hf, GRect(16, hy, 70, 18), GTextAlignmentLeft);
    lg_text(ctx, "nochmal", hf, GRect(b.size.w - 86, hy, 64, 18), GTextAlignmentRight);
    for (int i = 0; i <= 4; i++)                                  // SELECT ▶
      graphics_draw_line(ctx, GPoint(b.size.w - 13 + i, hy + 9 - (4 - i)), GPoint(b.size.w - 13 + i, hy + 9 + (4 - i)));
    return;
  }

  int top = draw_hud(ctx, b);
  GRect panel = GRect(8, top + 4, b.size.w - 10 - STRIP, b.size.h - top - 12);

  // which face / language?
  bool de_front = (s_dir == DIR_DE_ZH);
  bool showing_back = (s_phase == ST_BACK || s_phase == ST_FEEDBACK)
                      || (s_flip && s_show_back_face);
  bool german_face = de_front ? !showing_back : showing_back;

  // flip squash
  GRect p = panel;
  if (s_flip) {
    int half = FLIP_N / 2;
    int d = s_flip <= half ? (half - s_flip) : (s_flip - half);
    int factor = (d * 100) / half;                       // 100 at ends → 0 mid
    int nw = panel.size.w * factor / 100;
    p = GRect(panel.origin.x + (panel.size.w - nw) / 2, panel.origin.y, nw, panel.size.h);
  }

  // German faces are a white card (dark ink); Chinese faces are a dark card
  // (white ink) — guaranteed contrast, and a satisfying flip between the two.
  GColor panel_fill = german_face ? GColorWhite : GColorBlack;
  bool on_dark = !german_face;
  lg_panel(ctx, p, panel_fill, s_g->accent);

  // Right-edge button rail: a hint at each button's height with an arrow that
  // points at the physical button. SELECT (flip) up front; ✓/✗ on the answer.
  int up_y = b.size.h * 27 / 100, sel_y = b.size.h / 2, down_y = b.size.h * 73 / 100;
  if (!showing_back) {
    draw_btn_hint(ctx, b, sel_y, "Antwort", GColorWhite, 0);     // SELECT → reveal
  } else {
    draw_btn_hint(ctx, b, up_y,   "Richtig", GColorGreen, 1);    // UP → got it
    draw_btn_hint(ctx, b, down_y, "Falsch",  GColorRed,   2);    // DOWN → missed
  }

  // don't draw the card's text while it's edge-on mid-flip
  if (s_flip && p.size.w < panel.size.w * 6 / 10) return;

  GRect content = GRect(p.origin.x + 7, p.origin.y + 5, p.size.w - 14, p.size.h - 10);
  const Card *c = &s_g->cards[s_cur];

  if (!showing_back) {
    draw_face(ctx, c, german_face, content, on_dark);            // front: the prompt
  } else {
    // back: the big answer + an English gloss. For a Chinese answer we also show
    // the German prompt as a small reminder; a Chinese prompt is skipped (our
    // Chinese is a fixed size and would crowd the answer).
    int gh = 20;
    GRect ans = GRect(content.origin.x, content.origin.y, content.size.w, content.size.h - gh);
    if (!german_face) {                                          // Chinese answer
      int qh = 26;
      graphics_context_set_text_color(ctx, GColorLightGray);
      lg_text(ctx, c->de, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
              GRect(content.origin.x, content.origin.y, content.size.w, qh), GTextAlignmentCenter);
      ans.origin.y += qh; ans.size.h -= qh;
    }
    draw_face(ctx, c, german_face, ans, on_dark);

    graphics_context_set_text_color(ctx, on_dark ? GColorLightGray : GColorDarkGray);
    lg_text(ctx, c->en, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(content.origin.x, content.origin.y + content.size.h - gh, content.size.w, 18),
            GTextAlignmentCenter);
  }

  // feedback overlay: the cat + a praise/nudge word
  if (s_phase == ST_FEEDBACK) {
    bool ok = (s_fb_kind == 1);
    if (ok) sparkles(ctx, b, s_anim);
    int cx = b.size.w / 2, cy = b.size.h / 2;
    GRect chip = GRect(cx - 76, cy - 34, 152, 70);
    lg_panel(ctx, chip, GColorBlack, ok ? GColorIslamicGreen : GColorFolly);
    lg_draw_cat(ctx, GPoint(chip.origin.x + 28, cy), 18, ok ? 1 : 2, s_anim, s_g->accent);
    const char *msg = ok ? UIZH_PRAISE[s_msg_i] : UIZH_NUDGE[s_msg_i];
    han_draw_box(ctx, &s_ui, msg, GRect(chip.origin.x + 52, cy - 22, 96, 44),
                 ok ? GColorMintGreen : GColorMelon);
  }
}

// ---- ticking ---------------------------------------------------------------
static void tick(void *data) {
  s_anim++;
  if (s_flip) {
    s_flip++;
    if (s_flip > FLIP_N / 2) s_show_back_face = true;   // past the edge → answer side
    if (s_flip > FLIP_N) { s_flip = 0; s_phase = ST_BACK; }
  }
  if (s_phase == ST_FEEDBACK && ++s_fb_frame >= FB_FRAMES) advance();
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(TICK_MS, tick, NULL);
}

// ---- input -----------------------------------------------------------------
static void sel_click(ClickRecognizerRef r, void *ctx) {
  switch (s_phase) {
    case ST_FRONT:  if (!s_flip) { s_flip = 1; } break;     // flip to answer
    case ST_BACK:   s_phase = ST_FRONT; s_flip = 0; s_show_back_face = false; break; // peek back
    case ST_FEEDBACK: advance(); break;
    case ST_SUMMARY:  start_session(); break;
  }
  layer_mark_dirty(s_layer);
}
static void up_click(ClickRecognizerRef r, void *ctx) {
  if (s_phase == ST_BACK) grade(true);
  else if (s_phase == ST_FEEDBACK) advance();
  layer_mark_dirty(s_layer);
}
static void down_click(ClickRecognizerRef r, void *ctx) {
  if (s_phase == ST_BACK) grade(false);
  else if (s_phase == ST_FEEDBACK) advance();
  layer_mark_dirty(s_layer);
}
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, sel_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
}

// ---- window ----------------------------------------------------------------
static void win_load(Window *window) {
  s_layer = window_get_root_layer(window);
  layer_set_update_proc(s_layer, render);
  han_load(&s_ui, &ATLAS_UI);
  han_load(&s_grp, s_g->atlas);
  start_session();
}
static void win_appear(Window *window) {
  s_timer = app_timer_register(TICK_MS, tick, NULL);
}
static void win_disappear(Window *window) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}
static void win_unload(Window *window) {
  han_unload(&s_ui);
  han_unload(&s_grp);
  window_destroy(s_window);
  s_window = NULL;
}

void study_push(int group_index) {
  s_group = group_index;
  s_g = &g_groups[group_index];
  s_dir = lg_dir_get();
  s_window = window_create();
  window_set_background_color(s_window, LG_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = win_load, .appear = win_appear, .disappear = win_disappear, .unload = win_unload,
  });
  window_set_click_config_provider(s_window, click_config);
  window_stack_push(s_window, true);
}
