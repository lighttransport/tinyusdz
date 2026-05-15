"""usdRender RenderSettings, RenderProduct, RenderVar."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_render_settings_basic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def RenderSettings "Settings" {
    int2 resolution = (1920, 1080)
    float pixelAspectRatio = 1.0
}
''')
    assert "RenderSettings" in txt
    assert "(1920, 1080)" in txt
    assert "pixelAspectRatio" in txt


def test_render_product_with_var(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def RenderProduct "Beauty" {
    token productType = "raster"
    asset productName = @./out.exr@
    rel orderedVars = [</Vars/color>]
}
def Scope "Vars" {
    def RenderVar "color" {
        token dataType = "color3f"
        token sourceName = "Ci"
    }
}
''')
    assert "RenderProduct" in txt
    assert "RenderVar" in txt
    assert "@./out.exr@" in txt
    assert '"raster"' in txt


def test_render_settings_includes_purposes(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def RenderSettings "Settings" {
    uniform token[] includedPurposes = ["default", "render"]
    uniform token[] materialBindingPurposes = ["full", "allPurpose"]
}
''')
    assert "includedPurposes" in txt
    assert "materialBindingPurposes" in txt


def test_render_settings_camera_rel(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Camera "Cam" {}
def RenderSettings "Settings" {
    rel camera = </Cam>
    int2 resolution = (640, 480)
}
''')
    assert "rel camera" in txt
    assert "</Cam>" in txt
