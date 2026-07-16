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
MAX_LOADER_DIFF = 0.5  # next vs legacy: same deform, same bounds, same frame


def render(binary, scene, out, time, camera, extra=(), env=None, backend=(),
           loader="--next"):
    e = dict(os.environ)
    if env:
        e.update(env)
    cmd = [binary, loader, "--headless", "--mode", "depth", "--camera", camera,
           "--frames", "3", "--time", str(time), "--screenshot", out,
           *backend, *extra, scene]
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
              "[backend] [camera] [rest_time] [pose_time]")
        return SKIP
    binary, scene, work = sys.argv[1:4]
    which = sys.argv[4] if len(sys.argv) > 4 else "raster"
    camera = sys.argv[5] if len(sys.argv) > 5 else "Cam"
    rest_t = sys.argv[6] if len(sys.argv) > 6 else "1"
    pose_t = sys.argv[7] if len(sys.argv) > 7 else "20"
    # The CUDA/HIP tracers build their BVH from draw_ geometry, so they take the
    # deform through poseNextDrawForTracer rather than the vertex shader. Same
    # claim, different plumbing -- and until that landed they were pinned to the
    # load-time CPU bake, a SECOND deform implementation free to disagree with the
    # shader's (it did: it skinned before it morphed).
    backend = [] if which == "raster" else [f"--{which}"]
    for p in (binary, scene):
        if not os.path.exists(p):
            print(f"SKIP: missing {p}")
            return SKIP
    os.makedirs(work, exist_ok=True)

    tag = f"{os.path.splitext(os.path.basename(scene))[0]}_{which}"
    rest = os.path.join(work, f"{tag}_rest.png")
    gpu = os.path.join(work, f"{tag}_gpu.png")
    cpu = os.path.join(work, f"{tag}_cpu.png")

    if not render(binary, scene, rest, rest_t, camera, backend=backend):
        print(f"SKIP: the {which} backend produced no image (unavailable here?)")
        return SKIP
    if not render(binary, scene, gpu, pose_t, camera, backend=backend):
        print(f"SKIP: the {which} backend produced no image (unavailable here?)")
        return SKIP
    # The reference: every deform baked on the CPU, in mesh-local space -- through
    # the SAME backend, so this compares deforms and not backends.
    if not render(binary, scene, cpu, pose_t, camera, backend=backend,
                  extra=["--skinning", "cpu"],
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

    # The two LOADERS must also agree, and not just on the mesh: the whole frame.
    # The scene box drives the ground grid, the depth normalization and the
    # auto-fit, and the next loader used to take it from the 8 corners of each
    # mesh's local bbox pushed through its world matrix (loose under rotation) and
    # then never refresh it after the deform (so an animated load framed on the
    # REST pose). Both are now derived the way the Tydra path derives them: from
    # the posed vertices.
    legacy = os.path.join(work, f"{tag}_legacy.png")
    if which == "raster" and render(binary, scene, legacy, pose_t, camera,
                                    loader="--legacy-load"):
        ldiff = mean_diff(gpu, legacy)
        if ldiff > MAX_LOADER_DIFF:
            print(f"FAIL: {tag}: the next and legacy loaders do not render the same "
                  f"frame (mean depth diff {ldiff:.3f} > {MAX_LOADER_DIFF}) even "
                  f"though the next deform matches its own CPU bake ({diff:.3f}). "
                  f"That points at the SCENE BOUNDS, not the deform: the next "
                  f"loader has to take its box from the batches' vertices (not from "
                  f"corner-transformed local bboxes) AND refresh it for the pose at "
                  f"each time code (BuildNextPosedSceneBounds), or the grid and the "
                  f"depth ramp sit somewhere the legacy path does not put them.")
            return 1
        print(f"PASS: {tag}: GPU deform matches the CPU bake (mean depth diff "
              f"{diff:.3f}; the deform moves depth by {pose:.3f}); the legacy loader "
              f"renders the same frame ({ldiff:.3f})")
        return 0

    print(f"PASS: {tag}: GPU deform matches the CPU bake (mean depth diff "
          f"{diff:.3f}; the deform moves depth by {pose:.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
