"""Stage.set_metadata / get_metadata generic API."""
import tinyusdz


def test_stage_set_get_doc(tmp_path):
    s = tinyusdz.Stage()
    s.set_metadata("doc", "scene description")
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "scene description" in txt


def test_stage_set_get_default_prim(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="Root"))
    s.set_metadata("defaultPrim", "Root")
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert 'defaultPrim = "Root"' in txt


def test_stage_get_metadata_after_load(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
(
    upAxis = "Z"
    metersPerUnit = 0.01
    doc = "z up scene"
)
def Xform "X" {}
''')
    s = tinyusdz.load(str(src))
    assert s.get_metadata("upAxis") == "Z"
    assert abs(s.get_metadata("metersPerUnit") - 0.01) < 1e-12


def test_stage_metadata_persists_through_usdc(tmp_path):
    s = tinyusdz.Stage()
    s.set_up_axis("Z")
    s.set_meters_per_unit(0.5)
    s.set_default_prim("Root")
    s.add_root_prim(tinyusdz.Prim("Xform", name="Root"))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert s2.get_metadata("upAxis") == "Z"
    assert abs(s2.get_metadata("metersPerUnit") - 0.5) < 1e-12
    assert s2.get_default_prim() == "Root"
