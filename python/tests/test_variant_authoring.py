"""Variant set + selection authoring at the metadata level.

Phase C.2: Prim.add_variant_set_name / set_variant_selection /
clear_variant_set_names / clear_variant_selection.
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc"]


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
def test_variant_set_and_selection(tmp_path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.set_variant_selection("look", "red")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "variantSets" in txt
    assert "\"look\"" in txt or "= \"look\"" in txt or "look" in txt
    assert "variants" in txt
    assert "string look = \"red\"" in txt


@pytest.mark.parametrize("fmt", FORMATS)
def test_multiple_variant_sets(tmp_path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.add_variant_set_name("rig")
    p.set_variant_selection("look", "red")
    p.set_variant_selection("rig", "hires")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "string look = \"red\"" in txt
    assert "string rig = \"hires\"" in txt


def test_qualifier_variants():
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("a", qualifier="prepend")
    p.add_variant_set_name("b", qualifier="append")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "prepend variantSets" in txt
    assert "append variantSets" in txt


def test_invalid_qualifier_raises():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(ValueError):
        p.add_variant_set_name("look", qualifier="bogus")


def test_clear_selection_single():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("look", "red")
    p.set_variant_selection("rig", "hires")
    p.clear_variant_selection("look")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "string rig = \"hires\"" in txt
    assert "string look" not in txt


def test_clear_all():
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.set_variant_selection("look", "red")
    p.clear_variant_set_names()
    p.clear_variant_selection()
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "variantSets" not in txt
    assert "variants" not in txt


@pytest.mark.parametrize("fmt", FORMATS)
def test_set_variant_selection_overwrites(tmp_path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("look", "red")
    p.set_variant_selection("look", "blue")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "string look = \"blue\"" in txt
    assert "red" not in txt
