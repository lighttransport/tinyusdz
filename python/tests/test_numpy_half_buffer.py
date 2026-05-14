"""set_attribute(..., dtype="half[]") accepts numpy arrays.

Previously only Python list/tuple of float worked; passing a numpy
ndarray raised TypeError. The fast path uses the buffer protocol
(`PyObject_GetBuffer`) and recognises:
  - format 'e' (float16): zero-copy bit copy.
  - format 'f' (float32) / 'd' (float64): widening conversion.
"""
import pytest

np = pytest.importorskip("numpy")
import tinyusdz


@pytest.mark.parametrize("np_dtype", [np.float16, np.float32, np.float64])
def test_numpy_half_array_write(np_dtype, tmp_path):
    arr = np.array([0.5, 1.5, 2.5], dtype=np_dtype)
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("widths", arr, dtype="half[]")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "half[] widths = [0.5, 1.5, 2.5]" in txt


def test_numpy_half_array_usdc_roundtrip(tmp_path):
    arr = np.array([0.5, 1.5, 2.5, 0.25], dtype=np.float16)
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("widths", arr, dtype="half[]")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/M").get_attribute("widths")
    assert a.type_name == "half[]"
    # Read-side buffer protocol already supported 'e' format.
    arr2 = np.asarray(a.value)
    assert arr2.dtype == np.float16
    assert list(arr2) == [0.5, 1.5, 2.5, 0.25]


def test_list_path_still_works():
    """The pre-existing list/tuple path is unchanged."""
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("widths", [0.5, 1.5], dtype="half[]")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    assert "half[] widths = [0.5, 1.5]" in s.export_to_string()
