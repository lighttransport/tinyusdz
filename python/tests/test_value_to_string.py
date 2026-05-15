"""Value.to_string formatting."""
import tinyusdz


def _val(p, name):
    return p.get_attribute(name).value


def test_int_to_string():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 42)
    assert _val(p, "n").to_string() == "42"


def test_float_to_string():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("f", 1.5, dtype="float")
    s = _val(p, "f").to_string()
    assert "1.5" in s


def test_string_to_string_quoted():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("s", "hello")
    s = _val(p, "s").to_string()
    assert "hello" in s


def test_array_to_string():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("arr", [1, 2, 3], dtype="int[]")
    s = _val(p, "arr").to_string()
    assert "1" in s and "3" in s


def test_value_to_string_diff_dtypes():
    """Value.to_string output is sensible for several dtypes."""
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 1.5, dtype="float")
    p.set_attribute("b", 2, dtype="int")
    p.set_attribute("c", "x", dtype="string")
    assert "1.5" in _val(p, "a").to_string()
    assert "2" in _val(p, "b").to_string()
    assert "x" in _val(p, "c").to_string()


def test_value_is_array_flag():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("scalar", 5)
    p.set_attribute("arr", [1, 2, 3], dtype="int[]")
    assert _val(p, "scalar").is_array is False
    assert _val(p, "arr").is_array is True
