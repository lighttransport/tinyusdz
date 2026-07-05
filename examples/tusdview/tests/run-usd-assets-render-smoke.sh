#!/usr/bin/env bash
#
# Batch smoke render USD-family assets with tusdview.
#
# This is intentionally external-data gated: set USD_ASSETS_ROOT to a local
# usd-wg/assets checkout or another USD asset suite. Without it, the script
# SKIPs so normal ctest runs do not depend on large assets.
#
# Each (mode, asset) is classified into a bucket: load_error, timeout,
# backend_unavailable, backend_error, no_renderable, rendered_with_warnings,
# rendered, or golden_mismatch. Results are written as TSV (results.tsv) and
# JSON (results.json). With a golden baseline (--golden / TUSDVIEW_USD_ASSETS_
# GOLDEN), each successful render is fingerprinted (coarse 12x12x3 quantized
# grid) and compared, catching gross visual regressions the non-blank check
# cannot. Record/refresh a baseline with --update-golden.
#
# Backends:
#   vk-raster : tusdview --headless --backend vk
#   vk-rt     : tusdview --headless --backend vk --rt
#   cuda-rt   : tusdview --headless --cuda
#   tusdr-cpu : tusdrender -rtPreview
#   tusdr-vk  : tusdrender -vk
#   tusdr-vkr : tusdrender -vkr
#
# Useful NVIDIA hybrid/offload env:
#   TUSDVIEW_NVIDIA_OFFLOAD=1 TUSDVIEW_XVFB=1 TUSDVIEW_VK_DEVICE=nvidia \
#     USD_ASSETS_ROOT=/path/to/assets examples/tusdview/tests/run-usd-assets-render-smoke.sh
#
# Exit codes: 0 = pass, 1 = one or more hard failures, 77 = skip.
set -uo pipefail

SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

if [ -n "${TUSDVIEW:-}" ]; then
  TUSDVIEW_BIN="$TUSDVIEW"
elif [ -x "$REPO_ROOT/build_ninja/tusdview" ]; then
  TUSDVIEW_BIN="$REPO_ROOT/build_ninja/tusdview"
else
  TUSDVIEW_BIN="$REPO_ROOT/build/tusdview"
fi
if [ -n "${TUSDRENDER:-}" ]; then
  TUSDRENDER_BIN="$TUSDRENDER"
elif [ -x "$REPO_ROOT/build_ninja/tools/tusdrender/tusdrender" ]; then
  TUSDRENDER_BIN="$REPO_ROOT/build_ninja/tools/tusdrender/tusdrender"
else
  TUSDRENDER_BIN="$REPO_ROOT/build/tools/tusdrender/tusdrender"
fi

USD_ASSETS_ROOT="${USD_ASSETS_ROOT:-}"
OUT_DIR="${TUSDVIEW_USD_ASSETS_OUT:-}"
LIMIT="${TUSDVIEW_USD_ASSETS_LIMIT:-0}"
FRAMES="${TUSDVIEW_USD_ASSETS_FRAMES:-4}"
SIZE="${TUSDVIEW_USD_ASSETS_SIZE:-256x256}"
WIDTH="${SIZE%x*}"
HEIGHT="${SIZE#*x}"
TIMEOUT_DUR="${TUSDVIEW_USD_ASSETS_TIMEOUT:-45s}"
MODES="${TUSDVIEW_USD_ASSETS_MODES:-vk-raster,vk-rt,cuda-rt,tusdr-cpu,tusdr-vk,tusdr-vkr}"
VK_DEVICE="${TUSDVIEW_VK_DEVICE:-}"
NVIDIA_OFFLOAD="${TUSDVIEW_NVIDIA_OFFLOAD:-auto}"
USE_XVFB="${TUSDVIEW_XVFB:-auto}"
FAIL_ON="${TUSDVIEW_USD_ASSETS_FAIL_ON:-load_error,timeout,backend_error}"
# Golden-fingerprint regression layer (opt-in). Point GOLDEN at a TSV baseline
# (mode<TAB>asset<TAB>fingerprint). With GOLDEN_UPDATE=1 the baseline is
# (re)written from this run instead of compared. GOLDEN_TOL is the max L1
# distance (sum of per-nibble deltas over a 12x12x3 quantized grid, range
# 0..6480) tolerated before a render is downgraded to golden_mismatch.
GOLDEN_FILE="${TUSDVIEW_USD_ASSETS_GOLDEN:-}"
GOLDEN_UPDATE="${TUSDVIEW_USD_ASSETS_GOLDEN_UPDATE:-0}"
GOLDEN_TOL="${TUSDVIEW_USD_ASSETS_GOLDEN_TOL:-160}"
FINGERPRINT_PY="$SCRIPT_DIR/asset_fingerprint.py"
# JSON mirror of results.tsv (default on; set to a path or empty to disable).
JSON_OUT="${TUSDVIEW_USD_ASSETS_JSON:-auto}"

