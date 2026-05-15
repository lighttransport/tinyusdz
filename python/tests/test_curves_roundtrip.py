"""Curve prim round-trip — BasisCurves and NurbsCurves with various
attributes."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_basis_curves_linear_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "lines"
{
    point3f[] points = [(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0)]
    int[] curveVertexCounts = [4]
    uniform token type = "linear"
    uniform token wrap = "nonperiodic"
    float[] widths = [0.1, 0.1, 0.1, 0.1] (
        interpolation = "vertex"
    )
}
''')
    assert "BasisCurves" in txt
    assert "(2, 0, 0)" in txt
    assert '"linear"' in txt
    assert '"nonperiodic"' in txt
    assert "[0.1, 0.1, 0.1, 0.1]" in txt
    assert 'interpolation = "vertex"' in txt


def test_basis_curves_cubic_bezier(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "curve"
{
    point3f[] points = [
        (0, 0, 0), (1, 1, 0), (2, 1, 0), (3, 0, 0),
        (3, 0, 0), (4, -1, 0), (5, -1, 0), (6, 0, 0)
    ]
    int[] curveVertexCounts = [4, 4]
    uniform token type = "cubic"
    uniform token basis = "bezier"
}
''')
    assert "BasisCurves" in txt
    assert '"cubic"' in txt
    assert '"bezier"' in txt
    assert "[4, 4]" in txt


def test_basis_curves_widths_constant_interpolation(tmp_path):
    """Constant-interpolation widths array (single value) survives."""
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "lines"
{
    point3f[] points = [(0, 0, 0), (1, 0, 0)]
    int[] curveVertexCounts = [2]
    float[] widths = [0.5] (
        interpolation = "constant"
    )
}
''')
    assert "[0.5]" in txt
    assert 'interpolation = "constant"' in txt


def test_nurbs_curves_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def NurbsCurves "spline"
{
    point3f[] points = [(0, 0, 0), (1, 1, 0), (2, 1, 0), (3, 0, 0)]
    int[] curveVertexCounts = [4]
    int[] order = [4]
    double[] knots = [0, 0, 0, 0, 1, 1, 1, 1]
}
''')
    assert "NurbsCurves" in txt
    assert "[0, 0, 0, 0, 1, 1, 1, 1]" in txt


def test_curves_with_displayColor_primvar(tmp_path):
    """displayColor primvar attached to a BasisCurves."""
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "rainbow"
{
    point3f[] points = [(0, 0, 0), (1, 0, 0), (2, 0, 0)]
    int[] curveVertexCounts = [3]
    color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1)] (
        interpolation = "vertex"
    )
}
''')
    assert "primvars:displayColor" in txt
    assert "(1, 0, 0)" in txt
    assert "(0, 1, 0)" in txt
    assert "(0, 0, 1)" in txt
