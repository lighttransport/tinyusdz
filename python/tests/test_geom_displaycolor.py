"""displayColor, displayOpacity primvars on Mesh/Sphere/Cube."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_mesh_displaycolor_constant(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor = [(0.8, 0.4, 0.1)] (
        interpolation = "constant"
    )
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    assert "displayColor" in txt
    assert "(0.8, 0.4, 0.1)" in txt


def test_mesh_displayopacity(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    float[] primvars:displayOpacity = [0.5] (
        interpolation = "constant"
    )
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    assert "displayOpacity" in txt
    assert "0.5" in txt


def test_sphere_with_displaycolor(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Sphere "S" {
    double radius = 1.0
    color3f[] primvars:displayColor = [(1, 0, 0)]
}
''')
    assert "displayColor" in txt
    assert "(1, 0, 0)" in txt


def test_cube_with_displaycolor(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cube "C" {
    double size = 2.0
    color3f[] primvars:displayColor = [(0, 1, 0)]
}
''')
    assert "(0, 1, 0)" in txt
