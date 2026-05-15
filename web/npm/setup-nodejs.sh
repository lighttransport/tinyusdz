#!/usr/bin/env bash
set -euo pipefail

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "error: source this script instead of executing it: source ./setup-nodejs.sh" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="${SCRIPT_DIR}/.nodejs/current"
NODE_BIN_DIR="${NODE_ROOT}/bin"

if [[ ! -x "${NODE_BIN_DIR}/node" ]]; then
  echo "error: local Node.js not found at ${NODE_BIN_DIR}/node" >&2
  echo "run ./download-nodejs.sh first" >&2
  return 1
fi

case ":${PATH}:" in
  *":${NODE_BIN_DIR}:"*) ;;
  *) export PATH="${NODE_BIN_DIR}:${PATH}" ;;
esac

export TINYUSDZ_NPM_NODEJS_ROOT="${NODE_ROOT}"

node_major="$("${NODE_BIN_DIR}/node" -p "Number(process.versions.node.split('.')[0])")"
if (( node_major < 24 )); then
  echo "error: Node.js v24+ is required for built-in Zstd support." >&2
  return 1
fi

if ! "${NODE_BIN_DIR}/node" -p "typeof require('node:zlib').zstdCompressSync === 'function'" | grep -qx 'true'; then
  echo "error: This Node.js build does not expose built-in Zstd compression." >&2
  return 1
fi

echo "[setup-nodejs] node: $("${NODE_BIN_DIR}/node" -p "process.version")"
echo "[setup-nodejs] npm:  $("${NODE_BIN_DIR}/npm" -v)"
echo "[setup-nodejs] PATH updated with ${NODE_BIN_DIR}"
