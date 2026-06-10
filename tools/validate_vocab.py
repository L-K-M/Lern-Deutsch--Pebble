#!/usr/bin/env python3
# coding: utf-8
"""Sanity-check tools/vocab.py against the constraints the app actually has.

The word list is plain Python data, so nothing stops a typo from shipping —
and the watch fails *silently*: glyphs missing from an atlas simply don't
draw, decks beyond the session limit are quietly truncated, a wrong gender
paints the article the wrong colour. This validator turns each of those into
a loud build-time error instead.

It is dependency-free on purpose: CI runs it before installing anything, and
`gen_assets.py` runs it before generating. Standalone:

    python3 tools/validate_vocab.py
"""
import sys
import os

# Limits mirrored from the C side / layout. If you change one there, change
# it here (each constant says where it lives).
MAX_CARDS = 64        # study.c MAXCARDS — longer decks are silently truncated
MAX_NOUN_BYTES = 39   # study.c draw_german() copies the de-without-article
                      # into a 40-byte buffer
MAX_ZH_GLYPHS = 12    # the card box fits 3 glyphs x 4 lines comfortably
                      # (hanzi.c han_draw_box, 32px cells)
MAX_TITLE_GLYPHS = 3  # menu rows: a 4-glyph deck title collides with the
                      # star rating at the row's right edge

ARTICLES = {"m": "der ", "f": "die ", "n": "das "}
GENDERS = {"", "m", "f", "n"}


def _glyphs(s):
    """Characters that occupy an atlas cell (everything printable non-space)."""
    return [c for c in s if ord(c) > 0x20]


def run(vocab):
    """Return (errors, warnings) for the given vocab module."""
    errors, warnings = [], []

    tier_ids = set()
    for t in vocab.TIERS:
        if t["id"] in tier_ids:
            errors.append("TIERS: duplicate id %r" % t["id"])
        tier_ids.add(t["id"])
        for field in ("de", "zh", "en", "accent"):
            if not t.get(field):
                errors.append("tier %r: missing %r" % (t["id"], field))
        if len(_glyphs(t["zh"])) > MAX_TITLE_GLYPHS:
            warnings.append("tier %r: zh title %r is %d glyphs (menu fits %d)"
                            % (t["id"], t["zh"], len(_glyphs(t["zh"])), MAX_TITLE_GLYPHS))

    keys = set()
    de_seen = {}                       # german -> first deck key (cross-deck dupes)
    for g in vocab.GROUPS:
        k = g["key"]
        where = "deck %r" % k
        if k in keys:
            errors.append("%s: duplicate key" % where)
        keys.add(k)
        if g["res"] != "HAN_" + k.upper():
            errors.append("%s: res %r should be %r" % (where, g["res"], "HAN_" + k.upper()))
        if g["tier"] not in tier_ids:
            errors.append("%s: unknown tier %r" % (where, g["tier"]))
        if len(_glyphs(g["zh"])) > MAX_TITLE_GLYPHS:
            warnings.append("%s: zh title %r is %d glyphs (menu fits %d)"
                            % (where, g["zh"], len(_glyphs(g["zh"])), MAX_TITLE_GLYPHS))
        if not g["cards"]:
            errors.append("%s: empty deck (the study session can't handle it)" % where)
        if len(g["cards"]) > MAX_CARDS:
            errors.append("%s: %d cards, but study.c truncates at %d"
                          % (where, len(g["cards"]), MAX_CARDS))

        in_deck = set()
        for card in g["cards"]:
            if len(card) != 4 or not all(isinstance(x, str) for x in card):
                errors.append("%s: malformed card %r" % (where, (card,)))
                continue
            de, zh, en, gender = card
            label = "%s: %r" % (where, de)
            if not de or not zh or not en:
                errors.append("%s: empty field" % label)
            if gender not in GENDERS:
                errors.append("%s: gender %r not in %s" % (label, gender, sorted(GENDERS)))
            elif gender and not de.startswith(ARTICLES[gender]):
                errors.append("%s: gender %r but the word doesn't start with %r"
                              % (label, gender, ARTICLES[gender].strip()))
            if de in in_deck:
                errors.append("%s: duplicate card in the same deck" % label)
            in_deck.add(de)
            if de in de_seen and de_seen[de] != k:
                warnings.append("%s: also appears in deck %r" % (label, de_seen[de]))
            de_seen.setdefault(de, k)

            noun = de.split(" ", 1)[1] if gender and " " in de else de
            if len(noun.encode("utf-8")) > MAX_NOUN_BYTES:
                errors.append("%s: word is %d bytes; study.c's buffer holds %d"
                              % (label, len(noun.encode("utf-8")), MAX_NOUN_BYTES))
            if len(_glyphs(zh)) > MAX_ZH_GLYPHS:
                errors.append("%s: zh %r is %d glyphs; the card box fits %d"
                              % (label, zh, len(_glyphs(zh)), MAX_ZH_GLYPHS))
            for c in zh:
                if ord(c) <= 0x20 and c != " ":
                    errors.append("%s: control char U+%04X in zh" % (label, ord(c)))
                if ord(c) > 0xFFFF:
                    errors.append("%s: U+%X in zh won't fit the uint16 codepoint table"
                                  % (label, ord(c)))
    return errors, warnings


def run_or_die(vocab):
    errors, warnings = run(vocab)
    for w in warnings:
        print("vocab warning: %s" % w)
    if errors:
        for e in errors:
            print("vocab ERROR: %s" % e, file=sys.stderr)
        sys.exit("vocab.py has %d error(s) — fix them before generating/building." % len(errors))
    total = sum(len(g["cards"]) for g in vocab.GROUPS)
    print("vocab OK: %d tiers, %d decks, %d cards, %d warning(s)"
          % (len(vocab.TIERS), len(vocab.GROUPS), total, len(warnings)))


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vocab
    run_or_die(vocab)
