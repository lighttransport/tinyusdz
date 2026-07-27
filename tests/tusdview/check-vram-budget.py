#!/usr/bin/env python3
"""tusdview: the large-scene budget tree must descend from the REAL GPU.

`ComputeResourceBudget` used to be fed the literals GiB(32) / GiB(16): every
budget below it -- `--max-gpu-mem`, the texture edge/byte caps, upload staging --
was computed for an imaginary 16 GiB card no matter what you ran on. An 8 GiB
card was handed an 8 GiB VRAM limit; a 24 GiB card left half its memory unused.

Now the GPU side is probed from the device, and `--vram-budget` overrides it.
This asserts, on a tiny scene (no large assets needed):

  1. a profile run reports a probed capacity and a positive derived limit;
  2. `--vram-budget G` overrides the probe, and the derived `--max-gpu-mem`
     follows it -- half the capacity, floored at 8 GiB (ComputeVramLimit); and
  3. a SMALLER --vram-budget yields a strictly smaller --max-gpu-mem, i.e. the
     knob actually propagates rather than being parsed and dropped.

Exits 77 (skip) when the binary is missing or Vulkan is unavailable.
"""
import os
import re
import subprocess
import sys
import tempfile

SKIP = 77

SCENE = """#usda 1.0
(upAxis = "Y")
def Xform "World" {
  def Mesh "M" {
    point3f[] points = [(-1,-1,0), (1,-1,0), (1,1,0), (-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
  }
}
"""


def run(binary, scene, work, extra):
    out = os.path.join(work, "vram.ppm")
    cmd = [binary, "--headless", "--large-scene-profile", "island",
           "--frames", "1", "--screenshot", out] + extra + [scene]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=600)
    return r.stdout.decode(errors="replace")


def budget_line(log):
    """-> (capacity, limit, source) from 'resource budget: vram capacity=...'."""
    m = re.search(r"resource budget: vram capacity=([\d.]+) GiB \(([^)]+)\) "
                  r"-> limit=([\d.]+) GiB", log)
    if not m:
        return None
    return float(m.group(1)), float(m.group(3)), m.group(2)


def max_gpu_mem(log):
    m = re.search(r"--max-gpu-mem=([\d.]+)", log)
    return float(m.group(1)) if m else None


def main():
    if len(sys.argv) < 3:
        print("usage: check-vram-budget.py <tusdview> <work_dir>")
        return SKIP
    binary, work = sys.argv[1], sys.argv[2]
    if not os.path.exists(binary):
        print(f"SKIP: missing binary ({binary})")
        return SKIP
    os.makedirs(work, exist_ok=True)
    scene = os.path.join(work, "vram_budget_quad.usda")
    with open(scene, "w") as f:
        f.write(SCENE)

    probed = run(binary, scene, work, [])
    if "Vulkan device" not in probed and "renderer: Vulkan" not in probed:
        print("SKIP: no Vulkan device available")
        return SKIP

    got = budget_line(probed)
    if not got:
        print("FAIL: no 'resource budget:' line -- the budget tree is not "
              "reporting what it planned against.\n" + probed[-800:])
        return 1
    cap, limit, source = got
    if source != "probed":
        print(f"FAIL: expected the capacity to be probed from the device, "
              f"got source '{source}'")
        return 1
    if cap <= 0.0 or limit <= 0.0:
        print(f"FAIL: probed a non-positive budget (capacity={cap}, "
              f"limit={limit}). A failed probe must fall back, not zero out "
              f"every downstream cap.")
        return 1

    # ComputeVramLimit: >=12 GiB -> half, floored at 8 GiB. 6 GiB -> capacity-2.
    for want_cap, want_limit in ((24.0, 12.0), (6.0, 4.0)):
        log = run(binary, scene, work, ["--vram-budget", str(want_cap)])
        got = budget_line(log)
        if not got:
            print(f"FAIL: no budget line for --vram-budget {want_cap}")
            return 1
        cap, limit, source = got
        if source != "--vram-budget" or abs(cap - want_cap) > 0.05:
            print(f"FAIL: --vram-budget {want_cap} did not override the probe "
                  f"(capacity={cap} from '{source}')")
            return 1
        if abs(limit - want_limit) > 0.05:
            print(f"FAIL: --vram-budget {want_cap} -> limit {limit} GiB, "
                  f"expected {want_limit} GiB per ComputeVramLimit")
            return 1
        mgm = max_gpu_mem(log)
        if mgm is None or abs(mgm - want_limit) > 0.05:
            print(f"FAIL: --vram-budget {want_cap} derived limit {want_limit} "
                  f"but --max-gpu-mem resolved to {mgm}. The umbrella flag is "
                  f"parsed but not propagating to the geometry cap.")
            return 1

    print("PASS: budgets descend from the probed device, and --vram-budget "
          "overrides it (24 GiB -> 12.0, 6 GiB -> 4.0 max-gpu-mem)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