usage() {
  cat <<EOF
Usage: USD_ASSETS_ROOT=/path/to/assets $0 [options]

Options:
  --root DIR       Asset root (same as USD_ASSETS_ROOT)
  --out DIR        Output dir (default: temporary dir)
  --limit N        Limit discovered USD files (default: all)
  --modes LIST     Comma list: vk-raster,vk-rt,cuda-rt,tusdr-cpu,tusdr-vk,tusdr-vkr
  --size WxH       tusdview --size (default: 256x256)
  --frames N       tusdview --frames (default: 4)
  --timeout DUR    Per-render timeout(1) duration (default: 45s)
  --vk-device DEV  Forward --vk-device DEV to Vulkan modes
  --fail-on LIST   Comma statuses that make the script fail
  --golden FILE    Compare rendered fingerprints against a golden TSV baseline
  --update-golden FILE  (Re)write the golden baseline from this run
  --golden-tol N   Max fingerprint L1 distance before golden_mismatch (default 160)
  --json FILE      Also write results as JSON (default: <out>/results.json)

Environment:
  TUSDVIEW=/path/to/tusdview
  TUSDRENDER=/path/to/tusdrender
  TUSDVIEW_NVIDIA_OFFLOAD=auto|1|0
  TUSDVIEW_XVFB=auto|1|0|external
  TUSDVIEW_USD_ASSETS_GOLDEN=FILE  TUSDVIEW_USD_ASSETS_GOLDEN_UPDATE=1
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --root) USD_ASSETS_ROOT="$2"; shift 2 ;;
    --out) OUT_DIR="$2"; shift 2 ;;
    --limit) LIMIT="$2"; shift 2 ;;
    --modes) MODES="$2"; shift 2 ;;
    --size) SIZE="$2"; shift 2 ;;
    --frames) FRAMES="$2"; shift 2 ;;
    --timeout) TIMEOUT_DUR="$2"; shift 2 ;;
    --vk-device) VK_DEVICE="$2"; shift 2 ;;
    --fail-on) FAIL_ON="$2"; shift 2 ;;
    --golden) GOLDEN_FILE="$2"; shift 2 ;;
    --update-golden) GOLDEN_FILE="$2"; GOLDEN_UPDATE=1; shift 2 ;;
    --golden-tol) GOLDEN_TOL="$2"; shift 2 ;;
    --json) JSON_OUT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -x "$TUSDVIEW_BIN" ]; then
  echo "SKIP: tusdview binary not found at $TUSDVIEW_BIN (set TUSDVIEW=...)"
  exit "$SKIP"
fi

if [ -z "$USD_ASSETS_ROOT" ]; then
  echo "SKIP: USD_ASSETS_ROOT is not set"
  exit "$SKIP"
fi
if [ ! -d "$USD_ASSETS_ROOT" ]; then
  echo "SKIP: USD_ASSETS_ROOT does not exist: $USD_ASSETS_ROOT"
  exit "$SKIP"
fi

