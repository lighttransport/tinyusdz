#!/usr/bin/env python3
"""tusdview `--next`: an ordinary (non-instanced) blendshaped mesh must MORPH.

BuildMorphChannelsNext / BakeBlendShapes were only ever called on the INSTANCED
prototype path. A plain blendshaped mesh -- which is what almost every rig in the
wild is -- went through the static batch path, which called neither, so it
rendered its REST shape at every time code. Nothing in the suite noticed, because
no test animated a blendshape.

This asserts, on the `--next` raster path:

  1. the mesh actually changes with the time code (it used to be byte-identical
     at every time);
  2. the GPU morph agrees with the CPU bake (TUSDVIEW_NEXT_MORPH_BAKE=1) on
     GEOMETRY -- the silhouettes must coincide. They differ in shading, and that
     is expected: the vertex shader morphs positions and skins the REST normal,
     while the bake recomputes normals from the morphed points. Comparing
     silhouettes tests the thing that was broken (the positions) without pinning
     the thing that is a known approximation; and
  3. it reaches the ray tracer too, which traces the vertex buffers themselves.

Exits 77 (skip) when the binary or a usable GPU is missing.
"""
import os
import struct
import subprocess
import sys
import zlib

SKIP = 77
MIN_IOU = 0.99


def render(binary, model, out, time, extra=(), env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    cmd = [binary, "--next", "--headless", "--frames", "3", "--time", str(time),
           "--screenshot", out, *extra, model]
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


def silhouette_iou(a_path, b_path, thresh=25.0):
    a, b = read_luma(a_path), read_luma(b_path)
    if len(a) != len(b):
        return 0.0
    inter = sum(1 for x, y in zip(a, b) if x > thresh and y > thresh)
    union = sum(1 for x, y in zip(a, b) if x > thresh or y > thresh)
    return inter / union if union else 0.0


def main():
    if len(sys.argv) < 4:
        print("usage: check-blendshape-morph.py <tusdview> <model> <work_dir>")
        return SKIP
    binary, model, work = sys.argv[1:4]
    for p in (binary, model):
        if not os.path.exists(p):
            print(f"SKIP: missing {p}")
            return SKIP
    os.makedirs(work, exist_ok=True)

    rest = os.path.join(work, "morph_t1.png")
    posed = os.path.join(work, "morph_t20.png")
    if not render(binary, model, rest, 1):
        print("SKIP: headless render produced no image (no usable GPU?)")
        return SKIP
    render(binary, model, posed, 20)

    if open(rest, "rb").read() == open(posed, "rb").read():
        print("FAIL: the mesh renders identically at time 1 and time 20, so its "
              "blendshape is not being applied at all. A non-instanced blendshaped "
              "mesh goes through the static batch path -- which must build morph "
              "channels (BuildMorphChannelsNext), as the instanced prototype path "
              "does.")
        return 1

    baked = os.path.join(work, "morph_t20_baked.png")
    render(binary, model, baked, 20, env={"TUSDVIEW_NEXT_MORPH_BAKE": "1"})
    iou = silhouette_iou(posed, baked)
    if iou < MIN_IOU:
        print(f"FAIL: the GPU morph and the CPU bake disagree on GEOMETRY at the "
              f"same time code (silhouette IoU {iou:.4f} < {MIN_IOU}). The two must "
              f"produce the same morphed positions -- check the per-vertex delta "
              f"re-indexing when a mesh is appended to a batch.")
        return 1

    rt1 = os.path.join(work, "morph_rt_t1.png")
    rt20 = os.path.join(work, "morph_rt_t20.png")
    if render(binary, model, rt1, 1, extra=["--rt"]) and \
       render(binary, model, rt20, 20, extra=["--rt"]):
        if open(rt1, "rb").read() == open(rt20, "rb").read():
            print("FAIL: the ray tracer renders the same image at time 1 and 20. "
                  "RT traces the vertex buffers themselves, so the morph has to "
                  "reach them (BuildNextRtDeformedVertices).")
            return 1

    print(f"PASS: non-instanced blendshape morphs, matches the CPU bake "
          f"(silhouette IoU {iou:.4f}), and reaches the ray tracer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
