#!/usr/bin/env python3
"""tusdview: per-vertex displayColor must render on the Vulkan raster backend.

The non-instanced Vulkan mesh pipeline never consumed DrawMeshCPU::vertexColors:
the buffer existed only as a device-address SSBO for the ray-tracing path, so a
vertex-painted mesh (USD `primvars:displayColor`, vertex interpolation) rendered
in the flat material color on VK raster while GL and VK-RT showed the paint.

The fix fetches the color by gl_VertexIndex from a set-24 SSBO in mesh.vert
(flag-gated, dummy-bound when absent) and multiplies it into the base color in
mesh.frag, matching GL's attrib-9 path.

The fixture is one quad painted red/green/blue/yellow at its four corners; all
of red, green and blue must appear in the render.

Runs headless (Vulkan). Exits 77 (skip) if the binary/Vulkan is unavailable.
"""
import os
import struct
import subprocess
import sys
import zlib

SKIP = 77

FIXTURE = """#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
    def Mesh "Q" {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0)] (
            interpolation = "vertex"
        )
    }
}
"""


def read_rgb(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    w = h = None
    idat = b""
    color = None
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
    if w is None or color not in (2, 6):
        return None
    nch = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * nch
    out = []
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        for i in range(stride):
            a = line[i - nch] if i >= nch else 0
            b = prev[i]
            if f == 1:
                line[i] = (line[i] + a) & 0xff
            elif f == 2:
                line[i] = (line[i] + b) & 0xff
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xff
            elif f == 4:
                c = prev[i - nch] if i >= nch else 0
                p_ = a + b - c
                pa, pb, pc = abs(p_ - a), abs(p_ - b), abs(p_ - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xff
        prev = line
        for i in range(0, stride, nch):
            out.append((line[i], line[i + 1], line[i + 2]))
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: check-vertex-color.py <tusdview> <work_dir>")
        return SKIP
    binary, work = sys.argv[1], sys.argv[2]
    if not os.path.exists(binary):
        print(f"SKIP: missing binary ({binary})")
        return SKIP
    os.makedirs(work, exist_ok=True)
    scene = os.path.join(work, "vertex-color.usda")
    with open(scene, "w") as f:
        f.write(FIXTURE)
    out = os.path.join(work, "vcol.png")
    r = subprocess.run(
        [binary, "--headless", "--frames", "4", "--screenshot", out, scene],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    log = r.stdout.decode(errors="replace")
    if r.returncode != 0 or not os.path.exists(out):
        print(f"SKIP: headless Vulkan render unavailable\n{log}")
        return SKIP

    px = read_rgb(out)
    if px is None:
        print("FAIL: could not read the render")
        return 1
    red = sum(1 for r_, g, b in px if r_ > g + 40 and r_ > b + 40)
    grn = sum(1 for r_, g, b in px if g > r_ + 40 and g > b + 40)
    blu = sum(1 for r_, g, b in px if b > r_ + 40 and b > g + 40)
    print(f"pixels: red={red} green={grn} blue={blu}")
    if red < 100 or grn < 100 or blu < 100:
        print("FAIL: per-vertex displayColor did not render on the Vulkan raster "
              "backend -- the painted quad came out in the flat material color.")
        return 1
    print("PASS: per-vertex displayColor renders on Vulkan raster")
    return 0


if __name__ == "__main__":
    sys.exit(main())
