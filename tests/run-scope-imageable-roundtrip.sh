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

exit "$status"
