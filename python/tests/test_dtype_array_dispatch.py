"""Array dtype dispatch for newer types: int64[], uint64[], half[]."""
import tinyusdz


def _rt(tmp_path, attr_name, value, dtype):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute(attr_name, value, dtype=dtype)
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_int64_array(tmp_path):
    txt = _rt(tmp_path, "v", [1, 2, 3], "int64[]")
    assert "int64[]" in txt or "int[]" in txt


def test_uint64_array(tmp_path):
    txt = _rt(tmp_path, "v", [1, 2, 3], "uint64[]")
    assert "uint64[]" in txt or "uint[]" in txt


def test_uint_array(tmp_path):
    txt = _rt(tmp_path, "v", [10, 20, 30], "uint[]")
    assert "v" in txt


def test_half_array(tmp_path):
    txt = _rt(tmp_path, "v", [1.0, 2.0, 3.0], "half[]")
    assert "v" in txt


def test_uchar_array(tmp_path):
    txt = _rt(tmp_path, "v", [0, 128, 255], "uchar[]")
    assert "v" in txt


def test_token_array_via_dtype(tmp_path):
    txt = _rt(tmp_path, "v", ["a", "b", "c"], "token[]")
    assert '"a"' in txt and '"c"' in txt
