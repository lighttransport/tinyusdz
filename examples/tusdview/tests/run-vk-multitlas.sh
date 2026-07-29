#!/usr/bin/env bash
# Force tiny TLAS chunks and require exact parity with ordinary single-TLAS RT.
set -uo pipefail

viewer="${TUSDVIEW:-build_ninja/tusdview}"
root="${TUSDVIEW_ROOT:-.}"
out="$(mktemp -d "${TMPDIR:-/tmp}/tusdview-multitlas.XXXXXX")"
trap 'rm -rf "$out"' EXIT
scene="$root/examples/tusdview/tests/deform-pointinstancer.usda"

if [[ ! -x "$viewer" ]]; then
  echo "FAIL: tusdview executable not found: $viewer" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "SKIP: python3 unavailable"
  exit 77
fi

render() {
  local mode="$1" variant="$2"
  local image="$out/$mode-$variant.ppm"
  local log="$out/$mode-$variant.log"
  local -a prefix=()
  [[ "$variant" == multi ]] &&
    prefix=(env TUSDVIEW_RT_TLAS_CHUNK_INSTANCES=2)
  if ! timeout 90s "${prefix[@]}" "$viewer" --headless --backend vk --rt \
      --next --frames 1 --size 256x192 --camera /root/Cam --mode "$mode" \
      --screenshot "$image" "$scene" >"$log" 2>&1; then
    if grep -Eq 'ray tracing is unavailable|no compatible Vulkan|no Vulkan device|renderer init failed' "$log"; then
      echo "SKIP: Vulkan ray-query unavailable"
      exit 77
    fi
    echo "FAIL: $mode $variant render failed" >&2
    cat "$log" >&2
    return 1
  fi
  [[ -s "$image" ]] || { echo "FAIL: missing $image" >&2; return 1; }
  if [[ "$variant" == multi ]] &&
     ! grep -q 'non-truncating multi-TLAS: 3 instances in 2 chunks' "$log"; then
    echo "FAIL: forced multi-TLAS path was not used" >&2
    cat "$log" >&2
    return 1
  fi
}

for mode in shaded instance-id ao soft-shadow; do
  render "$mode" single
  render "$mode" multi
  if ! cmp -s "$out/$mode-single.ppm" "$out/$mode-multi.ppm"; then
    echo "FAIL: $mode differs between single and forced multi-TLAS" >&2
    exit 1
  fi
done

# Capacity/allocation failures must be explicit degraded output, never silently
# truncate the TLAS. Force capacity to two for this three-instance fixture.
capacity_log="$out/capacity.log"
capacity_report="$out/capacity.json"
if ! timeout 90s env TUSDVIEW_RT_TLAS_CHUNK_INSTANCES=2 \
    TUSDVIEW_RT_TLAS_MAX_CHUNKS=1 "$viewer" --headless --backend vk --rt \
    --next --frames 1 --size 64x64 --camera /root/Cam \
    --screenshot "$out/capacity.ppm" --render-report "$capacity_report" \
    "$scene" >"$capacity_log" 2>&1; then
  echo "FAIL: forced capacity diagnostic run failed" >&2
  cat "$capacity_log" >&2
  exit 1
fi
grep -q 'refusing a partial render' "$capacity_log" || {
  echo "FAIL: missing non-truncating capacity diagnostic" >&2
  cat "$capacity_log" >&2
  exit 1
}
python3 - "$capacity_report" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as f:
    report = json.load(f)
render = report["render"]
assert render["rt_build_incomplete"] is True, render
assert render["truncated"] is True, render
assert render["rt_input_instances"] == 3, render
assert render["tlas_chunks"] == 0, render
assert any("acceleration structure build incomplete" in reason
           for reason in report["degradation_reasons"]), report
PY

echo "PASS: single/multi-TLAS primary, identity, AO, and shadow parity"
