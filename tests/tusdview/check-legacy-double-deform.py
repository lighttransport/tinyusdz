#!/usr/bin/env python3
"""tusdview LEGACY loader: an animated `--time` load must be posed ONCE.

`ConvertStageToScene` bakes the pose into the geometry at load (DeformSkinnedMeshes)
whenever the time code is finite -- i.e. on every animated screenshot. The app then
picks a skinning mode, and if that mode is GPU, the vertex shader (or, under --rt,
BuildRtSkinnedMeshVertices) deforms the geometry a SECOND time.

The rest-pose load that exists to prevent exactly this was gated on an EXPLICIT
`--skinning gpu`. The default is Auto -- which is what everyone actually runs -- so
the default path baked the pose and then posed the baked geometry again. A 60-degree
joint bend came out as 120 degrees.

Asserted on a fixture whose SkelRoot is rotated and non-uniformly scaled (the case
that makes a double deform unmistakable rather than a subtle overshoot): the default
raster path and the ray tracer must both land where the CPU bake lands. Compared as
a silhouette of the mesh in `--mode material-id` through a fixed USD camera with
`--no-grid`. Material ID is geometry-only and remains stable now that the RT pass
exports primary-hit depth for depth-tested overlays; using the depth AOV itself
would conflate pose parity with the backend-specific depth visualization curve.

Before the fix the cube silhouettes overlapped 0.43 (raster) and 0.42 (RT) against
the bake; after, 0.96 and 0.92.

Exits 77 (skip) when the binary or a usable GPU is missing.
"""
import os
import struct
import subprocess
import sys
import zlib

from gpu_backend import software_only_vulkan

SKIP = 77
MIN_IOU = 0.90
# The raster path and the CPU bake render the same geometry through the same
# rasterizer, so once the deform is applied once and both agree on the scene bounds
# (depth is normalized by them) the two images are IDENTICAL. Not a tolerance to
# tune: it is 0.000 or something is wrong.
MAX_RASTER_MEAN = 0.5


def render(binary, scene, out, extra=(), env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    config = os.path.join(os.path.dirname(out), "config.json")
    if not os.path.exists(config):
        with open(config, "w") as f:
            f.write('{"window_size":{"width":320,"height":320}}\n')
    cmd = [binary, "--legacy-load", "--headless", "--mode", "material-id",
           "--camera", "Cam", "--no-grid", "--frames", "3", "--time", "20",
           "--config", config, "--screenshot", out, *extra, scene]
    try:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       env=e, timeout=120)
    except subprocess.TimeoutExpired:
        return False
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


def mesh_mask(path):
    # Material 0's stable ID color is ochre. This rejects the dark background
    # and the RGB axis gizmo while retaining antialiased silhouette pixels.
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
            r, g, b = line[x * nch:x * nch + 3]
            out.append(r > 90 and g > 60 and b < 90 and r > g * 1.2)
        prev = line
    return out


def iou(a, b):
    A, B = mesh_mask(a), mesh_mask(b)
    if len(A) != len(B):
        return 0.0
    union = sum(1 for x, y in zip(A, B) if x or y)
    if not union:
        return 0.0
    return sum(1 for x, y in zip(A, B) if x and y) / union


def mean_diff(a, b):
    A, B = read_luma(a), read_luma(b)
    if len(A) != len(B):
        return float("inf")
    return sum(abs(x - y) for x, y in zip(A, B)) / len(A)


def main():
    if len(sys.argv) < 4:
        print("usage: check-legacy-double-deform.py <tusdview> <scene.usda> <work_dir>")
        return SKIP
    binary, scene, work = sys.argv[1:4]
    for p in (binary, scene):
        if not os.path.exists(p):
            print(f"SKIP: missing {p}")
            return SKIP
    os.makedirs(work, exist_ok=True)

    ref = os.path.join(work, "cpu_bake.png")     # posed once, on the CPU
    auto = os.path.join(work, "raster_auto.png")  # the DEFAULT path
    rt = os.path.join(work, "rt.png")

    if not render(binary, scene, ref, extra=["--skinning", "cpu"]):
        print("SKIP: headless render produced no image (no usable GPU?)")
        return SKIP
    if not render(binary, scene, auto):
        print("SKIP: headless render produced no image (no usable GPU?)")
        return SKIP

    a = iou(auto, ref)
    if a < MIN_IOU:
        print(f"FAIL: the DEFAULT (auto) legacy path does not pose the mesh where "
              f"the CPU bake does (mesh silhouette IoU {a:.4f} < {MIN_IOU}). The "
              f"load bakes the pose into the geometry whenever the time code is "
              f"finite, so the rest-pose load (gpuRestLoad) has to cover every mode "
              f"that might deform again downstream -- Auto as much as an explicit "
              f"GPU -- or the shader poses the already-posed geometry and the bend "
              f"comes out twice as far.")
        return 1

    # Same geometry, same rasterizer, same camera: the whole frame must match, not
    # just the mesh. Depth is normalized by the scene bounds, and the ground grid is
    # drawn from them, so this also catches the two paths computing DIFFERENT bounds
    # for identical geometry -- which they did: the load-time box was the local AABB's
    # 8 corners pushed through the world matrix (a strict superset once the mesh is
    # rotated), while GPU skinning re-derived a tight box from the vertices.
    d = mean_diff(auto, ref)
    if d > MAX_RASTER_MEAN:
        print(f"FAIL: the default legacy raster path and the CPU bake do not render "
              f"the same frame (mean depth diff {d:.3f} > {MAX_RASTER_MEAN}), even "
              f"though the mesh silhouettes agree ({a:.4f}). The geometry matches, so "
              f"this is the SCENE BOUNDS: depth is normalized by them and the ground "
              f"grid is sized from them. PlaceDrawMesh must take the world box from "
              f"the vertices, as UpdateMeshBoundsFromVertices does -- not from the "
              f"local AABB's corners, which inflate under rotation.")
        return 1

    if not software_only_vulkan() and render(binary, scene, rt, extra=["--rt"]):
        r = iou(rt, ref)
        if r < MIN_IOU:
            print(f"FAIL: the ray tracer does not pose the mesh where the CPU bake "
                  f"does (mesh silhouette IoU {r:.4f} < {MIN_IOU}). RT re-poses the "
                  f"vertex buffers from draw_ geometry, so it double-deforms for "
                  f"exactly the same reason the raster path does.")
            return 1
        print(f"PASS: the legacy loader poses once -- raster IoU {a:.4f}, RT IoU "
              f"{r:.4f} against the CPU bake")
    else:
        print(f"PASS: the legacy loader poses once -- raster IoU {a:.4f} against the "
              f"CPU bake (RT unavailable)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
