#!/usr/bin/env bash
# Compare authored camera extraction through the default and legacy loaders.
set -uo pipefail
BIN="${1:?usage: $0 /path/to/tusdview}"
ROOT="${2:?usage: $0 /repo/root}"
TMP="$(mktemp -d /tmp/tusdview-camera-equivalence.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

asset="$ROOT/tests/usda/camera-full-001.usda"
for camera in PerspectiveCamera OrthographicCamera LeftEyeCamera RightEyeCamera; do
  for loader in next legacy; do
    args=()
    [ "$loader" = legacy ] && args+=(--legacy-load)
    log="$TMP/$camera-$loader.log"
    timeout 60s "$BIN" --headless --backend vk --frames 2 --no-grid \
      --camera "$camera" --screenshot "$TMP/$camera-$loader.ppm" \
      "${args[@]}" "$asset" >"$log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
      if grep -Eqi 'no Vulkan|Vulkan.*unavailable|renderer init failed|no suitable GPU' "$log"; then
        echo "SKIP: Vulkan backend unavailable"
        exit 77
      fi
      cat "$log"
      exit 1
    fi
  done
done

python3 - "$TMP" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
for camera in ("PerspectiveCamera", "OrthographicCamera", "LeftEyeCamera", "RightEyeCamera"):
    def read(path):
        data = path.read_bytes()
        m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
        if not m:
            raise SystemExit(f"FAIL: invalid PPM {path}")
        return data[m.end():]
    a = read(root / f"{camera}-next.ppm")
    b = read(root / f"{camera}-legacy.ppm")
    if len(a) != len(b):
        raise SystemExit(f"FAIL: {camera} image dimensions differ")
    mean = sum(abs(x-y) for x, y in zip(a, b)) / len(a)
    if mean > 2.0:
        raise SystemExit(f"FAIL: {camera} default/legacy camera drift {mean:.3f}")
    print(f"PASS: {camera} default/legacy camera parity mean={mean:.3f}")
PY
