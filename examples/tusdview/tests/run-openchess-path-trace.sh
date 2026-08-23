#!/usr/bin/env bash
# Deterministic OpenChessSet production-render benchmark. This intentionally
# remains opt-in: its final-quality defaults are suitable for a workstation,
# not routine CI. A low-sample smoke can override SIZE/REFERENCE_SPP/CANDIDATE_SPP.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
ASSET="${OPENCHESS_ASSET:-$ROOT/usd-assets/full_assets/OpenChessSet/chess_set.usda}"
METRICS="$SCRIPT_DIR/asset_fingerprint.py"

[ "${TUSDVIEW_RUN_OPENCHESS_PATH:-0}" = "1" ] || {
  echo "SKIP: set TUSDVIEW_RUN_OPENCHESS_PATH=1 for the production benchmark"
  exit "$SKIP"
}
[ -x "$BIN" ] || { echo "SKIP: tusdview not found ($BIN)"; exit "$SKIP"; }
[ -f "$ASSET" ] || { echo "SKIP: OpenChessSet not found ($ASSET)"; exit "$SKIP"; }

SIZE="${TUSDVIEW_OPENCHESS_PT_SIZE:-512x512}"
REFERENCE_SPP="${TUSDVIEW_OPENCHESS_PT_REFERENCE_SPP:-1024}"
CANDIDATE_SPP="${TUSDVIEW_OPENCHESS_PT_CANDIDATE_SPP:-256}"
MAX_DEPTH="${TUSDVIEW_OPENCHESS_PT_MAX_DEPTH:-12}"
SEED="${TUSDVIEW_OPENCHESS_PT_SEED:-1}"
TIMEOUT="${TUSDVIEW_OPENCHESS_PT_TIMEOUT:-1800s}"
NRMSE_MAX="${TUSDVIEW_OPENCHESS_PT_NRMSE_MAX:-0.12}"
MEAN_MAX="${TUSDVIEW_OPENCHESS_PT_MEAN_MAX:-0.08}"
SSIM_MIN="${TUSDVIEW_OPENCHESS_PT_SSIM_MIN:-0.82}"

if [ -n "${TUSDVIEW_OPENCHESS_PT_OUT:-}" ]; then
  OUT="$TUSDVIEW_OPENCHESS_PT_OUT"
  mkdir -p "$OUT"
  CLEAN=0
else
  OUT="$(mktemp -d)"
  CLEAN=1
fi
cleanup() {
  if [ "$CLEAN" = "1" ] && [ "${TUSDVIEW_OPENCHESS_PT_KEEP:-0}" != "1" ]; then
    rm -rf "$OUT"
  fi
}
trap cleanup EXIT

# OpenChessSet intentionally ships without a shot camera or lighting rig. Build
# a tiny referencing layer with deterministic neutral key/fill lights so the
# benchmark exercises the materials rather than comparing black silhouettes.
SCENE="$OUT/openchess-path-rig.usda"
python3 - "$ASSET" "$SCENE" <<'PY'
import pathlib, sys
asset = pathlib.Path(sys.argv[1]).resolve().as_posix().replace("@", "%40")
text = '''#usda 1.0
(
    defaultPrim = "Scene"
    metersPerUnit = 1
    upAxis = "Y"
)
def Xform "Scene"
{
    def Xform "ChessSet" (prepend references = @%s@) {}
    def Scope "Lights"
    {
        def DistantLight "Key"
        {
            color3f inputs:color = (1.0, 0.93, 0.82)
            float inputs:intensity = 24.0
            float inputs:angle = 5.0
            double3 xformOp:rotateXYZ = (-42, -32, 0)
            uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
        }
        def DistantLight "Fill"
        {
            color3f inputs:color = (0.66, 0.78, 1.0)
            float inputs:intensity = 8.0
            float inputs:angle = 8.0
            double3 xformOp:rotateXYZ = (-24, 148, 0)
            uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
        }
        def DomeLight "World"
        {
            color3f inputs:color = (0.34, 0.25, 0.18)
            float inputs:intensity = 0.35
            float inputs:exposure = 0.0
        }
    }
}
''' % asset
pathlib.Path(sys.argv[2]).write_text(text, encoding="utf-8")
PY

VK_DEVICE_ARGS=()
[ -z "${TUSDVIEW_VK_DEVICE:-}" ] || VK_DEVICE_ARGS=(--vk-device "$TUSDVIEW_VK_DEVICE")
COMMON=(--headless --backend vk --path-trace --pt-quality final
        --pt-max-depth "$MAX_DEPTH" --pt-rr-depth 5 --pt-seed "$SEED"
        --pt-denoise off --pt-variance 0 --pt-motion-segments 8 --no-grid --size "$SIZE"
        --view-dir -0.62,-0.42,-0.66 --cam-dolly 0.78
        --f-stop 4 --focus-distance 0.72)

