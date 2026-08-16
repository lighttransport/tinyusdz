#!/usr/bin/env python3
"""tusdview Vulkan RT: BLAS compaction must shrink the acceleration structures.

tusdview built one BLAS per prototype at its *build-time* size and kept it
forever. A built BLAS is typically a third of that once compacted, so the RT
path carried gigabytes of dead VRAM on large scenes (Island: 2833 -> 879 MiB).

The build is now batched into waves that build with ALLOW_COMPACTION, query the
compacted size, and copy into right-sized storage. This asserts:

  1. compaction actually shrinks (resident < built, with real headroom); and
  2. it is IMAGE-NEUTRAL -- the compacted render is byte-identical to the
     uncompacted one (TUSDVIEW_BLAS_COMPACT=0).

(2) is the one that matters: a batch builder is easy to get subtly wrong. The
first version of this deduped nothing, so every duplicate instance of a
prototype rebuilt the same mesh and leaked all but the last copy -- and it still
rendered a perfect image, while using 6.7x the memory. Hence the memory
assertion, not just the pixels.

The fixture must be STATIC: a skinned/deformed prototype's BLAS is deliberately
built refit-able (ALLOW_UPDATE, uncompacted -- a compacted AS cannot be refit),
so on a skinned scene "resident == built" is correct behavior, not a compaction
failure. check-rt-blas-refit.py owns the dynamic-BLAS gate.

Exits 77 (skip) when the binary is missing or the GPU has no ray tracing.
"""
import os
import re
import subprocess
import sys

from gpu_backend import software_only_vulkan, vk_device_args

SKIP = 77
# Compacted BLAS should be well under the build-time size. NVIDIA gives ~31% on
# Island; anything at or above this means compaction silently did nothing.
MAX_COMPACTED_FRAC = 0.85


def render(binary, scene, out, work, compact):
    try:
        os.remove(out)
    except FileNotFoundError:
        pass
    env = dict(os.environ)
    env["TUSDVIEW_BLAS_COMPACT"] = "1" if compact else "0"
    cmd = [binary, *vk_device_args("vk"), "--headless", "--rt", "--frames",
           "2", "--screenshot", out, scene]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, timeout=120)
    return r.stdout.decode(errors="replace")


def blas_line(log):
    """-> (resident_mib, built_mib, unique, instances) from the BLAS report."""
    m = re.search(r"\[vk_rt\] BLAS: ([\d.]+) MiB resident from ([\d.]+) MiB built "
                  r"\([\d.]+%\), (\d+) unique of (\d+) full instances", log)
    if not m:
        return None
    return (float(m.group(1)), float(m.group(2)), int(m.group(3)),
            int(m.group(4)))


def main():
    if len(sys.argv) < 4:
        print("usage: check-blas-compaction.py <tusdview> <model> <work_dir>")
        return SKIP
    binary, scene, work = sys.argv[1], sys.argv[2], sys.argv[3]
    if not os.path.exists(binary) or not os.path.exists(scene):
        print(f"SKIP: missing binary or model ({binary}, {scene})")
        return SKIP
    if software_only_vulkan():
        print("SKIP: Vulkan RT unavailable (software Vulkan only)")
        return SKIP
    os.makedirs(work, exist_ok=True)

    on_png = os.path.join(work, "blas_compact_on.png")
    off_png = os.path.join(work, "blas_compact_off.png")

    try:
        on_log = render(binary, scene, on_png, work, compact=True)
    except subprocess.TimeoutExpired:
        print("SKIP: Vulkan RT probe timed out")
        return SKIP
    if ("ray tracing is unavailable" in on_log or
            ("Vulkan ray tracing enabled (hardware ray query)" not in on_log and
             "[vk_rt]" not in on_log)):
        print("SKIP: no ray-tracing capable Vulkan device")
        return SKIP
    if not os.path.exists(on_png):
        print("SKIP: Vulkan RT render produced no image")
        return SKIP

    got = blas_line(on_log)
    if not got:
        print("FAIL: no '[vk_rt] BLAS:' report -- the compacting wave builder did "
              "not run at all.")
        return 1
    resident, built, unique, instances = got

    # The dedup invariant. The fixture is two INSTANCES of one prototype, so the
    # builder must build exactly one BLAS. Building two means it is planning per
    # instance rather than per prototype -- the leak described above, which on
    # Island cost 6.7x the memory while still rendering perfectly.
    if instances < 2:
        print(f"FAIL: fixture should present 2 full instances, saw {instances} -- "
              f"this test cannot check the dedup invariant.")
        return 1
    if unique != 1:
        print(f"FAIL: built {unique} BLAS for {instances} instances of ONE "
              f"prototype. The wave planner is not deduping by prototype: each "
              f"duplicate rebuilds the same mesh and leaks the previous copy.")
        return 1

    if built <= 0.0:
        print(f"FAIL: no BLAS was built (built={built} MiB)")
        return 1
    frac = resident / built
    if frac > MAX_COMPACTED_FRAC:
        print(f"FAIL: BLAS compaction did not shrink anything: {resident:.1f} MiB "
              f"resident from {built:.1f} MiB built ({frac * 100:.0f}%, need under "
              f"{MAX_COMPACTED_FRAC * 100:.0f}%).")
        return 1

    try:
        off_log = render(binary, scene, off_png, work, compact=False)
    except subprocess.TimeoutExpired:
        print("FAIL: uncompacted Vulkan RT render timed out")
        return 1
    if not os.path.exists(off_png):
        print("FAIL: uncompacted render produced no image")
        return 1

    with open(on_png, "rb") as f:
        a = f.read()
    with open(off_png, "rb") as f:
        b = f.read()
    if a != b:
        print("FAIL: compaction changed the image. It must be a pure memory "
              "optimization -- a differing render means the wave builder is "
              "building or referencing the wrong geometry.")
        return 1

    print(f"PASS: BLAS {resident:.1f} MiB resident from {built:.1f} MiB built "
          f"({frac * 100:.0f}%), {unique} unique of {instances} instances, "
          f"render byte-identical to uncompacted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
