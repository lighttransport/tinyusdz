#!/usr/bin/env bash
# OpenChessSet MaterialX/standard-surface smoke coverage.
# The asset is maintained outside this repository; set OPENCHESS_ASSET or
# provide the usual usd-assets symlink. CPU RT is mandatory when available.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
ASSET="${OPENCHESS_ASSET:-$ROOT/usd-assets/full_assets/OpenChessSet/chess_set.usda}"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }
[ -f "$ASSET" ] || { echo "SKIP: OpenChessSet asset not found ($ASSET)"; exit "$SKIP"; }

# Keep this smoke tied to the authored MaterialX feature set.  The render
# checks below prove that the scene shades, while these checks prevent an
# accidentally reduced asset bundle (or a loader fallback that never sees the
# networks) from making the smoke pass without exercising the chess materials.
MTLX_ROOT="$(dirname "$ASSET")/assets"
if [ -d "$MTLX_ROOT" ] && command -v rg >/dev/null 2>&1; then
  standard_count="$(rg -l '<standard_surface[[:space:]]' "$MTLX_ROOT" -g '*.mtlx' | wc -l | tr -d ' ')"
  [ "${standard_count:-0}" -ge 6 ] || {
    echo "FAIL: OpenChessSet MaterialX corpus has too few standard_surface documents"
    exit 1
  }
  for feature in base_color metalness specular_roughness normal subsurface \
                 subsurface_color subsurface_radius subsurface_scale \
                 transmission transmission_color; do
    rg -q "input name=\"$feature\"" "$MTLX_ROOT" -g '*.mtlx' || {
      echo "FAIL: OpenChessSet MaterialX corpus is missing $feature"
      exit 1
    }
  done
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# Optional NVIDIA hardware mode.  Keep the default portable/headless behavior,
# but make the documented offload recipe reproducible when explicitly enabled.
# Vulkan headless does not strictly need X11, yet xvfb-run is intentional here:
# it keeps device selection and the GL leg consistent on PRIME/offload hosts.
NVIDIA_ENV=()
VK_PREFIX=()
if [ "${TUSDVIEW_NVIDIA_OFFLOAD:-0}" = "1" ]; then
  NVIDIA_ENV=(
    __NV_PRIME_RENDER_OFFLOAD=1
    __GLX_VENDOR_LIBRARY_NAME=nvidia
    __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
  )
  # Propagate the same selection to the delegated CUDA harness.
  export "${NVIDIA_ENV[@]}"
  if [ "${TUSDVIEW_XVFB:-1}" = "1" ] && command -v xvfb-run >/dev/null 2>&1; then
    VK_PREFIX=(xvfb-run -a env "${NVIDIA_ENV[@]}")
  else
    VK_PREFIX=(env "${NVIDIA_ENV[@]}")
  fi
else
  VK_PREFIX=()
fi
VK_DEVICE_ARGS=()
[ -z "${TUSDVIEW_VK_DEVICE:-}" ] || VK_DEVICE_ARGS=(--vk-device "$TUSDVIEW_VK_DEVICE")

timeout --kill-after=5s "${TUSDVIEW_OPENCHESS_TIMEOUT:-180s}" \
  env -u TUSDVIEW_VK_DEVICE "$BIN" --headless --backend vk --cpu-rt \
  --frames 1 --size 128x128 \
  --screenshot "$OUT/cpu.ppm" "$ASSET" >"$OUT/cpu.log" 2>&1 || {
    echo "FAIL: CPU RT OpenChessSet render failed"
    tail -40 "$OUT/cpu.log"
    exit 1
  }
grep -q 'CPU RT wrote' "$OUT/cpu.log" || {
  echo "FAIL: CPU RT did not write OpenChessSet screenshot"; exit 1;
}
[ -s "$OUT/cpu.ppm" ] || { echo "FAIL: OpenChessSet CPU screenshot missing"; exit 1; }

