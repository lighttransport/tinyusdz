"""Generic Shader (unknown info:id) property enumeration + lookup.

Phase C.5: Shaders with unknown info_id are reconstructed as
ShaderNode in `Shader::value`. Property listing must look there too,
and Prim.get_attribute must find them.
"""
import tempfile
import tinyusdz


_GENERIC_SHADER_USDA = '''#usda 1.0
def Shader "MyShader"
{
    uniform token info:id = "MyCustom"
    float inputs:roughness = 0.5
    color3f inputs:baseColor = (1, 0.5, 0)
    token outputs:surface
}
'''


def _load(tmp_path):
    p = tmp_path / "shader.usda"
    p.write_text(_GENERIC_SHADER_USDA)
    return tinyusdz.load(str(p))


def test_property_names_contains_inputs_and_outputs(tmp_path):
    s = _load(tmp_path)
    pr = s.get_prim_at_path("/MyShader")
    names = pr.property_names()
    assert "info:id" in names
    assert "inputs:roughness" in names
    assert "inputs:baseColor" in names
    assert "outputs:surface" in names


def test_get_attribute_resolves_generic_shader_inputs(tmp_path):
    s = _load(tmp_path)
    pr = s.get_prim_at_path("/MyShader")
    a = pr.get_attribute("inputs:baseColor")
    assert a is not None
    assert a.type_name == "color3f"
    a = pr.get_attribute("inputs:roughness")
    assert a is not None
    assert a.type_name == "float"


def test_outputs_surface_appears_in_property_names(tmp_path):
    """Terminal output (no value) is enumerated by property_names even
    though get_attribute() filters it out today (it has no authored
    value to surface). Property listing is what C.5 fixed."""
    s = _load(tmp_path)
    pr = s.get_prim_at_path("/MyShader")
    assert "outputs:surface" in pr.property_names()
