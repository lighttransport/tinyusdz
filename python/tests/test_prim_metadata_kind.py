"""Prim-level metadata: get_metadata / set_metadata for kind, hidden, doc."""
import tinyusdz


def test_set_get_kind_via_metadata(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("kind", "component")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert 'kind = "component"' in txt


def test_set_get_documentation(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("documentation", "the asset")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "the asset" in txt


def test_set_hidden_metadata(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("hidden", True)
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "hidden" in txt


def test_set_active_false(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("active", False)
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "active = false" in txt


def test_get_metadata_after_load(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    kind = "assembly"
) {}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    val = p.get_metadata("kind")
    assert val == "assembly"
