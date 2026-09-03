#!/usr/bin/env bash
# Verify that lusdrender's next-loader mask is also a payload composition filter.
set -uo pipefail

LUSDRENDER="${1:?usage: $0 /path/to/lusdrender}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SCENE="$ROOT/tests/feat/large-scene/fixture/deferred-nested/root.usda"
TMP="$(mktemp -d /tmp/lusdrender-payload-mask.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

if ! timeout 30s "$LUSDRENDER" "$SCENE" "$TMP/selected.png" \
    -rtPreview -mask /P -w 64 -height 64 -stats >"$TMP/selected.log" 2>&1; then
  cat "$TMP/selected.log"
  echo "FAIL: selected payload did not render"
  exit 1
fi
grep -q "rt meshes: 1" "$TMP/selected.log" || {
  cat "$TMP/selected.log"
  echo "FAIL: selected payload geometry was not composed"
  exit 1
}

set +e
timeout 30s "$LUSDRENDER" "$SCENE" "$TMP/deferred.png" \
  -rtPreview -mask /NotSelected -w 64 -height 64 -stats >"$TMP/deferred.log" 2>&1
rc=$?
set -e
if [ "$rc" -eq 0 ] || ! grep -q "payload(s) deferred outside -mask" "$TMP/deferred.log"; then
  cat "$TMP/deferred.log"
  echo "FAIL: unselected payload was not deferred"
  exit 1
fi

echo "PASS: -mask composes only intersecting payloads"