# Opt-in gate: if TUSDVIEW_USD_ASSETS_GATE names an env var and that var is
# empty/unset, SKIP. Golden fingerprints are a per-machine baseline, so the
# checked-in golden ctest gates on TUSDVIEW_RUN_GOLDEN to avoid false failures
# on CI GPUs that differ from the machine that recorded the baseline.
GATE_VAR="${TUSDVIEW_USD_ASSETS_GATE:-}"
if [ -n "$GATE_VAR" ] && [ -z "${!GATE_VAR:-}" ]; then
  echo "SKIP: gate variable '$GATE_VAR' is not set (opt-in test)"
  exit "$SKIP"
fi

if [ -z "$OUT_DIR" ]; then
  OUT_DIR="$(mktemp -d)"
  CLEAN_OUT=1
else
  mkdir -p "$OUT_DIR"
  CLEAN_OUT=0
fi
trap '[ "$CLEAN_OUT" -eq 1 ] && rm -rf "$OUT_DIR"' EXIT

RESULTS="$OUT_DIR/results.tsv"
: > "$RESULTS"
printf 'mode\tstatus\tgolden\tasset\timage\tlog\n' >> "$RESULTS"

# Golden baseline handling. In compare mode, load mode+asset -> fingerprint into
# an associative array. In update mode, freshly computed fingerprints are
# collected and written out at the end.
GOLDEN_ENABLED=0
declare -A GOLDEN_EXPECT
NEW_GOLDEN="$OUT_DIR/new-golden.tsv"
: > "$NEW_GOLDEN"
if [ -n "$GOLDEN_FILE" ]; then
  if [ ! -x "$FINGERPRINT_PY" ] && ! command -v python3 >/dev/null 2>&1; then
    echo "WARN: golden requested but python3/asset_fingerprint.py unavailable; disabling golden" >&2
  else
    GOLDEN_ENABLED=1
    if [ "$GOLDEN_UPDATE" != "1" ] && [ -f "$GOLDEN_FILE" ]; then
      while IFS=$'\t' read -r g_mode g_asset g_fp; do
        [ -z "$g_mode" ] && continue
        case "$g_mode" in \#*) continue ;; esac
        GOLDEN_EXPECT["$g_mode|$g_asset"]="$g_fp"
      done < "$GOLDEN_FILE"
    fi
  fi
fi

has_status() {
  case ",$1," in
    *",$2,"*) return 0 ;;
    *) return 1 ;;
  esac
}

should_use_nvidia_offload() {
  case "$NVIDIA_OFFLOAD" in
    1|ON|on|true|TRUE|yes|YES) return 0 ;;
    0|OFF|off|false|FALSE|no|NO) return 1 ;;
  esac
  [ -f /usr/share/glvnd/egl_vendor.d/10_nvidia.json ] && command -v nvidia-smi >/dev/null 2>&1
}

should_use_xvfb() {
  case "$USE_XVFB" in
    1|ON|on|true|TRUE|yes|YES) return 0 ;;
    0|OFF|off|false|FALSE|no|NO) return 1 ;;
    external|EXTERNAL) return 0 ;;
  esac
  should_use_nvidia_offload && command -v xvfb-run >/dev/null 2>&1
}

use_external_xvfb() {
  case "$USE_XVFB" in
    external|EXTERNAL) return 0 ;;
    *) return 1 ;;
  esac
}

nonblank_ppm() {
  local img="$1"
  python3 - "$img" <<'PY'
import sys

try:
    data = open(sys.argv[1], "rb").read()
except OSError:
    sys.exit(2)

if not data.startswith(b"P6"):
    sys.exit(2)

i = 2
tokens = []
while len(tokens) < 3 and i < len(data):
    c = data[i]
    if c == 35:  # '#'
        while i < len(data) and data[i] not in (10, 13):
            i += 1
    elif chr(c).isspace():
        i += 1
    else:
        start = i
        while i < len(data) and not chr(data[i]).isspace():
            i += 1
        tokens.append(data[start:i])
if len(tokens) != 3:
    sys.exit(2)
# PPM has exactly one whitespace byte after maxval. Do not skip an arbitrary
# whitespace run here: binary pixel data may legitimately start with bytes such
# as 0x09, 0x0a, 0x0d, 0x1c..0x1f that Python classifies as whitespace.
if i < len(data) and chr(data[i]).isspace():
    i += 1

w, h, maxv = [int(x) for x in tokens]
if maxv != 255:
    sys.exit(2)
px = data[i:i + w * h * 3]
if len(px) < w * h * 3:
    sys.exit(2)
first = px[:3]
for p in range(3, len(px), 3):
    if px[p:p+3] != first:
        sys.exit(0)
sys.exit(1)
PY
}