run_render() {
  local name="$1" spp="$2" backend="$3"
  local backend_args=()
  case "$backend" in
    vk) backend_args=("${VK_DEVICE_ARGS[@]}") ;;
    cuda) backend_args=(--cuda) ;;
    hip) backend_args=(--hip) ;;
    *) echo "FAIL: unknown backend $backend"; return 1 ;;
  esac
  echo "OpenChessSet path trace: backend=$backend spp=$spp size=$SIZE"
  timeout --kill-after=10s "$TIMEOUT" "$BIN" "${COMMON[@]}" \
    "${backend_args[@]}" --pt-samples "$spp" \
    --screenshot "$OUT/$name.png" --linear-output "$OUT/$name.exr" \
    --render-report "$OUT/$name.json" "$SCENE" >"$OUT/$name.log" 2>&1 || {
      echo "FAIL: $backend path trace failed"
      tail -80 "$OUT/$name.log"
      return 1
    }
  [ -s "$OUT/$name.png" ] && [ -s "$OUT/$name.exr" ] &&
    [ -s "$OUT/$name.json" ] || {
      echo "FAIL: $backend did not produce PNG, EXR, and report"; return 1;
    }
  if grep -Eq 'unsupported_mtlx=[1-9]|missing_textures=[1-9]|degraded_materials=[1-9]' \
      "$OUT/$name.log"; then
    echo "FAIL: $backend degraded an OpenChessSet material or texture"
    grep -E 'load summary:' "$OUT/$name.log" || true
    return 1
  fi
  python3 - "$OUT/$name.json" "$spp" "$backend" <<'PY'
import json, sys
report = json.load(open(sys.argv[1], encoding="utf-8"))
render = report.get("render", {})
backend = report.get("backend", {})
diagnostics = report.get("load_diagnostics", {})
assert report.get("schema_version", 0) >= 2, "old render report schema"
assert render.get("integrator") == "path", "production integrator not active"
assert render.get("target_samples") == int(sys.argv[2]), "sample target mismatch"
assert render.get("max_depth", 0) >= 1, "invalid path depth"
assert render.get("motion_segments") == 8, "motion segment setting lost"
assert not render.get("rt_build_incomplete", False), "incomplete RT build"
if backend.get("device_type") == "cpu" and \
        __import__("os").environ.get("TUSDVIEW_OPENCHESS_ALLOW_CPU") != "1":
    raise AssertionError("OpenChess benchmark selected a CPU Vulkan device; "
                         "select a GPU with TUSDVIEW_VK_DEVICE or explicitly "
                         "set TUSDVIEW_OPENCHESS_ALLOW_CPU=1")
for key in ("degraded_materials", "missing_textures", "unsupported_mtlx"):
    assert diagnostics.get(key, 0) == 0, "%s=%s" % (key, diagnostics.get(key))
print("validated report:", sys.argv[3], render.get("samples"), "samples")
PY
}

compare_to_reference() {
  local name="$1"
  local json
  json="$(python3 "$METRICS" metrics "$OUT/reference.png" "$OUT/$name.png")" || return 1
  echo "$name metrics: $json"
  python3 - "$json" "$NRMSE_MAX" "$MEAN_MAX" "$SSIM_MIN" <<'PY'
import json, sys
m = json.loads(sys.argv[1])
limits = tuple(float(v) for v in sys.argv[2:])
assert m["normalized_rmse"] <= limits[0], m
assert m["relative_mean_error"] <= limits[1], m
assert m["ssim"] >= limits[2], m
PY
}

run_render reference "$REFERENCE_SPP" vk || exit 1
run_render vk "$CANDIDATE_SPP" vk || exit 1
compare_to_reference vk || { echo "FAIL: Vulkan candidate missed quality thresholds"; exit 1; }

BACKENDS="${TUSDVIEW_OPENCHESS_PT_BACKENDS:-auto}"
if [ "$BACKENDS" = "auto" ]; then
  BACKENDS=""
  command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1 && BACKENDS="cuda"
  if [ -e /dev/kfd ] && [ -d /opt/rocm ]; then BACKENDS="${BACKENDS:+$BACKENDS,}hip"; fi
fi
for backend in ${BACKENDS//,/ }; do
  [ "$backend" != "none" ] || continue
  [ -n "$backend" ] || continue
  run_render "$backend" "$CANDIDATE_SPP" "$backend" || exit 1
  compare_to_reference "$backend" || {
    echo "FAIL: $backend candidate missed quality thresholds"; exit 1;
  }
done

echo "PASS: OpenChessSet production path benchmark ($OUT)"
