"""Prim.to_string output content."""
import tinyusdz


def test_prim_to_string_includes_name_and_type():
    p = tinyusdz.Prim("Sphere", name="Ball")
    p.set_attribute("radius", 2.0, dtype="double")
    s = p.to_string()
    assert "Sphere" in s
    assert "Ball" in s


def test_prim_to_string_with_children():
    root = tinyusdz.Prim("Xform", name="root")
    root.add_child(tinyusdz.Prim("Sphere", name="s"))
    s = root.to_string()
    assert "root" in s
    assert "Sphere" in s


def test_prim_to_string_with_attributes():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("count", 7)
    p.set_attribute("label", "demo")
    s = p.to_string()
    assert "count" in s
    assert "7" in s


def test_prim_repr_basic():
    p = tinyusdz.Prim("Xform", name="X")
    r = repr(p)
    assert "Xform" in r
    assert "X" in r
