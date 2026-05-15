"""Attribute variability: uniform vs varying."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_uniform_token(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    uniform token subdivisionScheme = "none"
}
''')
    assert "uniform token subdivisionScheme" in txt


def test_uniform_int(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    uniform int materialIndex = 3
}
''')
    assert "uniform int" in txt
    assert "materialIndex = 3" in txt


def test_varying_default(tmp_path):
    """No `uniform` keyword = varying."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int weight = 5
}
''')
    assert "uniform" not in txt.split("custom int weight")[1].split("\n")[0]
    assert "weight = 5" in txt


def test_uniform_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    uniform float[] weights = [1.0, 2.0, 3.0]
}
''')
    assert "uniform float[]" in txt


def test_uniform_xformoporder_required(tmp_path):
    """xformOpOrder must be uniform per USD spec — verify it survives."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 xformOp:translate = (1, 2, 3)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "uniform token[] xformOpOrder" in txt
