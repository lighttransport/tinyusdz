"""Tydra render-scene conversion tests."""
from __future__ import annotations

import pytest

import tinyusdz


USDA = """#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Mesh "Cube"
    {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [
            0,1,2,3, 4,5,6,7, 0,1,5,4, 2,3,7,6, 0,3,7,4, 1,2,6,5
        ]
        point3f[] points = [
            (-1,-1,-1), ( 1,-1,-1), ( 1, 1,-1), (-1, 1,-1),
            (-1,-1, 1), ( 1,-1, 1), ( 1, 1, 1), (-1, 1, 1)
        ]
        color3f[] primvars:displayColor = [(0.8, 0.4, 0.1)]
    }

    def Camera "MainCam"
    {
        float focalLength = 50
        float horizontalAperture = 24
        float verticalAperture = 16
    }

    def SphereLight "KeyLight"
    {
        float inputs:intensity = 1000
        color3f inputs:color = (1, 0.9, 0.8)
    }
}
"""


@pytest.fixture(scope="module")
def render_scene():
    stage = tinyusdz.loads(USDA)
    return tinyusdz.tydra.convert_to_render_scene(stage)


def test_counts(render_scene):
    assert len(render_scene.meshes()) == 1
    assert len(render_scene.cameras()) == 1
    assert len(render_scene.lights()) == 1


def test_mesh_basics(render_scene):
    m = render_scene.meshes()[0]
    assert m.name == "Cube"
    assert m.abs_path == "/World/Cube"
    assert m.is_right_handed is True
    assert isinstance(m.display_color, tuple) and len(m.display_color) == 3


def test_mesh_buffers_are_zero_copy(render_scene):
    np = pytest.importorskip("numpy")
    m = render_scene.meshes()[0]

    pts = np.asarray(m.points)
    assert pts.ndim == 2 and pts.shape[1] == 3
    assert pts.dtype == np.float32

    fvi = np.asarray(m.face_vertex_indices)
    fvc = np.asarray(m.face_vertex_counts)
    assert fvi.dtype == np.uint32
    assert fvc.dtype == np.uint32
    assert int(fvc.sum()) == int(fvi.size)

    # Pointer stability across calls = true zero-copy.
    a = np.asarray(m.points).ctypes.data
    b = np.asarray(m.points).ctypes.data
    assert a == b


def test_buffer_outlives_mesh(render_scene):
    np = pytest.importorskip("numpy")
    m = render_scene.meshes()[0]
    buf = m.points
    del m
    # RenderScene still alive (held by fixture), so the BufferView must stay
    # valid after we drop the RenderMesh wrapper.
    arr = np.asarray(buf)
    assert arr.shape[0] > 0


def test_camera_fields(render_scene):
    c = render_scene.cameras()[0]
    assert c.name == "MainCam"
    assert c.projection == "perspective"
    assert abs(c.focal_length - 50.0) < 1e-3
    assert abs(c.horizontal_aperture - 24.0) < 1e-3


def test_light_fields(render_scene):
    l = render_scene.lights()[0]
    assert l.name == "KeyLight"
    assert l.type == "sphere"
    assert isinstance(l.color, tuple) and len(l.color) == 3
