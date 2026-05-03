"""Attribute with both default value and time samples."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_default_only(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v = 5.0
}
''')
    assert "v = 5" in txt


def test_timesamples_only(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        0: 1.0,
        10: 2.0
    }
}
''')
    assert "v.timeSamples" in txt
    assert "0: 1" in txt


def test_default_and_timesamples_both(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v = 5.0
    float v.timeSamples = {
        0: 1.0,
        10: 2.0
    }
}
''')
    assert "v = 5" in txt
    assert "v.timeSamples" in txt


def test_typed_with_default_and_samples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3 t = (0, 0, 0)
    double3 t.timeSamples = {
        0: (1, 0, 0),
        10: (0, 1, 0)
    }
}
''')
    assert "t.timeSamples" in txt
    assert "(1, 0, 0)" in txt
