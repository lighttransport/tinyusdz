"""Role-typed attribute round-trip: color3f/d, normal3f/d, point3f/d, vector3f/d."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_color3f_scalar(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom color3f c = (0.8, 0.2, 0.1)
}
''')
    assert "color3f" in txt
    assert "(0.8, 0.2, 0.1)" in txt


def test_color3d(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom color3d c = (0.8, 0.2, 0.1)
}
''')
    assert "color3d" in txt


def test_color4f(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom color4f c = (0.5, 0.5, 0.5, 1.0)
}
''')
    assert "color4f" in txt
    assert "(0.5, 0.5, 0.5, 1)" in txt


def test_normal3f_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    normal3f[] normals = [(0, 0, 1), (1, 0, 0)]
}
''')
    assert "normal3f[]" in txt
    assert "(0, 0, 1)" in txt


def test_point3f_vs_vector3f(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom point3f p = (1, 2, 3)
    custom vector3f v = (4, 5, 6)
}
''')
    assert "point3f" in txt
    assert "vector3f" in txt


def test_texcoord2f_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)]
}
''')
    assert "texCoord2f" in txt
    assert "(0, 0)" in txt
    assert "(1, 1)" in txt


def test_color3f_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
}
''')
    assert "color3f[]" in txt
    assert "(1, 0, 0)" in txt
