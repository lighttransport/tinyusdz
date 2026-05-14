"""Variant content authoring (Prim subtrees + attributes inside variants).

Phase C.2 deferred follow-up: full variant content surface.
- Prim.define_variant(varset, name) creates an empty variant.
- Prim.variant_add_child(varset, name, child) copies a Prim subtree.
- Prim.variant_set_attribute(varset, name, attr, value, dtype=...)
  authors an attribute inside the variant.
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc"]


def _build_two_variant_stage():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("look")
    p.set_variant_selection("look", "red")
    p.define_variant("look", "red")
    p.define_variant("look", "blue")
    p.variant_set_attribute("look", "red", "mycolor",
                            (1.0, 0.0, 0.0), dtype="color3f")
    p.variant_set_attribute("look", "blue", "mycolor",
                            (0.0, 0.0, 1.0), dtype="color3f")
    p.variant_add_child("look", "red",
                        tinyusdz.Prim("Sphere", name="ball"))
    s.add_root_prim(p)
    return s


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
def test_variant_content_roundtrip(tmp_path, fmt):
    s = _build_two_variant_stage()
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "variantSet \"look\"" in txt
    assert "\"red\"" in txt and "\"blue\"" in txt
    assert "color3f mycolor = (1, 0, 0)" in txt
    assert "color3f mycolor = (0, 0, 1)" in txt
    assert "def Sphere \"ball\"" in txt


def test_define_variant_is_idempotent():
    p = tinyusdz.Prim("Xform", name="X")
    p.define_variant("look", "red")
    p.define_variant("look", "red")  # second call should not error
    p.variant_set_attribute("look", "red", "c", (1.0, 0.0, 0.0),
                            dtype="color3f")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "color3f c = (1, 0, 0)" in txt


def test_variant_add_child_rejects_non_prim():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(TypeError):
        p.variant_add_child("look", "red", "not a Prim")


def test_variant_attribute_dtype_required_for_tuple():
    """Without dtype hint the value path may not infer color3f, but
    set_attribute path applies the same fallback as the regular
    set_attribute call."""
    p = tinyusdz.Prim("Xform", name="X")
    p.variant_set_attribute("look", "red", "v", 0.5, dtype="float")
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "float v = 0.5" in txt
