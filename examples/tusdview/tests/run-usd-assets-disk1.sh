#!/usr/bin/env bash
#
# Convenience wrapper: batch-render the local usd-wg/assets checkout at
# /mnt/disk1/work/usd-assets through BOTH tusdview and tusdrender.
#
# This just bakes the asset root, both-tool mode set, and sane batch defaults
# into run-usd-assets-render-smoke.sh (the real harness). Any extra args are
# forwarded to that harness, so you can override e.g. --modes / --limit /
# --timeout / --out. Examples:
#
#   examples/tusdview/tests/run-usd-assets-disk1.sh
#   examples/tusdview/tests/run-usd-assets-disk1.sh --limit 20
#   examples/tusdview/tests/run-usd-assets-disk1.sh --modes vk-rt,tusdr-vk --out /tmp/batch
#
# Same test is registered in ctest as `tusdview-usd-assets-disk1-smoke`
# (opt-in: TUSDVIEW_USD_ASSETS_DISK1=1 ctest -R tusdview-usd-assets-disk1).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${USD_ASSETS_ROOT:=/mnt/disk1/work/usd-assets}"
: "${TUSDVIEW:=$REPO_ROOT/build/tusdview}"
: "${TUSDRENDER:=$REPO_ROOT/build/tools/tusdrender/tusdrender}"
: "${TUSDVIEW_USD_ASSETS_MODES:=vk-raster,vk-rt,tusdr-cpu,tusdr-vk}"
: "${TUSDVIEW_XVFB:=1}"
: "${TUSDVIEW_USD_ASSETS_TIMEOUT:=60s}"
# Report unsupported/empty assets, but only fail on crashes/hangs/backend breakage.
: "${TUSDVIEW_USD_ASSETS_FAIL_ON:=timeout,backend_error}"

export USD_ASSETS_ROOT TUSDVIEW TUSDRENDER TUSDVIEW_USD_ASSETS_MODES \
  TUSDVIEW_XVFB TUSDVIEW_USD_ASSETS_TIMEOUT TUSDVIEW_USD_ASSETS_FAIL_ON

exec bash "$SCRIPT_DIR/run-usd-assets-render-smoke.sh" "$@"
