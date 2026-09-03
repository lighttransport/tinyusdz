#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE_DIR="${LIGHTUSD_VERIFY_CACHE:-${ROOT_DIR}/.cache/lightusd-verification}"
DEST="${CACHE_DIR}/usd-assets"
MANIFEST="${LIGHTUSD_VERIFY_MANIFEST:-${ROOT_DIR}/tests/verification/manifest.json}"
CHECKOUT_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --checkout-only) CHECKOUT_ONLY=1; shift ;;
    --cache-dir) CACHE_DIR="$2"; DEST="${CACHE_DIR}/usd-assets"; shift 2 ;;
    --manifest) MANIFEST="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: scripts/prepare-usd-assets.sh [--checkout-only] [--cache-dir DIR] [--manifest FILE]"
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

REPO="$(node -e 'console.log(require(process.argv[1]).repositories.usd_assets.url)' "${MANIFEST}")"
REF="$(node -e 'console.log(require(process.argv[1]).repositories.usd_assets.ref)' "${MANIFEST}")"

if [[ ! -d "${DEST}/.git" ]]; then
  [[ "${LIGHTUSD_VERIFY_OFFLINE:-0}" == 1 ]] && { echo "offline: missing USD-WG assets checkout ${DEST}" >&2; exit 2; }
  mkdir -p "$(dirname "${DEST}")"
  git clone --no-tags --filter=blob:none "${REPO}" "${DEST}"
fi
if [[ "${LIGHTUSD_VERIFY_OFFLINE:-0}" != 1 ]]; then
  git -C "${DEST}" fetch --no-tags origin "${REF}"
fi
if ! git -C "${DEST}" cat-file -e "${REF}^{commit}" 2>/dev/null; then
  echo "USD-WG assets revision is not cached: ${REF}" >&2
  exit 2
fi
git -C "${DEST}" checkout --quiet --detach "${REF}"

if [[ "${CHECKOUT_ONLY}" == 1 ]]; then
  echo "USD-WG assets checkout ready: ${DEST} (${REF})"
  exit 0
fi

[[ -d "${DEST}/test_assets" ]] || { echo "USD-WG checkout lacks test_assets: ${DEST}" >&2; exit 1; }
echo "USD-WG assets ready: ${DEST} ($(git -C "${DEST}" rev-parse HEAD))"
