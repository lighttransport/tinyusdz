#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_BIN="${SCRIPT_DIR}/.nodejs/current/bin/node"
EMSDK_ENV_SH="${SCRIPT_DIR}/.emsdk/emsdk_env.sh"
EMSDK_CONFIG="${SCRIPT_DIR}/.emsdk/.emscripten"

ensure_local_node() {
  if [[ ! -x "${NODE_BIN}" ]]; then
    echo "[build-package] Local Node.js not found. Downloading."
    "${SCRIPT_DIR}/download-nodejs.sh"
  fi
}

ensure_local_emsdk() {
  if [[ ! -f "${EMSDK_ENV_SH}" || ! -f "${EMSDK_CONFIG}" ]]; then
    echo "[build-package] Local emsdk not found. Downloading."
    "${SCRIPT_DIR}/download-emsdk.sh"
  fi
}

ensure_npm_dependencies() {
  if [[ ! -d "${SCRIPT_DIR}/node_modules/three" || "${FORCE_NPM_CI:-0}" == "1" ]]; then
    echo "[build-package] Installing npm dependencies with npm ci"
    npm ci
  else
    echo "[build-package] Reusing existing node_modules"
  fi
}

main() {
  ensure_local_node
  source "${SCRIPT_DIR}/setup-nodejs.sh"

  ensure_local_emsdk
  source "${SCRIPT_DIR}/setup-emsdk.sh"

  ensure_npm_dependencies

  "${SCRIPT_DIR}/build-wasm.sh"
  node "${SCRIPT_DIR}/scripts/stage-package.mjs" "$@"
  node "${SCRIPT_DIR}/scripts/validate-package.mjs"

  echo "[build-package] Complete."
}

main "$@"
