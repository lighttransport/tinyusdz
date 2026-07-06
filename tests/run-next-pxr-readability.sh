#!/bin/bash
#
# Flatten a set of scenes with the next USDC writer and assert the output is
# readable by BOTH the tinyusdz legacy core reader (required) and, when
# available, pxr OpenUSD's usdcat (optional). Guards against regressions of:
#   bug #1 - value-less (declared-only) attributes emitting a `default` field
#            with an Invalid ValueRep (type enum 0), which pxr and the legacy
#            reader reject ("Attempted to unpack unsupported type enum value 0").
#   bug #2 - a flattened layer retaining a `subLayers` metadata field (written
#            as token[], which pxr/legacy require to be string[]).
#   bug #3 - Reference/Payload ListOp items mis-encoded: customData written
#            before the layerOffset (and a bogus customData slot on payloads),
#            so pxr/legacy read a layerOffset double as the customData count.
#
# Env overrides:
#   NEXT_USDCAT  path to next_usdcat  (default: build-next/next_usdcat)
#   TUSDCAT      path to legacy tusdcat (default: build/tusdcat)
#   PXR_USDCAT   path to pxr OpenUSD usdcat (default: ref/dist/bin/usdcat if present)
#   MODELS_DIR   extra models dir; if NormalsTextureBiasAndScale.usdz is there it
#                is included (a real-world value-less regression case).

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
NEXT_USDCAT="${NEXT_USDCAT:-$ROOT_DIR/build-next/next_usdcat}"
TUSDCAT="${TUSDCAT:-$ROOT_DIR/build/tusdcat}"
PXR_USDCAT="${PXR_USDCAT:-$ROOT_DIR/ref/dist/bin/usdcat}"
PXR_LIB="$ROOT_DIR/ref/dist/lib"
SCENES_DIR="$SCRIPT_DIR/next/pxr-readability"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$NEXT_USDCAT" ]; then
  echo "SKIP: next_usdcat not found at $NEXT_USDCAT (build src/next first)"; exit 0
fi
have_legacy=0; [ -x "$TUSDCAT" ] && have_legacy=1
have_pxr=0;   [ -x "$PXR_USDCAT" ] && have_pxr=1
if [ "$have_legacy" = 0 ] && [ "$have_pxr" = 0 ]; then
  echo "SKIP: neither legacy tusdcat nor pxr usdcat available"; exit 0
fi

# Collect inputs: the crafted .usda scenes, plus NormalsTextureBiasAndScale
# from a models dir if present (extracted so textures resolve).
inputs=()
for f in "$SCENES_DIR"/value-less.usda "$SCENES_DIR"/sublayers-main.usda \
         "$SCENES_DIR"/references.usda; do
  [ -f "$f" ] && inputs+=("$f")
done
for md in "${MODELS_DIR:-}" "$ROOT_DIR/../models"; do
  [ -n "$md" ] && [ -f "$md/NormalsTextureBiasAndScale.usdz" ] && {
    unzip -o -q "$md/NormalsTextureBiasAndScale.usdz" -d "$TMP/nt" 2>/dev/null && \
      inputs+=("$TMP/nt/NormalsTextureBiasAndScale.usda")
    break
  }
done

pass=0; fail=0; i=0
for in_scene in "${inputs[@]}"; do
  i=$((i+1))
  out="$TMP/out_$i.usdc"
  if ! "$NEXT_USDCAT" -f "$in_scene" -o "$out" >/dev/null 2>"$TMP/err"; then
    echo "FAIL (flatten): $(basename "$in_scene")"; head -2 "$TMP/err" | sed 's/^/    /'
    fail=$((fail+1)); continue
  fi
  ok=1; detail=""
  if [ "$have_legacy" = 1 ]; then
    if ! "$TUSDCAT" "$out" >/dev/null 2>"$TMP/lerr"; then
      ok=0; detail="legacy: $(grep -m1 -iE 'invalid|string\[\]|unpack|reconstruct' "$TMP/lerr" | head -c 90)"
    fi
  fi
  if [ "$have_pxr" = 1 ]; then
    if ! LD_LIBRARY_PATH="$PXR_LIB:${LD_LIBRARY_PATH:-}" "$PXR_USDCAT" "$out" >/dev/null 2>"$TMP/perr"; then
      ok=0; detail="$detail | pxr: $(grep -m1 -iE 'unpack|corrupt|error' "$TMP/perr" | head -c 90)"
    fi
  fi
  if [ "$ok" = 1 ]; then
    echo "ok   $(basename "$in_scene") (legacy=$have_legacy pxr=$have_pxr)"; pass=$((pass+1))
  else
    echo "FAIL $(basename "$in_scene") -> $detail"; fail=$((fail+1))
  fi
done

echo "== next pxr-readability: $pass passed, $fail failed =="
[ "$fail" = 0 ]
