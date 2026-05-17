#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SITE_DIR="${REPO_ROOT}/web/site"
DEMO_DIR="${REPO_ROOT}/web/demo"
DIST_DIR="${SITE_DIR}/dist"
DEMO_DIST_DIR="${DEMO_DIR}/dist"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

cp "${SITE_DIR}/index.html" "${DIST_DIR}/index.html"

if [ -d "${REPO_ROOT}/doc/static" ]; then
  cp -R "${REPO_ROOT}/doc/static/." "${DIST_DIR}/"
fi

(
  cd "${DEMO_DIR}"
  bun run build
)

mkdir -p "${DIST_DIR}/demos"
cp -R "${DEMO_DIST_DIR}/." "${DIST_DIR}/demos/"

echo "Staged GitHub Pages site at ${DIST_DIR}"
