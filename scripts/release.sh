#!/usr/bin/env bash
# Cuts a release: bumps the version, commits, and creates an annotated "v<version>"
# tag. There is no release workflow (yet) — the tag is what marks the release.
# Pushing it does run .github/workflows/build.yml (it builds on every push, tags
# included), which uploads the .pbw as a plain CI artifact, but nothing publishes a
# GitHub Release. The watch reports package.json's top-level "version" (`pebble
# build` ships it as the .pbw's versionLabel), so this script keeps that committed
# version and the tag in step. Versions are X.Y (the SDK docs' form) or X.Y.Z;
# pebble-tool accepts both.
#
#   scripts/release.sh 1.3.0          # bump package.json + README, commit, tag v1.3.0
#   scripts/release.sh 1.3.0 --push   # …also push the commit + tag
#   scripts/release.sh                # tag the current version as-is
#
# Usage: scripts/release.sh [X.Y[.Z]] [--push]
# Shared engine: https://github.com/L-K-M/release-tool (this stub only sets config).
set -euo pipefail

export RELEASE_APP_NAME="Lern Deutsch"
export RELEASE_KIND="pebble"
export RELEASE_CI_NOTE="No release workflow (yet) — the tag marks the release; build.yml will upload the .pbw as a CI artifact."
export RELEASE_PUSH_HINT="Push it to publish the tag:"
export RELEASE_INVOKED_AS="scripts/release.sh"

BIN="${LKM_RELEASE_BIN:-lkm-release}"
command -v "$BIN" >/dev/null 2>&1 || {
  echo "error: lkm-release not found — clone https://github.com/L-K-M/release-tool and run ./install.sh" >&2
  exit 1
}
exec "$BIN" "$@"
