#!/usr/bin/env python3
"""Smoke-test tusdrender output without third-party Python packages."""

import struct
import subprocess
import sys
import zlib
from pathlib import Path


def read_png_rgba(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError("not a PNG file")
    pos = 8
    width = height = None
    color_type = bit_depth = None
    idat = bytearray()
    while pos + 8 <= len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif typ == b"IDAT":
            idat.extend(payload)
        elif typ == b"IEND":
            break
    if width is None or height is None:
        raise RuntimeError("missing IHDR")
    if bit_depth != 8 or color_type != 6:
        raise RuntimeError(f"expected 8-bit RGBA PNG, got depth={bit_depth} color={color_type}")
    raw = zlib.decompress(bytes(idat))
    stride = width * 4
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(height):
        f = raw[p]
        p += 1
        row = bytearray(raw[p:p + stride])
        p += stride
        for i in range(stride):
            left = row[i - 4] if i >= 4 else 0
            up = prev[i]
            up_left = prev[i - 4] if i >= 4 else 0
            if f == 1:
                row[i] = (row[i] + left) & 0xFF
            elif f == 2:
                row[i] = (row[i] + up) & 0xFF
            elif f == 3:
                row[i] = (row[i] + ((left + up) >> 1)) & 0xFF
            elif f == 4:
                pa = abs(up - up_left)
                pb = abs(left - up_left)
                pc = abs(left + up - 2 * up_left)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
                row[i] = (row[i] + pred) & 0xFF
            elif f != 0:
                raise RuntimeError(f"unsupported PNG filter {f}")
        rows.append(bytes(row))
        prev = row
    return width, height, b"".join(rows)


def write_png_rgba(path, width, height, rgba):
    def chunk(typ, payload):
        return (
            struct.pack(">I", len(payload))
            + typ
            + payload
            + struct.pack(">I", zlib.crc32(typ + payload) & 0xFFFFFFFF)
        )

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw)))
        + chunk(b"IEND", b"")
    )


