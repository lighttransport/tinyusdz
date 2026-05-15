"""NurbsCurves and BasisCurves variants beyond the basic types."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_basis_curves_bezier(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "Bez" {
    uniform token type = "cubic"
    uniform token basis = "bezier"
    int[] curveVertexCounts = [4]
    point3f[] points = [(0,0,0), (1,1,0), (2,1,0), (3,0,0)]
    float[] widths = [0.1] (interpolation = "constant")
}
''')
    assert "BasisCurves" in txt
    assert '"bezier"' in txt
    assert '"cubic"' in txt


def test_basis_curves_linear(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "Lin" {
    uniform token type = "linear"
    int[] curveVertexCounts = [3]
    point3f[] points = [(0,0,0), (1,1,0), (2,0,0)]
}
''')
    assert '"linear"' in txt


def test_basis_curves_catmull_rom(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "CR" {
    uniform token type = "cubic"
    uniform token basis = "catmullRom"
    int[] curveVertexCounts = [4]
    point3f[] points = [(0,0,0), (1,1,0), (2,1,0), (3,0,0)]
}
''')
    assert '"catmullRom"' in txt


def test_basis_curves_wrap_periodic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BasisCurves "Loop" {
    uniform token type = "cubic"
    uniform token basis = "bezier"
    uniform token wrap = "periodic"
    int[] curveVertexCounts = [4]
    point3f[] points = [(0,0,0), (1,1,0), (2,1,0), (3,0,0)]
}
''')
    assert '"periodic"' in txt


def test_nurbs_curves_basic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def NurbsCurves "NC" {
    int[] curveVertexCounts = [4]
    point3f[] points = [(0,0,0), (1,1,0), (2,1,0), (3,0,0)]
    int[] order = [4]
    double[] knots = [0, 0, 0, 0, 1, 1, 1, 1]
    double2[] ranges = [(0, 1)]
}
''')
    assert "NurbsCurves" in txt
    assert "knots" in txt
    assert "order" in txt
