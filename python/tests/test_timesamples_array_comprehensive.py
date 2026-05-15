"""Comprehensive time-sampled array round-trip across many dtypes."""
import numpy as np
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out))


# ----------------------------------------------------------------------
# Numeric scalar arrays
# ----------------------------------------------------------------------

def test_int_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int[] v.timeSamples = {
        0: [1, 2, 3],
        10: [4, 5, 6],
        20: [7, 8, 9, 10]
    }
}
''')
    txt = s.export_to_string()
    assert "v.timeSamples" in txt
    assert "[1, 2, 3]" in txt
    assert "[7, 8, 9, 10]" in txt


def test_float_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float[] v.timeSamples = {
        0: [1.0, 2.0, 3.0],
        10: [10.5, 20.5, 30.5]
    }
}
''')
    txt = s.export_to_string()
    assert "[1, 2, 3]" in txt
    assert "[10.5, 20.5, 30.5]" in txt


def test_double_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double[] v.timeSamples = {
        0: [3.14159265358979, 2.71828182845905]
    }
}
''')
    txt = s.export_to_string()
    assert "double[]" in txt
    assert "v.timeSamples" in txt


def test_int64_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int64[] v.timeSamples = {
        0: [9000000000, -9000000000],
        10: [1, 2, 3]
    }
}
''')
    txt = s.export_to_string()
    assert "v.timeSamples" in txt


def test_uint64_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    uint64[] v.timeSamples = {
        0: [10, 20, 30]
    }
}
''')
    txt = s.export_to_string()
    assert "v.timeSamples" in txt


def test_half_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    half[] v.timeSamples = {
        0: [1.0, 2.0, 3.0],
        10: [4.0, 5.0]
    }
}
''')
    txt = s.export_to_string()
    assert "v.timeSamples" in txt


# ----------------------------------------------------------------------
# Vector arrays
# ----------------------------------------------------------------------

def test_float3_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float3[] verts.timeSamples = {
        0: [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
        10: [(0, 0, 0), (2, 0, 0), (0, 2, 0)]
    }
}
''')
    txt = s.export_to_string()
    assert "verts.timeSamples" in txt
    assert "(2, 0, 0)" in txt


def test_double3_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    double3[] v.timeSamples = {
        0: [(1, 2, 3)]
    }
}
''')
    txt = s.export_to_string()
    assert "double3[]" in txt
    assert "v.timeSamples" in txt


def test_int2_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int2[] v.timeSamples = {
        0: [(1, 2), (3, 4)],
        10: [(5, 6), (7, 8)]
    }
}
''')
    txt = s.export_to_string()
    assert "int2[]" in txt
    assert "(1, 2)" in txt and "(7, 8)" in txt


def test_int3_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int3[] v.timeSamples = {
        0: [(1, 2, 3)],
        10: [(4, 5, 6), (7, 8, 9)]
    }
}
''')
    txt = s.export_to_string()
    assert "int3[]" in txt


def test_int4_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int4[] v.timeSamples = {
        0: [(1, 2, 3, 4)]
    }
}
''')
    txt = s.export_to_string()
    assert "int4[]" in txt


def test_color3f_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor.timeSamples = {
        0: [(1, 0, 0)],
        10: [(0, 1, 0)],
        20: [(0, 0, 1)]
    }
}
''')
    txt = s.export_to_string()
    assert "primvars:displayColor.timeSamples" in txt
    assert "(1, 0, 0)" in txt and "(0, 0, 1)" in txt


def test_normal3f_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    normal3f[] normals.timeSamples = {
        0: [(0, 0, 1), (0, 1, 0)],
        10: [(0, 1, 0), (1, 0, 0)]
    }
}
''')
    txt = s.export_to_string()
    assert "normal3f[]" in txt
    assert "normals.timeSamples" in txt


def test_point3f_array_timesamples(tmp_path):
    """Mesh.points with time samples is the canonical animated mesh."""
    s = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points.timeSamples = {
        0: [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
        10: [(0, 0, 0), (1, 0, 0.5), (0, 1, 0.5)]
    }
}
''')
    txt = s.export_to_string()
    assert "points.timeSamples" in txt
    assert "(1, 0, 0.5)" in txt


