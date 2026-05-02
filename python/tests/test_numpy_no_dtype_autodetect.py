"""Buffer-protocol auto-detection in the no-dtype branch of set_attribute.

A 1-D numpy ndarray of float16 / float32 / float64 should route to
half[] / float[] / double[] respectively without requiring an explicit
``dtype=`` hint.
"""
import pytest

np = pytest.importorskip("numpy")

import tinyusdz


def _attr(arr, dtype=None):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    if dtype is None:
        p.set_attribute("widths", arr)
    else:
        p.set_attribute("widths", arr, dtype=dtype)
    s.add_root_prim(p)
    a = p.get_attribute("widths")
    return a, s


def test_numpy_float16_autodetects_half_array():
    arr = np.array([0.5, 1.5, 2.5], dtype=np.float16)
    a, _ = _attr(arr)
    assert a.type_name == "half[]"


def test_numpy_float32_autodetects_float_array():
    arr = np.array([0.5, 1.5, 2.5], dtype=np.float32)
    a, _ = _attr(arr)
    assert a.type_name == "float[]"


def test_numpy_float64_autodetects_double_array():
    arr = np.array([0.5, 1.5, 2.5], dtype=np.float64)
    a, _ = _attr(arr)
    assert a.type_name == "double[]"


def test_numpy_float16_roundtrips_via_buffer():
    src = np.array([0.5, 1.5, 2.5, -0.25], dtype=np.float16)
    a, _ = _attr(src)
    got = np.asarray(a.value, dtype=np.float16)
    assert np.array_equal(got, src)


def test_numpy_float32_roundtrips_via_buffer():
    src = np.array([0.5, 1.5, 2.5, -0.25], dtype=np.float32)
    a, _ = _attr(src)
    got = np.asarray(a.value, dtype=np.float32)
    assert np.array_equal(got, src)


def test_numpy_float64_roundtrips_via_buffer():
    src = np.array([0.5, 1.5, 2.5, -0.25], dtype=np.float64)
    a, _ = _attr(src)
    got = np.asarray(a.value, dtype=np.float64)
    assert np.array_equal(got, src)


def test_explicit_dtype_overrides_autodetect():
    # float32 input but caller asks for half[] — explicit hint wins.
    arr = np.array([0.5, 1.5, 2.5], dtype=np.float32)
    a, _ = _attr(arr, dtype="half[]")
    assert a.type_name == "half[]"


def test_2d_ndarray_does_not_match_autodetect():
    # 2-D ndarray (e.g. point3f[]) should not be picked up by the 1-D
    # fast path. Either it raises or routes through other handling.
    arr = np.array([[0.0, 0.0, 0.0], [1.0, 1.0, 1.0]], dtype=np.float32)
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    # Either of these outcomes is acceptable; we just want to confirm
    # the 1-D fast path did not misclassify it as float[].
    try:
        p.set_attribute("p", arr)
    except (TypeError, ValueError):
        return
    a = p.get_attribute("p")
    assert a is None or a.type_name != "float[]"