# Reuse the full CUDA harness for the actual chess asset when an NVIDIA device
# is visible.  Keep this optional on CPU-only hosts so the core smoke remains
# useful in ordinary CI.
run_cuda="${TUSDVIEW_OPENCHESS_CUDA:-auto}"
if [ "$run_cuda" = "auto" ]; then
  run_cuda=0
  if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1; then
    run_cuda=1
  fi
fi
if [ "$run_cuda" = "1" ]; then
  TUSDVIEW="$BIN" ASSET="$ASSET" \
    "$SCRIPT_DIR/run-cuda-render.sh" || {
      rc=$?
      [ "$rc" -eq "$SKIP" ] || exit "$rc"
      echo "INFO: CUDA RT leg skipped (driver/NVRTC unavailable)"
    }
else
  echo "INFO: CUDA RT leg skipped (no NVIDIA adapter detected)"
fi

# Exercise Vulkan RT automatically on a non-llvmpipe adapter.  CI can force
# the leg with TUSDVIEW_OPENCHESS_VKRT=1 (or suppress it with =0).
run_vkrt="${TUSDVIEW_OPENCHESS_VKRT:-auto}"
if [ "$run_vkrt" = "auto" ]; then
  run_vkrt=0
  if command -v vulkaninfo >/dev/null 2>&1 &&
     ! vulkaninfo --summary 2>/dev/null | grep -qi 'llvmpipe'; then
    run_vkrt=1
  fi
fi
if [ "$run_vkrt" = "1" ]; then
  timeout --kill-after=5s "${TUSDVIEW_OPENCHESS_VKRT_TIMEOUT:-180s}" \
    "${VK_PREFIX[@]}" "$BIN" --headless --backend vk \
    "${VK_DEVICE_ARGS[@]}" \
    --rt --frames 1 --size 128x128 \
    --screenshot "$OUT/vk-rt.png" "$ASSET" >"$OUT/vk-rt.log" 2>&1 || {
      echo "FAIL: Vulkan RT OpenChessSet render failed"
      tail -40 "$OUT/vk-rt.log"
      exit 1
    }
  grep -q 'wrote .*vk-rt.png' "$OUT/vk-rt.log" || {
    echo "FAIL: Vulkan RT did not write OpenChessSet screenshot"; exit 1;
  }
  [ -s "$OUT/vk-rt.png" ] || {
    echo "FAIL: OpenChessSet Vulkan RT screenshot missing"; exit 1;
  }
  if command -v identify >/dev/null 2>&1; then
    vkrt_sd="$(identify -format '%[fx:standard_deviation]' "$OUT/vk-rt.png" 2>/dev/null || echo 0)"
    awk -v sd="$vkrt_sd" 'BEGIN { if (sd < 0.005) exit 1 }' || {
      echo "FAIL: OpenChessSet Vulkan RT screenshot is unexpectedly uniform"
      exit 1
    }
  fi
else
  echo "INFO: Vulkan RT leg skipped (no hardware adapter detected)"
fi

timeout --kill-after=5s "${TUSDVIEW_OPENCHESS_RASTER_TIMEOUT:-120s}" \
  "${VK_PREFIX[@]}" "$BIN" --headless --backend vk \
  "${VK_DEVICE_ARGS[@]}" \
  --frames 1 --size 128x128 \
  --screenshot "$OUT/vk-raster.png" "$ASSET" >"$OUT/vk-raster.log" 2>&1 || {
    echo "FAIL: Vulkan raster OpenChessSet render failed"
    tail -40 "$OUT/vk-raster.log"
    exit 1
  }
grep -q 'wrote .*vk-raster.png' "$OUT/vk-raster.log" || {
  echo "FAIL: Vulkan raster did not write OpenChessSet screenshot"; exit 1;
}
[ -s "$OUT/vk-raster.png" ] || {
  echo "FAIL: OpenChessSet Vulkan raster screenshot missing"; exit 1;
}
if command -v identify >/dev/null 2>&1; then
  vk_sd="$(identify -format '%[fx:standard_deviation]' "$OUT/vk-raster.png" 2>/dev/null || echo 0)"
  awk -v sd="$vk_sd" 'BEGIN { if (sd < 0.005) exit 1 }' || {
    echo "FAIL: OpenChessSet Vulkan raster screenshot is unexpectedly uniform"
    exit 1
  }