def test_texcoord2f_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    texCoord2f[] primvars:st.timeSamples = {
        0: [(0, 0), (1, 0), (1, 1), (0, 1)],
        10: [(0.1, 0), (0.9, 0), (0.9, 1), (0.1, 1)]
    }
}
''')
    txt = s.export_to_string()
    assert "primvars:st.timeSamples" in txt


# ----------------------------------------------------------------------
# Quaternion arrays
# ----------------------------------------------------------------------

def test_quatf_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    quatf[] rots.timeSamples = {
        0: [(1, 0, 0, 0)],
        10: [(0.7071, 0, 0.7071, 0)]
    }
}
''')
    txt = s.export_to_string()
    assert "quatf[]" in txt
    assert "rots.timeSamples" in txt


def test_quatd_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    quatd[] rots.timeSamples = {
        0: [(1, 0, 0, 0), (1, 0, 0, 0)],
        10: [(0.5, 0.5, 0.5, 0.5), (0.7071, 0, 0.7071, 0)]
    }
}
''')
    txt = s.export_to_string()
    assert "quatd[]" in txt


# ----------------------------------------------------------------------
# Matrix arrays
# ----------------------------------------------------------------------

def test_matrix4d_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    matrix4d[] mats.timeSamples = {
        0: [
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
        ],
        10: [
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (5, 0, 0, 1))
        ]
    }
}
''')
    txt = s.export_to_string()
    assert "matrix4d[]" in txt
    assert "(5, 0, 0, 1)" in txt


def test_matrix3d_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    matrix3d[] mats.timeSamples = {
        0: [((1, 0, 0), (0, 1, 0), (0, 0, 1))],
        10: [((2, 0, 0), (0, 2, 0), (0, 0, 2))]
    }
}
''')
    txt = s.export_to_string()
    assert "matrix3d[]" in txt


# ----------------------------------------------------------------------
# String / token arrays
# ----------------------------------------------------------------------

def test_string_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    string[] tags.timeSamples = {
        0: ["a", "b"],
        10: ["c", "d", "e"]
    }
}
''')
    txt = s.export_to_string()
    assert "string[]" in txt
    assert "tags.timeSamples" in txt


def test_token_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    token[] phases.timeSamples = {
        0: ["start"],
        10: ["middle"],
        20: ["end"]
    }
}
''')
    txt = s.export_to_string()
    assert "token[]" in txt
    assert "phases.timeSamples" in txt


def test_asset_array_timesamples_usda_only(tmp_path):
    """asset[] timeSamples — USDA round-trip fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    asset[] textures.timeSamples = {
        0: [@./a.png@],
        10: [@./b.png@]
    }
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "asset[]" in txt
    assert "@./a.png@" in txt
    assert "@./b.png@" in txt


# ----------------------------------------------------------------------
# Edge cases: variable-length arrays, empty samples
# ----------------------------------------------------------------------

def test_variable_length_array_samples(tmp_path):
    """Length of array can change per time sample."""
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int[] v.timeSamples = {
        0: [1],
        10: [1, 2, 3, 4, 5],
        20: [9, 8]
    }
}
''')
    txt = s.export_to_string()
    assert "[1]" in txt
    assert "[1, 2, 3, 4, 5]" in txt
    assert "[9, 8]" in txt


def test_empty_array_in_samples(tmp_path):
    """Empty array in a time sample."""
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int[] v.timeSamples = {
        0: [],
        10: [1, 2, 3]
    }
}
''')
    txt = s.export_to_string()
    assert "v.timeSamples" in txt


def test_single_sample_array(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    int[] v.timeSamples = {
        42: [100, 200, 300]
    }
}
''')
    txt = s.export_to_string()
    assert "42: [100, 200, 300]" in txt


def test_many_sample_array(tmp_path):
    """20 time samples with growing arrays."""
    samples = ",\n        ".join(
        f"{t}: [{', '.join(str(t * 10 + i) for i in range(t + 1))}]"
        for t in range(20)
    )
    s = _rt(tmp_path, f'''#usda 1.0
def Xform "X" {{
    int[] v.timeSamples = {{
        {samples}
    }}
}}
''')
    txt = s.export_to_string()
    assert "v.timeSamples" in txt
    # Sample 19 has 20 entries
    assert "190" in txt or "199" in txt


