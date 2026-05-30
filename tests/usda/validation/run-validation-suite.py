#!/usr/bin/env python3
"""Self-checking runner for the USD validation fixture suite.

Each fixture embeds machine-readable expectations as `#` comment markers:

  # EXPECT-OK                       the file must produce no validation issues
  # EXPECT: <rule_id>               the file must report this rule (repeatable)

The runner invokes `tusdcat --validate-all <file>`, parses the reported
`[rule_id]` tokens, and checks them against the markers. For fixtures whose
expected rules are all in opt-in groups (geom.* / shade.*), it additionally runs
core-only `--validate` and asserts no issues are reported -- verifying the
groups really are opt-in.

Usage:
  python3 run-validation-suite.py --app ./build/tusdcat [--basedir tests/usda/validation]
"""

import argparse
import glob
import os
import re
import subprocess
import sys

RULE_RE = re.compile(r"\[([A-Za-z0-9._]+)\]")
EXPECT_RE = re.compile(r"^#\s*EXPECT:\s*(\S+)")
EXPECT_OK_RE = re.compile(r"^#\s*EXPECT-OK\b")


def parse_markers(path):
    """Return (expect_ok, [expected_rule_ids]) from a fixture's # markers."""
    expect_ok = False
    rules = []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if EXPECT_OK_RE.match(s):
                expect_ok = True
            m = EXPECT_RE.match(s)
            if m:
                rules.append(m.group(1))
    return expect_ok, rules


def run_validator(app, fixture, all_groups):
    cmd = [app, "--validate-all" if all_groups else "--validate", fixture]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True)
    return proc.stdout


def reported_rules(output):
    return set(RULE_RE.findall(output))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="./build/tusdcat",
                    help="path to tusdcat")
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--basedir", default=here)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    fixtures = sorted(glob.glob(os.path.join(args.basedir, "**", "*.usda"),
                                recursive=True))

    checked = 0
    skipped = 0
    failures = []

    for fixture in fixtures:
        expect_ok, expected_rules = parse_markers(fixture)
        if not expect_ok and not expected_rules:
            skipped += 1
            if args.verbose:
                print("SKIP (no markers): %s" % os.path.relpath(fixture, args.basedir))
            continue

        checked += 1
        rel = os.path.relpath(fixture, args.basedir)
        out = run_validator(args.app, fixture, all_groups=True)
        got = reported_rules(out)

        problems = []

        if expect_ok:
            if got:
                problems.append("expected no issues, but got: %s" % ", ".join(sorted(got)))
        for rule in expected_rules:
            if rule not in got:
                problems.append("expected rule `%s` not reported" % rule)

        # Opt-in regression: if every expected rule is in an opt-in group,
        # core-only validation must report nothing.
        if expected_rules and all(r.startswith(("geom.", "shade.")) for r in expected_rules):
            core_out = run_validator(args.app, fixture, all_groups=False)
            core_got = reported_rules(core_out)
            if core_got:
                problems.append("opt-in rules leaked into core --validate: %s"
                                % ", ".join(sorted(core_got)))

        if problems:
            failures.append((rel, problems, out))
            print("FAIL %s" % rel)
            for p in problems:
                print("      - %s" % p)
        else:
            print("PASS %s" % rel)
            if args.verbose:
                for line in out.splitlines():
                    if line.startswith(("ERROR", "WARN")):
                        print("      %s" % line)

    print("")
    print("validation suite: %d checked, %d failed, %d skipped (no markers)"
          % (checked, len(failures), skipped))

    if failures:
        print("\nFailing fixtures:")
        for rel, problems, out in failures:
            print("  %s" % rel)
        sys.exit(1)


if __name__ == "__main__":
    main()
