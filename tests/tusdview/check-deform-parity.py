#!/usr/bin/env python3
"""tusdview `--next`: the GPU deform must land where the CPU bake lands.

One harness, one claim, run over several fixtures: whatever the vertex shader
does per frame (blendshape morph, then linear-blend skinning) has to put the
geometry in the SAME PLACE as baking those deforms into the mesh on the CPU. The
CPU bake is the reference because it works in mesh-local space on the authored
points, before any of the batching, welding or world-baking the GPU path depends
on -- so the two agree only if that machinery is right.

Everything is rendered in `--mode depth` through a FIXED USD camera:

  * depth is purely geometric, so the comparison is not polluted by the known,
    by-design shading difference (the GPU path skins the REST normal; the bake
    recomputes normals from the deformed points); and
  * a fixed camera, because the two paths legitimately frame the scene
    differently under auto-fit -- the GPU path pads the mesh box by `morphExtent`
    so a morphed mesh is never frustum-culled, while the bake's box is exact.
    Auto-framing would move the camera between the two renders and swamp any real
    geometric difference. (An earlier version of this comparison thresholded luma
    at 25 to get a silhouette; the background grid is brighter than that, so the
    mask covered the whole frame and the check was vacuously true.)

It also asserts the fixture DEFORMS at all (rest vs posed depth), so the parity
check above can never go quietly vacuous.

Exits 77 (skip) when the binary, the scene or a usable GPU is missing.
"""
import os
import struct
import subprocess
import sys
import zlib

SKIP = 77
MAX_MEAN_DIFF = 0.5   # GPU deform vs CPU bake: same geometry, bar raster edges
MIN_POSE_DIFF = 1.0   # rest vs posed: the deform must actually move something


def render(binary, scene, out, time, camera, extra=(), env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    cmd = [binary, "--next", "--headless", "--mode", "depth", "--camera", camera,
           "--frames", "3", "--time", str(time), "--screenshot", out, *extra, scene]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   env=e, timeout=600)
    return os.path.exists(out) and os.path.getsize(out) > 0


def read_luma(path):
    d = open(path, "rb").read()
    w = h = color = None
    idat = b""
    pos = 8
    while pos + 8 <= len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, _bd, color = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    nch = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * nch
    out, prev, p = [], bytearray(stride), 0
    for _y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        for i in range(stride):
            a = line[i - nch] if i >= nch else 0
            b = prev[i]
            c = prev[i - nch] if i >= nch else 0
            if f == 1: line[i] = (line[i] + a) & 0xFF
            elif f == 2: line[i] = (line[i] + b) & 0xFF
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        for x in range(w):
            px = line[x * nch:x * nch + 3]
            out.append((px[0] + px[1] + px[2]) / 3.0)
        prev = line
    return out


def mean_diff(a_path, b_path):
    a, b = read_luma(a_path), read_luma(b_path)
    if len(a) != len(b):
        return float("inf")
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main():
    if len(sys.argv) < 4:
        print("usage: check-deform-parity.py <tusdview> <scene.usda> <work_dir> "
              "[camera] [rest_time] [pose_time]")
        return SKIP
    binary, scene, work = sys.argv[1:4]
    camera = sys.argv[4] if len(sys.argv) > 4 else "Cam"
    rest_t = sys.argv[5] if len(sys.argv) > 5 else "1"
    pose_t = sys.argv[6] if len(sys.argv) > 6 else "20"
    for p in (binary, scene):
        if not os.path.exists(p):
            print(f"SKIP: missing {p}")
            return SKIP
    os.makedirs(work, exist_ok=True)

    tag = os.path.splitext(os.path.basename(scene))[0]
    rest = os.path.join(work, f"{tag}_rest.png")
    gpu = os.path.join(work, f"{tag}_gpu.png")
    cpu = os.path.join(work, f"{tag}_cpu.png")

    if not render(binary, scene, rest, rest_t, camera):
        print("SKIP: headless render produced no image (no usable GPU?)")
        return SKIP
    if not render(binary, scene, gpu, pose_t, camera):
        print("SKIP: headless render produced no image (no usable GPU?)")
        return SKIP
    # The reference: every deform baked on the CPU, in mesh-local space.
    if not render(binary, scene, cpu, pose_t, camera, extra=["--skinning", "cpu"],
                  env={"TUSDVIEW_NEXT_MORPH_BAKE": "1"}):
        print("SKIP: the CPU-bake reference did not render")
        return SKIP

    pose = mean_diff(rest, gpu)
    if pose < MIN_POSE_DIFF:
        print(f"FAIL: {tag}: the mesh barely moves between time {rest_t} and time "
              f"{pose_t} (mean depth diff {pose:.3f} < {MIN_POSE_DIFF}), so it is not "
              f"being deformed at all -- and the parity check below would be "
              f"vacuous.")
        return 1

    diff = mean_diff(gpu, cpu)
    if diff > MAX_MEAN_DIFF:
        print(f"FAIL: {tag}: the GPU deform and the CPU bake put the geometry in "
              f"DIFFERENT places (mean depth diff {diff:.3f} > {MAX_MEAN_DIFF}; the "
              f"deform itself moves depth by {pose:.3f}). They must agree exactly. "
              f"Suspects, in the order they bite: the deform ORDER (a blendshape "
              f"deforms the bind-space points and the skeleton poses the RESULT, "
              f"never the other way round); the SPACE (the static batch path "
              f"world-bakes its vertices, so morph deltas and bone rows both have to "
              f"be carried through that transform); and the per-batch RE-INDEXING of "
              f"the sparse morph delta lists when a GeomSubset splits a mesh.")
        return 1

    print(f"PASS: {tag}: GPU deform matches the CPU bake (mean depth diff "
          f"{diff:.3f}; the deform moves depth by {pose:.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
