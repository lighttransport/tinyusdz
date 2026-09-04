#!/usr/bin/env bash
# End-to-end OptiX gate. Missing NVIDIA/OptiX hardware is a CTest skip (77);
# once the transport is available, selection, reporting, fallback, and animated
# GAS updates are hard failures.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDVIEW="${LUSDVIEW:-$REPO_ROOT/build_ninja/lusdview}"
STATIC_ASSET="${STATIC_ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}"
ANIM_ASSET="${ANIM_ASSET:-$REPO_ROOT/models/skintest-animated.usda}"

if [ ! -x "$LUSDVIEW" ] || [ ! -f "$STATIC_ASSET" ]; then
  echo "SKIP: lusdview or OptiX test asset is unavailable"
  exit $SKIP
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

run_static() {
  local mode="$1" backend="$2" stem="$3"
  "$LUSDVIEW" --headless --cuda --cuda-rt-backend "$backend" \
    --mode "$mode" --frames 1 --size 96x96 --view-dir 0,0,-1 \
    --screenshot "$TMP/$stem.ppm" --render-report "$TMP/$stem.json" \
    "$STATIC_ASSET" 2>&1
}

echo "=== forced OptiX geometric-normal frame ==="
optix_log="$(run_static geom-normal optix optix)"
echo "$optix_log"
if ! grep -q "CUDA RT wrote" <<<"$optix_log"; then
  if grep -Eq "OptiX.*(unavailable|not built|pipeline/IAS unavailable)|CUDA ray tracing unavailable" \
      <<<"$optix_log"; then
    echo "SKIP: OptiX transport is unavailable on this host"
    exit $SKIP
  fi
  echo "FAIL: forced OptiX did not render"
  exit 1
fi

echo "=== automatic unsupported-AOV fallback ==="
fallback_log="$(run_static ao auto fallback)"
echo "$fallback_log"
if ! grep -q "CUDA RT wrote" <<<"$fallback_log"; then
  echo "FAIL: CUDA software fallback did not render"
  exit 1
fi

echo "=== explicit OptiX-off frame ==="
off_log="$("$LUSDVIEW" --headless --no-optix --mode material-id --frames 1 \
  --size 96x96 --view-dir 0,0,-1 --screenshot "$TMP/off.ppm" \
  --render-report "$TMP/off.json" "$STATIC_ASSET" 2>&1)"
echo "$off_log"
if ! grep -q "CUDA RT wrote" <<<"$off_log"; then
  echo "FAIL: --no-optix did not render through the CUDA software BVH"
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "FAIL: python3 is required to validate render-report JSON"
  exit 1
fi
python3 - "$TMP/optix.json" "$TMP/fallback.json" "$TMP/off.json" <<'PY'
import json, pathlib, sys
optix = json.loads(pathlib.Path(sys.argv[1]).read_text())["backend"]
fallback = json.loads(pathlib.Path(sys.argv[2]).read_text())["backend"]
off = json.loads(pathlib.Path(sys.argv[3]).read_text())["backend"]
required = {"optix_abi", "optix_gas_bytes", "optix_ias_bytes",
            "optix_acceleration_bytes", "fallback_reason",
            "transport", "transport_requested"}
if not required.issubset(optix) or not required.issubset(fallback):
    raise SystemExit("missing OptiX report fields")
if optix["transport"] != "optix" or not optix["optix_available"]:
    raise SystemExit("forced OptiX report did not select OptiX")
if optix["optix_abi"] <= 0 or optix["optix_acceleration_bytes"] <= 0:
    raise SystemExit("invalid OptiX ABI or acceleration footprint")
if fallback["transport"] != "software-bvh" or not fallback["fallback_reason"]:
    raise SystemExit("automatic fallback was not reported")
if off["transport"] != "software-bvh" or off["transport_requested"] != "software":
    raise SystemExit("--no-optix did not force and report software transport")
PY

if command -v xvfb-run >/dev/null 2>&1 && [ -f "$ANIM_ASSET" ]; then
  echo "=== animated update-capable GAS ==="
  anim_log="$(xvfb-run -a -s '-screen 0 640x480x24' "$LUSDVIEW" \
    --cuda --cuda-rt-backend optix --play --frames 40 --size 320x240 \
    "$ANIM_ASSET" 2>&1)"
  echo "$anim_log"
  if ! grep -q "OptiX refit active:" <<<"$anim_log"; then
    echo "FAIL: animated OptiX GAS update was not exercised"
    exit 1
  fi
else
  echo "NOTE: animated OptiX refit subtest skipped (Xvfb/asset unavailable)"
fi

echo "PASS: OptiX on/off, AOV, fallback, reporting, and refit integration"
