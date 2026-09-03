#!/usr/bin/env bash
# lusdquicklook MCP stdio smoke test.
#
# The MCP transport is live only in the interactive app, so this intentionally
# runs the windowed path under Xvfb. It exercises initialize, tools/list, a
# queued load, scene inspection, settings, a real PNG capture, and quit.
set -euo pipefail

BIN="${1:?usage: run-quicklook-mcp-smoke.sh <binary> <repo-root>}"
ROOT="${2:?missing repo root}"

fail() { echo "FAIL: $*" >&2; exit 1; }
skip() { echo "SKIP: $*"; exit 77; }

command -v Xvfb >/dev/null 2>&1 || skip "Xvfb not installed"
command -v xvfb-run >/dev/null 2>&1 || skip "xvfb-run not installed"
command -v xdpyinfo >/dev/null 2>&1 || skip "xdpyinfo not installed"

# Some shared runners expose the commands but have no usable X socket. Probe
# the wrapper first so that this test is a clean capability skip, not a false
# protocol failure.
xvfb-run -a -s "-screen 0 800x600x24" xdpyinfo >/dev/null 2>&1 \
  || skip "Xvfb could not open a display"

ASSET="$ROOT/models/cube-previewsurface.usda"
[ -f "$ASSET" ] || skip "test asset not found: $ASSET"

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "== lusdquicklook MCP stdio smoke"
set +e
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"load_usd","arguments":{"path":"'"$ASSET"'"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_scene_info","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"render_settings","arguments":{"mode":"albedo","shadows":false}}}' \
  '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"screenshot","arguments":{"path":"'"$OUT"'/mcp.png","width":480,"height":360}}}' \
  '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"quit","arguments":{}}}' \
  | timeout 45 xvfb-run -a -s "-screen 0 800x600x24" \
      "$BIN" "$ASSET" --backend cpu --size 640x480 --mcp-stdio \
      >"$OUT/stdout" 2>"$OUT/stderr"
rc=$?
set -e
[ "$rc" -eq 0 ] || { cat "$OUT/stderr" >&2; fail "MCP process exited with $rc"; }

grep -q '"id":1' "$OUT/stdout" || fail "initialize response missing"
grep -q 'lusdquicklook-mcp' "$OUT/stdout" || fail "server identity missing"
grep -q '"name":"load_usd"' "$OUT/stdout" || fail "tool list missing load_usd"
grep -q '"id":6' "$OUT/stdout" || fail "screenshot response missing"
grep -q '"id":7' "$OUT/stdout" || fail "quit response missing"
[ -s "$OUT/mcp.png" ] || fail "MCP screenshot was not written"

echo "== lusdquicklook MCP stdio smoke: PASS"
