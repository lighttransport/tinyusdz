#!/usr/bin/env python3
"""Run the tinyusdz vs OpenUSD benchmark over a set of USD files.

Usage:
  python3 runner.py [--build-dir build] [--iters 10] [files...]

If no files are given, a default set from ../../models is used.
Each bench binary prints CSV lines: op,status,iters,median_ms,min_ms,max_ms
"""

import argparse
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MODELS = [
    "../../models/suzanne.usdc",
    "../../models/texturedcube.usda",
    "../../models/cube.usdc",
    "../../models/blendshape.usda",
    "../../models/texture-cat-plane.usdz",
]
OPS = ["parse", "write_usda", "write_usdc", "composite"]


def run_bench(binary, usd_file, iters):
    """Returns {op: median_ms or None}."""
    results = {}
    try:
        out = subprocess.run(
            [binary, usd_file, "--iters", str(iters)],
            capture_output=True, text=True, timeout=600)
    except (subprocess.TimeoutExpired, OSError) as e:
        print(f"  error running {binary}: {e}", file=sys.stderr)
        return results
    for line in out.stdout.splitlines():
        parts = line.strip().split(",")
        if len(parts) == 6 and parts[0] in OPS:
            results[parts[0]] = float(parts[3]) if parts[1] == "OK" else None
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*", help="USD files to benchmark")
    ap.add_argument("--build-dir", default=os.path.join(SCRIPT_DIR, "build"))
    ap.add_argument("--iters", type=int, default=10)
    args = ap.parse_args()

    tiny = os.path.join(args.build_dir, "bench_tinyusdz")
    pxr = os.path.join(args.build_dir, "bench_openusd")
    if not os.path.exists(tiny):
        sys.exit(f"missing {tiny}; build the harness first (see README.md)")
    has_pxr = os.path.exists(pxr)
    if not has_pxr:
        print("note: bench_openusd not found, reporting tinyusdz only\n")

    files = args.files or [
        os.path.normpath(os.path.join(SCRIPT_DIR, m)) for m in DEFAULT_MODELS]
    files = [f for f in files if os.path.exists(f)]
    if not files:
        sys.exit("no input files found")

    header = f"{'op':<12} {'tinyusdz (ms)':>14} {'OpenUSD (ms)':>14} {'ratio':>8}"
    for f in files:
        size_kb = os.path.getsize(f) / 1024.0
        print(f"=== {f} ({size_kb:.1f} KB, median of {args.iters} runs) ===")
        t = run_bench(tiny, f, args.iters)
        p = run_bench(pxr, f, args.iters) if has_pxr else {}
        print(header)
        for op in OPS:
            tv, pv = t.get(op), p.get(op)
            ts = f"{tv:.3f}" if tv is not None else "FAIL"
            ps = f"{pv:.3f}" if pv is not None else ("FAIL" if has_pxr else "-")
            ratio = f"{pv / tv:.2f}x" if tv and pv else "-"
            print(f"{op:<12} {ts:>14} {ps:>14} {ratio:>8}")
        print()
    print("ratio = OpenUSD time / tinyusdz time (>1 means tinyusdz is faster)")


if __name__ == "__main__":
    main()
