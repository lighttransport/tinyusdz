#!/usr/bin/env bash
# Parser-level regression for production path-tracing controls. These cases
# fail before renderer initialization and therefore run on GPU-less builders.
set -uo pipefail

TUSDVIEW="${1:?usage: $0 /path/to/tusdview}"

expect_failure() {
  local expected="$1"
  shift
  local log rc
  log="$("$TUSDVIEW" "$@" 2>&1)"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "FAIL: command unexpectedly succeeded: $*"
    exit 1
  fi
  if ! grep -Fq -- "$expected" <<<"$log"; then
    echo "FAIL: command did not report '$expected': $*"
    echo "$log"
    exit 1
  fi
}

expect_failure "--pt-quality must be interactive or final" --pt-quality draft
expect_failure "--pt-samples must be an unsigned integer" --pt-samples nope
expect_failure "--pt-max-depth must be a positive integer" --pt-max-depth 0
expect_failure "--pt-rr-depth must be an unsigned integer" --pt-rr-depth -1
expect_failure "--pt-motion-segments must be a positive integer" \
  --pt-motion-segments 0
expect_failure "--pt-variance must be a finite non-negative number" \
  --pt-variance nan
expect_failure "--pt-denoise must be off, auto, or on" --pt-denoise maybe
expect_failure "--f-stop must be a finite positive number" --f-stop 0
expect_failure "--focus-distance must be a finite positive number" \
  --focus-distance inf
expect_failure "--linear-output requires --path-trace" \
  --linear-output output.exr
expect_failure "--linear-output must use the .exr extension" \
  --path-trace --linear-output output.png
expect_failure "--path-trace supports Vulkan, CUDA, and HIP" \
  --path-trace --cpu-rt

help="$("$TUSDVIEW" --help 2>&1)" || {
  echo "FAIL: --help returned failure"
  exit 1
}
for flag in --path-trace --pt-quality --pt-samples --pt-max-depth \
            --pt-rr-depth --pt-denoise --pt-motion-segments --pt-seed \
            --linear-output --f-stop --focus-distance --live-shader-reload; do
  grep -Fq -- "$flag" <<<"$help" || {
    echo "FAIL: $flag is missing from help"
    exit 1
  }
done

echo "PASS: production path-tracing CLI validation"
