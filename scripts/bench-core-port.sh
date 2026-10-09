#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2024-Present Light Transport Entertainment Inc.
#
# bench-core-port.sh -- measure the four metrics that gate the C11 core port:
#   1. clean `lightusd_next` build wall time (serial sum of TU compile times)
#   2. maximum single-TU compile latency
#   3. static archive + per-object text/data size
#   4. linked wasm32 / memory64 module size (optional; see --wasm)
#
# Usage:
#   scripts/bench-core-port.sh [--build-dir DIR] [--output FILE] [--jobs N] [--wasm]
#
# The record belongs in a git-ignored build directory, not in the repository.
# Methodology follows doc/refactor-c-core.md: keep compiler, SDK, flags, feature
# configuration and parallelism fixed; alternate baseline/changed trees when
# comparing; treat < 5% deltas as noise.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build_ninja_next_port"
OUTPUT=""
JOBS="$(nproc)"
WANT_WASM=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --output)    OUTPUT="$2"; shift 2 ;;
    --jobs)      JOBS="$2"; shift 2 ;;
    --wasm)      WANT_WASM=1; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

# Resolve to an absolute path: the script cds into the build dir.
case "${BUILD_DIR}" in
  /*) : ;;
  *)  BUILD_DIR="${PWD}/${BUILD_DIR}" ;;
esac

SRC="${BUILD_DIR}" # ninja lives in the build dir
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"

report() { # report <text...>
  local line="$*"
  echo "$line"
  if [[ -n "${OUTPUT}" ]]; then echo "$line" >> "${OUTPUT}"; fi
}

if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
  echo "error: ${BUILD_DIR} has no build.ninja -- configure it first" >&2
  exit 1
fi

[[ -n "${OUTPUT}" ]] && : > "${OUTPUT}"

report "# C11 core port measurement (${STAMP})"
report "# build dir: ${BUILD_DIR}  jobs: ${JOBS}"
git -C "${ROOT_DIR}" rev-parse --short HEAD >/dev/null 2>&1 && \
  report "# commit: $(git -C "${ROOT_DIR}" rev-parse --short HEAD)"

cd "${BUILD_DIR}"

# --- clean build wall time ---------------------------------------------------
START=$(date +%s.%N)
cmake --build "${BUILD_DIR}" --target lightusd_next -j"${JOBS}" >/dev/null
END=$(date +%s.%N)
WALL=$(python3 -c "print(f'{$END-$START:.2f}')")
report "clean_build_wall_seconds: ${WALL}"

# --- per-TU compile time -----------------------------------------------------
python3 - "${BUILD_DIR}" "${JOBS}" <<'PY' > /tmp/opencode/bench-tu.txt 2>/dev/null || true
import sys, os
log = os.path.join(sys.argv[1], ".ninja_log")
best = {}
for line in open(log):
    f = line.split()
    if len(f) < 4:
        continue
    try:
        s, e = int(f[0]), int(f[1])
    except ValueError:
        continue
    if e <= s:
        continue
    o = f[3]
    if "lightusd_next.dir" not in o:
        continue
    k = o.split("lightusd_next.dir/")[-1]
    d = (e - s) / 1000.0
    if d > best.get(k, 0.0):
        best[k] = d
tu = sorted(best.items(), key=lambda kv: -kv[1])
print("tu_count: %d" % len(tu))
print("serial_compile_seconds: %.2f" % sum(d for _, d in tu))
print("max_tu_seconds: %.2f" % (tu[0][1] if tu else 0.0))
print("max_tu_source: %s" % (tu[0][0] if tu else ""))
print("top_tus:")
for k, d in tu[:10]:
    print("  %7.2fs  %s" % (d, k))
PY
while IFS= read -r line; do
  [[ -n "${line}" ]] && report "${line}"
done < /tmp/opencode/bench-tu.txt

# --- object / archive size ---------------------------------------------------
python3 - "${BUILD_DIR}" <<'PY' > /tmp/opencode/bench-size.txt 2>/dev/null || true
import os, subprocess, sys
build = sys.argv[1]
objdir = os.path.join(build, "src/next/CMakeFiles/lightusd_next.dir")
objs = []
for r, _d, fs in os.walk(objdir):
    for f in fs:
        if f.endswith(".o"):
            objs.append(os.path.join(r, f))
text = data = n = 0
for o in objs:
    try:
        out = subprocess.check_output(["size", o], text=True)
    except Exception:
        continue
    fields = out.strip().splitlines()[-1].split()
    if len(fields) >= 3:
        text += int(fields[0]); data += int(fields[1]); n += 1
print("object_count: %d" % n)
print("object_text_bytes: %d" % text)
print("object_text_mib: %.2f" % (text / 1048576.0))
print("object_data_bytes: %d" % data)
arch = os.path.join(build, "src/next/liblightusd_next.a")
if os.path.exists(arch):
    print("archive_bytes: %d" % os.path.getsize(arch))
    print("archive_mib: %.2f" % (os.path.getsize(arch) / 1048576.0))
# largest objects
rows = []
for o in objs:
    try:
        out = subprocess.check_output(["size", o], text=True).strip().splitlines()[-1].split()
        rows.append((int(out[0]), o.split("lightusd_next.dir/")[-1]))
    except Exception:
        pass
rows.sort(reverse=True)
print("largest_objects:")
for t, k in rows[:8]:
    print("  %8d  %s" % (t, k))
PY
while IFS= read -r line; do
  [[ -n "${line}" ]] && report "${line}"
done < /tmp/opencode/bench-size.txt

# --- WASM linked size --------------------------------------------------------
if [[ "${WANT_WASM}" -eq 1 ]]; then
  report "wasm_note: configure an emcmake tree separately; record .wasm bytes here"
  find "${ROOT_DIR}/web/build_ninja" -name '*.wasm' -printf '%s %p\n' 2>/dev/null \
    | sort -rn | head -4 | while read -r sz path; do
        report "wasm_bytes: ${sz}  ${path}"
      done
fi

report "# done"
