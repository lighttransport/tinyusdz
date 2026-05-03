"""tinyusdz.Attribute object surface."""
import tinyusdz


def test_attribute_repr():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 5)
    a = p.get_attribute("n")
    r = repr(a)
    assert "n" in r
    assert "Attribute" in r


def test_attribute_name():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("count", 5)
    a = p.get_attribute("count")
    assert a.name == "count"


def test_attribute_value_property():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 7)
    a = p.get_attribute("n")
    assert a.value.as_scalar() == 7


def test_attribute_type_name_after_load(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 1.5, dtype="float")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    a = s2.get_prim_at_path("/X").get_attribute("a")
    assert "float" in a.type_name


def test_get_attribute_after_save_then_modify(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 5)
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    p2.set_attribute("n", 99)
    out2 = tmp_path / "y.usdc"
    s2.save(str(out2))
    s3 = tinyusdz.load(str(out2))
    assert s3.get_prim_at_path("/X").get_attribute("n").value.as_scalar() == 99
