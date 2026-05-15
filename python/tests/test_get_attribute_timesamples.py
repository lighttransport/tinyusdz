"""Prim.get_attribute_timesamples returns (time, Value) tuples."""
import tinyusdz


def test_basic_timesamples_read(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        0: 1.0,
        10: 2.0,
        20: 3.0
    }
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    samples = p.get_attribute_timesamples("v")
    assert len(samples) == 3
    times = [t for t, _ in samples]
    assert sorted(times) == [0, 10, 20]


def test_timesamples_value_form(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        0: 1.5
    }
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    samples = p.get_attribute_timesamples("v")
    t, v = samples[0]
    assert t == 0
    if hasattr(v, "as_scalar"):
        assert abs(v.as_scalar() - 1.5) < 1e-6
    else:
        assert "1.5" in str(v)


def test_no_timesamples_returns_empty(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    float v = 42.0
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/X")
    samples = p.get_attribute_timesamples("v")
    assert samples == [] or samples is None


def test_set_attribute_at_time_round_trip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute_at_time("v", 0, 1.0, dtype="float")
    p.set_attribute_at_time("v", 10, 2.0, dtype="float")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    samples = p2.get_attribute_timesamples("v")
    times = sorted(t for t, _ in samples)
    assert times == [0, 10]
