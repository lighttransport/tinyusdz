"""Time-sampled vec3 / quat / matrix attribute round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_float3_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float3 t.timeSamples = {
        0: (1, 2, 3),
        10: (4, 5, 6),
        20: (7, 8, 9)
    }
}
''')
    assert "t.timeSamples" in txt
    assert "(1, 2, 3)" in txt
    assert "(7, 8, 9)" in txt


def test_double3_translate_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 xformOp:translate.timeSamples = {
        0: (0, 0, 0),
        24: (10, 0, 0)
    }
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "xformOp:translate.timeSamples" in txt
    assert "(10, 0, 0)" in txt


def test_quatf_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    quatf rot.timeSamples = {
        0: (1, 0, 0, 0),
        10: (0.7071, 0, 0.7071, 0)
    }
}
''')
    assert "rot.timeSamples" in txt
    assert "0.7071" in txt


def test_color3f_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor.timeSamples = {
        0: [(1, 0, 0)],
        10: [(0, 1, 0)]
    }
}
''')
    assert "primvars:displayColor.timeSamples" in txt


def test_int_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int frame.timeSamples = {
        0: 1,
        10: 25,
        20: 50
    }
}
''')
    assert "frame.timeSamples" in txt
    assert "10: 25" in txt
