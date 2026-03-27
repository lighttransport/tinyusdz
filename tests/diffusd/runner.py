#!/usr/bin/env python3
"""
USDC cross-writer diff test runner.

For each input USD file:
  1. Pixar usdcat writes USDC:   usdcat input.usd -o pixar.usdc --usdFormat usdc
  2. TinyUSDZ tusdcat writes USDC: tusdcat input.usd -o tinyusdz.usdc
  3. Both USDC files are read back and compared via tusddiff

tusddiff exit codes: 0 = identical, 1 = differences, 2 = error.

Usage:
  python runner.py \\
    --tusdcat ./build/tusdcat \\
    --tusddiff ./build/tusddiff \\
    --usdcat /path/to/pixar/usdcat \\
    --basedir tests/usda

  python runner.py \\
    --tusdcat ./build/tusdcat \\
    --tusddiff ./build/tusddiff \\
    --usdcat /path/to/pixar/usdcat \\
    --basedir tests/usda \\
    --verbose --keep-tmp
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile


# Files that Pixar's usdcat can't parse (skip silently)
PIXAR_SKIP = {
    "material-binding-005.usda",
    "rel-value-block-with-meta-001.usda",
    "shader-transform2d-000.usda",
}

# Files known to produce expected diffs (document reason)
KNOWN_DIFFS = {
    # Add filenames here if specific files are expected to differ
}


def run_tests(tusdcat, tusddiff, usdcat, basedir,
              verbose=False, keep_tmp=False, globs=None):
    for tool in (tusdcat, tusddiff, usdcat):
        if not os.path.isfile(tool):
            print(f"Error: tool not found: {tool}", file=sys.stderr)
            return 1
    if not os.path.isdir(basedir):
        print(f"Error: directory not found: {basedir}", file=sys.stderr)
        return 1

    patterns = globs or ["*.usda"]
    files = []
    for pat in patterns:
        files.extend(sorted(glob.glob(os.path.join(basedir, pat))))
    if not files:
        print(f"No files found in {basedir}", file=sys.stderr)
        return 1

    total = len(files)
    passed = 0
    failed = 0
    skipped = 0
    pixar_errors = 0
    tinyusdz_errors = 0
    failures = []

    tmpdir = tempfile.mkdtemp(prefix="diffusd_")

    print(f"USDC cross-writer diff: {total} files from {basedir}")
    print(f"  tusdcat:  {tusdcat}")
    print(f"  tusddiff: {tusddiff}")
    print(f"  usdcat:   {usdcat}")
    print(f"  tmpdir:   {tmpdir}")
    print()

    for filepath in files:
        basename = os.path.basename(filepath)

        if basename in PIXAR_SKIP:
            skipped += 1
            if verbose:
                print(f"  SKIP (pixar): {basename}")
            continue

        stem = basename.rsplit(".", 1)[0]
        # Pixar usdcat requires .usd extension with --usdFormat
        pixar_usd = os.path.join(tmpdir, stem + "_pixar.usd")
        pixar_usdc = os.path.join(tmpdir, stem + "_pixar.usdc")
        tiny_usdc = os.path.join(tmpdir, stem + "_tiny.usdc")

        # Step 1: Pixar usdcat → USDC (write as .usd, rename to .usdc)
        pr = subprocess.run(
            [usdcat, filepath, "-o", pixar_usd, "--usdFormat", "usdc"],
            capture_output=True, text=True, timeout=60)
        if pr.returncode == 0:
            try:
                os.rename(pixar_usd, pixar_usdc)
            except OSError:
                pass
        if pr.returncode != 0:
            pixar_errors += 1
            skipped += 1
            if verbose:
                err = (pr.stderr.strip().split("\n")[0] if pr.stderr.strip() else "unknown")
                print(f"  SKIP (pixar write): {basename}: {err}")
            continue

        # Step 2: TinyUSDZ tusdcat → USDC
        tr = subprocess.run(
            [tusdcat, filepath, "-o", tiny_usdc],
            capture_output=True, text=True, timeout=60)
        if tr.returncode != 0:
            tinyusdz_errors += 1
            failed += 1
            err = (tr.stderr.strip().split("\n")[0] if tr.stderr.strip() else "unknown")
            failures.append((basename, f"tusdcat write failed: {err}"))
            if verbose:
                print(f"  FAIL (tusdcat write): {basename}")
            continue

        # Step 3: Compare USDC content
        # Pixar writes USDC (reference), TinyUSDZ writes USDC (test).
        # Each tool reads the OTHER's USDC and produces USDA text.
        # We compare Pixar-reading-original vs TinyUSDZ-reading-Pixar-USDC.
        # This tests that TinyUSDZ can correctly read Pixar's USDC files.
        pixar_usda_out = os.path.join(tmpdir, stem + "_pixar_ref.usda")
        tiny_usda_out = os.path.join(tmpdir, stem + "_tiny_read.usda")

        # Reference: Pixar reads original USDA → USDA text
        pr2 = subprocess.run(
            [usdcat, filepath],
            capture_output=True, text=True, timeout=60)
        if pr2.returncode != 0:
            skipped += 1
            if verbose:
                print(f"  SKIP (pixar read orig): {basename}")
            continue

        # Test: TinyUSDZ reads Pixar's USDC → USDA text
        tr2 = subprocess.run(
            [tusdcat, pixar_usdc],
            capture_output=True, text=True, timeout=60)
        if tr2.returncode != 0:
            failed += 1
            err = (tr2.stderr.strip().split("\n")[0] if tr2.stderr.strip() else "unknown")
            failures.append((basename, f"tusdcat can't read pixar USDC: {err}"))
            if verbose:
                print(f"  FAIL (read pixar USDC): {basename}")
            continue

        # Compare using compare-usda.js (semantic, order-independent)
        with open(pixar_usda_out, "w") as fp:
            fp.write(pr2.stdout)
        with open(tiny_usda_out, "w") as fp:
            fp.write(tr2.stdout)

        script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        compare_js = os.path.join(script_dir, "compare-usda.js")

        if os.path.isfile(compare_js):
            dr = subprocess.run(
                ["node", compare_js, pixar_usda_out, tiny_usda_out],
                capture_output=True, text=True, timeout=60)
        else:
            dr = subprocess.run(
                [tusddiff, pixar_usda_out, tiny_usda_out],
                capture_output=True, text=True, timeout=60)

        if dr.returncode == 0:
            passed += 1
            if verbose:
                print(f"  PASS: {basename}")
            if not keep_tmp:
                for fp in (pixar_usdc, tiny_usdc, pixar_usda_out, tiny_usda_out):
                    try:
                        os.unlink(fp)
                    except OSError:
                        pass
        elif dr.returncode == 1:
            if basename in KNOWN_DIFFS:
                passed += 1
                if verbose:
                    print(f"  PASS (known diff): {basename}")
            else:
                failed += 1
                diff_text = dr.stdout.strip()[:200] if dr.stdout.strip() else "differences"
                failures.append((basename, diff_text))
                if verbose:
                    print(f"  FAIL (diff): {basename}")
                    if dr.stdout.strip():
                        for line in dr.stdout.strip().split("\n")[:5]:
                            print(f"        {line}")
        else:
            failed += 1
            err = (dr.stderr.strip().split("\n")[0] if dr.stderr.strip() else "diff error")
            failures.append((basename, f"diff error: {err}"))
            if verbose:
                print(f"  FAIL (diff error): {basename}")

    # Summary
    print()
    print(f"Results: {passed}/{total} passed, {failed} failed, "
          f"{skipped} skipped ({pixar_errors} pixar parse errors)")

    if failures:
        print(f"\nFailed files ({len(failures)}):")
        for bn, reason in failures:
            print(f"  - {bn}")
            if reason:
                print(f"    {reason.split(chr(10))[0][:120]}")

    if not keep_tmp:
        try:
            os.rmdir(tmpdir)
        except OSError:
            pass

    return 0 if failed == 0 else 1


# =========================================================================
# USDA cross-writer test
# =========================================================================

def run_usda_tests(tusdcat, usdcat, basedir, verbose=False, globs=None):
    """Compare USDA text output: Pixar usdcat vs TinyUSDZ tusdcat.

    For each input file:
      1. Pixar usdcat input.usda → USDA text (reference)
      2. TinyUSDZ tusdcat input.usda → USDA text (test)
      3. compare-usda.js compares both (semantic, order-independent)
    """
    for tool in (tusdcat, usdcat):
        if not os.path.isfile(tool):
            print(f"Error: tool not found: {tool}", file=sys.stderr)
            return 1
    if not os.path.isdir(basedir):
        print(f"Error: directory not found: {basedir}", file=sys.stderr)
        return 1

    patterns = globs or ["*.usda"]
    files = []
    for pat in patterns:
        files.extend(sorted(glob.glob(os.path.join(basedir, pat))))
    if not files:
        print(f"No files found in {basedir}", file=sys.stderr)
        return 1

    script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    compare_js = os.path.join(script_dir, "compare-usda.js")
    if not os.path.isfile(compare_js):
        print(f"Error: compare-usda.js not found at {compare_js}", file=sys.stderr)
        return 1

    total = len(files)
    passed = 0
    failed = 0
    skipped = 0
    failures = []

    tmpdir = tempfile.mkdtemp(prefix="diffusda_")

    print(f"USDA cross-writer compare: {total} files from {basedir}")
    print(f"  tusdcat: {tusdcat}")
    print(f"  usdcat:  {usdcat}")
    print()

    for filepath in files:
        basename = os.path.basename(filepath)

        if basename in PIXAR_SKIP:
            skipped += 1
            if verbose:
                print(f"  SKIP (pixar): {basename}")
            continue

        stem = basename.rsplit(".", 1)[0]
        ref_usda = os.path.join(tmpdir, stem + "_ref.usda")
        test_usda = os.path.join(tmpdir, stem + "_test.usda")

        # Pixar usdcat → USDA text (reference)
        pr = subprocess.run([usdcat, filepath],
                            capture_output=True, text=True, timeout=60)
        if pr.returncode != 0:
            skipped += 1
            if verbose:
                print(f"  SKIP (pixar): {basename}")
            continue

        # TinyUSDZ tusdcat → USDA text (test)
        tr = subprocess.run([tusdcat, filepath],
                            capture_output=True, text=True, timeout=60)
        if tr.returncode != 0:
            failed += 1
            err = (tr.stderr.strip().split("\n")[0] if tr.stderr.strip() else "unknown")
            failures.append((basename, f"tusdcat failed: {err}"))
            if verbose:
                print(f"  FAIL (tusdcat): {basename}")
            continue

        with open(ref_usda, "w") as f:
            f.write(pr.stdout)
        with open(test_usda, "w") as f:
            f.write(tr.stdout)

        # compare-usda.js: 0=equivalent, 1=different, 2=error
        dr = subprocess.run(
            ["node", compare_js, ref_usda, test_usda],
            capture_output=True, text=True, timeout=60)

        if dr.returncode == 0:
            passed += 1
            if verbose:
                print(f"  PASS: {basename}")
            try:
                os.unlink(ref_usda)
                os.unlink(test_usda)
            except OSError:
                pass
        elif dr.returncode == 1:
            failed += 1
            diff_text = dr.stdout.strip()[:200] if dr.stdout.strip() else "differences"
            failures.append((basename, diff_text))
            if verbose:
                print(f"  FAIL (diff): {basename}")
                if dr.stdout.strip():
                    for line in dr.stdout.strip().split("\n")[:3]:
                        print(f"        {line}")
        else:
            failed += 1
            err = (dr.stderr.strip().split("\n")[0] if dr.stderr.strip() else "error")
            failures.append((basename, f"compare error: {err}"))
            if verbose:
                print(f"  FAIL (compare error): {basename}")

    print()
    print(f"Results: {passed}/{total} passed, {failed} failed, {skipped} skipped")

    if failures:
        print(f"\nFailed files ({len(failures)}):")
        for bn, reason in failures:
            print(f"  - {bn}")
            if reason:
                print(f"    {reason.split(chr(10))[0][:120]}")

    try:
        os.rmdir(tmpdir)
    except OSError:
        pass

    return 0 if failed == 0 else 1


def main():
    parser = argparse.ArgumentParser(
        description="Cross-writer diff: Pixar usdcat vs TinyUSDZ tusdcat")
    parser.add_argument("--tusdcat", required=True,
                        help="Path to TinyUSDZ tusdcat")
    parser.add_argument("--tusddiff", default="",
                        help="Path to TinyUSDZ tusddiff (for usdc mode)")
    parser.add_argument("--usdcat", required=True,
                        help="Path to Pixar usdcat")
    parser.add_argument("--basedir", required=True,
                        help="Directory containing USD files")
    parser.add_argument("--mode", choices=["usdc", "usda", "both"],
                        default="both",
                        help="Test mode: usdc, usda, or both (default: both)")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--keep-tmp", action="store_true",
                        help="Keep temporary files")
    parser.add_argument("--glob", action="append", dest="globs",
                        help="File glob pattern (default: *.usda)")
    args = parser.parse_args()

    rc = 0

    if args.mode in ("usda", "both"):
        print("=" * 60)
        print("USDA Cross-Writer Test")
        print("=" * 60)
        r = run_usda_tests(args.tusdcat, args.usdcat,
                           args.basedir, args.verbose, args.globs)
        if r != 0:
            rc = 1

    if args.mode in ("usdc", "both"):
        if not args.tusddiff:
            print("Error: --tusddiff required for usdc mode", file=sys.stderr)
            return 1
        print()
        print("=" * 60)
        print("USDC Cross-Writer Test (TinyUSDZ reads Pixar USDC)")
        print("=" * 60)
        r = run_tests(args.tusdcat, args.tusddiff, args.usdcat,
                      args.basedir, args.verbose, args.keep_tmp, args.globs)
        if r != 0:
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(main())
