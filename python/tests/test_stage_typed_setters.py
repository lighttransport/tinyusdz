"""Stage typed setters: set_up_axis, set_meters_per_unit, time codes."""
import tinyusdz


def test_set_up_axis(tmp_path):
    s = tinyusdz.Stage()
    s.set_up_axis("Z")
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert '"Z"' in txt or "upAxis" in txt


def test_set_meters_per_unit(tmp_path):
    s = tinyusdz.Stage()
    s.set_meters_per_unit(0.01)
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "0.01" in txt
    assert "metersPerUnit" in txt


def test_set_time_codes_per_second(tmp_path):
    s = tinyusdz.Stage()
    s.set_time_codes_per_second(24.0)
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "timeCodesPerSecond" in txt


def test_set_start_end_time_code(tmp_path):
    s = tinyusdz.Stage()
    s.set_start_time_code(0)
    s.set_end_time_code(120)
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "startTimeCode" in txt
    assert "endTimeCode" in txt


def test_set_frames_per_second(tmp_path):
    s = tinyusdz.Stage()
    s.set_frames_per_second(30.0)
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "framesPerSecond" in txt


def test_set_default_prim(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="Root"))
    s.set_default_prim("Root")
    assert s.get_default_prim() == "Root"
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert 'defaultPrim = "Root"' in txt


def test_combined_metadata(tmp_path):
    s = tinyusdz.Stage()
    s.set_up_axis("Y")
    s.set_meters_per_unit(1.0)
    s.set_time_codes_per_second(24)
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    s.set_default_prim("X")
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "upAxis" in txt
    assert "metersPerUnit" in txt
    assert "timeCodesPerSecond" in txt
