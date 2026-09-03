#!/usr/bin/env bash
#
# Batch-render a USD asset corpus through lusdview only (Vulkan raster + ray
# query). Thin wrapper over the shared harness
# examples/lusdview/tests/run-usd-assets-render-smoke.sh: it bakes lusdview-only
# modes and sane batch defaults, and forwards any extra args (--root / --limit /
# --modes / --timeout / --out / --golden ...).
#
# Corpus: set USD_ASSETS_ROOT (or --root DIR). Defaults to the repository-local
# `usd-assets` symlink; the harness SKIPs if it does not exist. usd-wg/assets
# uses '..' parent-relative references, so
# --allow-parent-paths is enabled for lusdview here.
#
# Examples:
#   tests/lusdview/run-usd-assets-batch.sh
#   tests/lusdview/run-usd-assets-batch.sh --root ~/usd-assets --limit 30
#   tests/lusdview/run-usd-assets-batch.sh --modes vk-raster --out /tmp/tv-batch
#
# For the combined lusdview+lusdrender sweep see
# examples/lusdview/tests/run-usd-assets-disk1.sh (ctest: lusdview-usd-assets-disk1).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

: "${USD_ASSETS_ROOT:=${USD_WG_ASSETS_DIR:-$REPO_ROOT/usd-assets}}"
if [ -d "$USD_ASSETS_ROOT" ]; then
  USD_ASSETS_ROOT="$(cd "$USD_ASSETS_ROOT" && pwd -P)"
fi
: "${LUSDVIEW:=$REPO_ROOT/build/lusdview}"
: "${LUSDVIEW_USD_ASSETS_MODES:=vk-raster,vk-rt}"
: "${LUSDVIEW_USD_ASSETS_ALLOW_PARENT:=1}"
: "${LUSDVIEW_XVFB:=1}"
: "${LUSDVIEW_USD_ASSETS_TIMEOUT:=60s}"
# Curated expectations make known warnings/degradation explicit. Assets outside
# the manifest remain ordinary smoke coverage.
: "${LUSDVIEW_USD_ASSETS_EXPECTATIONS:=$REPO_ROOT/examples/lusdview/tests/usd-assets-expectations.tsv}"
: "${LUSDVIEW_USD_ASSETS_FAIL_ON:=timeout,backend_error,unexpected_degradation,expectation_mismatch}"

export USD_ASSETS_ROOT LUSDVIEW LUSDVIEW_USD_ASSETS_MODES \
  LUSDVIEW_USD_ASSETS_ALLOW_PARENT LUSDVIEW_XVFB LUSDVIEW_USD_ASSETS_TIMEOUT \
  LUSDVIEW_USD_ASSETS_FAIL_ON LUSDVIEW_USD_ASSETS_EXPECTATIONS

exec bash "$REPO_ROOT/examples/lusdview/tests/run-usd-assets-render-smoke.sh" "$@"
