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

exit "$status"
