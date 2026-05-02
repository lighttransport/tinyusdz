"""matrix2d / matrix3d / matrix4d round-trip — scalar and array."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_matrix4d_identity(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom matrix4d m = (
        (1, 0, 0, 0),
        (0, 1, 0, 0),
        (0, 0, 1, 0),
        (0, 0, 0, 1)
    )
}
''')
    assert "matrix4d m" in txt
    assert "(1, 0, 0, 0)" in txt
    assert "(0, 0, 0, 1)" in txt


def test_matrix4d_translation(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom matrix4d m = (
        (1, 0, 0, 0),
        (0, 1, 0, 0),
        (0, 0, 1, 0),
        (10, 20, 30, 1)
    )
}
''')
    assert "(10, 20, 30, 1)" in txt


def test_matrix3d(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom matrix3d m = (
        (1, 0, 0),
        (0, 2, 0),
        (0, 0, 3)
    )
}
''')
    assert "matrix3d m" in txt
    assert "(0, 2, 0)" in txt
    assert "(0, 0, 3)" in txt


def test_matrix2d_usda_only(tmp_path):
    """matrix2d USDA->USDA round-trip works; USDC roundtrip currently
    inflates to matrix4d (writer-side bug, separate from this test)."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom matrix2d m = ((1.5, 0), (0, 2.5))
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "matrix2d" in txt
    assert "(1.5, 0)" in txt
    assert "(0, 2.5)" in txt


def test_matrix4d_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom matrix4d[] mats = [
        ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)),
        ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (5, 0, 0, 1))
    ]
}
''')
    assert "matrix4d[]" in txt
    assert "(5, 0, 0, 1)" in txt


def test_matrix4d_python_authoring(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("m", [
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (1.5, 2.5, 3.5, 1.0),
    ], dtype="matrix4d")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "(1.5, 2.5, 3.5, 1)" in txt
