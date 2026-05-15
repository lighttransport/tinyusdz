"""Empty stage and minimal-stage edge cases."""
import tinyusdz


def test_empty_stage_save_load(tmp_path):
    s = tinyusdz.Stage()
    out = tmp_path / "empty.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert len(s2.root_prims()) == 0


def test_empty_stage_usda(tmp_path):
    s = tinyusdz.Stage()
    out = tmp_path / "empty.usda"
    s.save(str(out))
    text = out.read_text()
    assert text.startswith("#usda 1.0")


def test_empty_stage_export_string():
    s = tinyusdz.Stage()
    txt = s.export_to_string()
    assert "#usda" in txt


def test_load_empty_usda(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text("#usda 1.0\n")
    s = tinyusdz.load(str(src))
    assert len(s.root_prims()) == 0


def test_load_with_only_metadata(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
(
    upAxis = "Y"
    metersPerUnit = 1.0
)
''')
    s = tinyusdz.load(str(src))
    assert len(s.root_prims()) == 0


def test_single_attribute_only_prim(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom int n = 1
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    assert p is not None
    assert "n" in p.property_names()
