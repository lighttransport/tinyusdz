"""xformOpOrder with `!resetXformStack!` sentinel and inverse ops."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_reset_xform_stack(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 xformOp:translate = (1, 2, 3)
    uniform token[] xformOpOrder = ["!resetXformStack!", "xformOp:translate"]
}
''')
    assert "!resetXformStack!" in txt
    assert "xformOp:translate" in txt


def test_inverse_xform_op(tmp_path):
    """`!invert!xformOp:foo` is the inverse-op syntax."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 xformOp:translate = (1, 2, 3)
    uniform token[] xformOpOrder = ["xformOp:translate", "!invert!xformOp:translate"]
}
''')
    assert "!invert!xformOp:translate" in txt


def test_xform_op_with_suffix(tmp_path):
    """xformOp names can carry suffixes: xformOp:translate:offset."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 xformOp:translate:pivot = (0, 0, 0)
    double3 xformOp:translate:offset = (1, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate:pivot", "xformOp:translate:offset"]
}
''')
    assert "xformOp:translate:pivot" in txt
    assert "xformOp:translate:offset" in txt


def test_xform_op_rotate_xyz(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float3 xformOp:rotateXYZ = (10, 20, 30)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
}
''')
    assert "xformOp:rotateXYZ" in txt
    assert "(10, 20, 30)" in txt
