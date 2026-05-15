"""Shader/NodeGraph input-to-output connections."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_shader_input_to_shader_output(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @./tex.png@
        float3 outputs:rgb
    }
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor.connect = </M/Tex.outputs:rgb>
        token outputs:surface
    }
}
''')
    assert "UsdUVTexture" in txt
    assert "UsdPreviewSurface" in txt
    assert "</M/Tex.outputs:rgb>" in txt


def test_material_terminal_connection(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    token outputs:surface.connect = </M/Surface.outputs:surface>
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        token outputs:surface
    }
}
''')
    assert "outputs:surface" in txt
    assert "</M/Surface.outputs:surface>" in txt


def test_nodegraph_passthrough(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def NodeGraph "NG" {
    float inputs:weight = 0.5
    float outputs:result.connect = </NG/Mul.outputs:result>
    def Shader "Mul" {
        uniform token info:id = "UsdPrimvarReader_float"
        float outputs:result
    }
}
''')
    assert "NodeGraph" in txt
    assert "outputs:result" in txt


def test_shader_input_st_primvar_reader(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "STReader" {
        uniform token info:id = "UsdPrimvarReader_float2"
        token inputs:varname = "st"
        float2 outputs:result
    }
}
''')
    assert "UsdPrimvarReader_float2" in txt
    assert '"st"' in txt
