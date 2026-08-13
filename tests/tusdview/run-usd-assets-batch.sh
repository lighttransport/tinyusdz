#!/usr/bin/env bash
#
# Batch-render a USD asset corpus through tusdview only (Vulkan raster + ray
# query). Thin wrapper over the shared harness
# examples/tusdview/tests/run-usd-assets-render-smoke.sh: it bakes tusdview-only
# modes and sane batch defaults, and forwards any extra args (--root / --limit /
# --modes / --timeout / --out / --golden ...).
#
# Corpus: set USD_ASSETS_ROOT (or --root DIR). Defaults to the repository-local
# `usd-assets` symlink; the harness SKIPs if it does not exist. usd-wg/assets
# uses '..' parent-relative references, so
# --allow-parent-paths is enabled for tusdview here.
#
# Examples:
#   tests/tusdview/run-usd-assets-batch.sh
#   tests/tusdview/run-usd-assets-batch.sh --root ~/usd-assets --limit 30
#   tests/tusdview/run-usd-assets-batch.sh --modes vk-raster --out /tmp/tv-batch
#
# For the combined tusdview+tusdrender sweep see
# examples/tusdview/tests/run-usd-assets-disk1.sh (ctest: tusdview-usd-assets-disk1).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

: "${USD_ASSETS_ROOT:=${USD_WG_ASSETS_DIR:-$REPO_ROOT/usd-assets}}"
if [ -d "$USD_ASSETS_ROOT" ]; then
  USD_ASSETS_ROOT="$(cd "$USD_ASSETS_ROOT" && pwd -P)"
fi
: "${TUSDVIEW:=$REPO_ROOT/build/tusdview}"
: "${TUSDVIEW_USD_ASSETS_MODES:=vk-raster,vk-rt}"
: "${TUSDVIEW_USD_ASSETS_ALLOW_PARENT:=1}"
: "${TUSDVIEW_XVFB:=1}"
: "${TUSDVIEW_USD_ASSETS_TIMEOUT:=60s}"
# Curated expectations make known warnings/degradation explicit. Assets outside
# the manifest remain ordinary smoke coverage.
: "${TUSDVIEW_USD_ASSETS_EXPECTATIONS:=$REPO_ROOT/examples/tusdview/tests/usd-assets-expectations.tsv}"
: "${TUSDVIEW_USD_ASSETS_FAIL_ON:=timeout,backend_error,unexpected_degradation,expectation_mismatch}"

export USD_ASSETS_ROOT TUSDVIEW TUSDVIEW_USD_ASSETS_MODES \
  TUSDVIEW_USD_ASSETS_ALLOW_PARENT TUSDVIEW_XVFB TUSDVIEW_USD_ASSETS_TIMEOUT \
  TUSDVIEW_USD_ASSETS_FAIL_ON TUSDVIEW_USD_ASSETS_EXPECTATIONS

exec bash "$REPO_ROOT/examples/tusdview/tests/run-usd-assets-render-smoke.sh" "$@"
