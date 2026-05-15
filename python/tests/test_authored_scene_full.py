"""End-to-end: Python authors a non-trivial scene and round-trips."""
import tinyusdz


def test_full_scene_round_trip(tmp_path):
    s = tinyusdz.Stage()
    s.set_up_axis("Y")
    s.set_meters_per_unit(1.0)

    # World root
    world = tinyusdz.Prim("Xform", name="World")

    # Mesh with topology + xform + displayColor primvar
    mesh = tinyusdz.Prim("Mesh", name="Box")
    mesh.set_attribute("faceVertexCounts", [4, 4, 4, 4, 4, 4],
                       dtype="int[]")
    mesh.set_attribute("faceVertexIndices",
                       [0, 1, 2, 3, 4, 5, 6, 7,
                        0, 4, 5, 1, 1, 5, 6, 2,
                        2, 6, 7, 3, 3, 7, 4, 0],
                       dtype="int[]")
    mesh.set_attribute("points",
                       [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)],
                       dtype="point3f[]")
    world.add_child(mesh)

    # Camera
    cam = tinyusdz.Prim("Camera", name="MainCam")
    cam.set_attribute("focalLength", 50.0, dtype="float")
    cam.set_attribute("horizontalAperture", 36.0, dtype="float")
    world.add_child(cam)

    # Light
    light = tinyusdz.Prim("DistantLight", name="Sun")
    light.set_attribute("inputs:intensity", 1000.0, dtype="float")
    world.add_child(light)

    s.add_root_prim(world)
    s.set_default_prim("World")

    out = tmp_path / "scene.usdc"
    s.save(str(out))

    s2 = tinyusdz.load(str(out))
    assert s2.get_default_prim() == "World"
    assert s2.get_prim_at_path("/World/Box") is not None
    assert s2.get_prim_at_path("/World/MainCam") is not None
    assert s2.get_prim_at_path("/World/Sun") is not None

    # Render scene extraction
    rs = tinyusdz.tydra.convert_to_render_scene(s2)
    assert len(rs.meshes()) == 1
    assert len(rs.cameras()) == 1
    assert len(rs.lights()) == 1


def test_authored_scene_via_usda_string():
    s = tinyusdz.Stage()
    s.set_up_axis("Z")
    p = tinyusdz.Prim("Xform", name="Asset")
    p.set_attribute("custom_label", "demo")
    p.apply_api_schema("MaterialBindingAPI")
    s.add_root_prim(p)
    s.set_default_prim("Asset")

    txt = s.export_to_string()
    assert '"Asset"' in txt
    assert '"Z"' in txt
    assert "MaterialBindingAPI" in txt


def test_round_trip_preserves_mesh_topology(tmp_path):
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Mesh", name="M")
    m.set_attribute("faceVertexCounts", [3], dtype="int[]")
    m.set_attribute("faceVertexIndices", [0, 1, 2], dtype="int[]")
    m.set_attribute("points",
                    [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
                    dtype="point3f[]")
    s.add_root_prim(m)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "faceVertexCounts" in txt
    assert "faceVertexIndices" in txt
    assert "points" in txt
