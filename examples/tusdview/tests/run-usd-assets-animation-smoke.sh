#!/usr/bin/env bash
# Render representative animated usd-wg/assets samples at two time codes and
# require each available backend to produce at least one changed image.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${USD_ASSETS_ROOT:-$(cd "$SCRIPT_DIR/../../.." && pwd)/usd-assets}"
if [ -d "$ROOT" ]; then
  ROOT="$(cd "$ROOT" && pwd -P)"
fi
OUT="${TUSDVIEW_USD_ASSETS_ANIM_OUT:-}"
MODES="${TUSDVIEW_USD_ASSETS_ANIM_MODES:-vk-raster,tusdr-cpu}"
TIME_A="${TUSDVIEW_USD_ASSETS_TIME_A:-0}"
TIME_B="${TUSDVIEW_USD_ASSETS_TIME_B:-1}"
SKIP=77

if [ -z "$ROOT" ] || [ ! -d "$ROOT" ]; then
  echo "SKIP: USD_ASSETS_ROOT is not available"
  exit "$SKIP"
fi
if [ -n "${TUSDVIEW_USD_ASSETS_GATE:-}" ] &&
   [ -z "${!TUSDVIEW_USD_ASSETS_GATE:-}" ]; then
  echo "SKIP: gate variable '$TUSDVIEW_USD_ASSETS_GATE' is not set"
  exit "$SKIP"
fi

if [ -z "$OUT" ]; then
  OUT="$(mktemp -d)"
  CLEAN_OUT=1
else
  mkdir -p "$OUT"
  CLEAN_OUT=0
fi
trap '[ "$CLEAN_OUT" -eq 1 ] && rm -rf "$OUT"' EXIT

FILES='test_assets/USDZ/AnimatedCube/AnimatedCube.usdz
test_assets/USDZ/AnimatedTriangle/AnimatedTriangle.usdz
test_assets/USDZ/BoxAnimated/BoxAnimated.usdz
test_assets/USDZ/InterpolationTest/InterpolationTest.usdz
test_assets/USDZ/CesiumMan/CesiumMan.usdz
test_assets/USDZ/RiggedFigure/RiggedFigure.usdz
test_assets/USDZ/RiggedSimple/RiggedSimple.usdz'

run_time() {
  local time="$1" out="$2"
  TUSDVIEW_USD_ASSETS_FILES="$FILES" \
  TUSDVIEW_USD_ASSETS_FAIL_ON=timeout,backend_error,unexpected_degradation,expectation_mismatch \
    bash "$SCRIPT_DIR/run-usd-assets-render-smoke.sh" \
      --root "$ROOT" --out "$out" --modes "$MODES" --time "$time"
}

run_time "$TIME_A" "$OUT/time-a"
run_time "$TIME_B" "$OUT/time-b"

python3 - "$OUT/time-a/results.tsv" "$OUT/time-b/results.tsv" \
  "$SCRIPT_DIR/asset_fingerprint.py" "$OUT/animation-summary.tsv" <<'PY'
import csv
import subprocess
import sys

def rows(path):
    with open(path, newline="") as f:
        return {(r["mode"], r["asset"]): r
                for r in csv.DictReader(f, delimiter="\t")}

a = rows(sys.argv[1])
b = rows(sys.argv[2])
fingerprint = sys.argv[3]
summary_path = sys.argv[4]
rendered = {"rendered", "rendered_with_warnings", "degraded_material"}
counts = {}
lines = []
for key in sorted(set(a) & set(b)):
    ra, rb = a[key], b[key]
    if ra["status"] not in rendered or rb["status"] not in rendered:
        continue
    try:
        fa = subprocess.check_output(
            [sys.executable, fingerprint, "hash", ra["image"]], text=True).strip()
        fb = subprocess.check_output(
            [sys.executable, fingerprint, "hash", rb["image"]], text=True).strip()
    except subprocess.CalledProcessError:
        continue
    changed = fa != fb
    mode = key[0]
    counts.setdefault(mode, [0, 0])
    counts[mode][changed] += 1
    lines.append((mode, "changed" if changed else "unchanged", key[1]))

with open(summary_path, "w") as f:
    f.write("mode\tstatus\tasset\n")
    for line in lines:
        f.write("\t".join(line) + "\n")

failed = False
for mode in sorted(counts):
    unchanged, changed = counts[mode]
    print(f"{mode}: {changed} changed, {unchanged} unchanged")
    if changed == 0:
        print(f"FAIL: {mode} produced no animation change", file=sys.stderr)
        failed = True
if not counts:
    print("SKIP: no animation samples rendered at both times")
    sys.exit(77)
sys.exit(1 if failed else 0)
PY

echo "PASS: usd-assets animation changed across time codes $TIME_A and $TIME_B"
echo "summary: $OUT/animation-summary.tsv"