fi

if command -v Xvfb >/dev/null 2>&1; then
  # Ask Xvfb to allocate an unused display instead of guessing a display
  # number that may have a stale socket from another harness. A private
  # display also makes this leg deterministic when DISPLAY is unrelated.
  if [ -n "${TUSDVIEW_OPENCHESS_GL_DISPLAY:-}" ]; then
    GL_DISPLAY="$TUSDVIEW_OPENCHESS_GL_DISPLAY"
    Xvfb ":$GL_DISPLAY" -screen 0 256x256x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 &
  else
    Xvfb -displayfd 9 -screen 0 256x256x24 -nolisten tcp \
      >"$OUT/xvfb.log" 9>"$OUT/xvfb.display" 2>&1 &
    GL_DISPLAY=""
    for _ in 1 2 3 4 5 6 7 8 9 10; do
      [ -s "$OUT/xvfb.display" ] && {
        GL_DISPLAY="$(head -n 1 "$OUT/xvfb.display")"
        break
      }
      sleep 0.1
    done
  fi
  XVFB_PID=$!
  cleanup_xvfb() { kill "$XVFB_PID" >/dev/null 2>&1 || true; }
  trap 'cleanup_xvfb; rm -rf "$OUT"' EXIT
  sleep 1
  if [ -n "$GL_DISPLAY" ] && kill -0 "$XVFB_PID" >/dev/null 2>&1; then
    timeout --kill-after=5s "${TUSDVIEW_OPENCHESS_GL_TIMEOUT:-120s}" \
      env "${NVIDIA_ENV[@]}" DISPLAY=":$GL_DISPLAY" "$BIN" --backend gl --frames 1 \
      --size 128x128 --screenshot "$OUT/gl.png" "$ASSET" >"$OUT/gl.log" 2>&1 || {
        echo "FAIL: OpenGL OpenChessSet render failed"
        tail -40 "$OUT/gl.log"
        tail -20 "$OUT/xvfb.log"
        exit 1
      }
    grep -q 'wrote .*gl.png' "$OUT/gl.log" || {
      echo "FAIL: OpenGL did not write OpenChessSet screenshot"; exit 1;
    }
    [ -s "$OUT/gl.png" ] || {
      echo "FAIL: OpenChessSet OpenGL screenshot missing"; exit 1;
    }
    if command -v identify >/dev/null 2>&1; then
      gl_sd="$(identify -format '%[fx:standard_deviation]' "$OUT/gl.png" 2>/dev/null || echo 0)"
      awk -v sd="$gl_sd" 'BEGIN { if (sd < 0.005) exit 1 }' || {
        echo "FAIL: OpenChessSet OpenGL screenshot is unexpectedly uniform"
        exit 1
      }
    fi
    cleanup_xvfb
  else
    echo "INFO: Xvfb could not bind a private display; skipping headless OpenGL leg"
  fi
else
  echo "INFO: Xvfb unavailable; skipping headless OpenGL leg"
fi

python3 - "$OUT/cpu.ppm" <<'PY'
import re, sys
d = open(sys.argv[1], 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', d)
if not m:
    raise SystemExit('invalid PPM')
p = d[m.end():]
if len(set(p[::3])) < 32 or max(p) - min(p) < 40:
    raise SystemExit('OpenChessSet render is unexpectedly uniform')
print('OpenChessSet CPU RT material smoke: non-uniform shaded image')
PY

echo "PASS: OpenChessSet MaterialX/standard-surface CPU RT + Vulkan raster smoke (OpenGL checked when Xvfb is available)"
