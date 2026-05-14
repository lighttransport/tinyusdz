"""Extended attribute metadata coverage — interpolation, customData,
displayName, displayGroup, documentation, hidden, weight, colorSpace.
"""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    return s2.export_to_string()


def test_interpolation_metadata_per_value(tmp_path):
    """Each interpolation value supported on primvars."""
    for interp in ["constant", "uniform", "varying", "vertex",
                   "faceVarying"]:
        txt = _rt(tmp_path, f'''#usda 1.0
def Mesh "M" {{
    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)] (
        interpolation = "{interp}"
    )
}}
''')
        assert f'interpolation = "{interp}"' in txt


def test_displayname_and_displaygroup(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int frob = 0 (
        displayName = "Frob count"
        displayGroup = "Tweaks"
    )
}
''')
    assert 'displayName = "Frob count"' in txt
    assert 'displayGroup = "Tweaks"' in txt


def test_documentation_alias_doc(tmp_path):
    """`doc` and `documentation` both end up as `documentation` after
    re-emit (they're synonyms in the parser)."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int a = 1 (
        doc = "shorthand"
    )
    custom int b = 2 (
        documentation = "long form"
    )
}
''')
    assert "shorthand" in txt
    assert "long form" in txt


def test_hidden_attr_metadata(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int internal = 0 (
        hidden = true
    )
}
''')
    assert "hidden = 1" in txt or "hidden = true" in txt


def test_attribute_customdata_dictionary(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int frob = 7 (
        customData = {
            string note = "tracked"
            int version = 2
        }
    )
}
''')
    assert "customData" in txt
    assert "tracked" in txt
    assert "version = 2" in txt


def test_colorspace_metadata(tmp_path):
    """colorSpace meta on color attributes is common in PBR pipelines."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom color3f tint = (1, 1, 1) (
        colorSpace = "srgb"
    )
}
''')
    assert 'colorSpace = "srgb"' in txt


def test_python_set_attribute_metadata(tmp_path):
    """Use the Python-side helper to author metadata after the fact."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Mesh", name="M")
    p.set_attribute("points", [(0, 0, 0), (1, 0, 0)], dtype="point3f[]")
    p.set_attribute_metadata("points", "interpolation", "vertex")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert 'interpolation = "vertex"' in txt


def test_get_attribute_metadata():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("frob", 0)
    p.set_attribute_metadata("frob", "displayName", "Frob")
    s.add_root_prim(p)
    val = p.get_attribute_metadata("frob", "displayName")
    assert val == "Frob"


def test_get_attribute_metadata_missing_returns_none():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("frob", 0)
    s.add_root_prim(p)
    assert p.get_attribute_metadata("frob", "displayName") is None


def test_multiple_metadata_keys_on_one_attribute(tmp_path):
    """Several metadata keys on the same custom attribute survive
    together. Note: schema-typed Mesh.points only round-trips
    `interpolation` today (other AttrMeta keys are routed through
    a custom-attr-only path)."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom point3f[] custom_pts = [(0, 0, 0)] (
        interpolation = "vertex"
        elementSize = 1
        customData = {
            string note = "x"
        }
    )
}
''')
    assert 'interpolation = "vertex"' in txt
    assert "elementSize" in txt
    assert "note = " in txt
