"""Stage metadata typed setters: ergonomic wrappers for set_metadata.

Phase C.6.
"""
import pytest
import tinyusdz


def test_set_up_axis_z():
    s = tinyusdz.Stage()
    s.set_up_axis("Z")
    txt = s.export_to_string()
    assert "upAxis" in txt and "\"Z\"" in txt


def test_set_up_axis_invalid():
    s = tinyusdz.Stage()
    with pytest.raises(ValueError):
        s.set_up_axis("Q")


def test_set_meters_per_unit():
    s = tinyusdz.Stage()
    s.set_meters_per_unit(0.01)
    assert s.get_metadata("metersPerUnit") == pytest.approx(0.01)
    txt = s.export_to_string()
    assert "metersPerUnit" in txt


def test_set_time_codes_per_second_and_frames_per_second():
    s = tinyusdz.Stage()
    s.set_time_codes_per_second(48.0)
    s.set_frames_per_second(24.0)
    assert s.get_metadata("timeCodesPerSecond") == pytest.approx(48.0)
    assert s.get_metadata("framesPerSecond") == pytest.approx(24.0)


def test_set_start_end_time_code():
    s = tinyusdz.Stage()
    s.set_start_time_code(0.0)
    s.set_end_time_code(120.0)
    assert s.get_metadata("startTimeCode") == pytest.approx(0.0)
    assert s.get_metadata("endTimeCode") == pytest.approx(120.0)


def test_roundtrip_through_usdc(tmp_path):
    s = tinyusdz.Stage()
    s.set_up_axis("Z")
    s.set_meters_per_unit(0.01)
    s.set_time_codes_per_second(48.0)
    s.set_start_time_code(0.0)
    s.set_end_time_code(100.0)
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert s2.get_metadata("upAxis") == "Z"
    assert s2.get_metadata("metersPerUnit") == pytest.approx(0.01)
    assert s2.get_metadata("timeCodesPerSecond") == pytest.approx(48.0)
    assert s2.get_metadata("startTimeCode") == pytest.approx(0.0)
    assert s2.get_metadata("endTimeCode") == pytest.approx(100.0)
