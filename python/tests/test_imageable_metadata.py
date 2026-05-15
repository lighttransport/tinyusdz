"""UsdGeomImageable metadata: visibility, purpose, kind, hidden."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_visibility_invisible(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    token visibility = "invisible"
}
''')
    assert '"invisible"' in txt


def test_visibility_inherited(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    token visibility = "inherited"
}
''')
    assert "Xform" in txt


def test_purpose_render(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    uniform token purpose = "render"
}
''')
    assert '"render"' in txt


def test_purpose_proxy_and_guide(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "P" {
    uniform token purpose = "proxy"
}
def Xform "G" {
    uniform token purpose = "guide"
}
''')
    assert '"proxy"' in txt
    assert '"guide"' in txt


def test_kind_component(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Asset" (
    kind = "component"
) {}
''')
    assert 'kind = "component"' in txt


def test_kind_assembly_and_group(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" (
    kind = "group"
) {
    def Xform "A" (
        kind = "assembly"
    ) {}
}
''')
    assert 'kind = "group"' in txt
    assert 'kind = "assembly"' in txt


def test_hidden_prim_meta(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    hidden = true
) {}
''')
    assert "hidden = true" in txt


def test_active_false_usda_only(tmp_path):
    """USDA->USDA preserves `active = false`. USDC may drop it
    (separate gap if so)."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    active = false
) {}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "active = false" in txt


def test_instanceable_true(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    instanceable = true
    references = @./other.usda@</Asset>
) {}
''')
    assert "instanceable = true" in txt
