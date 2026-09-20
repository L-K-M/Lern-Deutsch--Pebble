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
├── vocab_gen.c    GENERATED — decks, codepoint tables (from vocab.py); the
│                  card text itself is packed into resources/data/cards.bin
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
(**Actions → Build Pebble app**). Pushing a `v*` tag additionally publishes a
GitHub Release with the compiled `.pbw` attached — that's the download the
README points learners at. To backfill a release for an already-pushed tag,
run the workflow manually (**Run workflow**) with the tag name as input.

## Editing the vocabulary

The whole word list lives in [`tools/vocab.py`](tools/vocab.py): decks grouped
into difficulty tiers, each card a `(german, chinese, english, gender)` tuple.
Edit it, then regenerate the atlases and generated C:

```bash
python3 -m venv .venv && .venv/bin/pip install pillow
# fetch the font used to bake the glyphs (OFL, not committed):
curl -fsSL -o tools/fonts/NotoSansSC-Regular.otf \
  https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
.venv/bin/python tools/gen_assets.py     # rewrites han_*.png + cards.bin + vocab_gen.c
pebble build
```

(`./build.sh --assets` does all of the above in one go.)

`gen_assets.py` figures out exactly which Chinese characters the word list
uses and bakes only those, so the app stays tiny no matter how big the
dictionary feels. It also maintains the resource list in `package.json`.

The card text (German/Chinese/English) is packed into a single raw resource,
`resources/data/cards.bin`, rather than compiled into the app binary: each deck
records a byte offset+length into it, and `study.c` loads just that slice into
RAM when you open the deck. That keeps ~1200 cards' worth of strings out of the
binary, which otherwise hits Pebble's hard 64 KB app-image limit (resources get
their own, roomier 256 KB budget). Deck *titles* still live in the binary.

`tools/validate_vocab.py` sanity-checks the word list against the app's real
constraints (gender ↔ article, deck sizes, characters the renderer can draw…).
It runs automatically at the start of `gen_assets.py` and as a CI step, or
standalone: `python3 tools/validate_vocab.py`.

### Rules that keep saved progress and rendering intact

* **Never reorder existing decks in `GROUPS`** — a deck's position is its
  persisted best-score slot; reordering shuffles everyone's saved progress.
  Append new decks at the end; their `tier` key decides where they appear in
  the menu.
* German nouns include their article (`"das Brot"`); the `gender` field
  (`"m"`/`"f"`/`"n"`) only drives the article colour-coding (der = blue,
  die = pink, das = green).
* Spelling follows **Swiss usage**: always *ss*, never *ß*.
* Don't edit `src/c/vocab_gen.c`, `resources/data/cards.bin`, or
  `resources/images/han_*.png` by hand — they're generated. New Chinese
  characters are fine; they get baked into the
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

<!-- shared-rules:start -->

## Working practices

- Follow explicit task instructions over the default workflow below.
- Before editing, inspect the branch and working tree, fetch remote updates,
  and fast-forward where safe. Never overwrite existing work to update.
- Resolve ambiguity before making consequential changes. State low-risk
  assumptions; ask when scope, safety, or expected behavior is unclear.
- Keep changes focused. Do not modify unrelated code, formatting, or comments.
- Prefer surgical edits over whole-file rewrites when the result is equivalent.
- Stage only intended files. Inspect the diff before committing.

## Communication

- Be concise, factual, and direct. Preserve necessary context and uncertainty.
- Avoid praise, motivational filler, emojis, and em dashes in new prose.
- Address the reader directly in user-facing copy.
- Report what was verified and what remains unverified. Never imply that an
  unavailable check passed.

## Code design

- Prefer early returns and shallow nesting. Separate logical blocks with
  blank lines.
- Use descriptive constants or enums for meaningful or repeated values.
  Use existing standard definitions for protocol/specification constants.
  Keep obvious, one-off values inline.
- Use enums for behavioral modes that would otherwise require ambiguous
  boolean arguments.
- Default members to private. Widen visibility only for required consumers,
  and review the change as an API design decision.
- Follow the repository's declared dependency boundaries. UI and controllers
  must use application services rather than directly accessing databases,
  subprocesses, sockets, or other low-level mechanisms.
- Encapsulate low-level mechanics behind domain-oriented interfaces.
- Reuse genuinely shared logic. Avoid speculative abstractions and layers
  that only forward calls.
