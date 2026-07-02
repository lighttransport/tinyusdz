#!/usr/bin/env bash
# Large-scene USDA/USDC parser/writer matrix for next and current tinyusdz.
#
# Defaults stage intermediates in /dev/shm and refuse to run if the requested
# tmpfs budget is not available. This keeps large flattened USDA/USDC outputs
# off persistent disks while making peak working-set failures explicit.
#
# Usage:
#   tests/next/bench_usd_io_matrix.sh [--tmp /dev/shm] [--max-tmp-bytes N]
#                                     [--next-bin build-next-release/next_usdcat]
#                                     [--tusdcat build_ninja/tusdcat]
#                                     [--threads N] [--keep] [scene ...]
#
# Scenes default to public roots available on this workstation:
#   island  /mnt/nvme02/data/island/usd/island.usda
#   caldera /mnt/nvme02/data/caldera/caldera.usda
#
# Optional private scenes can be supplied by env without hard-coding names:
#   HOUSE_USD=/path/to/root.usda ALAB_USD=/path/to/entry.usda ...
set -u

NEXT_BIN="${NEXT_BIN:-build-next-release/next_usdcat}"
TUSDCAT_BIN="${TUSDCAT_BIN:-build_ninja/tusdcat}"
TMP="${TMP:-/dev/shm}"
MAX_TMP_BYTES="${MAX_TMP_BYTES:-21474836480}"
THREADS="${THREADS:-}"
USE_SYSTEMD="${USE_SYSTEMD:-0}"
KEEP=0
SCENES=()

declare -A SCENE_INPUT=(
  [island]="/mnt/nvme02/data/island/usd/island.usda"
  [caldera]="/mnt/nvme02/data/caldera/caldera.usda"
  [house]="${HOUSE_USD:-}"
  [alab]="${ALAB_USD:-}"
)

while [ $# -gt 0 ]; do
  case "$1" in
    --tmp) TMP="$2"; shift 2;;
    --max-tmp-bytes) MAX_TMP_BYTES="$2"; shift 2;;
    --next-bin) NEXT_BIN="$2"; shift 2;;
    --tusdcat) TUSDCAT_BIN="$2"; shift 2;;
    --threads) THREADS="$2"; shift 2;;
    --keep) KEEP=1; shift;;
    *) SCENES+=("$1"); shift;;
  esac
