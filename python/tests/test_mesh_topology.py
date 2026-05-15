"""Mesh topology: faceVertexCounts/Indices, creases, subdivision."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_quad_mesh_topology(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Quad" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
}
''')
    assert "faceVertexCounts = [4]" in txt
    assert "faceVertexIndices = [0, 1, 2, 3]" in txt


def test_triangle_strip_mesh(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Tris" {
    int[] faceVertexCounts = [3, 3, 3, 3]
    int[] faceVertexIndices = [0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0), (2,0,0), (0,2,0), (3,0,0)]
}
''')
    assert "[3, 3, 3, 3]" in txt


def test_subdivision_scheme(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Sub" {
    uniform token subdivisionScheme = "catmullClark"
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
}
''')
    assert '"catmullClark"' in txt


def test_subdivision_none(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "NoSub" {
    uniform token subdivisionScheme = "none"
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    assert '"none"' in txt


def test_mesh_creases(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Creased" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
    int[] creaseIndices = [0, 1, 1, 2]
    int[] creaseLengths = [2, 2]
    float[] creaseSharpnesses = [10.0, 5.0]
}
''')
    assert "creaseIndices" in txt
    assert "creaseLengths" in txt
    assert "creaseSharpnesses" in txt


def test_mesh_corners(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Corners" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
    int[] cornerIndices = [0, 2]
    float[] cornerSharpnesses = [10.0, 5.0]
}
''')
    assert "cornerIndices" in txt
    assert "cornerSharpnesses" in txt


def test_mesh_holes(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Holey" {
    int[] faceVertexCounts = [4, 4]
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0),
                        (2,0,0), (3,0,0), (3,1,0), (2,1,0)]
    int[] holeIndices = [1]
}
''')
    assert "holeIndices" in txt


def test_mesh_orientation_left_handed(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    uniform token orientation = "leftHanded"
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    assert '"leftHanded"' in txt
