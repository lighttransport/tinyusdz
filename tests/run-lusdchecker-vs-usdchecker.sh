#!/usr/bin/env bash
# Differential parity harness: lusdchecker vs pxr usdchecker.
#
# For every fixture in tests/usda, every rule family pxr's usdchecker reports
# must have a MAPPED lusdchecker finding on the same fixture (parity, not
# cleanliness -- fixtures trip content lints on purpose). Fails on any pxr
# finding with no mapped lightusd finding; prints a summary either way.
#
# Self-skips (exit 0) when pxr usdchecker is not installed.
#
# Env:
#   USDCHECKER_PATH   path to pxr usdchecker (auto-probed otherwise)
#   LUSDCHECKER_PATH  path to lusdchecker (default: build/lusdchecker)
#   FIXTURE_DIR       fixture directory (default: tests/usda)
set -u

cd "$(dirname "$0")/.." || exit 2

PXR="${USDCHECKER_PATH:-}"
if [ -z "$PXR" ]; then
  for candidate in "$HOME/work/OpenUSD/dist/bin/usdchecker" \
                   "$(command -v usdchecker 2>/dev/null)"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then PXR="$candidate"; break; fi
  done
fi
if [ -z "$PXR" ] || [ ! -x "$PXR" ]; then
  echo "SKIP: pxr usdchecker not found (set USDCHECKER_PATH)"
  exit 0
fi

LIGHTUSD="${LUSDCHECKER_PATH:-build/lusdchecker}"
if [ ! -x "$LIGHTUSD" ]; then
  echo "ERROR: lusdchecker not found at $LIGHTUSD (set LUSDCHECKER_PATH)"
  exit 2
fi

FIXTURE_DIR="${FIXTURE_DIR:-tests/usda}"

# pxr "<Validator>.<Rule>" -> space-separated acceptable lightusd rule ids.
# A pxr finding is satisfied when ANY of the mapped ids appears in the
# lusdchecker report for the same fixture.
map_rule() {
  case "$1" in
    StageMetadataChecker.MissingUpAxisMetadata)
      echo "geom.stage.upAxis" ;;
    StageMetadataChecker.MissingMetersPerUnitMetadata)
      echo "geom.stage.metersPerUnit" ;;
    StageMetadataChecker.MissingDefaultPrim)
      echo "core.layer.defaultPrim.missing core.layer.defaultPrim" ;;
    MissingReferenceValidator.UnresolvableDependency)
      echo "core.dependency.unresolvable core.composition.error" ;;
    MaterialBindingApiAppliedValidator.*)
      echo "shade.material.bindingAPI" ;;
    MaterialBindingRelationships.*)
      echo "shade.material.binding" ;;
    MaterialBindingCollectionValidator.*)
      echo "shade.material.binding shade.collection.binding" ;;
    SkelBindingApiAppliedValidator.*)
      echo "geom.skel.binding.api geom.skel.binding.root" ;;
    SkelBindingApiValidator.*)
      echo "geom.skel.binding.api geom.skel.binding.root" ;;
    EncapsulationRulesValidator.*|EncapsulationValidator.*)
      echo "shade.encapsulation.shaderParent shade.encapsulation.imageableInMaterial" ;;
    EncapsulationMaterialValidator.*)
      echo "shade.encapsulation.imageableInMaterial" ;;
    EncapsulationChecker.*|NestedGprimChecker.*)
      echo "geom.encapsulation.nestedGprim" ;;
    AttributeTypeMismatch.*)
      echo "core.schema.attributeType core.composition.attributeTypeMismatch" ;;
    SubsetFamilies.*)
      echo "geom.subset.familyType geom.subset.indices shade.subset.materialBindFamily" ;;
    SubsetParentIsImageable.*)
      echo "geom.subset.parent" ;;
    SubsetMaterialBindFamilyName.*|SubsetsMaterialBindFamily.*)
      echo "shade.subset.materialBindFamily" ;;
    ShaderSdrCompliance.MismatchedPropertyType)
      echo "shade.shader.typeMismatch shade.preview.inputType shade.primvarReader.result" ;;
    ShaderSdrCompliance.MissingShaderIdInRegistry|ShaderSdrCompliance.MissingSourceTypeInRegistry)
      echo "shade.shader.id" ;;
    NormalMapTextureValidator.InvalidFile)
      echo "core.dependency.unresolvable core.composition.error" ;;
    NormalMapTextureValidator.*)
      echo "shade.normalMap.scaleBias" ;;
    CompositionErrorTest.*)
      echo "core.composition.error" ;;
    StageMetadataChecker.*)
      echo "core.layer" ;;
    FileExtensionValidator.*|UsdzPackageValidator.*|PackageEncapsulationValidator.*)
      echo "package.entry.extension package.entry.alignment package.dependency.containment" ;;
    *)
      echo "" ;;
  esac
}

total_files=0
files_with_pxr_findings=0
total_findings=0
unmapped_count=0
unmatched_count=0
declare -A unmapped_rules
declare -A unmatched_rules

for f in "$FIXTURE_DIR"/*.usda; do
  total_files=$((total_files + 1))
  pxr_out=$("$PXR" -s "$f" 2>/dev/null)
  # Findings look like: Error: (usdUtilsValidators:MissingReferenceValidator.UnresolvableDependency) ...
  pxr_rules=$(printf '%s\n' "$pxr_out" |
    grep -oE '\([A-Za-z]+:[A-Za-z0-9_]+\.[A-Za-z0-9_]+\)' |
    sed -E 's/^\([A-Za-z]+:(.*)\)$/\1/' | sort -u)
  [ -z "$pxr_rules" ] && continue
  files_with_pxr_findings=$((files_with_pxr_findings + 1))

  lightusd_out=$("$LIGHTUSD" --usdchecker-compat --composed -s "$f" 2>/dev/null)

  while IFS= read -r rule; do
    [ -z "$rule" ] && continue
    total_findings=$((total_findings + 1))
    mapped=$(map_rule "$rule")
    if [ -z "$mapped" ]; then
      unmapped_count=$((unmapped_count + 1))
      unmapped_rules["$rule"]="${unmapped_rules[$rule]:-}$f "
      continue
    fi
    hit=0
    for id in $mapped; do
      if printf '%s' "$lightusd_out" | grep -qF "[$id]"; then hit=1; break; fi
    done
    if [ "$hit" -eq 0 ]; then
      unmatched_count=$((unmatched_count + 1))
      unmatched_rules["$rule"]="${unmatched_rules[$rule]:-}$f "
    fi
  done <<< "$pxr_rules"
done

echo "Checked $total_files fixtures; $files_with_pxr_findings with pxr findings ($total_findings finding families)."

status=0
if [ "$unmapped_count" -gt 0 ]; then
  status=1
  echo ""
  echo "UNMAPPED pxr rules (no lightusd rule id mapping in this script):"
  for rule in "${!unmapped_rules[@]}"; do
    echo "  $rule"
    for f in ${unmapped_rules[$rule]}; do echo "    $f"; done
  done
fi
if [ "$unmatched_count" -gt 0 ]; then
  status=1
  echo ""
  echo "UNMATCHED findings (mapped, but lusdchecker did not report any mapped id):"
  for rule in "${!unmatched_rules[@]}"; do
    echo "  $rule"
    for f in ${unmatched_rules[$rule]}; do echo "    $f"; done
  done
fi

if [ "$status" -eq 0 ]; then
  echo "PARITY OK: every pxr finding family has a mapped lusdchecker finding."
else
  echo ""
  echo "PARITY FAILED: $unmapped_count unmapped + $unmatched_count unmatched."
fi
exit "$status"