- Prefer pure functions for business rules and immutable data where practical.
  Isolate side effects; document non-obvious state ownership or synchronization.
- Explain non-obvious intent, constraints, and tradeoffs in comments.
  Do not narrate obvious code. Add examples or diagrams when they clarify it.

## Validation and errors

- Validate untrusted input at entry points. Where practical, represent valid
  states in types and enforce persistent invariants in database schemas.
- Represent absence and failure explicitly.
- Use assertions for internal programming invariants, not external-input
  validation or required runtime error handling.
- Prefer explicit, actionable errors over silent failure or undocumented
  fallback. Document intentional recovery behavior.
- Never report a skipped or failed operation as successful.

## Bug fixes

1. Identify the root cause and define an observable success criterion.
2. Add a regression test and observe the relevant failure before fixing it.
3. Implement the fix and observe the test passing.
4. Check surrounding behavior for regressions and architectural consistency.

If an automated regression test is impractical, document the reproduction
and verification procedure. State any inability to reproduce the failure.

## Verification

- Run relevant tests and lint after changes.
- Choose coverage by affected behavior and risk, not patch size.
- Use integration or end-to-end tests for critical workflows and boundaries;
  test isolated business rules at the lowest effective level.
- Run broader suites for cross-cutting or high-risk changes, and the full
  required release checks before releasing.
- Validate the requested command, options, platform, and configuration.
  Unrelated green CI is not proof that the reported problem is fixed.
- Recheck after the final edit. Distinguish local checks from CI results.

## Commit messages

- Use a capitalized, imperative subject without a final period.
- Target 50 characters; never exceed 72.
- Separate the subject and body with one blank line.
- Wrap body text at 72 characters.
- Explain what changed and why. Leave implementation mechanics to the code.

## Implementation and review

Unless explicitly instructed otherwise:

1. Work on a focused branch and open a PR against main.
2. Inspect CI results and completed review feedback for the latest commit.
   A successful reviewer job does not mean the review found no problems.
3. Address important findings or explain why they do not apply. Handle minor
   findings according to the stopping rules below.
4. Evaluate each fix in the surrounding project, add regression coverage,
   and rerun affected checks before pushing.
5. Repeat until a stopping criterion is met.
6. Merge without asking again once the stopping criterion is met, required
   checks pass on the latest commit, and no unresolved blockers or required
   human review requests remain.

### Reviewer context limits

The automated PR reviewer does not see the user's original prompt or
conversation. It may suggest changes that go against or beyond what the
user asked for. Do not implement such suggestions. Note each conflict and
report it to the user at the end of the thread.

### Automated review stopping rules

Judge findings by verified impact, not the reviewer's severity label.
Important findings concern correctness, security, data loss, broken builds,
or materially degraded behavior/performance.

Track completed review rounds and consecutive rounds without important
findings. Reruns of the same revision and integration failures do not count.

- No applicable actionable feedback: finish immediately.
- First minor-only round: optionally fix worthwhile, low-risk findings.
  Do not manufacture another push merely to obtain another review.
- Two consecutive rounds without important findings: stop responding to
  automated nitpicks, even if actionable minor suggestions remain.
  Defer worthwhile leftovers rather than continuing the cycle.
- A confirmed important finding resets the minor-only streak. Address it
  and verify the fix before continuing.

After ten completed rounds, enter stabilization:

- Stop optional cleanup, refactoring, and nitpick fixes.
- One completed review without confirmed important findings is sufficient
  to finish, even if minor suggestions remain.
- Continue only for confirmed important defects. If resolving them stalls,
  report the blockers rather than continuing indefinitely.

These limits end optional automated-feedback work. They do not waive
confirmed blockers, unresolved human review requests, or required checks.

### Reviewer integration failures

After two consecutive reviewer-integration failures, stop and report the
review gap. Do not treat failures as approval. An explicit user instruction
may waive review; report that waiver rather than claiming review passed.

## Completion checklist

- The requested behavior is implemented without unrelated changes.
- Relevant checks pass for the latest code.
- Important review findings are addressed or rejected with reasons.
- Deferred suggestions, remaining risks, and validation gaps are disclosed.
- The final response accurately states whether work is committed, pushed,
  and merged.

<!-- shared-rules:end -->
