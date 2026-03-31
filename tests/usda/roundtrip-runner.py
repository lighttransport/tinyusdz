#!/usr/bin/env python3
# SPDX-License-Identifier: Apache 2.0
# USDA roundtrip test runner:
# Runs usda_roundtrip executable on all .usda files in the test directory

import os
import sys
import glob
import subprocess

# Known failing files - these have known issues with roundtrip
# that should be fixed in the future
KNOWN_FAILURES = [
    # (All previous known failures have been fixed)
]

def main():
    from argparse import ArgumentParser
    parser = ArgumentParser(description="USDA roundtrip test runner")
    parser.add_argument("--basedir", type=str, default="tests/usda",
                        help="Base directory containing .usda test files")
    parser.add_argument("--app", type=str, default="./build/usda_roundtrip",
                        help="Path to usda_roundtrip executable")
    parser.add_argument("--verbose", action="store_true",
                        help="Enable verbose output")
    parser.add_argument("--dump-on-fail", action="store_true",
                        help="Dump exported USDA on failure")
    parser.add_argument("--skip-file", type=str, action="append", default=[],
                        help="Skip specific files (can be used multiple times)")
    parser.add_argument("--no-skip-known", action="store_true",
                        help="Don't skip known failing files")
    args = parser.parse_args()

    app = args.app

    if not os.path.exists(app):
        print(f"Error: Executable not found: {app}")
        sys.exit(1)

    failed = []
    skipped = []
    skipped_known = []
    passed = []

    print(f"Basedir: {args.basedir}")
    print(f"App: {args.app}")

    # Build skip list
    skip_files = set(args.skip_file)
    if not args.no_skip_known:
        skip_files.update(KNOWN_FAILURES)
        print(f"Skipping {len(KNOWN_FAILURES)} known failing files")

    # Get all .usda files in basedir (not in subdirectories like fail-case)
    usda_files = glob.glob(os.path.join(args.basedir, "*.usda"))
    usda_files.sort()  # Sort for consistent ordering

    total_count = len(usda_files)

    for i, fname in enumerate(usda_files):
        basename = os.path.basename(fname)

        # Check if file should be skipped
        if basename in skip_files:
            if basename in KNOWN_FAILURES and not args.no_skip_known:
                skipped_known.append(fname)
            else:
                skipped.append(fname)
            if args.verbose:
                print(f"[{i+1}/{total_count}] SKIP: {fname}")
            continue

        if args.verbose:
            print(f"[{i+1}/{total_count}] Testing: {fname}")

        cmd = [app, fname]
        if args.verbose:
            cmd.append("--verbose")
        if args.dump_on_fail:
            cmd.append("--dump-on-fail")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if result.returncode != 0:
                failed.append(fname)
                if not args.verbose:
                    print(f"FAIL: {fname}")
                if result.stderr:
                    print(f"  Error: {result.stderr.strip()}")
            else:
                passed.append(fname)
                if args.verbose:
                    print(f"  OK")
        except subprocess.TimeoutExpired:
            failed.append(fname)
            print(f"TIMEOUT: {fname}")
        except Exception as e:
            failed.append(fname)
            print(f"ERROR: {fname}: {e}")

    print("\n=================================")
    print(f"Total:   {total_count}")
    print(f"Passed:  {len(passed)}")
    print(f"Failed:  {len(failed)}")
    print(f"Skipped (known): {len(skipped_known)}")
    print(f"Skipped (user):  {len(skipped)}")
    print("=================================")

    if skipped_known:
        print(f"\nKnown failing files (skipped): {len(skipped_known)}")
        for fname in skipped_known:
            print(f"  - {os.path.basename(fname)}")

    if failed:
        print("\nUnexpected failures:")
        for fname in failed:
            print(f"  - {fname}")
        print("\nUSDA roundtrip test FAILED")
        sys.exit(1)
    else:
        print("\nUSDA roundtrip test PASSED")
        sys.exit(0)

if __name__ == '__main__':
    main()
