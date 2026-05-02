"""Extended variant authoring coverage — multiple attribute dtypes
inside variants, multiple variants in a set, variants with timesamples.
"""
import tinyusdz


def test_variant_with_multiple_attribute_dtypes(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.define_variant("look", "red")
    p.variant_set_attribute("look", "red", "color", (1, 0, 0),
                             dtype="color3f")
    p.variant_set_attribute("look", "red", "intensity", 1.0,
                             dtype="float")
    p.variant_set_attribute("look", "red", "label", "warm",
                             dtype="string")
    p.variant_set_attribute("look", "red", "count", 3, dtype="int")
    p.set_variant_selection("look", "red")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "(1, 0, 0)" in txt
    assert "intensity = 1" in txt
    assert '"warm"' in txt
    assert "count = 3" in txt


def test_three_variants_in_one_set(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    for name, color in [("red", (1, 0, 0)),
                         ("green", (0, 1, 0)),
                         ("blue", (0, 0, 1))]:
        p.define_variant("look", name)
        p.variant_set_attribute("look", name, "color", color,
                                 dtype="color3f")
    p.set_variant_selection("look", "green")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert "look" in p2.variant_sets()
    names = sorted(p2.variant_names("look"))
    assert names == ["blue", "green", "red"]
    assert p2.variant_selection("look") == "green"


def test_two_variant_sets_independent(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    for setname, variants in [("shading", ["red", "blue"]),
                               ("rigging", ["lo", "hi"])]:
        p.add_variant_set_name(setname)
        for v in variants:
            p.define_variant(setname, v)
    p.set_variant_selection("shading", "red")
    p.set_variant_selection("rigging", "hi")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert sorted(p2.variant_sets()) == ["rigging", "shading"]
    assert p2.variant_selection("shading") == "red"
    assert p2.variant_selection("rigging") == "hi"


def test_variant_with_child_prim(tmp_path):
    """Variants can author entire subtree replacements via add_child."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("topology")
    p.define_variant("topology", "high_poly")
    p.define_variant("topology", "low_poly")

    high_mesh = tinyusdz.Prim("Mesh", name="m")
    high_mesh.set_attribute("points",
                             [(0, 0, 0)] * 100, dtype="point3f[]")
    p.variant_add_child("topology", "high_poly", high_mesh)

    low_mesh = tinyusdz.Prim("Mesh", name="m")
    low_mesh.set_attribute("points",
                            [(0, 0, 0), (1, 0, 0)], dtype="point3f[]")
    p.variant_add_child("topology", "low_poly", low_mesh)

    p.set_variant_selection("topology", "low_poly")
    s.add_root_prim(p)

    out = tmp_path / "x.usda"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "variantSet" in txt or "high_poly" in txt
    assert "high_poly" in txt
    assert "low_poly" in txt


def test_variant_clear_selection_specific(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("a")
    p.add_variant_set_name("b")
    p.set_variant_selection("a", "v1")
    p.set_variant_selection("b", "v2")
    p.clear_variant_selection("a")
    s.add_root_prim(p)

    assert p.variant_selection("a") is None
    assert p.variant_selection("b") == "v2"


def test_variant_clear_all_selections():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("a")
    p.add_variant_set_name("b")
    p.set_variant_selection("a", "v1")
    p.set_variant_selection("b", "v2")
    p.clear_variant_selection()  # no arg = all

    assert p.variant_selection("a") is None
    assert p.variant_selection("b") is None


def test_variant_define_idempotent_after_redefine():
    """Defining the same variant twice should not duplicate."""
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.define_variant("look", "red")
    p.define_variant("look", "red")  # second call — must not crash
    p.define_variant("look", "blue")
    names = sorted(p.variant_names("look"))
    assert names == ["blue", "red"]
