#!/usr/bin/env bash
set -euo pipefail

# Copy the repository-owned demo fixtures into the JS test asset directory.
# External USD-WG assets are prepared separately by scripts/prepare-usd-assets.sh.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${WEB_DIR}/demo/public/assets"
DEST_DIR="${SCRIPT_DIR}/assets"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --destination) DEST_DIR="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: web/js/setup-assets.sh [--destination DIR]"
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

[[ -d "$SOURCE_DIR" ]] || { echo "missing tracked demo assets: $SOURCE_DIR" >&2; exit 1; }
mkdir -p "$DEST_DIR"
cp -a "$SOURCE_DIR/." "$DEST_DIR/"
echo "LightUSD demo fixtures ready: $DEST_DIR"
