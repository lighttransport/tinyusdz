"""Xform op chain round-trip — translate, rotate, scale, transform
matrix in a single xformOpOrder."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_xform_with_translate_only(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X"
{
    double3 xformOp:translate = (1, 2, 3)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "xformOp:translate = (1, 2, 3)" in txt
    assert '"xformOp:translate"' in txt


def test_xform_translate_rotate_scale(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X"
{
    double3 xformOp:translate = (10, 20, 30)
    float3 xformOp:rotateXYZ = (45, 0, 0)
    float3 xformOp:scale = (2, 2, 2)
    uniform token[] xformOpOrder = [
        "xformOp:translate",
        "xformOp:rotateXYZ",
        "xformOp:scale"
    ]
}
''')
    assert "xformOp:translate = (10, 20, 30)" in txt
    assert "xformOp:rotateXYZ = (45, 0, 0)" in txt
    assert "xformOp:scale = (2, 2, 2)" in txt
    # Order must be preserved
    assert '"xformOp:translate"' in txt
    assert '"xformOp:scale"' in txt


def test_xform_with_transform_matrix(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X"
{
    matrix4d xformOp:transform = (
        (1, 0, 0, 0),
        (0, 1, 0, 0),
        (0, 0, 1, 0),
        (5, 0, 0, 1)
    )
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
''')
    assert "xformOp:transform" in txt
    assert "(5, 0, 0, 1)" in txt


def test_xform_op_with_suffix(tmp_path):
    """xformOp:translate:offset (custom suffix) must round-trip."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X"
{
    double3 xformOp:translate:pivot = (1, 0, 0)
    double3 xformOp:translate = (10, 0, 0)
    uniform token[] xformOpOrder = [
        "xformOp:translate:pivot",
        "xformOp:translate",
        "!invert!xformOp:translate:pivot"
    ]
}
''')
    assert "xformOp:translate:pivot" in txt
    assert "!invert!xformOp:translate:pivot" in txt


def test_xform_op_timesampled(tmp_path):
    """xformOp:translate.timeSamples for animated objects."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X"
{
    double3 xformOp:translate.timeSamples = {
        0: (0, 0, 0),
        24: (10, 0, 0),
        48: (10, 5, 0)
    }
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "(10, 0, 0)" in txt
    assert "(10, 5, 0)" in txt


def test_python_xform_op_authoring(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("xformOp:translate", (1.0, 2.0, 3.0), dtype="double3")
    p.set_attribute("xformOpOrder", ["xformOp:translate"], dtype="token[]")
    # xformOpOrder must be uniform per the schema; set variability
    # explicitly because Python authoring doesn't infer it.
    p.set_attribute_metadata("xformOpOrder", "variability", "uniform")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "xformOp:translate" in txt
    assert "(1, 2, 3)" in txt
