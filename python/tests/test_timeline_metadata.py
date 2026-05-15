"""Stage-level timeline metadata round-trip — startTimeCode,
endTimeCode, framesPerSecond, timeCodesPerSecond.
"""
import tinyusdz


def test_typed_setters_roundtrip(tmp_path):
    s = tinyusdz.Stage()
    s.set_up_axis("Y")
    s.set_meters_per_unit(0.01)
    s.set_time_codes_per_second(48.0)
    s.set_frames_per_second(24.0)
    s.set_start_time_code(0.0)
    s.set_end_time_code(120.0)
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert 'upAxis = "Y"' in txt
    assert "metersPerUnit = 0.01" in txt
    assert "timeCodesPerSecond = 48" in txt
    assert "framesPerSecond = 24" in txt
    assert "startTimeCode = 0" in txt
    assert "endTimeCode = 120" in txt


def test_timeline_metadata_via_usda(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
(
    startTimeCode = 1
    endTimeCode = 240
    timeCodesPerSecond = 24
    framesPerSecond = 24
    upAxis = "Z"
    metersPerUnit = 1.0
)
def Xform "X" {}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "startTimeCode = 1" in txt
    assert "endTimeCode = 240" in txt
    assert "timeCodesPerSecond = 24" in txt
    assert "framesPerSecond = 24" in txt
    assert 'upAxis = "Z"' in txt


def test_only_endtimecode_authored(tmp_path):
    """Either bound can be authored independently; missing the other
    should not crash the writer."""
    s = tinyusdz.Stage()
    s.set_end_time_code(100.0)
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "endTimeCode = 100" in txt


def test_negative_timecode(tmp_path):
    """Pre-roll frames can have negative time codes."""
    s = tinyusdz.Stage()
    s.set_start_time_code(-10.0)
    s.set_end_time_code(50.0)
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "startTimeCode = -10" in txt
    assert "endTimeCode = 50" in txt


def test_default_prim_metadata(tmp_path):
    """defaultPrim must round-trip alongside other stage metadata."""
    s = tinyusdz.Stage()
    s.set_metadata("defaultPrim", "Hero")
    p = tinyusdz.Prim("Xform", name="Hero")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert 'defaultPrim = "Hero"' in txt
