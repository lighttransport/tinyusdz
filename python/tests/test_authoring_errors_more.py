"""Authoring API error inputs."""
import pytest
import tinyusdz


def test_set_attribute_unsupported_type():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(TypeError):
        p.set_attribute("v", object())


def test_invalid_dtype_string_fallback():
    """Unknown dtype falls back rather than erroring; verify call returns."""
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", 1, dtype="not-a-real-dtype")
    # Either the attr was authored with a default dtype, or silently
    # ignored — either way, no crash.


def test_load_nonexistent_file():
    with pytest.raises(Exception):
        tinyusdz.load("/nonexistent/path/x.usda")


def test_loads_invalid_usda():
    with pytest.raises(Exception):
        tinyusdz.loads("this is not USDA")


def test_save_to_invalid_path(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    with pytest.raises(Exception):
        s.save("/nonexistent/dir/foo.usdc")


def test_get_attribute_nonexistent_returns_none():
    p = tinyusdz.Prim("Xform", name="X")
    assert p.get_attribute("nope") is None
