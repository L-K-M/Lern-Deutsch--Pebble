# AGENTS.md — developer guide

Lern Deutsch is a Pebble watchapp (C, Pebble SDK 3) that drills German ⇄
Chinese flashcards with a small English hint gloss on every card. It targets
only **`emery`** — the Pebble Time 2, 200×228. [README.md](README.md) is
deliberately learner-facing and bilingual (Chinese / simple German in Swiss
spelling — always *ss*, never *ß*); keep build and development details in
this file instead.

## Layout

```
src/c/
├── lg.h           shared framework: data model, renderer + helper APIs
├── lg_common.c    persistence, the cat mascot, icons, stars, gender colours
├── hanzi.c        the custom Chinese bitmap-font renderer
├── vocab_gen.c    GENERATED — cards, decks, codepoint tables (from vocab.py)
├── main.c         the scrolling menu + app lifecycle
└── study.c        the flashcard session: flip, self-grade, score, summary
tools/
├── vocab.py       the word list (the one file you edit)
├── gen_assets.py  bakes glyph atlases + generates vocab_gen.c
└── make_icon.py   the launcher icon
```

## Build

The quickest path is the included script — it sets up a Python venv with
pebble-tool and installs the SDK (both only on first run), then builds:

```bash
./build.sh              # -> build/*.pbw
./build.sh --assets     # also regenerate the Chinese atlases from tools/vocab.py
./build.sh --clean      # clean rebuild
```

It needs `python3.12` (or 3.10/3.11) on your PATH — `brew install python@3.12`
on macOS. Python 3.13 breaks the SDK's waf step ("SRE module mismatch"). The
first run downloads the ARM toolchain, which takes a few minutes.

Prefer to do it by hand?

```bash
# 1. Install the command-line tool in an isolated environment
python3.12 -m venv ~/.venvs/pebble
~/.venvs/pebble/bin/pip install pebble-tool
export PATH="$HOME/.venvs/pebble/bin:$PATH"

# 2. Install the SDK (downloads the ARM toolchain)
pebble sdk install latest

# 3. Build  ->  build/*.pbw
pebble build
```

The committed glyph atlases mean a plain build needs nothing beyond the SDK.
See <https://developer.repebble.com/> for SDK help.

### Run it

* Emulator: `pebble install --emulator emery`
* Real watch over local Wi-Fi: `pebble install --phone <watch-ip>`
* Real watch via Dev Connect (no IP needed): in the phone app go to
  **Devices → ⋯ → Enable Dev Connect** and sign in with GitHub, then
  `pebble login` with the same account and `pebble install`.

### CI

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds the `.pbw`
on every push and uploads it as the **`LernDeutsch-pbw`** artifact
(**Actions → Build Pebble app**) — that's the download the README points
learners at.

## Editing the vocabulary

The whole word list lives in [`tools/vocab.py`](tools/vocab.py): decks grouped
into difficulty tiers, each card a `(german, chinese, english, gender)` tuple.
Edit it, then regenerate the atlases and generated C:

```bash
python3 -m venv .venv && .venv/bin/pip install pillow fonttools
# fetch the font used to bake the glyphs (OFL, not committed):
curl -fsSL -o tools/fonts/NotoSansSC-Regular.otf \
  https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
.venv/bin/python tools/gen_assets.py     # rewrites resources/images/han_*.png + src/c/vocab_gen.c
pebble build
```

(`./build.sh --assets` does all of the above in one go.)

`gen_assets.py` figures out exactly which Chinese characters the word list
uses and bakes only those, so the app stays tiny no matter how big the
dictionary feels. It also maintains the resource list in `package.json`.

### Rules that keep saved progress and rendering intact

* **Never reorder existing decks in `GROUPS`** — a deck's position is its
  persisted best-score slot; reordering shuffles everyone's saved progress.
  Append new decks at the end; their `tier` key decides where they appear in
  the menu.
* German nouns include their article (`"das Brot"`); the `gender` field
  (`"m"`/`"f"`/`"n"`) only drives the article colour-coding (der = blue,
  die = pink, das = green).
* Spelling follows **Swiss usage**: always *ss*, never *ß*.
* Don't edit `src/c/vocab_gen.c` or `resources/images/han_*.png` by hand —
  they're generated. New Chinese characters are fine; they get baked into the
  deck's atlas on the next `gen_assets.py` run.

## The interesting bit: drawing Chinese on a Pebble

Pebble's built-in fonts contain **no Chinese glyphs** — apps that just call
the system font (AnkiPebble among them) render Chinese as blank boxes. The
SDK's font sub-setter is also unreliable for non-ASCII character sets.

So Lern Deutsch ships **its own Chinese font, as bitmaps**. `tools/gen_assets.py`
rasterises *only* the characters the word list actually uses (a few hundred)
from **Noto Sans SC** into small PNG "glyph atlases" — one per deck, plus a
shared UI atlas. On the watch, [`hanzi.c`](src/c/hanzi.c) UTF-8-decodes a
string, binary-searches a sorted codepoint table to find each character's
cell, and blits it. The atlases import as 1-bit transparent bitmaps whose ink
colour is recoloured on the fly, so the same sheet draws white on a dark card
or dark on a light one.

German and English (umlauts ä ö ü ß included) just use the system fonts.

## Credits

Chinese glyphs baked from **Noto Sans SC** © The Noto Project Authors, used
under the [SIL Open Font License 1.1](https://openfontlicense.org/). Only the
handful of characters the vocabulary needs are bundled.
