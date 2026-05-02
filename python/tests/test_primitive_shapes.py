"""GeomSphere/Cube/Cylinder/Cone/Capsule schema attribute round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_sphere_radius_extent(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Sphere "ball"
{
    double radius = 2.5
    float3[] extent = [(-2.5, -2.5, -2.5), (2.5, 2.5, 2.5)]
}
''')
    assert "Sphere" in txt
    assert "radius = 2.5" in txt
    assert "(-2.5, -2.5, -2.5)" in txt
    assert "(2.5, 2.5, 2.5)" in txt


def test_cube_size(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cube "box"
{
    double size = 1.5
}
''')
    assert "Cube" in txt
    assert "size = 1.5" in txt


def test_cylinder_radius_height(tmp_path):
    """Note: schema-typed `axis` field is currently dropped on USDC
    roundtrip (writer doesn't extract it for Cylinder/Cone/Capsule).
    Fence radius and height which do round-trip."""
    txt = _rt(tmp_path, '''#usda 1.0
def Cylinder "tube"
{
    double radius = 0.5
    double height = 2.0
}
''')
    assert "Cylinder" in txt
    assert "radius = 0.5" in txt
    assert "height = 2" in txt


def test_cone_radius_height(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cone "cone"
{
    double radius = 1.0
    double height = 2.0
}
''')
    assert "Cone" in txt
    assert "radius = 1" in txt
    assert "height = 2" in txt


def test_capsule(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Capsule "pill"
{
    double radius = 0.25
    double height = 1.0
}
''')
    assert "Capsule" in txt
    assert "radius = 0.25" in txt
    assert "height = 1" in txt


def test_mixed_primitives_in_stage(tmp_path):
    """Multiple primitive types in one stage all round-trip."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "scene"
{
    def Sphere "s" { double radius = 1 }
    def Cube "c" { double size = 1 }
    def Cylinder "cy" { double radius = 0.5 }
    def Cone "co" { double radius = 0.5 }
    def Capsule "cap" { double radius = 0.25 }
}
''')
    for shape in ["Sphere", "Cube", "Cylinder", "Cone", "Capsule"]:
        assert shape in txt


def test_python_authored_sphere(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Sphere", name="ball")
    p.set_attribute("radius", 3.0, dtype="double")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert 'def Sphere "ball"' in txt
    assert "radius = 3" in txt
