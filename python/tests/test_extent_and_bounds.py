"""Boundable extent attribute round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_mesh_extent(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    float3[] extent = [(-1, -1, -1), (1, 1, 1)]
}
''')
    assert "extent" in txt
    assert "(-1, -1, -1)" in txt
    assert "(1, 1, 1)" in txt


def test_sphere_extent(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Sphere "S" {
    double radius = 2.0
    float3[] extent = [(-2, -2, -2), (2, 2, 2)]
}
''')
    assert "(-2, -2, -2)" in txt


def test_cube_extent(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cube "C" {
    double size = 4.0
    float3[] extent = [(-2, -2, -2), (2, 2, 2)]
}
''')
    assert "Cube" in txt


def test_xformable_purpose_extent_combo(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    uniform token purpose = "render"
    float3[] extent = [(0, 0, 0), (10, 10, 10)]
}
''')
    assert "extent" in txt
    assert '"render"' in txt
