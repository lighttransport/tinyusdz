#!/usr/bin/env bash
#
# Scope is a UsdGeomImageable: it carries `visibility` and `purpose`, and
# authoring purpose on a Scope is the standard way to ship an asset's render and
# proxy representations side by side.
#
# Both are parsed into TYPED fields rather than the generic `props` map, so
# every writer has to emit them explicitly. Neither did: `visibility` was parsed
# and then silently dropped by the USDA printer AND the crate writer (Scope is
# not a GPrim, so ExtractGPrimProperties skipped it, and stage-converter had no
# Scope case at all). A file round-tripped through tusdcat came back with the
# attribute simply gone -- silent data loss.
#
# This pins both directions: usda -> usda and usda -> usdc -> usda.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TUSDCAT="${1:-${TUSDCAT:-$REPO_ROOT/build/tusdcat}}"

if [ ! -x "$TUSDCAT" ]; then
  echo "SKIP: tusdcat binary not found at $TUSDCAT"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/scope.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "World"
)

def Scope "World"
{
    token visibility = "invisible"
    uniform token purpose = "proxy"

    def Cube "shape"
    {
    }
}
USD

status=0

# Every authored attribute must survive; `shape` guards against the whole prim
# being dropped rather than just the attributes.
check() {
  local label="$1" file="$2"
  local out="$TMP/$label.usda"
  if ! "$TUSDCAT" "$file" > "$out" 2>"$TMP/$label.err"; then
    echo "FAIL[$label]: tusdcat exited nonzero"
    cat "$TMP/$label.err"
    status=1
    return
  fi
  for expect in 'purpose = "proxy"' 'visibility = "invisible"' 'def Cube "shape"'; do
    if ! grep -qF "$expect" "$out"; then
      echo "FAIL[$label]: lost '$expect' on round-trip"
      echo "--- got ---"
      cat "$out"
      status=1
      return
    fi
  done
  echo "ok[$label]: Scope visibility + purpose survived"
}

check usda-to-usda "$TMP/scope.usda"

if ! "$TUSDCAT" --output-format usdc -o "$TMP/scope.usdc" "$TMP/scope.usda" \
     >"$TMP/write.log" 2>&1; then
  echo "FAIL: tusdcat could not write usdc"
  cat "$TMP/write.log"
  exit 1
fi
check usdc-to-usda "$TMP/scope.usdc"

# Scope was one instance of a class: `visibility` / `purpose` are TYPED fields on
# every imageable, so each writer must emit them, and several forgot. All the
# lights and Volume lost BOTH through the crate writer; Material and NodeGraph
# lost `purpose` through both writers. Sweep every imageable prim type so a new
# one cannot quietly start dropping them again.
TYPES="Xform Scope Mesh Sphere Cube Cylinder Cone Capsule Plane Points BasisCurves
NurbsCurves PointInstancer Camera SkelRoot Skeleton Material NodeGraph SphereLight
DistantLight RectLight DiskLight DomeLight Volume GeomSubset Model"

{
  echo '#usda 1.0'
  echo '('
  echo '    defaultPrim = "W"'
  echo ')'
  echo
  echo 'def Xform "W"'
  echo '{'
  for t in $TYPES; do
    echo "    def $t \"P$t\""
    echo '    {'
    echo '        token visibility = "invisible"'
    echo '        uniform token purpose = "render"'
    echo '    }'
  done
  echo '}'
} > "$TMP/imageable.usda"

# Reports every prim that came back missing either attribute.
sweep() {
  local label="$1" file="$2"
  local out="$TMP/sweep-$label.usda"
  if ! "$TUSDCAT" "$file" > "$out" 2>/dev/null; then
    echo "FAIL[$label]: tusdcat exited nonzero on the imageable sweep"
    status=1
    return
  fi
  local lost=""
  for t in $TYPES; do
    # The prim's body runs until the next `def` or the closing brace.
    local body
    body="$(sed -n "/def $t \"P$t\"/,/^    }/p" "$out")"
    case "$body" in
      *'purpose = "render"'*) ;;
      *) lost="$lost $t:purpose" ;;
    esac
    case "$body" in
      *'visibility = "invisible"'*) ;;
      *) lost="$lost $t:visibility" ;;
    esac
  done
  if [ -n "$lost" ]; then
    echo "FAIL[$label]: dropped on round-trip:$lost"
    status=1
  else
    echo "ok[$label]: all imageable prim types kept visibility + purpose"
  fi
}

