#!/usr/bin/env bash
set -euo pipefail

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "error: source this script instead of executing it: source ./setup-emsdk.sh" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EMSDK_DIR="${SCRIPT_DIR}/.emsdk"
EMSDK_ENV_SH="${EMSDK_DIR}/emsdk_env.sh"
EM_CONFIG_PATH="${EMSDK_DIR}/.emscripten"
LOCAL_NODE="${SCRIPT_DIR}/.nodejs/current/bin/node"

source "${SCRIPT_DIR}/setup-nodejs.sh"

if [[ ! -f "${EMSDK_ENV_SH}" ]]; then
  echo "error: local emsdk not found at ${EMSDK_ENV_SH}" >&2
  echo "run ./download-emsdk.sh first" >&2
  return 1
fi

if [[ ! -f "${EM_CONFIG_PATH}" ]]; then
  echo "error: local emsdk config not found at ${EM_CONFIG_PATH}" >&2
  echo "run ./download-emsdk.sh first" >&2
  return 1
fi

if [[ ! -x "${LOCAL_NODE}" ]]; then
  echo "error: local Node.js not found at ${LOCAL_NODE}" >&2
  echo "run ./download-nodejs.sh first" >&2
  return 1
fi

export EM_CONFIG="${EM_CONFIG_PATH}"

"${LOCAL_NODE}" <<'NODE'
const fs = require('node:fs');
const path = require('node:path');

const emConfigPath = process.env.EM_CONFIG;
const nodeExpr = "emsdk_path + '/../.nodejs/current/bin/node'";
let content = fs.readFileSync(emConfigPath, 'utf8');

if (/^NODE_JS = .*$/m.test(content)) {
  content = content.replace(/^NODE_JS = .*$/m, `NODE_JS = ${nodeExpr}`);
} else {
  content += `\nNODE_JS = ${nodeExpr}\n`;
}

fs.writeFileSync(emConfigPath, content, 'utf8');
NODE

source "${EMSDK_ENV_SH}"
source "${SCRIPT_DIR}/setup-nodejs.sh"

export EMSDK_NODE="${LOCAL_NODE}"

echo "[setup-emsdk] emsdk: ${EMSDK_DIR}"
echo "[setup-emsdk] emcmake: $(command -v emcmake)"
echo "[setup-emsdk] emcc:    $(command -v emcc)"