done
[ ${#SCENES[@]} -eq 0 ] && SCENES=(island caldera)

mkdir -p "$TMP" || exit 1

tmp_avail_bytes() {
  df -PB1 "$TMP" | awk 'NR==2 {print $4}'
}

require_tmp_budget() {
  local avail
  avail="$(tmp_avail_bytes)"
  if [ -z "$avail" ] || [ "$avail" -lt "$MAX_TMP_BYTES" ]; then
    echo "ERROR: $TMP has ${avail:-0} bytes available; need $MAX_TMP_BYTES" >&2
    exit 1
  fi
}

cleanup_files=()
cleanup() {
  if [ "$KEEP" = 0 ]; then
    for f in "${cleanup_files[@]}"; do
      [ -n "$f" ] && rm -f "$f"
    done
  fi
}
trap cleanup EXIT

run_timed() {
  local log="$1"; shift
  if [ "$USE_SYSTEMD" = 1 ] && command -v systemd-run >/dev/null 2>&1; then
    /usr/bin/time -v systemd-run --user --scope -q \
      -p MemoryMax=32G -p MemorySwapMax=0 "$@" >"$log.out" 2>"$log"
  else
    /usr/bin/time -v "$@" >"$log.out" 2>"$log"
  fi
}

get_timing_ms() {
  local key="$1" log="$2"
  grep -o "$key=[0-9.]*ms" "$log" | tail -1 | sed -e "s/$key=//" -e 's/ms//'
}

get_rss_kb() {
  grep -F "Maximum resident set size" "$1" | tail -1 | awk '{print $6}'
}

get_elapsed() {
  grep -F "Elapsed (wall clock) time" "$1" | tail -1 | awk '{print $8}'
}

size_or_zero() {
  stat -c%s "$1" 2>/dev/null || echo 0
}

mbps() {
  awk -v b="$1" -v ms="$2" 'BEGIN{ if(ms+0>0) printf "%.0f", b/1048576/(ms/1000); else print "-"; }'
}

envv=(env TINYUSDZ_NEXT_TIMING=1)
[ -n "$THREADS" ] && envv+=(TINYUSDZ_NEXT_NUM_THREADS="$THREADS")

echo "== USD IO matrix =="
echo "tmp=$TMP max_tmp_bytes=$MAX_TMP_BYTES threads=${THREADS:-auto}"
df -h "$TMP" | tail -1
echo

printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
  scene op bytes write_ms parse_ms rss_kb notes
printf -- '---------+--------------------+---------------+------------+------------+------------+----------------\n'

for scene in "${SCENES[@]}"; do
  input="${SCENE_INPUT[$scene]:-}"
  if [ -z "$input" ]; then
    echo "skip $scene: no input path configured" >&2
    continue
  fi
  if [ ! -e "$input" ]; then
    echo "skip $scene: missing $input" >&2
    continue
  fi

  prefix="$TMP/tinyusdz-${scene}-$$"
  flat_usda="$prefix.flat.usda"
  rewrite_usda="$prefix.rewrite.usda"
  flat_usdc="$prefix.flat.usdc"
  cleanup_files+=("$flat_usda" "$rewrite_usda" "$flat_usdc" \
                  "$prefix.flat-usda.log" "$prefix.flat-usda.log.out" \
                  "$prefix.rewrite.log" "$prefix.rewrite.log.out" \
                  "$prefix.usdc-write.log" "$prefix.usdc-write.log.out" \
                  "$prefix.usdc-file.log" "$prefix.usdc-file.log.out" \
                  "$prefix.usdc-memory.log" "$prefix.usdc-memory.log.out" \
                  "$prefix.current.log" "$prefix.current.log.out")

  if [ -x "$NEXT_BIN" ]; then
    require_tmp_budget
    if ! run_timed "$prefix.flat-usda.log" "${envv[@]}" "$NEXT_BIN" --timing -f \
      -o "$flat_usda" --write-threads "${THREADS:-0}" "$input"; then
      printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
        "$scene" next_flat_usda "-" "FAIL" "-" \
        "$(get_rss_kb "$prefix.flat-usda.log")" "see $prefix.flat-usda.log"
      continue
    fi
    bytes="$(size_or_zero "$flat_usda")"
    wms="$(get_timing_ms "write" "$prefix.flat-usda.log")"
    printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
      "$scene" next_flat_usda "$bytes" "${wms:-?}" "-" \
      "$(get_rss_kb "$prefix.flat-usda.log")" "elapsed=$(get_elapsed "$prefix.flat-usda.log") MBps=$(mbps "$bytes" "${wms:-0}")"

    require_tmp_budget
    if ! run_timed "$prefix.rewrite.log" "${envv[@]}" "$NEXT_BIN" --timing \
      --rewrite-layer \
      -o "$rewrite_usda" --parse-threads "${THREADS:-0}" \
      --write-threads "${THREADS:-0}" "$flat_usda"; then
      printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
        "$scene" next_rewrite_usda "-" "FAIL" "FAIL" \
        "$(get_rss_kb "$prefix.rewrite.log")" "see $prefix.rewrite.log"
      continue
    fi
    pms="$(get_timing_ms "parse" "$prefix.rewrite.log")"
    wms="$(get_timing_ms "write" "$prefix.rewrite.log")"
    printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
      "$scene" next_rewrite_usda "$(size_or_zero "$rewrite_usda")" \
      "${wms:-?}" "${pms:-?}" "$(get_rss_kb "$prefix.rewrite.log")" \
      "parse_MBps=$(mbps "$bytes" "${pms:-0}") write_MBps=$(mbps "$(size_or_zero "$rewrite_usda")" "${wms:-0}")"
    if [ "$KEEP" = 0 ]; then
      rm -f "$rewrite_usda"
    fi

    if [ "$KEEP" = 0 ]; then
      rm -f "$flat_usda"
    fi
    require_tmp_budget
    if ! run_timed "$prefix.usdc-write.log" "${envv[@]}" "$NEXT_BIN" --timing -f \
      -o "$flat_usdc" --write-threads "${THREADS:-0}" "$input"; then
      printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
        "$scene" next_flat_usdc "-" "FAIL" "-" \
        "$(get_rss_kb "$prefix.usdc-write.log")" "see $prefix.usdc-write.log"
      continue
    fi
    usdc_bytes="$(size_or_zero "$flat_usdc")"
    wms="$(get_timing_ms "write" "$prefix.usdc-write.log")"
    printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
      "$scene" next_flat_usdc "$usdc_bytes" "${wms:-?}" "-" \
      "$(get_rss_kb "$prefix.usdc-write.log")" "MBps=$(mbps "$usdc_bytes" "${wms:-0}")"

    if ! run_timed "$prefix.usdc-file.log" "${envv[@]}" "$NEXT_BIN" --timing \
      -l "$flat_usdc"; then
      printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
        "$scene" next_parse_usdc "$usdc_bytes" "-" "FAIL" \
        "$(get_rss_kb "$prefix.usdc-file.log")" "see $prefix.usdc-file.log"
      continue
    fi
    printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
      "$scene" next_parse_usdc "$usdc_bytes" "-" \
      "$(get_timing_ms "load" "$prefix.usdc-file.log")" \
      "$(get_rss_kb "$prefix.usdc-file.log")" "file"

    if ! run_timed "$prefix.usdc-memory.log" "${envv[@]}" "$NEXT_BIN" --timing \
      --parse-usdc-memory "$flat_usdc"; then
      printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
        "$scene" next_parse_usdc_mem "$usdc_bytes" "-" "FAIL" \
        "$(get_rss_kb "$prefix.usdc-memory.log")" "see $prefix.usdc-memory.log"
      continue
    fi
    printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
      "$scene" next_parse_usdc_mem "$usdc_bytes" "-" \
      "$(get_timing_ms "usdc-parse-memory" "$prefix.usdc-memory.log")" \
      "$(get_rss_kb "$prefix.usdc-memory.log")" "borrowed-memory"
  else
    echo "skip next: $NEXT_BIN is not executable" >&2
  fi

  if [ -x "$TUSDCAT_BIN" ]; then
    if ! run_timed "$prefix.current.log" "$TUSDCAT_BIN" -f --memstat \
      --output-format=usda -o /dev/null "$input"; then
      printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
        "$scene" current_flat_usda "-" "FAIL" "-" \
        "$(get_rss_kb "$prefix.current.log")" "see $prefix.current.log"
      continue
    fi
    printf "%-8s | %-18s | %13s | %10s | %10s | %10s | %s\n" \
      "$scene" current_flat_usda "-" "-" "-" \
      "$(get_rss_kb "$prefix.current.log")" "elapsed=$(get_elapsed "$prefix.current.log")"
  else
    echo "skip current: $TUSDCAT_BIN is not executable" >&2
  fi
done
