"""set_attribute accepts several input forms for the same dtype."""
import tinyusdz


def _attr_value(p, name):
    return p.get_attribute(name).value.as_scalar()


def test_int_via_python_int():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 42)
    assert _attr_value(p, "n") == 42


def test_float_via_python_float():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("f", 3.14)
    assert abs(_attr_value(p, "f") - 3.14) < 1e-3


def test_float_via_int_with_dtype():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("f", 5, dtype="float")
    assert _attr_value(p, "f") == 5.0


def test_string_value():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("s", "hi")
    assert _attr_value(p, "s") == "hi"


def test_bool_via_python_bool():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("b", True)
    assert _attr_value(p, "b") is True


def test_tuple_for_vec3():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", (1.0, 2.0, 3.0), dtype="float3")
    val = p.get_attribute("v").value
    assert "1" in val.to_string()
    assert "3" in val.to_string()


def test_list_overrides_with_dtype():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", [1, 2, 3], dtype="int[]")
    val = p.get_attribute("v").value
    assert val.is_array


def test_repeat_set_attribute_overwrites():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 1)
    p.set_attribute("n", 99)
    assert _attr_value(p, "n") == 99
