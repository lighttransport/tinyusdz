"""xformOp invocations: scale, rotate, translate combos."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_scale_only(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float3 xformOp:scale = (2, 3, 4)
    uniform token[] xformOpOrder = ["xformOp:scale"]
}
''')
    assert "xformOp:scale" in txt
    assert "(2, 3, 4)" in txt


def test_rotate_x_y_z(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float xformOp:rotateX = 30
    float xformOp:rotateY = 45
    float xformOp:rotateZ = 60
    uniform token[] xformOpOrder = ["xformOp:rotateX", "xformOp:rotateY", "xformOp:rotateZ"]
}
''')
    assert "xformOp:rotateX" in txt
    assert "30" in txt and "45" in txt and "60" in txt


def test_translate_rotate_scale_order(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 xformOp:translate = (1, 2, 3)
    float3 xformOp:rotateXYZ = (10, 20, 30)
    float3 xformOp:scale = (2, 2, 2)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ", "xformOp:scale"]
}
''')
    assert "xformOp:translate" in txt
    assert "xformOp:rotateXYZ" in txt
    assert "xformOp:scale" in txt


def test_orient_quat(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    quatf xformOp:orient = (1, 0, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:orient"]
}
''')
    assert "xformOp:orient" in txt


def test_transform_matrix_op(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    matrix4d xformOp:transform = (
        (1, 0, 0, 0),
        (0, 1, 0, 0),
        (0, 0, 1, 0),
        (5, 6, 7, 1)
    )
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
''')
    assert "xformOp:transform" in txt
    assert "(5, 6, 7, 1)" in txt
