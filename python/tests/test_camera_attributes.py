"""Camera schema round-trip — focal length, aperture, projection,
clipping planes, focus distance.
"""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_camera_perspective_basic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Camera "main"
{
    float focalLength = 50
    float horizontalAperture = 36
    float verticalAperture = 24
    uniform token projection = "perspective"
    float2 clippingRange = (0.1, 1000)
}
''')
    assert "Camera" in txt
    assert "focalLength = 50" in txt
    assert "horizontalAperture = 36" in txt
    assert "verticalAperture = 24" in txt
    assert '"perspective"' in txt
    assert "(0.1, 1000)" in txt


def test_camera_orthographic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Camera "ortho"
{
    uniform token projection = "orthographic"
    float horizontalAperture = 10
    float verticalAperture = 8
}
''')
    assert '"orthographic"' in txt
    assert "horizontalAperture = 10" in txt


def test_camera_focus_distance(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Camera "dof"
{
    float focalLength = 35
    float focusDistance = 5.0
    float fStop = 2.8
}
''')
    assert "focusDistance = 5" in txt
    assert "fStop = 2.8" in txt


def test_camera_with_xform(tmp_path):
    """Camera prims often carry xformOps."""
    txt = _rt(tmp_path, '''#usda 1.0
def Camera "panning"
{
    float focalLength = 50
    double3 xformOp:translate = (0, 1.7, 5)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
''')
    assert "Camera" in txt
    assert "(0, 1.7, 5)" in txt
    assert "xformOp:translate" in txt


def test_camera_animated_focal_length_usda_only(tmp_path):
    """USDA round-trip preserves Camera time-sampled focalLength;
    USDC currently drops it (Camera-schema timesamples gap)."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Camera "anim"
{
    float focalLength.timeSamples = {
        0: 35,
        24: 50,
        48: 85
    }
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "focalLength.timeSamples" in txt
    assert "0: 35" in txt
    assert "48: 85" in txt
