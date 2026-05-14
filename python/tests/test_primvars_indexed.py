"""Indexed primvars: primvar + matching :indices array."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_indexed_displaycolor(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
    int[] primvars:displayColor:indices = [0, 1, 2, 0, 1, 2]
}
''')
    assert "primvars:displayColor" in txt
    assert "primvars:displayColor:indices" in txt
    assert "[0, 1, 2, 0, 1, 2]" in txt


def test_indexed_st_with_interpolation(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    texCoord2f[] primvars:st = [(0, 0), (1, 0), (0, 1), (1, 1)] (
        interpolation = "faceVarying"
    )
    int[] primvars:st:indices = [0, 1, 2, 3]
}
''')
    assert "primvars:st" in txt
    assert "faceVarying" in txt


def test_primvar_constant_interpolation(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor = [(0.5, 0.5, 0.5)] (
        interpolation = "constant"
    )
}
''')
    assert '"constant"' in txt


def test_primvar_uniform_interpolation(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    int[] primvars:matIndex = [0, 1, 0] (
        interpolation = "uniform"
    )
}
''')
    assert '"uniform"' in txt


def test_primvar_vertex_interpolation(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    float[] primvars:weight = [0.1, 0.2, 0.3, 0.4] (
        interpolation = "vertex"
    )
}
''')
    assert '"vertex"' in txt
    assert "primvars:weight" in txt


def test_custom_namespace_primvar(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    float[] primvars:custom:density = [1.0, 2.0, 3.0]
}
''')
    assert "primvars:custom:density" in txt
