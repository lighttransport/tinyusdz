"""variant_add_child: nested prim inside a variant body."""
import tinyusdz


def test_variant_add_child_basic(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Asset")
    p.add_variant_set_name("geom")
    p.define_variant("geom", "hi")
    p.define_variant("geom", "lo")
    p.variant_add_child("geom", "hi", tinyusdz.Prim("Sphere", name="shape"))
    p.variant_add_child("geom", "lo", tinyusdz.Prim("Cube", name="shape"))
    p.set_variant_selection("geom", "hi")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "variantSet" in txt
    assert "Sphere" in txt
    assert "Cube" in txt


def test_variant_add_child_round_trip_usda(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="A")
    p.add_variant_set_name("v")
    p.define_variant("v", "one")
    p.variant_add_child("v", "one", tinyusdz.Prim("Xform", name="child"))
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert '"v"' in txt
    assert '"one"' in txt
