#!/usr/bin/env python3
"""Headless CPU tusdrender regression for deep connected MaterialX graphs."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} TUSDRENDER REPO_ROOT", file=sys.stderr)
        return 2
    binary = pathlib.Path(sys.argv[1]).resolve()
    repo = pathlib.Path(sys.argv[2]).resolve()
    scene = repo / "tests/feat/node-mtlx/ChainTest.usda"
    with tempfile.TemporaryDirectory(prefix="tusdrender-mtlx-cpu-") as tmp:
        output = pathlib.Path(tmp) / "chain.png"
        run = subprocess.run(
            [str(binary), str(scene), str(output), "-samples", "2", "-w", "96",
             "-height", "64", "-autoframe", "-stats"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            timeout=30, check=False)
        log = run.stdout
        if run.returncode != 0:
            print(log, file=sys.stderr)
            return 1
        forbidden = ("Max evaluation depth exceeded",
                     "MaterialX connection for base_color could not be resolved")
        if any(token in log for token in forbidden):
            print(log, file=sys.stderr)
            return 1
        if not output.is_file() or output.stat().st_size < 500:
            print("CPU MaterialX graph render is missing or trivial", file=sys.stderr)
            return 1
    print("CPU tusdrender deep MaterialX graph: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
