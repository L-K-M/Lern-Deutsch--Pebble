# awesome.md — a code review of Lern Deutsch

A thorough pass over the whole app: the C sources, the Chinese-glyph pipeline,
the tools, and the CI. Overall verdict first, because it deserves saying: this
is a *lovely* codebase. The bitmap-atlas approach to Chinese on a Pebble is the
right call, the code is small and readable, the comments explain *why*, and the
cat has more personality than most production apps. The findings below are
polish on something already good.

Items marked **[implemented]** have a companion PR; the rest are written up so
they're ready to pick up later. Every companion PR was verified with a local
`pebble build` for emery before being opened.

| PR | What it carries |
|---|---|
| [#2](../../pull/2) | Stroke-colour bug fixes (1.2) |
| [#3](../../pull/3) | The `T恤` atlas fix (1.1) |
| [#4](../../pull/4) | Honest „Gelernt" counting, daily streak, „Rekord!" badge (1.3, 3.1, 3.2, 4.3) |
| [#5](../../pull/5) | Idle timers + released menu atlas (2.1, 2.2) |
| [#6](../../pull/6) | Wrist-flick flip, wrong-answer haptic, backlight, came-back badge (3.3, 3.4, 4.1) |
| [#7](../../pull/7) | Vocabulary validator + CI step + `build.sh` fixes (1.6, 2.4, 2.7, 2.8) |
| [#8](../../pull/8) | Pet the cat (4.2) |

---

## 1 · Bugs

### 1.1 `T恤` renders as just `恤` on the watch — **[implemented → PR #3]**

`tools/gen_assets.py::han_codepoints()` only bakes codepoints `> 0x7F` into a
deck's glyph atlas, and `hanzi.c::decode_glyphs()` silently skips characters
that aren't in the atlas. The Clothes deck card `("das T-Shirt", "T恤", …)` is
the one string in the vocabulary with an ASCII character on its Chinese side,
so the watch shows the answer as just `恤`. Learners get taught the wrong word.

**Fix:** bake every printable codepoint (`> 0x20`) found in a Chinese string
into the atlas — Noto Sans SC has perfectly good Latin glyphs, and a 32 px cell
renders the `T` like a fullwidth `Ｔ`, which is exactly how it should look in
CJK text. One extra glyph in one atlas; future cards like `X光` or `K歌` just
work.

### 1.2 Arrows and chevrons drawn in stale stroke colours (5 sites) — **[implemented → PR #2]**

`graphics_draw_line()` uses the GContext **stroke** colour, but several
call-sites set only the **fill** colour first, so the little
triangles-built-from-lines render in whatever stroke colour the previous
drawing operation happened to leave behind:

| Site | Intended | Actually drawn with |
|---|---|---|
| `study.c::draw_btn_hint` arrow ▶ | hint colour | card border accent — and worse: on the answer face the **DOWN/„Falsch" arrow inherits green** from the ✓ just drawn above it, i.e. a green arrow points at the "wrong" button |
| `study.c` summary ◀/▶ chevrons | white | `GColorWindsorTan` left over from the gold star outlines |
| `main.c::draw_dir_chip` ▶ between 德/中 | row ink | `GColorPictonBlue` from `draw_swap` when the row isn't selected |
| `main.c::draw_header` back-chevron ◀ | black | leftover from the last deck row's star rating |
| `main.c::draw_stats` back-chevron ◀ | light gray | leftover star-outline colour |

**Fix:** set the stroke colour at each site. (The mistaken `set_fill_color`
calls were doing nothing for these shapes.)

### 1.3 The „Gelernt" statistic inflates forever — **[implemented → PR #4]**

`study.c::advance()` calls `lg_learned_add(s_total)` **every** time a round
completes — including replays of a deck you've already mastered. The README
promises the Stats page counts „学过的单词" (words you've learned), which is
naturally capped at the 599 words that exist; instead the counter grows without
bound and stops meaning anything.

**Fix:** count a deck's words exactly once, on its *first* completion (when its
best-star slot is still 0). The stat becomes honest and consistent with the
neighbouring „Decks x/39" stat.

### 1.4 Latent: `utf8_next()` can read past the end of a truncated string

A string ending in a multi-byte lead byte (e.g. a buffer cut mid-character)
makes `utf8_next` read `u[1]`/`u[2]` beyond the NUL and then skip past it. Not
triggerable today — all Chinese strings come from the generator and are
well-formed — but worth hardening if hand-built buffers ever feed the renderer.

### 1.5 Latent: an empty deck would crash the study session

`start_session()` with `card_count == 0` leaves the queue empty and
`q_front()` returns garbage, which indexes `s_g->cards[garbage]`. No empty
decks exist, and the new vocabulary validator (PR #7) rejects them at build
time, so this stays theoretical.

### 1.6 `build.sh` aborts after a first-time SDK install — **[implemented → PR #7]**

Found by running it on a clean machine: the script sets `set -o pipefail`, and
in `yes | pebble sdk install latest` the `yes` process dies of SIGPIPE (141)
when the installer closes stdin — so the pipeline "fails", `set -e` kills the
script right after `Installed.`, and `pebble build` never runs. Re-running
works (the install is skipped), which is exactly the kind of heisenbug that
wastes a newcomer's first build. Fix: `{ yes || true; } | pebble sdk install`
so the installer's own exit code is what counts.

---

## 2 · General issues

### 2.1 Fast timers never sleep → battery drain — **[implemented → PR #5]**

Both windows run aggressive repeating timers for their whole lifetime: the menu
ticks every 60 ms (for the cat blink + smooth scroll) and the study session
every 45 ms — even while the screen is completely static, which during study is
*most of the time* (you're thinking about a card). On a watch this is real
battery. Fix: the study timer now stops whenever nothing animates (front/back
idle) and restarts on input; the menu drops to a 250 ms heartbeat once the
scroll settles (the cat still blinks).

### 2.2 The UI glyph atlas is loaded twice during study — **[implemented → PR #5]**

The menu window holds the ~14 KB UI atlas (512×224 @ 1-bit) and the study
window loads its own second copy, while the menu's sits invisible underneath.
Fix: the menu releases its copy in `disappear` and reloads in `appear` —
peak usage drops by one whole atlas, the largest bitmap in the app.

### 2.3 Dead generated data: six UI strings are baked but never drawn

`UIZH_FLIP, UIZH_LEARNED, UIZH_AGAIN, UIZH_MENU, UIZH_WORDS, UIZH_DECKS` are
generated, declared in `lg.h`, and their glyphs occupy cells in the UI atlas —
but no C code draws them. The likely reason they went unused: the atlas only
has one size (32 px), far too large for the 14–18 px text rows they'd decorate.
See idea 4.4 for the fix that would revive them; alternatively drop them from
generation to slim the atlas.

### 2.4 No vocabulary validation anywhere — **[implemented → PR #7]**

Nothing checks the word list: a gender that contradicts the article
(`("die Mann", …, "m")`), duplicate cards in a deck, control characters,
non-BMP codepoints (the `uint16_t` tables silently can't hold them), decks
larger than `MAXCARDS` (64 — `start_session` silently truncates!), phrases
longer than the card box… Bug 1.1 shipped precisely because no tool was
looking. Fix: `tools/validate_vocab.py`, dependency-free, run by
`gen_assets.py` before generating and by CI before building. Its first run on
the real data already surfaced something: `gross` and `klein` each live in two
decks (*Alltag* and *Aussehen*) — flagged as warnings in case it's deliberate.

### 2.5 CI can't see vocab drift

If someone edits `tools/vocab.py` and forgets to regenerate (or hand-edits
`src/c/vocab_gen.c` despite the warning banner), CI happily builds the stale
data. A freshness check is awkward because the PNGs aren't byte-stable across
Pillow versions — but `vocab_gen.c` *is* deterministic, so regenerating in CI
and diffing just that file would catch most drift. Written up, not implemented.

### 2.6 `gen_assets.py` clobbers manual resources in `package.json`

`write_package_json()` rebuilds the media list as *menu icon + atlases*,
discarding any resource a developer added by hand (a custom font, a photo).
Fine today; surprising later. A merge-instead-of-replace would be kinder.

### 2.7 `build.sh` doesn't check for Node — **[implemented → PR #7]**

The SDK's build step needs `node`; CI installs it explicitly but the local
script doesn't verify it, so on a bare machine you get a cryptic waf error
instead of "please install node".

### 2.8 Small documentation nits — **[implemented → PR #7]**

AGENTS.md tells contributors to `pip install pillow fonttools`, but nothing
imports fonttools. Also `subtitle()`'s 44-byte buffer is an exact fit for the
longest deck subtitle („Länder & Sprachen · Countries & Languages", 43 bytes +
NUL) — one more character and it truncates. Harmless (`snprintf`), but worth a
comment in the doc so nobody loses an afternoon to a missing `s`.

---

## 3 · Missing features

### 3.1 A daily streak — **[implemented → PR #4]**

The single most motivating mechanic a daily-drill app can have, and a watch is
the perfect place for it: it's on your wrist every day. Implemented as: finish
at least one round on consecutive days → streak grows; shown as a little flame
+ count on the home banner and a „Serie: N Tage" line on the round summary.
Two persist keys, day arithmetic done properly via a days-from-civil-date
conversion (no year-boundary bugs).

### 3.2 You can't tell when you've beaten your best — **[implemented → PR #4]**

The summary shows this round's stars but never compares them with the stored
best, so a personal record feels identical to any other run. Implemented: a
gold „Rekord!" tag appears beside the stars when the round beats (or sets)
your best.

### 3.3 Distinct haptics for wrong answers — **[implemented → PR #6]**

Correct answers vibrate; wrong answers are silent. With your eyes on the
feedback card both feel the same on the wrist. A soft double-tick for „noch
nicht" makes grading legible without looking.

### 3.4 No marker on cards that came back — **[implemented → PR #6]**

Missed cards re-enter the queue, but their second appearance looks identical
to a fresh card. A small ↻ in the card's corner („you've seen this one — it
bit you") is a classic flashcard affordance.

### 3.5 Review-only-missed mode (not implemented)

The app forgets *which* cards you struggled with the moment a round ends. With
one persist blob per deck (one byte per card, well within Pebble's 4 KB persist
budget) the app could offer „nur die schweren" — a session seeded only with
cards you've historically missed. This is the highest-value feature on this
list; it needs a small persistence design (per-deck blobs, versioned) and a
menu affordance (e.g. long-press SELECT on a deck), so it deserves its own
focused PR.

### 3.6 Tier-wide mixed practice (not implemented)

„Quiz me on all of Grundstufe" — shuffle across a tier's decks. Technically
interesting because each deck has its own glyph atlas: a mixed session must
swap atlases per card (a ~2 KB PNG decode per swap, fine in practice, or an
LRU of two). Star/“learned” bookkeeping for mixed rounds needs deciding before
building it.

### 3.7 A settings row (not implemented)

Vibration on/off (silent classrooms!), flip-animation on/off, maybe
left-handed mode. One more home row and three persist bits — but each setting
is UI surface forever, so add them only when someone actually asks.

---

## 4 · Novel / cool / delightful / quirky ideas

### 4.1 Flick your wrist to flip the card — **[implemented → PR #6]**

The flagship "it's a watch!" interaction: the accelerometer tap service
(`accel_tap_service_subscribe`) detects a wrist flick, and a flick is exactly
the physical metaphor for *turning a card over*. Front → reveal answer; back →
peek at the front again. Grading stays on the buttons (a shake should never
accidentally grade). Paired with `light_enable_interaction()` so the backlight
comes on for the reveal — flick, glow, answer.

### 4.2 Pet the cat — **[implemented → PR #8]**

On the Stats screen, SELECT — which currently just duplicates BACK — now *pets
the mascot*: it purrs (soft double-buzz), closes its eyes happily, and a little
ring of hearts appears. Completely undocumented on purpose; the best easter
eggs are found, not announced. (BACK still exits, nothing is lost.)

### 4.3 Streak flame on the home banner — **[implemented → PR #4]**

The banner has empty space right of the title; a tiny vector flame with the
day count lives there now. Walking past your watch and seeing 🔥 12 is the
whole growth loop of a certain green owl, miniaturised.

### 4.4 A second, smaller UI atlas → bilingual labels everywhere (not implemented)

Everything UI-Chinese renders at 32 px because that's the only size baked.
Baking a *second* UI atlas at ~18 px (same pipeline, one more `HanAtlas`)
would let the stats rows read „Gelernt · 学会", the summary hints read
„nochmal · 再来", and would finally use the six dead strings from issue 2.3.
Cheap at the tool level, transformative for the app's Chinese-native audience.

### 4.5 AppGlance: progress in the launcher (not implemented)

`app_glance_reslice()` could set the launcher subtitle to „🔥 12 · 34★ ·
210 Wörter" when the app exits. Zero UI inside the app; pure ambient
motivation. Needs a quick check that the targeted firmware exposes the
AppGlance API for emery builds.

### 4.6 Seasonal cat (not implemented, but irresistible)

`localtime()` is already linked: give the cat a Santa hat in December, a party
hat on the app's birthday, sunglasses in July. ~15 lines each in
`lg_draw_cat`, pure delight, zero risk to anything that matters.

### 4.7 Atlas packing: stop shipping air (not implemented)

`COLS = 16` means even the 13-glyph Numbers deck ships a 512-px-wide
bitmap row that's mostly transparent. Sizing columns to
`min(16, glyph_count)` (cells are 32 px, so any column count keeps byte
alignment) trims both resource size and the decoded heap footprint of every
small deck. A few KB across 40 atlases — worth folding into the next asset
regeneration.

---

## Cross-cutting notes for whoever picks items up

* Deck order in `GROUPS` is a persistence contract (best-star slots) — none of
  the changes above may reorder it. New persist keys used: 3 (streak length)
  and 4 (streak day); best-star keys start at 100, so keys 5–99 remain free.
* The PRs were cut to minimise overlap, but #2/#4/#5/#6 all touch `study.c`
  (and #2/#4/#5/#8 touch `main.c`) in different regions; most pairs merge
  cleanly, and where they don't the loser needs a trivial rebase, not thought.
* Everything was reviewed against SDK behaviour (stroke vs fill semantics,
  `gbitmap_create_as_sub_bitmap` alignment, persist limits); the CI build on
  each PR is the final word on compilation for the emery target.
