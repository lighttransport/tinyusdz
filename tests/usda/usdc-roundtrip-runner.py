#!/usr/bin/env python3
"""
USDC roundtrip test runner.
Tests USDA -> USDC (memory) -> re-parse USDC -> JSON compare

Usage:
  python usdc-roundtrip-runner.py --app ./usdc_roundtrip --basedir tests/usda
"""

import argparse
import glob
import os
import subprocess
import sys


def run_tests(app_path: str, basedir: str, verbose: bool = False) -> int:
    """Run USDC roundtrip tests on all .usda files in basedir."""
    
    if not os.path.isfile(app_path):
        print(f"Error: App not found: {app_path}", file=sys.stderr)
        return 1
    
    if not os.path.isdir(basedir):
        print(f"Error: Directory not found: {basedir}", file=sys.stderr)
        return 1
    
    # Find all .usda files
    usda_files = sorted(glob.glob(os.path.join(basedir, "*.usda")))
    
    if not usda_files:
        print(f"No .usda files found in {basedir}", file=sys.stderr)
        return 1
    
    total = len(usda_files)
    passed = 0
    failed = 0
    failures = []
    
    print(f"Running USDC roundtrip tests on {total} files...")
    
    for filepath in usda_files:
        filename = os.path.basename(filepath)
        
        args = [app_path, filepath]
        if verbose:
            args.append("--verbose")
        
        result = subprocess.run(args, capture_output=True, text=True)
        
        if result.returncode == 0:
            passed += 1
            if verbose:
                print(f"  PASS: {filename}")
        else:
            failed += 1
            failures.append((filename, result.stderr))
            if verbose:
                print(f"  FAIL: {filename}")
                print(f"        {result.stderr.strip()}")
    
    # Summary
    print()
    print(f"Results: {passed}/{total} passed, {failed} failed")
    
    if failures:
        print("\nFailed files:")
        for filename, error in failures:
            print(f"  - {filename}")
            if error.strip():
                # Show first line of error
                first_line = error.strip().split('\n')[0]
                print(f"    {first_line}")
    
    return 0 if failed == 0 else 1


def main():
    parser = argparse.ArgumentParser(description="USDC roundtrip test runner")
    parser.add_argument("--app", required=True, help="Path to usdc_roundtrip executable")
    parser.add_argument("--basedir", required=True, help="Directory containing .usda files")
    parser.add_argument("--verbose", action="store_true", help="Verbose output")
    
    args = parser.parse_args()
    
    return run_tests(args.app, args.basedir, args.verbose)


if __name__ == "__main__":
    sys.exit(main())
