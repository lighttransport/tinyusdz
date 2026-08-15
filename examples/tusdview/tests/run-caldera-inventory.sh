#!/usr/bin/env bash
set -euo pipefail

# Lightweight cold-process inventory for every USD file in an external Caldera
# tree. This deliberately measures parsing/memory only; composed-root viewer
# timings belong to run-caldera-matrix.sh.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CALDERA_ROOT="${CALDERA_ROOT:-/mnt/disk1/data/caldera}"
OUT_DIR="${OUT_DIR:-/tmp/tusdview-caldera-inventory}"
TUSDCAT="${TUSDCAT:-${ROOT_DIR}/build_ninja/tusdcat}"
JOBS="${JOBS:-8}"
TIMEOUT_SECS="${TIMEOUT_SECS:-30}"

if [[ ! -x "${TUSDCAT}" ]]; then
  echo "FAIL: tusdcat not executable: ${TUSDCAT}" >&2
  exit 1
fi
if [[ ! -d "${CALDERA_ROOT}" ]]; then
  echo "FAIL: Caldera root not found: ${CALDERA_ROOT}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

classify() {
  local path="$1"
  case "${path}" in
    */assets/xmodel/generated_splined/*|*/assets/xmodel/generated_proxies/*|\
    *.gdt.usd|*.geo.usd|*.terrain.usd)
      printf '%s' leaf_geometry
      ;;
    */audio/*|*_audio.usd|*/lighting/*|*_lighting.usd|*/cameras/*|\
    */layers/*|*/build/*)
      printf '%s' support_layer
      ;;
    */map_source/prefabs/*)
      printf '%s' prefab_candidate
      ;;
    *)
      printf '%s' other
      ;;
  esac
}

if [[ "${1:-}" == --probe ]]; then
  scene="$2"
  record="$3"
  bytes="$(stat -c '%s' "${scene}" 2>/dev/null || stat -f '%z' "${scene}")"
  kind="$(classify "${scene}")"
  set +e
  timeout "${TIMEOUT_SECS}s" /usr/bin/time -f '__TIME__ %e %M %x' \
    "${TUSDCAT}" -l --memstat "${scene}" >"${record}.out" 2>&1
  rc=$?
  set -e
  elapsed="$(sed -n 's/^__TIME__ \([^ ]*\) .*/\1/p' "${record}.out" | tail -1)"
  rss="$(sed -n 's/^__TIME__ [^ ]* \([^ ]*\) .*/\1/p' "${record}.out" | tail -1)"
  actual="$(sed -n 's/^  Actual (in use): .* (\([0-9][0-9]*\) bytes).*/\1/p' "${record}.out" | tail -1)"
  status=ok
  [[ ${rc} -eq 0 ]] || status="fail(${rc})"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "${scene}" "${kind}" "${bytes}" "${status}" \
    "${elapsed:-NA}" "${rss:-NA}" "${actual:-NA}" >"${record}"
  rm -f "${record}.out"
  exit 0
fi

record_dir="${OUT_DIR}/records-$(date +%s)-$$"
mkdir -p "${record_dir}"
SUMMARY="${OUT_DIR}/inventory.tsv"
printf 'scene\tclass\tfile_bytes\tstatus\tparse_s\tmax_rss_kb\tstage_bytes\n' >"${SUMMARY}"
mapfile -d '' scenes < <(find "${CALDERA_ROOT}" -type f \
  \( -name '*.usd' -o -name '*.usda' -o -name '*.usdc' \) -print0)

printf 'Discovered %s USD files; probing with %s workers...\n' "${#scenes[@]}" "${JOBS}"
manifest="$(mktemp "${OUT_DIR}/manifest.XXXXXX")"
trap 'rm -f "${manifest}"' EXIT
idx=0
for scene in "${scenes[@]}"; do
  idx=$((idx + 1))
  id="${idx}"
  printf '%s\0%s\0' "${scene}" "${record_dir}/${id}" >>"${manifest}"
done
set +e
xargs -0 -n2 -P"${JOBS}" bash "${BASH_SOURCE[0]}" --probe <"${manifest}"
probe_rc=$?
set -e

find "${record_dir}" -type f -not -name '*.out' -print0 | \
  sort -z | xargs -0 -r cat >>"${SUMMARY}"

echo "SUMMARY: ${SUMMARY}"
if [[ ${probe_rc} -ne 0 ]]; then
  echo "WARNING: probe workers returned ${probe_rc}; failed files remain represented by the inventory count." >&2
fi
echo "Counts by class:"
awk -F '\t' 'NR > 1 { count[$2]++ } END { for (k in count) print k, count[k] }' \
  "${SUMMARY}" | sort
