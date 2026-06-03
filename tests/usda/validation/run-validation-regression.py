#!/usr/bin/env python3
"""Corpus-wide validation regression test.

Runs `tusdcat --validate-all` over every parseable `.usda` in the tests/usda
fixture corpus (excluding fail-case/, whose files are expected not to parse) and
asserts the validator never crashes or hangs. This guards against regressions
where a validator change segfaults or loops on some real-world construct -- the
curated tests/usda/validation suite only covers ~20 hand-picked files, whereas
this exercises the whole corpus (450+ files).

A per-file outcome is one of:
  clean        validation reported no issues (exit 0)
  issues       validation reported errors/warnings (exit 1, normal report)
  load-fail    the file could not be loaded as a Layer (not a validator fault)
  CRASH        the process died from a signal / abnormal exit  -> FAIL
  TIMEOUT      the process exceeded the per-file timeout        -> FAIL

Only CRASH and TIMEOUT fail the test; the rest are reported for visibility.

Usage:
  python3 run-validation-regression.py --app ./build/tusdcat
                                       [--basedir tests/usda] [--timeout 30]
"""

import argparse
import glob
import os
import subprocess
import sys

# Subdirectories whose files are intentionally unparseable / out of scope.
EXCLUDE_DIRS = ("fail-case",)


def classify(app, fixture, timeout):
    try:
        proc = subprocess.run(
            [app, "--validate-all", fixture],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            universal_newlines=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "TIMEOUT"

    rc = proc.returncode
    # Killed by a signal (segfault etc.): Python reports a negative code; a
    # shell-style >=128 is treated the same defensively.
    if rc < 0 or rc >= 128:
        return "CRASH"
    if "Failed to load" in proc.stdout:
        return "load-fail"
    if rc == 0:
        return "clean"
    return "issues"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="./build/tusdcat")
    here = os.path.dirname(os.path.abspath(__file__))
    default_basedir = os.path.dirname(os.path.dirname(here))  # tests/usda
    ap.add_argument("--basedir", default=default_basedir)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    fixtures = []
    for path in sorted(glob.glob(os.path.join(args.basedir, "**", "*.usda"),
                                 recursive=True)):
        rel = os.path.relpath(path, args.basedir)
        if any(part in EXCLUDE_DIRS for part in rel.split(os.sep)):
            continue
        fixtures.append(path)

    counts = {"clean": 0, "issues": 0, "load-fail": 0, "CRASH": 0, "TIMEOUT": 0}
    fatal = []

    for fixture in fixtures:
        outcome = classify(args.app, fixture, args.timeout)
        counts[outcome] += 1
        rel = os.path.relpath(fixture, args.basedir)
        if outcome in ("CRASH", "TIMEOUT"):
            fatal.append((rel, outcome))
            print("%-9s %s" % (outcome, rel))
        elif args.verbose:
            print("%-9s %s" % (outcome, rel))

    print("")
    print("validation regression: %d files | clean=%d issues=%d load-fail=%d "
          "CRASH=%d TIMEOUT=%d"
          % (len(fixtures), counts["clean"], counts["issues"],
             counts["load-fail"], counts["CRASH"], counts["TIMEOUT"]))

    if fatal:
        print("\nFATAL: validator crashed or hung on:")
        for rel, outcome in fatal:
            print("  %s (%s)" % (rel, outcome))
        sys.exit(1)


if __name__ == "__main__":
    main()
