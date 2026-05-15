"""Complex multi-node shader graph round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_diffuse_color_with_texture(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    token outputs:surface.connect = </M/Surface.outputs:surface>

    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor.connect = </M/Tex.outputs:rgb>
        float inputs:roughness = 0.5
        float inputs:metallic = 0.0
        token outputs:surface
    }
    def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./diffuse.png@
        float2 inputs:st.connect = </M/STReader.outputs:result>
        float3 outputs:rgb
    }
    def Shader "STReader" {
        uniform token info:id = "UsdPrimvarReader_float2"
        token inputs:varname = "st"
        float2 outputs:result
    }
}
''')
    assert "UsdPreviewSurface" in txt
    assert "UsdUVTexture" in txt
    assert "UsdPrimvarReader_float2" in txt
    assert "@./diffuse.png@" in txt
    assert "</M/Tex.outputs:rgb>" in txt
    assert "</M/STReader.outputs:result>" in txt


def test_normal_map_chain(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "NormalTex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./normal.png@
        token inputs:sourceColorSpace = "raw"
        float3 outputs:rgb
    }
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        normal3f inputs:normal.connect = </M/NormalTex.outputs:rgb>
        token outputs:surface
    }
}
''')
    assert "@./normal.png@" in txt
    assert "</M/NormalTex.outputs:rgb>" in txt


def test_emission_with_factor(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:emissiveColor = (1, 0.8, 0.2)
        token outputs:surface
    }
}
''')
    assert "emissiveColor" in txt
    assert "(1, 0.8, 0.2)" in txt