def test_negative_time_array(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float[] v.timeSamples = {
        -10: [0.0],
        0: [1.0, 2.0],
        10: [3.0, 4.0, 5.0]
    }
}
''')
    txt = s.export_to_string()
    assert "-10" in txt
    assert "[3, 4, 5]" in txt


# ----------------------------------------------------------------------
# Authoring time-sampled arrays from Python
# ----------------------------------------------------------------------

def test_python_authoring_int_array_timesamples(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time("v", 0, [1, 2, 3], dtype="int[]")
    p.set_attribute_at_time("v", 10, [4, 5, 6, 7], dtype="int[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    samples = p2.get_attribute_timesamples("v")
    times = sorted(t for t, _ in samples)
    assert times == [0, 10]


def test_python_authoring_float_array_timesamples(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time("v", 0, [1.0, 2.0, 3.0], dtype="float[]")
    p.set_attribute_at_time("v", 10, [4.5, 5.5], dtype="float[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    txt = s2.export_to_string()
    assert "v.timeSamples" in txt


def test_python_authoring_point3f_array_timesamples(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time(
        "pts", 0, [(0, 0, 0), (1, 0, 0)], dtype="point3f[]")
    p.set_attribute_at_time(
        "pts", 10, [(0, 0, 0), (2, 0, 0)], dtype="point3f[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "pts.timeSamples" in txt
    assert "(2, 0, 0)" in txt


def test_python_authoring_color3f_array_timesamples(tmp_path):
    s = tinyusdz.Stage()
    m = tinyusdz.Prim("Mesh", name="M")
    m.set_attribute_at_time(
        "primvars:displayColor", 0, [(1, 0, 0)], dtype="color3f[]")
    m.set_attribute_at_time(
        "primvars:displayColor", 10, [(0, 1, 0)], dtype="color3f[]")
    s.add_root_prim(m)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "primvars:displayColor.timeSamples" in txt


def test_python_default_plus_timesamples_array(tmp_path):
    """Default value + timeSamples coexist for arrays."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", [1, 2, 3], dtype="int[]")
    p.set_attribute_at_time("v", 0, [10, 20], dtype="int[]")
    p.set_attribute_at_time("v", 10, [30, 40, 50], dtype="int[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "v.timeSamples" in txt


# ----------------------------------------------------------------------
# Read-back via numpy buffer
# ----------------------------------------------------------------------

def test_timesample_array_numpy_buffer(tmp_path):
    """Per-sample Value supports buffer protocol for arrays."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    float[] v.timeSamples = {
        0: [1.0, 2.0, 3.0, 4.0],
        10: [5.0, 6.0, 7.0, 8.0]
    }
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    samples = p.get_attribute_timesamples("v")
    by_time = {t: v for t, v in samples}
    assert 0 in by_time and 10 in by_time
    a0 = np.asarray(by_time[0])
    a10 = np.asarray(by_time[10])
    assert a0.tolist() == [1.0, 2.0, 3.0, 4.0]
    assert a10.tolist() == [5.0, 6.0, 7.0, 8.0]


def test_timesample_int3_array_numpy(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    int3[] v.timeSamples = {
        0: [(1, 2, 3), (4, 5, 6)],
        10: [(7, 8, 9), (10, 11, 12)]
    }
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    samples = p.get_attribute_timesamples("v")
    by_time = {t: v for t, v in samples}
    a0 = np.asarray(by_time[0])
    assert a0.shape == (2, 3)
    assert a0.tolist() == [[1, 2, 3], [4, 5, 6]]


# ----------------------------------------------------------------------
# Mixed scalar + array attributes on same prim
# ----------------------------------------------------------------------

def test_mixed_scalar_and_array_timesamples(tmp_path):
    s = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float scale.timeSamples = {
        0: 1.0,
        10: 2.0
    }
    float[] weights.timeSamples = {
        0: [0.5, 0.5],
        10: [1.0, 0.0]
    }
    int frame.timeSamples = {
        0: 1,
        10: 24
    }
}
''')
    txt = s.export_to_string()
    assert "scale.timeSamples" in txt
    assert "weights.timeSamples" in txt
    assert "frame.timeSamples" in txt
