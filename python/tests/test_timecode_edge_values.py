"""Time sample timecodes: negative, fractional, very large."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_negative_timecode(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        -10: 0.0,
        0: 1.0,
        10: 2.0
    }
}
''')
    assert "-10" in txt
    assert "0: 1" in txt


def test_fractional_timecode(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        0.5: 1.0,
        1.5: 2.0,
        2.25: 3.0
    }
}
''')
    assert "0.5" in txt
    assert "2.25" in txt


def test_large_timecode(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        100000: 1.0,
        1000000: 2.0
    }
}
''')
    assert "100000" in txt
    assert "1e+06" in txt or "1000000" in txt


def test_single_timesample(tmp_path):
    """A timeSamples block with a single entry round-trips."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    float v.timeSamples = {
        42: 3.14
    }
}
''')
    assert "42" in txt
    assert "3.14" in txt


def test_stage_timecode_metadata(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    startTimeCode = 0
    endTimeCode = 240
    timeCodesPerSecond = 24
)
def Xform "X" {}
''')
    assert "startTimeCode" in txt
    assert "endTimeCode" in txt
    assert "timeCodesPerSecond" in txt


def test_stage_negative_start_time(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    startTimeCode = -50
    endTimeCode = 100
)
def Xform "X" {}
''')
    assert "-50" in txt
