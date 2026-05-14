"""Inspect RenderMesh, RenderCamera, RenderLight contents."""
import numpy as np
import tinyusdz


def _scene_from(usda):
    s = tinyusdz.loads(usda)
    return tinyusdz.tydra.convert_to_render_scene(s)


def test_render_mesh_points_count():
    rs = _scene_from('''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    m = rs.meshes()[0]
    assert m.name == "M"
    assert m.abs_path == "/M"
    pts = np.asarray(m.points)
    assert pts.shape == (3, 3)


def test_render_mesh_face_vertex():
    rs = _scene_from('''#usda 1.0
def Mesh "Quad" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
}
''')
    m = rs.meshes()[0]
    counts = np.asarray(m.face_vertex_counts).tolist()
    indices = np.asarray(m.face_vertex_indices).tolist()
    # tydra triangulates: quad (4) -> two tris (3, 3)
    assert all(c == 3 for c in counts)
    assert sum(counts) == len(indices)


def test_render_camera_focal_length():
    rs = _scene_from('''#usda 1.0
def Camera "C" {
    float focalLength = 35
    float horizontalAperture = 24
    float verticalAperture = 18
}
''')
    c = rs.cameras()[0]
    assert abs(c.focal_length - 35) < 1e-3
    assert abs(c.horizontal_aperture - 24) < 1e-3
    assert abs(c.vertical_aperture - 18) < 1e-3


def test_render_camera_orthographic():
    rs = _scene_from('''#usda 1.0
def Camera "C" {
    uniform token projection = "orthographic"
}
''')
    c = rs.cameras()[0]
    assert "ortho" in str(c.projection).lower()


def test_render_light_attributes():
    rs = _scene_from('''#usda 1.0
def DistantLight "Sun" {
    float inputs:intensity = 1000
    color3f inputs:color = (1, 0.5, 0.25)
}
''')
    l = rs.lights()[0]
    assert l.name == "Sun"
    assert abs(l.intensity - 1000) < 1e-3


def test_render_mesh_double_sided():
    rs = _scene_from('''#usda 1.0
def Mesh "M" {
    uniform bool doubleSided = 1
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    m = rs.meshes()[0]
    assert m.is_double_sided is True
