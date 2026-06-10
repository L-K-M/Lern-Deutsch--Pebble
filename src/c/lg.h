#pragma once
#include <pebble.h>

// ============================================================================
//  Lern Deutsch  —  a cute German⇄Chinese flashcard trainer for Pebble Time 2.
//
//  lg.h is the shared "framework": the vocabulary data model (filled in by the
//  generated vocab_gen.c), the custom Chinese bitmap-font renderer (hanzi.c),
//  and a handful of drawing / persistence helpers everyone reuses.
//
//  Why a custom renderer? Pebble's firmware fonts have NO Chinese glyphs, so we
//  ship our own subsetted glyph atlases (see tools/gen_assets.py) and blit them
//  ourselves. German/Latin text (umlauts included) uses the system fonts.
// ============================================================================

// --- Vocabulary data model (defined in the generated vocab_gen.c) -----------

// One flashcard. `zh` is UTF-8 Chinese; `de`/`en` are Latin (system fonts).
// gender colour-codes the German article as a learning aid:
//   0 = none, 1 = der (m), 2 = die (f), 3 = das (n)
typedef struct {
  const char *de;
  const char *zh;
  const char *en;
  uint8_t     gender;
} Card;

// A bitmap glyph atlas: a PNG sprite-sheet of Chinese glyphs plus the sorted
// codepoint table that maps a character to its cell. Cells are laid out
// left-to-right, top-to-bottom in `cp` order.
typedef struct {
  uint32_t        res_id;     // RESOURCE_ID_* of the atlas PNG
  const uint16_t *cp;         // sorted codepoints, one per cell
  int             cp_count;   // number of glyphs
  uint8_t         cols;       // cells per row
  uint8_t         cell_w;     // cell width  (px, multiple of 8)
  uint8_t         cell_h;     // cell height (px)
} HanAtlas;

typedef enum {
  ICON_WAVE = 0, ICON_NUM, ICON_PEOPLE, ICON_CUP, ICON_SUN, ICON_SPEECH,
  ICON_PALETTE, ICON_PAW, ICON_SHIRT, ICON_HOUSE, ICON_CLOCK, ICON_CLOUD,
  ICON_PLANE, ICON_BOOK, ICON_BAG, ICON_FORK, ICON_PLUS, ICON_HEART,
  ICON_LEAF, ICON_BALL, ICON_PHONE, ICON_CAR, ICON_CALENDAR, ICON_WARNING,
  ICON_GLOBE, ICON_BUILDING, ICON_RUN, ICON_BOX
} IconId;

// A difficulty group: a themed deck of cards with its own glyph atlas.
typedef struct {
  const char     *de;          // group title (German)
  const char     *zh;          // group title (Chinese, lives in the UI atlas)
  const char     *en;          // group title (English)
  GColor          accent;
  uint8_t         icon;        // IconId
  const Card     *cards;
  int             card_count;
  const HanAtlas *atlas;       // glyph atlas for this group's answers
} Group;

extern const Group g_groups[];
extern const int   g_group_count;
extern const HanAtlas ATLAS_UI;     // always-loaded atlas for menu/UI Chinese

// A difficulty tier: a named bundle of decks (referenced by index into g_groups).
typedef struct {
  const char *de, *zh, *en;
  GColor      accent;
  const int  *decks;       // indices into g_groups
  int         deck_count;
} Tier;

extern const Tier g_tiers[];
extern const int  g_tier_count;

// Generated bilingual UI strings (Chinese; glyphs live in ATLAS_UI).
extern const char UIZH_TITLE[], UIZH_DE[], UIZH_ZH[], UIZH_FLIP[];
extern const char UIZH_DONE[], UIZH_LEARNED[], UIZH_AGAIN[], UIZH_MENU[];
extern const char UIZH_STATS[], UIZH_WORDS[], UIZH_DECKS[];
extern const char *const UIZH_PRAISE[]; extern const int UIZH_PRAISE_N;
extern const char *const UIZH_NUDGE[];  extern const int UIZH_NUDGE_N;

// --- The Chinese renderer (hanzi.c) -----------------------------------------

// A loaded atlas: the GBitmap plus the palette slot we recolour per draw so the
// same white-on-transparent sheet can be tinted any colour.
typedef struct {
  const HanAtlas *def;
  GBitmap        *bmp;
  GColor         *palette;
  int             fg_index;
} HanFont;

void han_load(HanFont *f, const HanAtlas *def);
void han_unload(HanFont *f);

// Pixel width of one rendered line of `utf8` in this atlas.
int  han_width(const HanFont *f, const char *utf8);

// Draw one line, top-left at `origin`, tinted `color`.
void han_draw(GContext *ctx, HanFont *f, const char *utf8, GPoint origin, GColor color);

// Draw centered within `box`, wrapping onto multiple lines if needed.
void han_draw_box(GContext *ctx, HanFont *f, const char *utf8, GRect box, GColor color);

// --- Persistent state (lg_common.c) -----------------------------------------
typedef enum { DIR_DE_ZH = 0, DIR_ZH_DE = 1 } Direction;

Direction lg_dir_get(void);
void      lg_dir_set(Direction d);

int  lg_best_stars(int group);              // best star rating earned (0..3)
void lg_best_submit(int group, int stars);  // store if it beats the record

int  lg_learned_total(void);                // lifetime cards mastered
void lg_learned_add(int n);

// --- Shared drawing helpers (lg_common.c) -----------------------------------
#define LG_BG GColorOxfordBlue

void  lg_text(GContext *ctx, const char *s, GFont font, GRect box, GTextAlignment a);
void  lg_panel(GContext *ctx, GRect r, GColor fill, GColor border);
void  lg_draw_icon(GContext *ctx, IconId id, GRect box, GColor fg);

// The mascot: a little cat that reacts. mood: 0 neutral, 1 happy, 2 sad.
void  lg_draw_cat(GContext *ctx, GPoint center, int r, int mood, int frame, GColor fur);

// A row of `total` stars with `filled` lit.
void  lg_draw_stars(GContext *ctx, GRect box, int filled, int total);

// A compact rating: `total` small stars left-to-right from x (centred on y),
// `filled` of them gold; the rest are a faint outline in `empty`.
void  lg_draw_rating(GContext *ctx, int x, int y, int filled, int total, GColor empty);

// Article colour for a noun gender, tuned for light or dark backgrounds.
GColor lg_gender_color(uint8_t gender, bool on_dark);

// --- Study session entry point (study.c) ------------------------------------
void study_push(int group_index);
