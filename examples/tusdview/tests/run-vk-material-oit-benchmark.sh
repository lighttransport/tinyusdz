#!/usr/bin/env bash
# Reproducible Vulkan raster benchmark for material canonicalization and
# transparency submission. Timing is reported for comparison, while pass/fail
# criteria are deliberately structural so normal host/GPU variance cannot make
# CI flaky.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
MATERIALS="${MATERIALS:-32}"
FRAMES="${FRAMES:-8}"
SIZE="${SIZE:-512x512}"

if [ ! -x "$BIN" ]; then
  echo "SKIP: tusdview executable not found: $BIN"
  exit "$SKIP"
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "SKIP: python3 is required to generate and inspect the benchmark"
  exit "$SKIP"
fi
if [ "$MATERIALS" -lt 4 ] || [ $((MATERIALS % 2)) -ne 0 ]; then
  echo "FAIL: MATERIALS must be an even integer >= 4" >&2
  exit 1
fi

OUT="$(mktemp -d "${TMPDIR:-/tmp}/tusdview-vk-material-oit.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
SCENE="$OUT/material-oit-benchmark.usda"

python3 - "$SCENE" "$MATERIALS" <<'PY'
import sys

path, material_count = sys.argv[1], int(sys.argv[2])
side = int(material_count ** 0.5)
while side * side < material_count:
    side += 1

lines = ["#usda 1.0", "(", '    defaultPrim = "World"', "    upAxis = \"Y\"", ")",
         'def Xform "World"', "{"]
for i in range(material_count):
    x = (i % side) * 0.34 - (side - 1) * 0.17
    y = (i // side) * 0.34 - (side - 1) * 0.17
    z = 0.03 if i >= material_count // 2 else 0.0
    lines += [
        f'    def Mesh "Tile{i}"', "    {",
        "        uniform bool doubleSided = 1",
        "        int[] faceVertexCounts = [4]",
        "        int[] faceVertexIndices = [0, 1, 2, 3]",
        f"        rel material:binding = </World/Looks/Material{i}>",
        "        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1)] (interpolation = \"faceVarying\")",
        f"        point3f[] points = [({x-0.20:.4f}, {y-0.20:.4f}, {z}), ({x+0.20:.4f}, {y-0.20:.4f}, {z}), ({x+0.20:.4f}, {y+0.20:.4f}, {z}), ({x-0.20:.4f}, {y+0.20:.4f}, {z})]",
        '        uniform token subdivisionScheme = "none"', "    }"]
lines += ['    def Scope "Looks"', "    {"]
for i in range(material_count):
    transparent = i >= material_count // 2
    opacity = "0.45" if transparent else "1"
    color = "(0.15, 0.55, 0.95)" if transparent else "(0.95, 0.35, 0.12)"
    lines += [
        f'        def Material "Material{i}"', "        {",
        f"            token outputs:surface.connect = </World/Looks/Material{i}/Preview.outputs:surface>",
        '            def Shader "Preview"', "            {",
        '                uniform token info:id = "UsdPreviewSurface"',
        f"                color3f inputs:diffuseColor = {color}",
        "                float inputs:metallic = 0",
        f"                float inputs:opacity = {opacity}",
        "                float inputs:roughness = 0.5",
        "                token outputs:surface", "            }", "        }"]
lines += ["    }", "}", ""]
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
PY

if command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a -s "-screen 0 1280x800x24")
else
  RUN=()
fi

for mode in weighted sorted; do
  report="$OUT/$mode.json"
  log="$OUT/$mode.log"
  "${RUN[@]}" "$BIN" --headless --backend vk \
      --transparency "$mode" --frames "$FRAMES" --size "$SIZE" \
      --view-dir 0,0,-1 --render-report "$report" \
      --screenshot "$OUT/$mode.ppm" "$SCENE" >"$log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ] && { [ ! -s "$report" ] ||
                           [ ! -s "$OUT/$mode.ppm" ]; }; then
    if grep -Eqi 'no compatible Vulkan|no Vulkan device|renderer init failed' "$log"; then
      echo "SKIP: Vulkan backend unavailable"
      exit "$SKIP"
    fi
    cat "$log" >&2
    echo "FAIL: Vulkan $mode benchmark run failed" >&2
    exit 1
  fi
  if [ "$mode" = weighted ] &&
     ! grep -q 'Vulkan weighted OIT resources ready' "$log"; then
    echo "SKIP: weighted OIT is unavailable on the selected Vulkan device"
    exit "$SKIP"
  fi
done

python3 - "$OUT/weighted.json" "$OUT/sorted.json" "$MATERIALS" <<'PY'
import json
import sys

weighted_path, sorted_path, expected = sys.argv[1], sys.argv[2], int(sys.argv[3])
with open(weighted_path, encoding="utf-8") as f:
    weighted = json.load(f)
with open(sorted_path, encoding="utf-8") as f:
    sorted_report = json.load(f)

def row(name, report):
    scene = report.get("scene_stats", {})
    backend = report.get("backend", {})
    render = report.get("render", {})
    return {
        "mode": name,
        "logical_materials": scene.get("materials"),
        "canonical_materials": scene.get("canonical_materials"),
        "deduplicated_materials": scene.get("deduplicated_materials"),
        "pipeline_binds": backend.get("pipeline_binds"),
        "descriptor_set_binds": backend.get("descriptor_set_binds"),
        "oit_draw_calls": backend.get("oit_draw_calls"),
        "oit_attachment_bytes": backend.get("oit_attachment_bytes"),
        "draw_calls": render.get("draw_calls"),
        "elapsed_seconds": render.get("elapsed_seconds"),
    }

rows = [row("weighted", weighted), row("sorted", sorted_report)]
for item in rows:
    # The viewer retains material slot zero as its fallback in addition to all
    # authored identities. It is intentionally a third canonical payload.
    if item["logical_materials"] != expected + 1:
        raise SystemExit(f"FAIL: {item['mode']} lost logical material identity: {item}")
    if item["canonical_materials"] != 3:
        raise SystemExit(f"FAIL: {item['mode']} expected three canonical payloads: {item}")
    if item["deduplicated_materials"] != expected - 2:
        raise SystemExit(f"FAIL: {item['mode']} material dedup count is wrong: {item}")
    for field in ("pipeline_binds", "descriptor_set_binds"):
        if not isinstance(item[field], int) or item[field] <= 0:
            raise SystemExit(f"FAIL: {item['mode']} invalid {field}: {item[field]!r}")
if not isinstance(rows[0]["oit_draw_calls"], int) or rows[0]["oit_draw_calls"] <= 0:
    raise SystemExit(f"FAIL: weighted mode recorded no OIT draws: {rows[0]}")
if rows[1]["oit_draw_calls"] != 0:
    raise SystemExit(f"FAIL: sorted mode unexpectedly recorded OIT draws: {rows[1]}")

print("mode      logical canonical dedup pipe_binds desc_binds oit_draws oit_bytes elapsed_s")
for item in rows:
    print(f"{item['mode']:<9} {item['logical_materials']:>7} {item['canonical_materials']:>9} "
          f"{item['deduplicated_materials']:>5} {item['pipeline_binds']:>10} "
          f"{item['descriptor_set_binds']:>10} {item['oit_draw_calls']:>9} "
          f"{item['oit_attachment_bytes']:>9} {item['elapsed_seconds']:>9.4f}")
print("PASS: logical identity, raster material deduplication, and OIT submission verified")
PY
