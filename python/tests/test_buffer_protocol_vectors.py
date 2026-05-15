"""Buffer protocol on vec/color/matrix Value arrays."""
import numpy as np
import tinyusdz


def _rt_value(tmp_path, attr_name, value, dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute(attr_name, value, dtype=dtype)
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    return s2.get_prim_at_path("/X").get_attribute(attr_name).value


def test_float3_array(tmp_path):
    val = _rt_value(tmp_path, "pts",
                    [(0.0, 1.0, 2.0), (3.0, 4.0, 5.0)], "float3[]")
    arr = np.asarray(val)
    assert arr.shape == (2, 3)
    assert arr.dtype == np.float32
    assert arr.tolist() == [[0, 1, 2], [3, 4, 5]]


def test_double3_array(tmp_path):
    val = _rt_value(tmp_path, "pts",
                    [(0.0, 1.0, 2.0)], "double3[]")
    arr = np.asarray(val)
    assert arr.shape == (1, 3)
    assert arr.dtype in (np.float32, np.float64)
    assert arr[0].tolist() == [0.0, 1.0, 2.0]


def test_int2_array(tmp_path):
    val = _rt_value(tmp_path, "pairs",
                    [(1, 2), (3, 4), (5, 6)], "int2[]")
    arr = np.asarray(val)
    assert arr.shape == (3, 2)
    assert arr.tolist() == [[1, 2], [3, 4], [5, 6]]


def test_color3f_array(tmp_path):
    val = _rt_value(tmp_path, "cs",
                    [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0)], "color3f[]")
    arr = np.asarray(val)
    assert arr.shape == (2, 3)
    assert arr.dtype == np.float32


def test_matrix4d_scalar_round_trip(tmp_path):
    """Matrix4d scalars round-trip through to_string."""
    m = [(1.0, 0.0, 0.0, 0.0),
         (0.0, 1.0, 0.0, 0.0),
         (0.0, 0.0, 1.0, 0.0),
         (5.0, 0.0, 0.0, 1.0)]
    val = _rt_value(tmp_path, "m", m, "matrix4d")
    s = val.to_string()
    assert "5" in s
