#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
scene="$script_dir/output/MetaHuman_Hero.usda"

test -s "$script_dir/output/TinyUSDZ_DefaultHuman_Head.usdc"
test -s "$script_dir/output/TinyUSDZ_DefaultHuman_Body.usdc"
test -s "$scene"
rg -q 'def BasisCurves "HairStrands"' "$scene"
rg -q 'def Mesh "HairCards"' "$scene"
rg -q 'inputs:subsurface_weight' "$scene"
rg -q 'tinyusdz:referenceBsdf = "chiang_hair_bsdf"' "$scene"
"$repo_root/build_ninja/tusdcat" "$scene" -o /tmp/tinyusdz-metahuman-smoke.usda
echo "MetaHuman USD/MaterialX smoke test passed"