nonblank_file() {
  local img="$1"
  [ -s "$img" ] && [ "$(wc -c < "$img")" -gt 256 ]
}

asset_rel() {
  local asset="$1"
  python3 - "$USD_ASSETS_ROOT" "$asset" <<'PY'
import os, sys
print(os.path.relpath(sys.argv[2], sys.argv[1]).replace(os.sep, "/"))
PY
}

safe_name() {
  printf '%s' "$1" | sed 's#[/\\ ]#_#g; s#[^A-Za-z0-9._-]#_#g'
}

run_one() {
  local mode="$1"
  local asset="$2"
  local rel safe out log status rc
  rel="$(asset_rel "$asset")"
  safe="$(safe_name "${mode}_${rel}")"
  out="$OUT_DIR/${safe}.ppm"
  log="$OUT_DIR/${safe}.log"

  local args=()
  local use_vk_env=0
  local use_xvfb_for_vk=0
  if [ "$mode" = "vk-raster" ] || [ "$mode" = "vk-rt" ]; then
    if should_use_xvfb; then
      use_xvfb_for_vk=1
    fi
  fi
  case "$mode" in
    vk-raster)
      args=(--backend vk)
      [ "$use_xvfb_for_vk" -eq 0 ] && args=(--headless "${args[@]}")
      use_vk_env=1
      ;;
    vk-rt)
      args=(--backend vk --rt)
      [ "$use_xvfb_for_vk" -eq 0 ] && args=(--headless "${args[@]}")
      use_vk_env=1
      ;;
    cuda-rt)
      args=(--headless --cuda)
      ;;
    tusdr-cpu|tusdr-vk|tusdr-vkr)
      if [ ! -x "$TUSDRENDER_BIN" ]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$mode" "backend_unavailable" "-" "$rel" "" "" >> "$RESULTS"
        printf '%-9s %-22s %-9s %s\n' "$mode" "backend_unavailable" "-" "$rel"
        return 0
      fi
      ;;
    *)
      echo "ERROR: unknown mode '$mode'" >&2
      return 2
      ;;
  esac

  local cmd=()
  if [[ "$mode" == tusdr-* ]]; then
    out="$OUT_DIR/${safe}.png"
    case "$mode" in
      tusdr-cpu) args=("$asset" "$out" -rtPreview) ;;
      tusdr-vk) args=("$asset" "$out" -vk) ;;
      tusdr-vkr) args=("$asset" "$out" -vkr) ;;
    esac
    args+=(-w "$WIDTH" -height "$HEIGHT" -autoframe -samples 1)
    cmd+=(env "$TUSDRENDER_BIN" "${args[@]}")
  else
    if [ "$use_vk_env" -eq 1 ] && [ -n "$VK_DEVICE" ]; then
      args+=(--vk-device "$VK_DEVICE")
    fi
    args+=(--frames "$FRAMES" --size "$SIZE" --screenshot "$out" "$asset")

    if [ "$use_vk_env" -eq 1 ] && [ "$use_xvfb_for_vk" -eq 1 ] && ! use_external_xvfb; then
      cmd+=(xvfb-run -a env)
    else
      cmd+=(env)
    fi
    if [ "$use_vk_env" -eq 1 ] && should_use_nvidia_offload; then
      cmd+=(__NV_PRIME_RENDER_OFFLOAD=1)
      cmd+=(__GLX_VENDOR_LIBRARY_NAME=nvidia)
      cmd+=(__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json)
    fi
    cmd+=("$TUSDVIEW_BIN" "${args[@]}")
  fi

  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "$TIMEOUT_DUR" "${cmd[@]}" >"$log" 2>&1
    rc=$?
  else
    "${cmd[@]}" >"$log" 2>&1
    rc=$?
  fi

  if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    status="timeout"
  elif grep -Eiq 'CUDA ray tracing unavailable|CUDA RT failed|no CUDA|NVRTC.*failed|renderer init failed: no Vulkan|no Vulkan physical device|Vulkan backend unavailable|Vulkan unavailable|backend .*unavailable|Failed to create Vulkan|lightrt_vk_engine_create failed' "$log"; then
    status="backend_unavailable"
  elif grep -Eiq 'load failed|Failed to load USD|LoadUSDFromFile.*failed|parse error|No such file|cannot open|^ERR .*load|^ERROR .*load' "$log"; then
    status="load_error"
  elif grep -Eq 'render stats: meshes 0/0 visible, instances 0/0 visible, drawn tris 0|loaded .*: 0 mesh\(es\), 0 tri\(s\)|rt meshes: 0|triangles: 0' "$log"; then
    status="no_renderable"
  elif [[ "$mode" == tusdr-* ]] && [ ! -s "$out" ]; then
    status="backend_error"
  elif [[ "$mode" == tusdr-* ]] && nonblank_file "$out"; then
    if grep -Eiq 'warn|warning|unsupported|fallback|TODO|failed to load texture|failed to load environment' "$log"; then
      status="rendered_with_warnings"
    else
      status="rendered"
    fi
  elif [[ "$mode" == tusdr-* ]]; then
    status="no_renderable"
  elif [ "$mode" = "cuda-rt" ] && ! grep -q 'CUDA RT wrote' "$log"; then
    status="backend_error"
  elif [ "$mode" != "cuda-rt" ] && ! grep -q 'render stats' "$log"; then
    status="backend_error"
  elif [ ! -s "$out" ]; then
    status="backend_error"
  elif nonblank_ppm "$out"; then
    if grep -Eiq 'warn|warning|unsupported|fallback|TODO' "$log"; then
      status="rendered_with_warnings"
    else
      status="rendered"
    fi
  elif ! nonblank_ppm "$out"; then
    status="no_renderable"
  elif grep -Eiq 'warn|warning|unsupported|fallback|TODO' "$log"; then
    status="rendered_with_warnings"
  else
    status="rendered"
  fi

  # Golden-fingerprint check: only meaningful for a real (non-blank) render.
  # A mismatch overrides the status so the existing FAIL_ON path handles it.
  local golden="-"
  if [ "$GOLDEN_ENABLED" -eq 1 ] \
     && { [ "$status" = "rendered" ] || [ "$status" = "rendered_with_warnings" ]; } \
     && [ -s "$out" ]; then
    local fp
    fp="$(python3 "$FINGERPRINT_PY" hash "$out" 2>/dev/null)"
    if [ -z "$fp" ]; then
      golden="nohash"
    elif [ "$GOLDEN_UPDATE" = "1" ]; then
      printf '%s\t%s\t%s\n' "$mode" "$rel" "$fp" >> "$NEW_GOLDEN"
      golden="updated"
    else
      local expect="${GOLDEN_EXPECT["$mode|$rel"]:-}"
      if [ -z "$expect" ]; then
        golden="missing"
      elif python3 "$FINGERPRINT_PY" compare "$fp" "$expect" "$GOLDEN_TOL" >/dev/null 2>&1; then
        golden="ok"
      else
        golden="mismatch"
        status="golden_mismatch"
      fi
    fi
  fi

  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$mode" "$status" "$golden" "$rel" "$out" "$log" >> "$RESULTS"
  printf '%-9s %-22s %-9s %s\n' "$mode" "$status" "$golden" "$rel"

  if has_status "$FAIL_ON" "$status"; then
    return 1
  fi
  return 0
}