def main():
    if len(sys.argv) != 4:
        print("usage: check_tusdrender_smoke.py <tusdrender> <srcdir> <outdir>", file=sys.stderr)
        return 2
    exe = sys.argv[1]
    srcdir = Path(sys.argv[2])
    outdir = Path(sys.argv[3])
    outdir.mkdir(parents=True, exist_ok=True)
    out = outdir / "tusdrender-smoke.png"
    cmd = [
        exe,
        str(srcdir / "tests/usda/suzanne.usda"),
        str(out),
        "-w",
        "64",
        "-height",
        "64",
        "-ambient",
        "0.08",
    ]
    subprocess.run(cmd, check=True)
    w, h, rgba = read_png_rgba(out)
    if (w, h) != (64, 64):
        raise RuntimeError(f"unexpected dimensions {(w, h)}")
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("render appears blank")

    subdiv_scene = outdir / "tusdrender-subdiv.usda"
    subdiv_scene.write_text("""#usda 1.0

def Mesh "SubdivQuad"
{
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    color3f[] primvars:displayColor = [(0.8, 0.35, 0.15)] (
        interpolation = "constant"
    )
    uniform token subdivisionScheme = "catmullClark"
}
""")
    subdiv_out = outdir / "tusdrender-subdiv.png"
    subdiv_cmd = [
        exe,
        str(subdiv_scene),
        str(subdiv_out),
        "-w",
        "32",
        "-height",
        "32",
        "-subdiv",
        "1",
        "-stats",
    ]
    stats = subprocess.run(
        subdiv_cmd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if "subdivision level: 1" not in stats.stderr:
        raise RuntimeError("subdivision level was not reported in stats")
    if "triangles: 8" not in stats.stderr:
        raise RuntimeError(f"unexpected subdiv triangle stats:\n{stats.stderr}")
    w, h, rgba = read_png_rgba(subdiv_out)
    if (w, h) != (32, 32):
        raise RuntimeError(f"unexpected subdiv dimensions {(w, h)}")
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("subdivision render appears blank")

    rt_out = outdir / "tusdrender-rt-preview.png"
    rt_cmd = [
        exe,
        str(subdiv_scene),
        str(rt_out),
        "-w",
        "32",
        "-height",
        "32",
        "-rtPreview",
        "-stats",
    ]
    rt_stats = subprocess.run(
        rt_cmd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    for expected in (
        "rt preview: 1",
        "rt meshes: 1",
        "triangles: 2",
    ):
        if expected not in rt_stats.stderr:
            raise RuntimeError(f"missing RT preview stat {expected!r}:\n{rt_stats.stderr}")
    w, h, rgba = read_png_rgba(rt_out)
    if (w, h) != (32, 32):
        raise RuntimeError(f"unexpected RT preview dimensions {(w, h)}")
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("RT preview render appears blank")

    # PointInstancer expansion in the -rtPreview (next) path: each prototype is a
    # deduped BLAS and each visible instance a TLAS placement. Three instances of
    # a one-triangle prototype must expand to 3 visible / 1 unique triangle.
    pi_scene = outdir / "tusdrender-pointinstancer.usda"
    pi_scene.write_text("""#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)
def Xform "World"
{
    def PointInstancer "Instancer"
    {
        point3f[] positions = [(-3, 0, 0), (0, 0, 0), (3, 0, 0)]
        int[] protoIndices = [0, 0, 0]
        quatf[] orientations = [(0, 0, 0, 1), (0, 0, 0, 1), (0, 0, 0, 1)]
        float3[] scales = [(1, 1, 1), (1, 1, 1), (1, 1, 1)]
        rel prototypes = [</World/Instancer/Proto>]

        def Mesh "Proto"
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
        }
    }
}
""")
    pi_out = outdir / "tusdrender-pointinstancer.png"
    pi_stats = subprocess.run(
        [exe, str(pi_scene), str(pi_out), "-w", "32", "-height", "32",
         "-rtPreview", "-stats"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    for expected in (
        "rt instancing: tlas",
        "rt point instancers: 1",
        "rt point instances: 3",
        "rt unique triangles: 1",
        "triangles: 3",
    ):
        if expected not in pi_stats.stderr:
            raise RuntimeError(
                f"missing PointInstancer stat {expected!r}:\n{pi_stats.stderr}")
    w, h, rgba = read_png_rgba(pi_out)
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("PointInstancer RT preview render appears blank")

    # BasisCurves ray tracing in the -rtPreview path (curves build into the
    # DirectScene as LightRT hair strands), including curve-prototype instancing
    # (a PointInstancer whose prototype is a BasisCurves is baked per instance).
    curve_scene = outdir / "tusdrender-curves.usda"
    curve_scene.write_text("""#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)
def Xform "World"
{
    def Mesh "Ground"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(-10, 0, -10), (10, 0, -10), (0, 0, 10)]
    }
    def BasisCurves "Hair"
    {
        uniform token type = "linear"
        int[] curveVertexCounts = [3]
        point3f[] points = [(-6, 0, 0), (-6, 3, 0), (-6, 6, 0)]
        float[] widths = [0.3, 0.2, 0.1]
    }
    def PointInstancer "Grass"
    {
        point3f[] positions = [(0, 0, 0), (4, 0, 0)]
        int[] protoIndices = [0, 0]
        rel prototypes = [</World/Grass/Blade>]
        def BasisCurves "Blade"
        {
            uniform token type = "linear"
            int[] curveVertexCounts = [3]
            point3f[] points = [(0, 0, 0), (0, 2, 0), (0, 4, 0)]
            float[] widths = [0.2, 0.15, 0.05]
        }
    }
}
""")
    curve_out = outdir / "tusdrender-curves.png"
    curve_stats = subprocess.run(
        [exe, str(curve_scene), str(curve_out), "-w", "64", "-height", "64",
         "-rtPreview", "-stats", "-autoframe"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    # 1 direct BasisCurves "Hair" (DirectScene) + a curve-prototype instancer
    # whose Blade curve geometry is stored once and TLAS-instanced 2×.
    for expected in (
        "rt curve strands: 1",
        "rt curve instances: 2",
        "rt point instances: 2",
        "rt instancing: tlas",
    ):
        if expected not in curve_stats.stderr:
            raise RuntimeError(
                f"missing curve stat {expected!r}:\n{curve_stats.stderr}")
    w, h, rgba = read_png_rgba(curve_out)
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("curve RT preview render appears blank")

    # primvars:displayColor (constant) as base color + primvars:displayOpacity
    # (constant) see-through blend: a 0.5-opacity green quad in front of an opaque
    # red quad -> the overlap blends to a red+green mix (both channels present).
    disp_scene = outdir / "tusdrender-display.usda"
    disp_scene.write_text("""#usda 1.0
(
    defaultPrim = "root"
    upAxis = "Y"
)
def Xform "root"
{
    def Mesh "back_red" {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-2, -2, 2), (2, -2, 2), (2, 2, 2), (-2, 2, 2)]
        color3f[] primvars:displayColor = [(1, 0, 0)] (interpolation = "constant")
    }
    def Mesh "front_green_glass" {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1.5, -1.5, -2), (1.5, -1.5, -2), (1.5, 1.5, -2), (-1.5, 1.5, -2)]
        color3f[] primvars:displayColor = [(0, 1, 0)] (interpolation = "constant")
        float[] primvars:displayOpacity = [0.5] (interpolation = "constant")
    }
}
""")
    disp_out = outdir / "tusdrender-display.png"
    subprocess.run(
        [exe, str(disp_scene), str(disp_out), "-w", "64", "-height", "64",
         "-rtPreview", "-viewDir", "0,0,-1", "-fitScale", "1.4", "-ambient", "1"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    w, h, rgba = read_png_rgba(disp_out)
    cx, cy = w // 2, h // 2
    o = (cy * w + cx) * 4
    cr, cg, cb = rgba[o], rgba[o + 1], rgba[o + 2]
    # Front glass is green, back is red; the blended overlap must show BOTH the
    # green (displayColor) and red-through (displayOpacity) — not pure green.
    if not (cg > 40 and cr > 40):
        raise RuntimeError(
            f"displayColor/displayOpacity blend wrong: center RGB=({cr},{cg},{cb})")

    # Per-vertex displayColor: a quad with a distinct color per vertex must
    # interpolate (opposite corners differ markedly).
    vcol_scene = outdir / "tusdrender-vcolor.usda"
    vcol_scene.write_text("""#usda 1.0
(
    defaultPrim = "root"
    upAxis = "Y"
)
def Mesh "root"
{
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-2, -2, 0), (2, -2, 0), (2, 2, 0), (-2, 2, 0)]
    color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0)] (interpolation = "vertex")
}
""")
    vcol_out = outdir / "tusdrender-vcolor.png"
    subprocess.run(
        [exe, str(vcol_scene), str(vcol_out), "-w", "64", "-height", "64",
         "-rtPreview", "-viewDir", "0,0,-1", "-fitScale", "1.1", "-ambient", "1"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    w, h, rgba = read_png_rgba(vcol_out)
    # Sample inside the quad near opposite corners; per-vertex interpolation must
    # produce clearly different colors (constant would make them identical).
    def px(fx, fy):
        x, y = int(w * fx), int(h * fy)
        o = (y * w + x) * 4
        return (rgba[o], rgba[o + 1], rgba[o + 2])
    p_bl, p_tr = px(0.3, 0.7), px(0.7, 0.3)
    if sum(abs(a - b) for a, b in zip(p_bl, p_tr)) < 60:
        raise RuntimeError(
            f"per-vertex displayColor not interpolated: {p_bl} vs {p_tr}")

    env_png = outdir / "tusdrender-env.png"
    env_pixels = bytearray()
    for y in range(2):
        for x in range(4):
            env_pixels.extend([
                32 + x * 45,
                64 + y * 80,
                180 - x * 25,
                255,
            ])
    write_png_rgba(env_png, 4, 2, env_pixels)
    ibl_scene = outdir / "tusdrender-ibl.usda"
    ibl_scene.write_text("""#usda 1.0

def Xform "World"
{
    def Mesh "Quad"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        color3f[] primvars:displayColor = [(0.65, 0.65, 0.65)] (
            interpolation = "constant"
        )
    }
    def DomeLight "Sky"
    {
        asset inputs:texture:file = @tusdrender-env.png@
        token inputs:texture:format = "latlong"
        float inputs:intensity = 1
    }
}
""")
    ibl_out = outdir / "tusdrender-ibl.png"
    ibl_cmd = [
        exe,
        str(ibl_scene),
        str(ibl_out),
        "-w",
        "32",
        "-height",
        "32",
        "-ambient",
        "0.0",
        "-stats",
    ]
    ibl_stats = subprocess.run(
        ibl_cmd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    for expected in (
        "domelight: 1",
        "ibl envmap: 1",
        "ibl diffuse size: 512",
        "ibl prefilter levels: 5",
        "ibl brdf lut size: 4096",
    ):
        if expected not in ibl_stats.stderr:
            raise RuntimeError(f"missing IBL stat {expected!r}:\n{ibl_stats.stderr}")
    w, h, rgba = read_png_rgba(ibl_out)
    if (w, h) != (32, 32):
        raise RuntimeError(f"unexpected IBL dimensions {(w, h)}")
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("IBL render appears blank")

    direct_scene = outdir / "tusdrender-direct-prims.usda"
    direct_scene.write_text("""#usda 1.0

def Xform "World"
{
    def Sphere "Ball"
    {
        double radius = 0.45
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (-1.2, 0, 0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Cylinder "Tube"
    {
        double radius = 0.25
        double height = 1.2
        uniform token axis = "Y"
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Cone "Hat"
    {
        double radius = 0.35
        double height = 1.0
        uniform token axis = "Y"
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (1.1, 0, 0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Capsule "Pill"
    {
        double radius = 0.18
        double height = 0.9
        uniform token axis = "Y"
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (1.8, 0, 0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Cube "Box"
    {
        double size = 0.5
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (-1.8, -0.7, 0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Plane "Ground"
    {
        double width = 3.5
        double length = 0.35
        uniform token axis = "Z"
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, -1.05, 0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Points "Dots"
    {
        point3f[] points = [(-1.4, 0.75, 0), (-1.1, 0.85, 0), (-0.8, 0.78, 0)]
        float[] widths = [0.12, 0.10, 0.12]
    }
    def TetMesh "Tet"
    {
        point3f[] points = [(-1.55, -0.2, 0), (-1.15, -0.2, 0), (-1.35, 0.25, 0), (-1.35, 0.0, 0.45)]
        int4[] tetVertexIndices = [(0, 1, 2, 3)]
    }
    def Xform "Proto"
    {
        def Cube "ProtoBox"
        {
            double size = 0.18
        }
    }
    def PointInstancer "Instancer"
    {
        rel prototypes = </World/Proto>
        int[] protoIndices = [0, 0]
        point3f[] positions = [(0.9, 0.85, 0), (1.25, 0.85, 0)]
        float3[] scales = [(1, 1, 1), (1.5, 1.5, 1.5)]
    }
    def BasisCurves "Hair"
    {
        uniform token type = "linear"
        int[] curveVertexCounts = [3]
        point3f[] points = [(-0.6, -0.8, 0), (-0.55, 0.0, 0.05), (-0.4, 0.8, 0)]
        float[] widths = [0.08, 0.08, 0.08]
    }
    def BasisCurves "Ribbon"
    {
        uniform token type = "linear"
        int[] curveVertexCounts = [2]
        point3f[] points = [(0.35, -0.75, 0), (0.55, 0.75, 0)]
        normal3f[] normals = [(0, 0, 1), (0, 0, 1)]
        float[] widths = [0.12, 0.12]
    }
    def HermiteCurves "Hermite"
    {
        int[] curveVertexCounts = [2]
        point3f[] points = [(0.75, -0.75, 0), (0.95, 0.65, 0)]
        vector3f[] tangents = [(0.8, 0.4, 0.25), (-0.5, 0.6, -0.15)]
        float[] widths = [0.08, 0.10]
    }
    def NurbsPatch "Patch"
    {
        int uVertexCount = 3
        int vVertexCount = 3
        int uOrder = 2
        int vOrder = 2
        double[] uKnots = [0, 0, 1, 2, 2]
        double[] vKnots = [0, 0, 1, 2, 2]
        point3f[] points = [(-0.5, -0.4, -0.45), (0, -0.5, -0.55), (0.5, -0.4, -0.45),
                            (-0.5, 0, -0.55), (0, 0.1, -0.65), (0.5, 0, -0.55),
                            (-0.5, 0.4, -0.45), (0, 0.5, -0.55), (0.5, 0.4, -0.45)]
    }
    def DomeLight "Sky"
    {
        color3f inputs:color = (0.03, 0.05, 0.08)
        float inputs:intensity = 1
    }
    def RectLight "Panel"
    {
        color3f inputs:color = (1, 0.85, 0.65)
        float inputs:intensity = 80
        float inputs:width = 1.0
        float inputs:height = 0.6
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 1.6, 1.0, 1) )
        uniform token[] xformOpOrder = ["xformOp:transform"]
    }
    def Mesh "Emitter" (
        prepend apiSchemas = ["MeshLightAPI"]
    )
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-0.2, 1.1, 0.2), (0.2, 1.1, 0.2), (0.2, 1.1, -0.2), (-0.2, 1.1, -0.2)]
        color3f inputs:color = (0.8, 0.95, 1.0)
        float inputs:intensity = 25
    }
}
""")
    direct_out = outdir / "tusdrender-direct-prims.png"
    direct_cmd = [
        exe,
        str(direct_scene),
        str(direct_out),
        "-w",
        "64",
        "-height",
        "64",
        "-ambient",
        "0.15",
        "-stats",
    ]
    stats = subprocess.run(
        direct_cmd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    for expected in (
        "direct spheres: 1",
        "direct round curve segments: 2",
        "direct flat curve segments: 1",
        "direct Hermite/Bezier curve segments: 1",
        "direct points: 3",
        "direct tetrahedra: 1",
        "direct analytic shapes: 3",
        "lights: 1",
        "mesh light triangles: 2",
        "domelight: 1",
        "light sampling finite cdf entries: 1",
        "light sampling mesh cdf entries: 2",
    ):
        if expected not in stats.stderr:
            raise RuntimeError(f"missing direct primitive stat {expected!r}:\n{stats.stderr}")
    w, h, rgba = read_png_rgba(direct_out)
    if (w, h) != (64, 64):
        raise RuntimeError(f"unexpected direct dimensions {(w, h)}")
    pixels = [rgba[i:i + 4] for i in range(0, len(rgba), 4)]
    if len(set(pixels)) <= 1:
        raise RuntimeError("direct primitive render appears blank")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
