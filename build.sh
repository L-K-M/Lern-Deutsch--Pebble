#!/usr/bin/env bash
#
# Build Lern Deutsch from a clean checkout.
#
# Sets up everything `pebble build` needs (a Python venv with pebble-tool, and
# the Pebble SDK) if it isn't there yet, then builds the .pbw. Safe to re-run —
# each step is skipped when it's already done.
#
#   ./build.sh            set up if needed, then `pebble build`
#   ./build.sh --assets   also regenerate the Chinese atlases from tools/vocab.py
#   ./build.sh --clean    run `pebble clean` first
#
set -euo pipefail
cd "$(dirname "$0")"

VENV=".pebble-venv"
FONT="tools/fonts/NotoSansSC-Regular.otf"
FONT_URL="https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"

DO_ASSETS=0
DO_CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --assets) DO_ASSETS=1 ;;
    --clean)  DO_CLEAN=1 ;;
    -h|--help) sed -n '3,13p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
  esac
done

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

# --- 1. A Python pebble-tool can use. It officially supports 3.10–3.13, but the
#        SDK's bundled `waf` build step breaks on 3.13 ("SRE module mismatch"),
#        so prefer 3.12 (what the repebble docs/CI use); 3.13 is a last resort. -
find_python() {
  for c in python3.12 python3.11 python3.10 python3.13; do
    command -v "$c" >/dev/null 2>&1 && { echo "$c"; return 0; }
  done
  if command -v python3 >/dev/null 2>&1; then
    case "$(python3 -c 'import sys;print("%d.%d"%sys.version_info[:2])')" in
      3.10|3.11|3.12) echo python3; return 0 ;;
    esac
  fi
  return 1
}

# --- 2. venv + pebble-tool ----------------------------------------------------
if [ ! -x "$VENV/bin/pebble" ]; then
  say "Setting up pebble-tool in $VENV"
  PY=$(find_python) || {
    echo "No Python 3.10–3.13 found (pebble-tool needs one)." >&2
    echo "Install one, e.g.  brew install python@3.12  (macOS)." >&2
    exit 1
  }
  echo "using $PY ($("$PY" --version 2>&1))"
  "$PY" -m venv "$VENV"
  "$VENV/bin/pip" install --quiet --upgrade pip setuptools wheel
  "$VENV/bin/pip" install --quiet pebble-tool
fi
export PATH="$PWD/$VENV/bin:$PATH"
echo "pebble-tool: $(pebble --version 2>/dev/null | head -1)"

# --- 3. Pebble SDK (first run downloads the ARM toolchain) --------------------
sdk_installed() {
  pebble sdk list 2>/dev/null | awk '
    /Installed SDKs:/ {ins=1; next}
    /Available SDKs:/ {ins=0}
    ins && /[0-9]+\.[0-9]+/ {found=1}
    END {exit found?0:1}'
}
if ! sdk_installed; then
  say "Installing the Pebble SDK (one-time, downloads the toolchain)"
  # `yes` dies of SIGPIPE when the installer closes stdin; under `pipefail`
  # that killed the whole script right here on every first run. Swallow it so
  # the installer's own exit code is what counts.
  { yes || true; } | pebble sdk install latest
fi

# --- 4. (optional) regenerate the Chinese glyph atlases ----------------------
if [ "$DO_ASSETS" = 1 ]; then
  say "Regenerating assets from tools/vocab.py"
  "$VENV/bin/pip" install --quiet pillow           # the generator rasterises with PIL
  if [ ! -f "$FONT" ]; then
    echo "downloading Noto Sans SC (glyph source)…"
    mkdir -p "$(dirname "$FONT")"
    curl -fsSL -o "$FONT" "$FONT_URL"
  fi
  "$VENV/bin/python" tools/gen_assets.py
fi

# --- 5. build -----------------------------------------------------------------
command -v node >/dev/null 2>&1 || {
  echo "node not found — the Pebble SDK needs Node.js to build resources." >&2
  echo "Install it first, e.g.  brew install node  (macOS)." >&2
  exit 1
}
[ "$DO_CLEAN" = 1 ] && { say "pebble clean"; pebble clean; }
say "pebble build"
pebble build

say "Done"
PBW=$(ls -t build/*.pbw 2>/dev/null | head -1 || true)
[ -n "$PBW" ] && echo "Built $PBW"
echo "Install on the watch:  pebble install --phone <watch-ip>   (or sideload the .pbw)"
echo "Try in the emulator:   pebble install --emulator emery"
