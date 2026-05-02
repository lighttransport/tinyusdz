"""Variant iteration helpers — read-side companion to define_variant /
variant_add_child / variant_set_attribute / set_variant_selection.

Phase C-followup. Exposes:
  Prim.variant_sets()         -> list[str]
  Prim.variant_names(varset)  -> list[str]
  Prim.variant_selection(varset) -> str | None
"""
import tinyusdz


def test_variant_sets_empty_by_default():
    p = tinyusdz.Prim("Xform", name="X")
    assert p.variant_sets() == []


def test_variant_sets_lists_authored_sets():
    p = tinyusdz.Prim("Xform", name="X")
    p.define_variant("look", "red")
    p.define_variant("rig", "hires")
    assert sorted(p.variant_sets()) == ["look", "rig"]


def test_variant_names_returns_keys_in_set():
    p = tinyusdz.Prim("Xform", name="X")
    p.define_variant("look", "red")
    p.define_variant("look", "blue")
    p.define_variant("look", "green")
    assert sorted(p.variant_names("look")) == ["blue", "green", "red"]


def test_variant_names_missing_set_returns_empty():
    p = tinyusdz.Prim("Xform", name="X")
    p.define_variant("look", "red")
    assert p.variant_names("nonexistent") == []


def test_variant_selection_returns_authored_value():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("look", "red")
    assert p.variant_selection("look") == "red"


def test_variant_selection_missing_returns_none():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("look", "red")
    assert p.variant_selection("rig") is None


def test_variant_selection_none_when_unauthored():
    p = tinyusdz.Prim("Xform", name="X")
    assert p.variant_selection("look") is None


def test_iteration_after_usdc_roundtrip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.set_variant_selection("look", "red")
    p.define_variant("look", "red")
    p.define_variant("look", "blue")
    p.variant_set_attribute("look", "red", "color",
                            (1.0, 0.0, 0.0), dtype="color3f")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2.variant_sets() == ["look"]
    assert sorted(p2.variant_names("look")) == ["blue", "red"]
    assert p2.variant_selection("look") == "red"
