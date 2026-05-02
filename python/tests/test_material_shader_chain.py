"""Material/Shader connection chains — UsdPreviewSurface with
texture inputs, output:surface terminal, and material binding.
"""
import tinyusdz


_MATERIAL_USDA = '''#usda 1.0
def Xform "World"
{
    def Material "Mat"
    {
        token outputs:surface.connect = </World/Mat/Preview.outputs:surface>

        def Shader "Preview"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor.connect = </World/Mat/Tex.outputs:rgb>
            float inputs:roughness = 0.4
            float inputs:metallic = 0.0
            token outputs:surface
        }

        def Shader "Tex"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @./tex.png@
            token inputs:wrapS = "repeat"
            token inputs:wrapT = "repeat"
            float3 outputs:rgb
        }
    }

    def Xform "Geom"
    {
        rel material:binding = </World/Mat>
    }
}
'''


def _rt(tmp_path, fmt):
    src = tmp_path / "x.usda"
    src.write_text(_MATERIAL_USDA)
    s = tinyusdz.load(str(src))
    out = tmp_path / f"x.{fmt}"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_preview_surface_inputs_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "UsdPreviewSurface" in txt
    assert "inputs:roughness = 0.4" in txt
    assert "inputs:metallic = 0" in txt


def test_uv_texture_inputs_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "UsdUVTexture" in txt
    assert "@./tex.png@" in txt
    assert '"repeat"' in txt


def test_shader_connection_target_paths_preserved(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "/World/Mat/Tex.outputs:rgb" in txt
    assert "/World/Mat/Preview.outputs:surface" in txt


def test_material_binding_relationship_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "rel material:binding" in txt
    assert "</World/Mat>" in txt


def test_material_to_geom_workflow_via_python(tmp_path):
    """Author the same content via the Python API and verify it
    survives USDC round-trip with all connections intact."""
    s = tinyusdz.Stage()

    world = tinyusdz.Prim("Xform", name="World")
    mat = tinyusdz.Prim("Material", name="Mat")
    preview = tinyusdz.Prim("Shader", name="Preview")
    preview.set_attribute("info:id", "UsdPreviewSurface", dtype="token")
    preview.set_attribute("inputs:roughness", 0.5, dtype="float")

    mat.add_child(preview)
    mat.add_attribute_connection(
        "outputs:surface",
        "/World/Mat/Preview.outputs:surface",
        dtype="token")

    geom = tinyusdz.Prim("Xform", name="Geom")
    geom.add_relationship("material:binding", "/World/Mat")

    world.add_child(mat)
    world.add_child(geom)
    s.add_root_prim(world)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "UsdPreviewSurface" in txt
    assert "/World/Mat/Preview.outputs:surface" in txt
    assert "</World/Mat>" in txt
