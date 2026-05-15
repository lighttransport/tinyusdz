"""Schema-typed primitives: Sphere/Cube/Cone/Cylinder/Capsule attrs."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_sphere_full(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Sphere "S" {
    double radius = 2.5
    float3[] extent = [(-2.5, -2.5, -2.5), (2.5, 2.5, 2.5)]
}
''')
    assert "radius = 2.5" in txt
    assert "extent" in txt


def test_cube_full(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cube "C" {
    double size = 4.0
}
''')
    assert "size = 4" in txt


def test_cone_full(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cone "C" {
    double height = 3.0
    double radius = 1.5
}
''')
    assert "height = 3" in txt
    assert "radius = 1.5" in txt


def test_cylinder_full(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cylinder "C" {
    double height = 4.0
    double radius = 0.5
}
''')
    assert "height = 4" in txt
    assert "radius = 0.5" in txt


def test_capsule_full(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Capsule "C" {
    double height = 2.0
    double radius = 0.5
}
''')
    assert "Capsule" in txt
    assert "height = 2" in txt


def test_plane_attrs(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Plane "P" {
    double width = 10.0
    double length = 5.0
}
''')
    assert "Plane" in txt
    assert "width = 10" in txt


def test_typed_prims_with_xform(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Sphere "S" {
    double radius = 1.0
    double3 xformOp:translate = (5, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "(5, 0, 0)" in txt
    assert "radius = 1" in txt