# An explicit curated file list (TUSDVIEW_USD_ASSETS_FILES: space/newline
# separated, paths relative to USD_ASSETS_ROOT or absolute) renders exactly
# those assets, in order. Empty = enumerate the whole root. Lets a checked-in
# ctest smoke a fast representative subset of the repo's own models/.
ASSET_FILES="${TUSDVIEW_USD_ASSETS_FILES:-}"
if [ -n "$ASSET_FILES" ]; then
  ASSETS=()
  for f in $ASSET_FILES; do
    case "$f" in
      /*) p="$f" ;;
      *)  p="$USD_ASSETS_ROOT/$f" ;;
    esac
    if [ -f "$p" ]; then
      ASSETS+=("$p")
    else
      echo "WARN: curated asset not found, skipping: $p" >&2
    fi
  done
else
  mapfile -t ASSETS < <(
    find "$USD_ASSETS_ROOT" -type f \( -name '*.usd' -o -name '*.usda' -o -name '*.usdc' -o -name '*.usdz' \) \
      | sort
  )
fi

if [ "${#ASSETS[@]}" -eq 0 ]; then
  echo "SKIP: no USD-family assets found under $USD_ASSETS_ROOT"
  exit "$SKIP"
fi

if [ "$LIMIT" -gt 0 ] 2>/dev/null && [ "${#ASSETS[@]}" -gt "$LIMIT" ]; then
  ASSETS=("${ASSETS[@]:0:$LIMIT}")
fi

IFS=',' read -r -a MODE_LIST <<< "$MODES"

echo "tusdview: $TUSDVIEW_BIN"
echo "assets : $USD_ASSETS_ROOT"
echo "out    : $OUT_DIR"
echo "modes  : $MODES"
echo "size   : $SIZE, frames: $FRAMES, timeout: $TIMEOUT_DUR"
[ -n "$VK_DEVICE" ] && echo "vk dev : $VK_DEVICE"
echo

fail=0
for asset in "${ASSETS[@]}"; do
  for mode in "${MODE_LIST[@]}"; do
    run_one "$mode" "$asset" || fail=1
  done
done

# Persist an updated golden baseline (sorted + de-duplicated, last wins).
if [ "$GOLDEN_ENABLED" -eq 1 ] && [ "$GOLDEN_UPDATE" = "1" ]; then
  {
    printf '# tusdview usd-assets render fingerprints (mode<TAB>asset<TAB>fp).\n'
    printf '# Per-machine baseline; regenerate with --update-golden.\n'
    sort -t$'\t' -k1,1 -k2,2 "$NEW_GOLDEN"
  } > "$GOLDEN_FILE"
  echo "golden : wrote $(grep -cv '^#' "$GOLDEN_FILE") fingerprints -> $GOLDEN_FILE"
fi

# Machine-readable JSON mirror of the TSV.
if [ "$JSON_OUT" = "auto" ]; then
  JSON_OUT="$OUT_DIR/results.json"
fi
if [ -n "$JSON_OUT" ] && command -v python3 >/dev/null 2>&1; then
  python3 - "$RESULTS" "$JSON_OUT" <<'PY'
import csv, json, sys
rows = []
with open(sys.argv[1], newline="") as f:
    for r in csv.DictReader(f, delimiter="\t"):
        rows.append(r)
summary = {}
for r in rows:
    key = "%s/%s" % (r.get("mode", ""), r.get("status", ""))
    summary[key] = summary.get(key, 0) + 1
with open(sys.argv[2], "w") as f:
    json.dump({"results": rows, "summary": summary}, f, indent=2)
PY
  echo "json   : $JSON_OUT"
fi

echo
echo "Summary:"
awk 'NR > 1 { counts[$1 "\t" $2]++ } END { for (k in counts) print counts[k] "\t" k }' "$RESULTS" | sort -k2,2 -k3,3
if [ "$GOLDEN_ENABLED" -eq 1 ] && [ "$GOLDEN_UPDATE" != "1" ]; then
  echo "Golden:"
  awk -F'\t' 'NR > 1 && $3 != "-" { g[$3]++ } END { for (k in g) print g[k] "\t" k }' "$RESULTS" | sort -k2,2
fi
echo "results: $RESULTS"

if [ "$fail" -ne 0 ]; then
  echo "FAIL: one or more statuses matched TUSDVIEW_USD_ASSETS_FAIL_ON=$FAIL_ON"
  exit 1
fi

echo "PASS: usd-assets tusdview smoke completed"
exit 0
