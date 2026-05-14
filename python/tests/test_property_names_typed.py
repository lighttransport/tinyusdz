"""Prim.property_names() across typed schemas."""
import tinyusdz


def test_property_names_xform():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("custom_n", 5)
    s.add_root_prim(p)
    names = p.property_names()
    assert "custom_n" in names


def test_property_names_after_load_xform(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom int extra = 7
    custom string label = "x"
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    names = p.property_names()
    assert "extra" in names
    assert "label" in names


def test_property_names_includes_relationships(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    rel target = </X>
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    names = p.property_names()
    assert "target" in names


def test_property_names_empty_prim():
    p = tinyusdz.Prim("Xform", name="empty")
    assert p.property_names() == []


def test_attribute_value_types_after_load(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom float a = 1.5
    custom int b = 10
    custom string c = "x"
    custom double d = 2.5
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    assert p.get_attribute("a").value.as_scalar() == 1.5
    assert p.get_attribute("b").value.as_scalar() == 10
    assert p.get_attribute("c").value.as_scalar() == "x"
    assert abs(p.get_attribute("d").value.as_scalar() - 2.5) < 1e-12
