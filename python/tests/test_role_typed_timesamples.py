"""Time samples for role-typed array values (color3f[], point3f[],
normal3f[], matrix4d) — round-trip via USDA→USDC→USDA.
"""
import tinyusdz


def _rt(tmp_path, usda_text):
    src = tmp_path / "x.usda"
    src.write_text(usda_text)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    return s2.export_to_string()


def test_point3f_array_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom point3f[] pts.timeSamples = {
        0: [(0, 0, 0), (1, 0, 0), (1, 1, 0)],
        24: [(0, 1, 0), (2, 0, 0), (2, 2, 0)]
    }
}
''')
    assert "(0, 0, 0)" in txt
    assert "(1, 1, 0)" in txt
    assert "(2, 2, 0)" in txt


def test_color3f_array_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom color3f[] cols.timeSamples = {
        0: [(1, 0, 0), (0, 1, 0)],
        10: [(0, 0, 1), (1, 1, 1)]
    }
}
''')
    assert "color3f[]" in txt
    assert "(1, 0, 0)" in txt
    assert "(0, 0, 1)" in txt


def test_normal3f_array_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom normal3f[] ns.timeSamples = {
        0: [(0, 0, 1)],
        5: [(0, 1, 0)]
    }
}
''')
    assert "normal3f[]" in txt


def test_matrix4d_scalar_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom matrix4d xform.timeSamples = {
        0: ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)),
        10: ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (5, 0, 0, 1))
    }
}
''')
    assert "matrix4d" in txt
    # Translation row at t=10 should preserve the (5, 0, 0, 1) row
    assert "5" in txt


def test_quatf_scalar_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom quatf rotation.timeSamples = {
        0: (1, 0, 0, 0),
        10: (0.707, 0.707, 0, 0),
        20: (0, 1, 0, 0)
    }
}
''')
    # USDA spelling [w, x, y, z]: identity = (1, 0, 0, 0).
    assert "0: (1, 0, 0, 0)" in txt
    assert "10: (0.707, 0.707, 0, 0)" in txt
    assert "20: (0, 1, 0, 0)" in txt


def test_int3_scalar_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int3 v.timeSamples = {
        0: (0, 0, 0),
        10: (1, 2, 3)
    }
}
''')
    assert "0: (0, 0, 0)" in txt
    assert "10: (1, 2, 3)" in txt


def test_token_array_timesamples(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token[] names.timeSamples = {
        0: ["alpha", "beta"],
        10: ["gamma"]
    }
}
''')
    assert '"alpha"' in txt
    assert '"gamma"' in txt
