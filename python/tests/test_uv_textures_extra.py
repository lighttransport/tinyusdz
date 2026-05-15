"""UV texture wrap modes and color space."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_uvtexture_wrap_modes(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./tex.png@
        token inputs:wrapS = "repeat"
        token inputs:wrapT = "clamp"
        float3 outputs:rgb
    }
}
''')
    assert '"repeat"' in txt
    assert '"clamp"' in txt


def test_uvtexture_source_color_space(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./tex.png@
        token inputs:sourceColorSpace = "sRGB"
        float3 outputs:rgb
    }
}
''')
    assert '"sRGB"' in txt


def test_uvtexture_fallback_and_scale_usdc(tmp_path):
    """USDC round-trip preserves fallback/scale after the
    prim-reconstruct-shader.cc PARSE_TYPED_ATTRIBUTE additions
    that route them into UsdUVTexture::fallback/::scale."""
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./tex.png@
        float4 inputs:fallback = (0.5, 0.5, 0.5, 1.0)
        float4 inputs:scale = (1, 1, 1, 1)
        float3 outputs:rgb
    }
}
''')
    assert "fallback" in txt
    assert "scale" in txt


def test_uvtexture_bias_usdc(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./tex.png@
        float4 inputs:bias = (0, 0, 0, 0)
        float3 outputs:rgb
    }
}
''')
    assert "bias" in txt
