#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_ROOT="${SCRIPT_DIR}/.nodejs"
DOWNLOAD_ROOT="${INSTALL_ROOT}/downloads"
VERSIONS_ROOT="${INSTALL_ROOT}/versions"
NODEJS_VERSION="${NODEJS_VERSION:-v24.7.0}"
NODEJS_DIST_BASE="${NODEJS_DIST_BASE:-https://nodejs.org/dist}"

uname_s="$(uname -s)"
uname_m="$(uname -m)"

case "${uname_s}" in
  Linux) node_platform="linux" ;;
  Darwin) node_platform="darwin" ;;
  *)
    echo "error: unsupported OS '${uname_s}'. Supported: Linux, Darwin." >&2
    exit 1
    ;;
esac

case "${uname_m}" in
  x86_64|amd64) node_arch="x64" ;;
  arm64|aarch64) node_arch="arm64" ;;
  *)
    echo "error: unsupported architecture '${uname_m}'. Supported: x64, arm64." >&2
    exit 1
    ;;
esac

archive_name="node-${NODEJS_VERSION}-${node_platform}-${node_arch}.tar.xz"
download_url="${NODEJS_DIST_BASE}/${NODEJS_VERSION}/${archive_name}"
archive_path="${DOWNLOAD_ROOT}/${archive_name}"
extract_dir="${VERSIONS_ROOT}/node-${NODEJS_VERSION}-${node_platform}-${node_arch}"

mkdir -p "${DOWNLOAD_ROOT}" "${VERSIONS_ROOT}"

if [[ ! -f "${archive_path}" ]]; then
  echo "[download-nodejs] Downloading ${download_url}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --retry-delay 2 -o "${archive_path}" "${download_url}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${archive_path}" "${download_url}"
  else
    echo "error: curl or wget is required to download Node.js." >&2
    exit 1
  fi
else
  echo "[download-nodejs] Reusing ${archive_path}"
fi

if [[ ! -x "${extract_dir}/bin/node" ]]; then
  rm -rf "${extract_dir}"
  mkdir -p "${extract_dir}"
  echo "[download-nodejs] Extracting ${archive_name}"
  tar -xJf "${archive_path}" --strip-components=1 -C "${extract_dir}"
else
  echo "[download-nodejs] Reusing ${extract_dir}"
fi

ln -sfn "${extract_dir}" "${INSTALL_ROOT}/current"

cat <<EOF
[download-nodejs] Ready:
  ${extract_dir}

To use this Node.js for packaging in the current shell:
  source "${SCRIPT_DIR}/setup-nodejs.sh"
EOF
