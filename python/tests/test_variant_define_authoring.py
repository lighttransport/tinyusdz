"""Authoring variant content via define_variant + variant_set_attribute."""
import tinyusdz


def test_define_variant_basic(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Asset")
    p.add_variant_set_name("color")
    p.define_variant("color", "red")
    p.define_variant("color", "blue")
    p.set_variant_selection("color", "red")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/Asset")
    names = p2.variant_names("color")
    assert "red" in names
    assert "blue" in names


def test_variant_set_attribute_inside(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Asset")
    p.add_variant_set_name("color")
    p.define_variant("color", "red")
    p.variant_set_attribute("color", "red", "label", "Red", dtype="string")
    p.set_variant_selection("color", "red")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert '"red"' in txt


def test_variant_iteration():
    p = tinyusdz.Prim("Xform", name="A")
    p.add_variant_set_name("size")
    p.define_variant("size", "S")
    p.define_variant("size", "M")
    p.define_variant("size", "L")
    sets = p.variant_sets()
    assert "size" in sets
    names = p.variant_names("size")
    assert set(names) == {"S", "M", "L"}


def test_clear_variant_selection():
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("c")
    p.define_variant("c", "a")
    p.set_variant_selection("c", "a")
    assert p.variant_selection("c") == "a"
    p.clear_variant_selection("c")
    assert p.variant_selection("c") in ("", None)
