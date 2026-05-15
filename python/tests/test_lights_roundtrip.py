"""Light prim round-trip — SphereLight, DistantLight, RectLight,
DiskLight, DomeLight."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_sphere_light_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SphereLight "key"
{
    float inputs:intensity = 1500
    color3f inputs:color = (1, 0.95, 0.85)
    float inputs:radius = 0.5
}
''')
    assert "SphereLight" in txt
    assert "inputs:intensity = 1500" in txt
    assert "(1, 0.95, 0.85)" in txt


def test_distant_light_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def DistantLight "sun"
{
    float inputs:intensity = 5000
    float inputs:angle = 0.53
    color3f inputs:color = (1, 0.98, 0.95)
}
''')
    assert "DistantLight" in txt
    assert "inputs:angle = 0.53" in txt


def test_rect_light_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def RectLight "panel"
{
    float inputs:intensity = 2500
    float inputs:width = 1.5
    float inputs:height = 0.5
}
''')
    assert "RectLight" in txt
    assert "inputs:width = 1.5" in txt
    assert "inputs:height = 0.5" in txt


def test_disk_light_roundtrip(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def DiskLight "spot"
{
    float inputs:intensity = 1000
    float inputs:radius = 0.3
}
''')
    assert "DiskLight" in txt
    assert "inputs:radius = 0.3" in txt


def test_dome_light_with_texture(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def DomeLight "dome"
{
    float inputs:intensity = 1.0
    asset inputs:texture:file = @./env.exr@
    token inputs:texture:format = "latlong"
}
''')
    assert "DomeLight" in txt
    assert "@./env.exr@" in txt
    assert '"latlong"' in txt


def test_multiple_lights_in_one_stage(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "scene"
{
    def SphereLight "key"
    {
        float inputs:intensity = 1500
    }

    def DistantLight "sun"
    {
        float inputs:intensity = 5000
    }

    def RectLight "fill"
    {
        float inputs:intensity = 800
    }
}
''')
    assert "SphereLight" in txt
    assert "DistantLight" in txt
    assert "RectLight" in txt
