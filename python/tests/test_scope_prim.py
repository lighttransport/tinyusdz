"""Scope prim type — non-imageable container."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_scope_basic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Scope "Looks" {
    def Material "Mat" {}
}
''')
    assert "Scope" in txt
    assert "Looks" in txt
    assert "Material" in txt


def test_scope_with_children(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Scope "Models" {
    def Xform "A" {}
    def Xform "B" {}
    def Xform "C" {}
}
''')
    assert '"A"' in txt and '"B"' in txt and '"C"' in txt


def test_scope_authored_in_python(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Scope", name="World")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/World")
    assert p2 is not None
    assert p2.type_name == "Scope"


def test_scope_no_visibility_kind_required(tmp_path):
    """Scope is non-imageable — no visibility/purpose required."""
    txt = _rt(tmp_path, '''#usda 1.0
def Scope "S" (
    kind = "group"
) {}
''')
    assert "Scope" in txt
    assert 'kind = "group"' in txt
