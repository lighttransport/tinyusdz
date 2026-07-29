#!/usr/bin/env bash
# Repeatable opt-in production captures. No Island path or output is checked in.
set -uo pipefail

viewer="${TUSDVIEW:-build_ninja/tusdview}"
scene="${ISLAND_USD:-}"
camera="${ISLAND_CAMERA:-}"
out_dir="${ISLAND_CAPTURE_DIR:-/tmp/tusdview-island-captures}"
sizes="${ISLAND_CAPTURE_SIZES:-1280x720}"
backends="${ISLAND_CAPTURE_BACKENDS:-gl vk vk-rt}"
capture_timeout="${ISLAND_CAPTURE_TIMEOUT:-600s}"

if [[ -z "$scene" || ! -f "$scene" ]]; then
  echo "SKIP: set ISLAND_USD to the external Island root layer"
  exit 77
fi
if [[ ! -x "$viewer" ]]; then
  echo "FAIL: tusdview executable not found: $viewer" >&2
  exit 1
fi

mkdir -p "$out_dir"
common=(--next --frames 2 --load-payloads --texture-compress auto
        --large-scene-profile island --timing --no-grid --vram-budget 12)
if [[ -n "$camera" ]]; then common+=(--camera "$camera"); fi

run_capture() {
  local backend="$1" size="$2" stem="$out_dir/${backend}-${size}"
  local -a mode=()
  case "$backend" in
    gl) mode=(--backend gl) ;;
    vk) mode=(--backend vk --headless) ;;
    vk-rt) mode=(--backend vk --headless --rt --no-rt-lod) ;;
    cuda) mode=(--backend vk --headless --cuda --rt-samples "${ISLAND_RT_SAMPLES:-1}") ;;
    *) echo "FAIL: unknown backend '$backend'" >&2; return 1 ;;
  esac
  local -a cmd=("$viewer" "${mode[@]}" "${common[@]}" --size "$size"
                --screenshot "${stem}.png" --render-report "${stem}.json"
                "$scene")
  echo "CAPTURE: $backend $size"
  if [[ "$backend" == gl ]]; then
    timeout --kill-after=10s "$capture_timeout" \
      env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
      /usr/bin/time -v xvfb-run -a "${cmd[@]}" >"${stem}.log" 2>&1
  else
    timeout --kill-after=10s "$capture_timeout" \
      env __NV_PRIME_RENDER_OFFLOAD=1 \
      /usr/bin/time -v "${cmd[@]}" >"${stem}.log" 2>&1
  fi
  local rc=$?
  if [[ $rc -ne 0 ]]; then
    echo "FAIL: $backend $size exited with $rc (see ${stem}.log)" >&2
    return 1
  fi
  if [[ ! -s "${stem}.png" || ! -s "${stem}.json" ]]; then
    echo "FAIL: $backend $size did not produce screenshot/report" >&2
    return 1
  fi
  echo "PASS: $backend $size"
}

failures=0
for size in $sizes; do
  for backend in $backends; do
    run_capture "$backend" "$size" || failures=$((failures + 1))
  done
done

if [[ $failures -ne 0 ]]; then
  echo "FAIL: $failures Island capture(s) failed; logs/reports are in $out_dir" >&2
  exit 1
fi
echo "OK: Island captures and JSON reports are in $out_dir"
