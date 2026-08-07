#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if ! command -v node >/dev/null 2>&1; then
  echo "error: node is required for scripts/verify.sh" >&2
  exit 2
fi

exec node "${SCRIPT_DIR}/verify.mjs" --root "${ROOT_DIR}" "$@"
