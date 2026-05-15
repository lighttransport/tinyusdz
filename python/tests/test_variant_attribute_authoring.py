"""Variant block with attribute set inside."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_variant_set_with_attr_overrides(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    variants = {
        string color = "red"
    }
    prepend variantSets = "color"
) {
    custom string defaultLabel = "base"
    variantSet "color" = {
        "red" {
            custom string label = "Red"
        }
        "blue" {
            custom string label = "Blue"
        }
    }
}
''')
    assert "variantSet" in txt
    assert '"red"' in txt
    assert '"blue"' in txt


def test_variant_select_in_metadata(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    variants = {
        string size = "large"
    }
    prepend variantSets = "size"
) {
    variantSet "size" = {
        "small" {}
        "large" {}
    }
}
''')
    assert '"size"' in txt
    assert '"large"' in txt


def test_multi_variant_set(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    prepend variantSets = ["color", "size"]
    variants = {
        string color = "blue"
        string size = "M"
    }
) {
    variantSet "color" = {
        "red" {}
        "blue" {}
    }
    variantSet "size" = {
        "S" {}
        "M" {}
        "L" {}
    }
}
''')
    assert '"color"' in txt
    assert '"size"' in txt


def test_variant_with_child_prim(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Asset" (
    prepend variantSets = "geom"
    variants = {
        string geom = "hi"
    }
) {
    variantSet "geom" = {
        "lo" {
            def Cube "shape" {}
        }
        "hi" {
            def Sphere "shape" {}
        }
    }
}
''')
    assert "variantSet" in txt


def test_python_variant_authoring(tmp_path):
    """Author variant set from Python and round-trip."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("color")
    p.set_variant_selection("color", "red")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2.variant_selection("color") == "red"
