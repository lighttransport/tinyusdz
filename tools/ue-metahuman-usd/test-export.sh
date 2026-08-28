#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
scene="$script_dir/output/MetaHuman_Hero.usda"

test -s "$script_dir/output/TinyUSDZ_DefaultHuman_Head.usdc"
test -s "$script_dir/output/TinyUSDZ_DefaultHuman_Body.usdc"
test -s "$script_dir/output/TinyUSDZ_DefaultHuman_Body.dna"
test -s "$scene"
test -s "$script_dir/output/MetaHuman_GroomStrands.usda"
test -s "$script_dir/output/MetaHuman_Deformers.usda"
test -s "$script_dir/output/MetaHuman_Physics.usda"
rg -q 'def BasisCurves "HairStrands"' "$scene"
rg -q 'def BasisCurves "HairStrands"' "$script_dir/output/MetaHuman_GroomStrands.usda"
rg -q 'unreal:sourceGroom' "$script_dir/output/MetaHuman_GroomStrands.usda"
rg -q 'unreal:exportedCurveCount = [1-9]' "$script_dir/output/MetaHuman_GroomStrands.usda"
rg -q 'inputs:subsurface_weight' "$scene"
rg -q 'tinyusdz:referenceBsdf = "chiang_hair_bsdf"' "$scene"
rg -q 'unreal:deformerFormat = "MetaHuman DNA / RigLogic"' "$script_dir/output/MetaHuman_Deformers.usda"
rg -q 'unreal:usdPhysicsStatus' "$script_dir/output/MetaHuman_Physics.usda"
"$repo_root/build_ninja/tusdcat" "$scene" -o /tmp/tinyusdz-metahuman-smoke.usda
"$repo_root/build_ninja/tusdcat" "$script_dir/output/TinyUSDZ_DefaultHuman_Head.usdc" -o /tmp/tinyusdz-metahuman-head.usda
"$repo_root/build_ninja/tusdcat" "$script_dir/output/TinyUSDZ_DefaultHuman_Body.usdc" -o /tmp/tinyusdz-metahuman-body.usda
rg -q 'def Skeleton "Skel"' /tmp/tinyusdz-metahuman-head.usda
rg -q 'primvars:skel:jointIndices' /tmp/tinyusdz-metahuman-head.usda
rg -q 'primvars:skel:jointWeights' /tmp/tinyusdz-metahuman-head.usda
rg -q 'def Skeleton "Skel"' /tmp/tinyusdz-metahuman-body.usda
rg -q 'primvars:skel:jointIndices' /tmp/tinyusdz-metahuman-body.usda
rg -q 'primvars:skel:jointWeights' /tmp/tinyusdz-metahuman-body.usda
if test -s "$script_dir/output/TinyUSDZ_DefaultHuman_Head.dna"; then
  echo "Fitted MetaHuman face DNA present"
else
  rg -q 'headDNAStatus = "missing: offline preset requires UE cloud auto-rigging"' \
    "$script_dir/output/MetaHuman_Rig.usda"
fi
echo "MetaHuman USD/MaterialX smoke test passed"
