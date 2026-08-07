#!/usr/bin/env bash
#
# Batch-render a USD asset corpus through tusdrender only (CPU ray preview +
# Vulkan). Thin wrapper over the shared harness
# examples/tusdview/tests/run-usd-assets-render-smoke.sh: it bakes tusdrender-only
# modes and sane batch defaults, and forwards any extra args (--root / --limit /
# --modes / --timeout / --out / --golden ...).
#
# Corpus: set USD_ASSETS_ROOT (or --root DIR). Defaults to the local
# usd-wg/assets checkout at /mnt/disk1/work/usd-assets; the harness SKIPs if it
# does not exist. tusdrender's next loader allows '..' parent-relative
# references by default, so no allow-parent-paths flag is needed here.
#
# Modes: tusdr-cpu (-rtPreview), tusdr-vk (-vk), tusdr-vkr (-vkr).
#
# Examples:
#   tests/tusdrender/run-usd-assets-batch.sh
#   tests/tusdrender/run-usd-assets-batch.sh --root ~/usd-assets --limit 30
#   tests/tusdrender/run-usd-assets-batch.sh --modes tusdr-cpu --out /tmp/tr-batch
#
# For the combined tusdview+tusdrender sweep see
# examples/tusdview/tests/run-usd-assets-disk1.sh (ctest: tusdview-usd-assets-disk1).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

: "${USD_ASSETS_ROOT:=${USD_WG_ASSETS_DIR:-}}"
: "${TUSDRENDER:=$REPO_ROOT/build/tools/tusdrender/tusdrender}"
# The shared harness SKIPs when the tusdview binary is absent (even for
# tusdrender-only modes), so point TUSDVIEW at the build path too; it is never
# invoked for tusdr-* modes.
: "${TUSDVIEW:=$REPO_ROOT/build/tusdview}"
: "${TUSDVIEW_USD_ASSETS_MODES:=tusdr-cpu,tusdr-vk}"
: "${TUSDVIEW_XVFB:=1}"
: "${TUSDVIEW_USD_ASSETS_TIMEOUT:=60s}"
# Report unsupported/empty assets, but only fail on crashes/hangs/backend breakage.
: "${TUSDVIEW_USD_ASSETS_FAIL_ON:=timeout,backend_error}"

export USD_ASSETS_ROOT TUSDRENDER TUSDVIEW TUSDVIEW_USD_ASSETS_MODES \
  TUSDVIEW_XVFB TUSDVIEW_USD_ASSETS_TIMEOUT TUSDVIEW_USD_ASSETS_FAIL_ON

exec bash "$REPO_ROOT/examples/tusdview/tests/run-usd-assets-render-smoke.sh" "$@"