sweep all-types-usda "$TMP/imageable.usda"

if ! "$TUSDCAT" --output-format usdc -o "$TMP/imageable.usdc" "$TMP/imageable.usda" \
     >"$TMP/write2.log" 2>&1; then
  echo "FAIL: tusdcat could not write the imageable sweep to usdc"
  cat "$TMP/write2.log"
  exit 1
fi
sweep all-types-usdc "$TMP/imageable.usdc"

# Lights are Xformable as well as imageable, and NO light extractor wrote
# xformOps -- so a scene round-tripped through .usdc came back with every light
# at the world origin. intensity/color/exposure were copy-pasted per light type
# and half of them omitted `exposure`; RectLight never wrote its texture.
cat > "$TMP/light.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "W"
)

def Xform "W"
{
    def RectLight "Key"
    {
        float inputs:exposure = 1.5
        float inputs:intensity = 900
        asset inputs:texture:file = @key.png@
        double3 xformOp:translate = (5, 3, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def DomeLight "Sky"
    {
        float inputs:exposure = 0.5
        asset inputs:texture:file = @sky.hdr@
        token inputs:texture:format = "latlong"
        double3 xformOp:translate = (0, 10, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/light.usdc" "$TMP/light.usda" \
     >"$TMP/write3.log" 2>&1; then
  echo "FAIL: tusdcat could not write the light scene to usdc"
  cat "$TMP/write3.log"
  exit 1
fi

"$TUSDCAT" "$TMP/light.usdc" > "$TMP/light-rt.usda" 2>/dev/null
lost=""
for expect in \
  'xformOp:translate = (5, 3, 0)' \
  'xformOp:translate = (0, 10, 0)' \
  'inputs:exposure = 1.5' \
  'inputs:exposure = 0.5' \
  'inputs:texture:file = @key.png@' \
  'inputs:texture:file = @sky.hdr@' \
  'inputs:texture:format = "latlong"'; do
  grep -qF "$expect" "$TMP/light-rt.usda" || lost="$lost
    $expect"
done
if [ -n "$lost" ]; then
  echo "FAIL[lights-usdc]: dropped on round-trip:$lost"
  status=1
else
  echo "ok[lights-usdc]: light transforms, exposure and textures survived"
fi

# A genuinely typeless prim (`def "bora"`, no typeName authored at all) was
# invented a typeName of "Model" by the crate writer: the writer's "infer a
# schema name for in-memory-built prims" fallback (stage-converter.cc) read
# the internal C++ label of the catch-all `Model` struct that backs typeless
# prims and treated it as if it were an authored/inferable typeName. Cover
# `def`/`over`/`class` -- the same fallback backs all three specifiers.
cat > "$TMP/typeless.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "W"
)

def "W"
{
    over "child"
    {
    }
}

class "TheClass"
{
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/typeless.usdc" "$TMP/typeless.usda" \
     >"$TMP/write4.log" 2>&1; then
  echo "FAIL: tusdcat could not write the typeless-prim scene to usdc"
  cat "$TMP/write4.log"
  exit 1
fi

"$TUSDCAT" "$TMP/typeless.usdc" > "$TMP/typeless-rt.usda" 2>/dev/null
lost=""
for expect in 'def "W"' 'over "child"' 'class "TheClass"'; do
  grep -qF "$expect" "$TMP/typeless-rt.usda" || lost="$lost
    $expect"
done
if [ -n "$lost" ]; then
  echo "FAIL[typeless-usdc]: typeless prim gained an invented typeName:$lost"
  echo "--- got ---"
  cat "$TMP/typeless-rt.usda"
  status=1
else
  echo "ok[typeless-usdc]: typeless def/over/class stayed typeless"
fi

# Camera's `shutter:open` / `shutter:close` are namespaced in the schema
# (usdGeom.hh, prim-property-tables.hh), unlike this Camera's other
# attributes, which are all plain names. The crate writer wrote them under
# the plain names `shutterOpen`/`shutterClose` instead -- a property name the
# reader never looks for -- so a round-trip came back as if they were never
# authored at all (the printer then fell back to the schema default of 0.0
# for both, silently changing an authored motion-blur interval).
cat > "$TMP/camera.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "W"
)

def Camera "W"
{
    double shutter:open = -0.25
    double shutter:close = 0.25
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/camera.usdc" "$TMP/camera.usda" \
     >"$TMP/write5.log" 2>&1; then
  echo "FAIL: tusdcat could not write the camera scene to usdc"
  cat "$TMP/write5.log"
  exit 1
fi

"$TUSDCAT" "$TMP/camera.usdc" > "$TMP/camera-rt.usda" 2>/dev/null
lost=""
for expect in 'shutter:open = -0.25' 'shutter:close = 0.25'; do
  grep -qF "$expect" "$TMP/camera-rt.usda" || lost="$lost
    $expect"
done
if [ -n "$lost" ]; then
  echo "FAIL[camera-shutter-usdc]: dropped/renamed on round-trip:$lost"
  echo "--- got ---"
  cat "$TMP/camera-rt.usda"
  status=1
else
  echo "ok[camera-shutter-usdc]: Camera shutter:open/shutter:close survived"
fi

# apiSchemas list-ops: the crate writer rebuilt a single ListOp from the
# RESOLVED view (APISchemas::names/unknownSchemas), which can only ever be one
# explicit-or-prepend op. That silently dropped every `delete apiSchemas`
# (Omniverse/Newton-asset pattern: `prepend` + `delete` on one prim), dropped
# `apiSchemas = None` entirely (both resolved vectors are empty, so the old
# code wrote nothing -- not even an explicit empty list), and reordered
# `prepend` items because known (`names`) and unknown (`unknownSchemas`)
# schemas live in separate vectors that got concatenated back in the wrong
# order. Covers all three in one round-trip.
cat > "$TMP/apischemas.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "W"
)

def Xform "W"
{
    def Xform "deleteAndPrepend" (
        prepend apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsMassAPI", "PhysicsArticulationRootAPI"]
        delete apiSchemas = ["PhysicsArticulationRootAPI"]
    )
    {
    }

    def Xform "explicitNone" (
        apiSchemas = None
    )
    {
    }

    def Xform "preserveOrder" (
        prepend apiSchemas = ["HoudiniViewportGuideAPI", "GeomModelAPI", "MotionAPI"]
    )
    {
    }
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/apischemas.usdc" "$TMP/apischemas.usda" \
     >"$TMP/write6.log" 2>&1; then
  echo "FAIL: tusdcat could not write the apiSchemas scene to usdc"
  cat "$TMP/write6.log"
  exit 1
fi

"$TUSDCAT" "$TMP/apischemas.usdc" > "$TMP/apischemas-rt.usda" 2>/dev/null
lost=""
for expect in \
  'delete apiSchemas = ["PhysicsArticulationRootAPI"]' \
  'prepend apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsMassAPI", "PhysicsArticulationRootAPI"]' \
  'apiSchemas = None' \
  'prepend apiSchemas = ["HoudiniViewportGuideAPI", "GeomModelAPI", "MotionAPI"]'; do
  grep -qF "$expect" "$TMP/apischemas-rt.usda" || lost="$lost
    $expect"
done
if [ -n "$lost" ]; then
  echo "FAIL[apischemas-usdc]: dropped/reordered on round-trip:$lost"
  echo "--- got ---"
  cat "$TMP/apischemas-rt.usda"
  status=1
else
  echo "ok[apischemas-usdc]: delete+prepend, explicit None, and prepend order survived"
fi

# Relationships: several distinct properties were dropped by
# ConvertRelationshipToFields (src/stage-converter.cc) because it never
# received the enclosing Property's `custom` flag, hardcoded `variability` to
# Uniform instead of consulting Relationship::is_varying_authored(), and never
# wrote `bindMaterialAs` metadata at all (only ConvertAttributeToFields did).
# Separately, GPrim's `proxyPrim` was parsed into a typed field and never
# re-emitted (same class of bug as the earlier Scope visibility/purpose fix),
# and the collection-based material-binding writer
# (mat_binding->materialBindingCollectionMap() in sconv-geom.cc) built
# `material:binding:collection:<name>:<purpose>` when a purpose is present,
# the wrong order -- it must be `<purpose>:<name>`.
cat > "$TMP/relationships.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "W"
)

def "W"
{
    varying rel myrel = </W>
    append custom varying rel myrel2 = </W>

    rel material:binding = </W> (
        bindMaterialAs = "strongerThanDescendants"
    )
}

def Xform "hasProxy"
{
    rel proxyPrim = </W>

    # material:binding:collection:<name>[:<purpose>] only goes through the
    # typed MaterialBinding map (the buggy code path) on a schema type that
    # inherits it -- a typeless prim stores these as generic named
    # relationships and would pass even with the writer bug present.
    rel material:binding:collection:beauty = </W>
    rel material:binding:collection:mypurpose:beauty = </W>
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/relationships.usdc" "$TMP/relationships.usda" \
     >"$TMP/write7.log" 2>&1; then
  echo "FAIL: tusdcat could not write the relationships scene to usdc"
  cat "$TMP/write7.log"
  exit 1
fi

"$TUSDCAT" "$TMP/relationships.usdc" > "$TMP/relationships-rt.usda" 2>/dev/null
lost=""
for expect in \
  'varying rel myrel = </W>' \
  'append custom varying rel myrel2 = </W>' \
  'bindMaterialAs = "strongerThanDescendants"' \
  'rel proxyPrim = </W>' \
  'material:binding:collection:beauty = </W>' \
  'material:binding:collection:mypurpose:beauty = </W>'; do
  grep -qF "$expect" "$TMP/relationships-rt.usda" || lost="$lost
    $expect"
done
if [ -n "$lost" ]; then
  echo "FAIL[relationships-usdc]: dropped/altered on round-trip:$lost"
  echo "--- got ---"
  cat "$TMP/relationships-rt.usda"
  status=1
else
  echo "ok[relationships-usdc]: varying/custom, bindMaterialAs, proxyPrim, and collection-binding order survived"
fi

# Variant statement metadata: a variant carries its own PrimMeta block
# (`active`, `hidden`, `kind`, and `variantSets` when it nests a variantSet),
# populated by the readers exactly like a Prim's. ConvertVariantToFields
# (src/stage-converter.cc) wrote only the `specifier` field and never called
# ExtractPrimMeta, so every one of those dropped on write.
cat > "$TMP/variant-meta.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "V"
)

def Xform "V" (
    variants = {
        string geo = "a"
    }
    prepend variantSets = "geo"
)
{
    variantSet "geo" = {
        "a" (
            active = true
            hidden = true
            kind = "component"
        ) {
            def Capsule "Inner"
            {
            }
        }

        "b" (
            prepend variantSets = "sub"
        ) {
            variantSet "sub" = {
                "x" {
                }
            }
        }
    }
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/variant-meta.usdc" "$TMP/variant-meta.usda" \
     >"$TMP/write8.log" 2>&1; then
  echo "FAIL: tusdcat could not write the variant-meta scene to usdc"
  cat "$TMP/write8.log"
  exit 1
fi

"$TUSDCAT" "$TMP/variant-meta.usdc" > "$TMP/variant-meta-rt.usda" 2>/dev/null
lost=""
for expect in \
  'active = true' \
  'hidden = true' \
  'kind = "component"' \
  'variantSets = "sub"'; do
  grep -qF "$expect" "$TMP/variant-meta-rt.usda" || lost="$lost
    $expect"
done
if [ -n "$lost" ]; then
  echo "FAIL[variant-meta-usdc]: variant statement metadata dropped on round-trip:$lost"
  echo "--- got ---"
  cat "$TMP/variant-meta-rt.usda"
  status=1
else
  echo "ok[variant-meta-usdc]: variant active/hidden/kind/variantSets survived"
fi

# Shader input connections must not be BAKED DOWN TO CONSTANTS. A shader input
# is `authored` when it is declared, whether or not it carries a value -- and
# TypedAttributeWithFallback::get_value() silently returns the SCHEMA FALLBACK
# when no value was authored. Every typed input in sconv-shader.cc except
# inputs:st / inputs:file / inputs:in / inputs:normal read get_value() without
# first checking is_value_empty(), and never passed its connections through, so
# a connection-only input (`token inputs:wrapS.connect = </...>`) was written as
# the fallback CONSTANT ("useMetadata") and the connection was lost -- silently
# rewriting the asset's shading network rather than dropping inert metadata.
#
# Separately, a Material terminal is a TypedConnection whose has_value() IS its
# connection count, so a DECLARED-but-unconnected `token outputs:surface` was
# dropped entirely by AddMaterialOutputSpecs.
cat > "$TMP/shader-connect.usda" <<'USD'
#usda 1.0
(
    defaultPrim = "mat"
)

def Material "mat"
{
    token outputs:surface

    def Shader "pbr"
    {
        uniform token info:id = "UsdPreviewSurface"
        int inputs:useSpecularWorkflowDriver = 1
        token inputs:wrapSDriver = "repeat"
        token inputs:sourceColorSpace = "raw"
        int inputs:useSpecularWorkflow.connect = </mat/pbr.inputs:useSpecularWorkflowDriver>
    }

    def Shader "tex"
    {
        uniform token info:id = "UsdUVTexture"
        token inputs:wrapS.connect = </mat/pbr.inputs:wrapSDriver>
        token inputs:sourceColorSpace.connect = </mat/pbr.inputs:sourceColorSpace>
    }

    def Shader "xf"
    {
        uniform token info:id = "UsdTransform2d"
        float inputs:rotationDriver = 45
        float inputs:rotation.connect = </mat/xf.inputs:rotationDriver>
    }
}
USD

if ! "$TUSDCAT" --output-format usdc -o "$TMP/shader-connect.usdc" "$TMP/shader-connect.usda" \
     >"$TMP/write9.log" 2>&1; then
  echo "FAIL: tusdcat could not write the shader-connect scene to usdc"
  cat "$TMP/write9.log"
  exit 1
fi

"$TUSDCAT" "$TMP/shader-connect.usdc" > "$TMP/shader-connect-rt.usda" 2>/dev/null
lost=""
for expect in \
  'int inputs:useSpecularWorkflow.connect = </mat/pbr.inputs:useSpecularWorkflowDriver>' \
  'token inputs:wrapS.connect = </mat/pbr.inputs:wrapSDriver>' \
  'token inputs:sourceColorSpace.connect = </mat/pbr.inputs:sourceColorSpace>' \
  'float inputs:rotation.connect = </mat/xf.inputs:rotationDriver>' \
  'token outputs:surface'; do
  grep -qF "$expect" "$TMP/shader-connect-rt.usda" || lost="$lost
    $expect"
done
# The connections must not have been replaced by their schema fallbacks.
for baked in \
  'inputs:useSpecularWorkflow = 0' \
  'inputs:wrapS = "useMetadata"' \
  'inputs:sourceColorSpace = "auto"' \
  'inputs:rotation = 0'; do
  if grep -qF "$baked" "$TMP/shader-connect-rt.usda"; then
    lost="$lost
    BAKED TO CONSTANT: $baked"
  fi
done
if [ -n "$lost" ]; then
  echo "FAIL[shader-connect-usdc]: connections lost or baked to constants:$lost"
  echo "--- got ---"
  cat "$TMP/shader-connect-rt.usda"
  status=1
else
  echo "ok[shader-connect-usdc]: shader input connections and the declared-but-unconnected Material terminal survived"
fi

exit "$status"
