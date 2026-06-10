# Fonts

The Chinese glyph atlases in `resources/images/han_*.png` are baked from
**Noto Sans SC** (Simplified Chinese), which is licensed under the
[SIL Open Font License 1.1](https://openfontlicense.org/).

The font file itself is **not committed** (it is ~8 MB and only needed to
regenerate the atlases). `tools/gen_assets.py` expects it here:

```
tools/fonts/NotoSansSC-Regular.otf
```

To fetch it:

```bash
curl -fsSL -o tools/fonts/NotoSansSC-Regular.otf \
  https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
```

Then regenerate:

```bash
.venv/bin/python tools/gen_assets.py
```

You only need this when you change the word list in `tools/vocab.py`. The
committed atlases are enough to build the `.pbw`.

> Noto Sans SC © The Noto Project Authors, used under the SIL OFL 1.1. Only the
> handful of glyphs our vocabulary uses are baked into the bundled bitmaps.
