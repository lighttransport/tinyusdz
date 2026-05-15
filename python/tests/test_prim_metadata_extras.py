"""Extended prim metadata round-trip — assetInfo, instanceable,
active, comments.
"""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_instanceable_flag_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    instanceable = true
)
{
}
''')
    assert "instanceable = 1" in txt or "instanceable = true" in txt


def test_active_flag_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    active = false
)
{
}
''')
    assert "active = 0" in txt or "active = false" in txt


def test_assetInfo_metadata_roundtrip(tmp_path):
    """assetInfo is a Dictionary stage-/prim-level metadata key."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    assetInfo = {
        string name = "MyAsset"
        string version = "1.0"
        string identifier = "/path/to/asset.usd"
    }
)
{
}
''')
    assert "assetInfo" in txt
    assert '"MyAsset"' in txt
    assert '"1.0"' in txt


def test_kind_metadata_roundtrip(tmp_path):
    """`kind` is one of Subcomponent/Component/Group/Assembly/Model/SceneLibrary."""
    for kind in ["component", "group", "assembly", "subcomponent"]:
        txt = _rt(tmp_path, f'''#usda 1.0
def Xform "X" (
    kind = "{kind}"
)
{{
}}
''')
        assert f'kind = "{kind}"' in txt


def test_specifier_class_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
class "Template"
{
    custom int n = 0
}
''')
    assert 'class' in txt
    assert "Template" in txt


def test_specifier_over_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
over "Overlay"
{
    custom int n = 7
}
''')
    assert "over" in txt
    assert "Overlay" in txt


def test_python_set_metadata_kind(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("kind", "component")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert 'kind = "component"' in txt


def test_python_get_metadata_kind():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("kind", "component")
    assert p.get_metadata("kind") == "component"


def test_python_set_metadata_active():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("active", False)
    assert p.get_metadata("active") is False
