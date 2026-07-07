# SPDX-License-Identifier: Apache-2.0
"""tinyusdz end-to-end tutorial: author -> save -> load -> inspect -> render.

Run:  python python/tutorial.py [output_dir]
"""
import sys
import tempfile
from pathlib import Path

import tinyusdz
from tinyusdz import tydra

try:
    import numpy as np
except ImportError:
    np = None


def author(out_dir: Path) -> Path:
    st = tinyusdz.Stage.create()
    st.up_axis = "Y"
    st.meters_per_unit = 1.0

    st.define_prim("/World", "Xform")
    grid = st.define_prim("/World/Grid", "Mesh")

    n = 8
    if np is not None:
        xs, ys = np.meshgrid(np.arange(n, dtype=np.float32),
                             np.arange(n, dtype=np.float32))
        points = np.dstack([xs, ys, np.zeros_like(xs)]).reshape(-1, 3)
        quads = []
        for y in range(n - 1):
            for x in range(n - 1):
                i = y * n + x
                quads += [i, i + 1, i + n + 1, i + n]
        grid.set("points", points, type="point3f[]")
        grid.set("faceVertexCounts",
                 np.full((n - 1) * (n - 1), 4, np.int32))
        grid.set("faceVertexIndices", np.array(quads, np.int32))
    else:
        grid.set("points", [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],
                 type="point3f[]")
        grid.set("faceVertexCounts", [4], type="int[]")
        grid.set("faceVertexIndices", [0, 1, 2, 3], type="int[]")

    grid.set("displayColor", [(0.8, 0.2, 0.1)], type="color3f[]")
    grid.set("xformOpOrder", ["xformOp:translate"], type="token[]",
             uniform=True)
    grid.set("xformOp:translate", (0.0, 0.0, 0.0), time=0.0)
    grid.set("xformOp:translate", (0.0, 3.0, 0.0), time=24.0)

    st.define_prim("/World/Looks/Red", "Material")
    grid.add_relationship("material:binding", ["/World/Looks/Red"])
    st.set_default_prim("World")

    out = out_dir / "tutorial.usdc"
    st.save(str(out))
    print(f"authored {out} ({len(st)} prims)")
    print(st.export_usda()[:400], "...\n")
    return out


def inspect(path: Path) -> tinyusdz.Stage:
    stage = tinyusdz.load(path)
    print(f"loaded {path}: {stage}")
    for prim in stage:
        print(f"  {prim.path:24} {prim.type_name}")

    grid = stage.prim_at("/World/Grid")
    pts = grid["points"]
    print(f"points: {pts}")
    if np is not None:
        print(f"  centroid: {np.asarray(pts).mean(axis=0)}")  # zero-copy

    attr = grid.attribute("xformOp:translate")
    if attr.has_timesamples:
        print(f"  translate times: {attr.timesamples.times}")
        print(f"  translate @ t=12: {attr.get(time=12.0)}")
    return stage


def render(stage: tinyusdz.Stage) -> None:
    scene = tydra.to_render_scene(stage, triangulate=True)
    print(scene)
    for mesh in scene.meshes:
        print(f"  {mesh}")
        if np is not None:
            v = np.asarray(mesh.points)
            t = np.asarray(mesh.triangulated_indices)
            print(f"    {len(v)} verts, {len(t) // 3} triangles")


def main() -> None:
    if len(sys.argv) > 1:
        out_dir = Path(sys.argv[1])
        out_dir.mkdir(parents=True, exist_ok=True)
        path = author(out_dir)
        render(inspect(path))
    else:
        with tempfile.TemporaryDirectory() as d:
            path = author(Path(d))
            render(inspect(path))


if __name__ == "__main__":
    main()
