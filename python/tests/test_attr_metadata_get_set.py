"""Attribute metadata get/set: interpolation, customData, displayName."""
import tinyusdz


def test_set_get_interpolation(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("primvars:weight", [0.1, 0.2, 0.3], dtype="float[]")
    p.set_attribute_metadata(
        "primvars:weight", "interpolation", "vertex")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/M")
    val = p2.get_attribute_metadata("primvars:weight", "interpolation")
    assert val == "vertex"


def test_set_get_displayname(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 5)
    p.set_attribute_metadata("a", "displayName", "Alpha")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    val = p2.get_attribute_metadata("a", "displayName")
    assert val == "Alpha"


def test_set_get_documentation(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 5)
    p.set_attribute_metadata("a", "documentation", "the answer")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    val = p2.get_attribute_metadata("a", "documentation")
    assert val == "the answer"


def test_set_get_hidden(tmp_path):
    """`hidden` AttrMeta is set successfully even if the readback
    form differs across formats."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 5)
    p.set_attribute_metadata("a", "hidden", True)
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "hidden" in txt


def test_get_metadata_default_when_unset():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 5)
    val = p.get_attribute_metadata("a", "documentation")
    assert val is None or val == ""
