# SPDX-License-Identifier: Apache-2.0
import pytest

import tinyusdz
from tinyusdz import tydra

np = pytest.importorskip("numpy")


@pytest.fixture
def render_scene(simple_stage):
    return tydra.to_render_scene(simple_stage)


def test_convert(render_scene):
    assert len(render_scene.meshes) == 1
    assert len(render_scene.materials) == 1
    assert render_scene.up_axis == "Y"
    assert render_scene.default_prim == "World"
    assert isinstance(render_scene.warnings(), list)


def test_mesh_buffers(render_scene):
    m = render_scene.meshes[0]
    assert m.prim_path == "/World/Quad"
    assert m.point_count == 4
    assert m.face_count == 1
    pts = np.asarray(m.points)
    assert pts.shape == (4, 3) and pts.dtype == np.float32
    assert m.is_triangulated
    tri = np.asarray(m.triangulated_indices)
    assert tri.dtype == np.uint32 and len(tri) == 6
    counts = np.asarray(m.face_vertex_counts)
    assert counts.tolist() == [4]
    if m.normals is not None:
        assert np.asarray(m.normals).shape[1] == 3
    if m.texcoords0 is not None:
        assert np.asarray(m.texcoords0).shape == (4, 2)
    assert m.subsets == []


def test_material(render_scene):
    m = render_scene.meshes[0]
    assert m.material_id >= 0
    mat = render_scene.materials[m.material_id]
    assert mat.shader == "preview_surface"
    diffuse = mat.param("diffuse_color")
    assert isinstance(diffuse, tuple)
    assert abs(diffuse[0] - 0.9) < 1e-5
    rough = mat.param("roughness")
    assert abs(rough[0] - 0.4) < 1e-5
    assert mat.param("does_not_exist") is None
    assert render_scene.material_by_path("/World/Looks/Red") is not None
    assert render_scene.mesh_by_path("/World/Quad") is not None
    assert render_scene.mesh_by_path("/nope") is None


def test_node_hierarchy(render_scene):
    roots = render_scene.root_nodes
    assert len(roots) == 1
    root = roots[0]
    assert root.prim_path == "/World"
    assert root.type == "xform"
    kids = root.children
    assert any(c.prim_path == "/World/Quad" for c in kids)
    quad = [c for c in kids if c.prim_path == "/World/Quad"][0]
    assert quad.type == "mesh"
    wt = quad.world_transform
    assert len(wt) == 4 and len(wt[0]) == 4


def test_scene_outlives_stage(simple_stage):
    scene = tydra.to_render_scene(simple_stage)
    simple_stage.close()
    # RenderScene owns its data; usable after the stage is gone.
    assert np.asarray(scene.meshes[0].points).shape == (4, 3)


def test_array_keeps_scene_alive(simple_stage):
    pts = tydra.to_render_scene(simple_stage).meshes[0].points
    a = np.asarray(pts)  # scene only reachable through the Array anchor
    import gc

    gc.collect()
    assert a.sum() > 0  # memory still valid
