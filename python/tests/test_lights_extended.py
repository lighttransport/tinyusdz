"""usdLux: DomeLight, DistantLight, RectLight, CylinderLight, DiskLight."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_dome_light_with_texture(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def DomeLight "Dome" {
    asset inputs:texture:file = @./hdri.exr@
    token inputs:texture:format = "latlong"
    float inputs:intensity = 1.5
}
''')
    assert "DomeLight" in txt
    assert "@./hdri.exr@" in txt
    assert '"latlong"' in txt


def test_distant_light_angle(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def DistantLight "Sun" {
    float inputs:angle = 0.53
    float inputs:intensity = 50000
    color3f inputs:color = (1, 0.95, 0.8)
}
''')
    assert "DistantLight" in txt
    assert "inputs:angle" in txt


def test_rect_light_dimensions(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def RectLight "Panel" {
    float inputs:width = 2.0
    float inputs:height = 1.0
    float inputs:intensity = 100
}
''')
    assert "RectLight" in txt
    assert "inputs:width" in txt
    assert "inputs:height" in txt


def test_cylinder_light(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def CylinderLight "Tube" {
    float inputs:length = 4.0
    float inputs:radius = 0.1
}
''')
    assert "CylinderLight" in txt


def test_disk_light(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def DiskLight "Disk" {
    float inputs:radius = 0.5
    float inputs:intensity = 200
}
''')
    assert "DiskLight" in txt


def test_light_shaping_api_usda_only(tmp_path):
    """ShapingAPI cone/focus inputs — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def SphereLight "Spot" (
    apiSchemas = ["ShapingAPI"]
) {
    float inputs:shaping:cone:angle = 30
    float inputs:shaping:cone:softness = 0.1
    float inputs:shaping:focus = 2.0
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "ShapingAPI" in txt
