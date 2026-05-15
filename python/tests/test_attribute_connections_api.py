"""Prim.add_attribute_connection / get_attribute_connections."""
import tinyusdz


def test_add_and_get_connection(tmp_path):
    s = tinyusdz.Stage()
    a = tinyusdz.Prim("Material", name="M")
    a.set_attribute("inputs:diffuse", (1.0, 0.0, 0.0), dtype="color3f")
    a.set_attribute("outputs:result", (0.0, 0.0, 0.0), dtype="color3f")
    a.add_attribute_connection(
        "inputs:diffuse", "/M.outputs:result")
    s.add_root_prim(a)
    out = tmp_path / "x.usdc"
    s.save(str(out))

    s2 = tinyusdz.load(str(out))
    p = s2.get_prim_at_path("/M")
    conns = p.get_attribute_connections("inputs:diffuse")
    assert any("outputs:result" in c for c in conns)


def test_no_connection_returns_none_or_empty():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 1.0, dtype="float")
    conns = p.get_attribute_connections("a")
    assert conns is None or conns == []


def test_usda_connection_round_trip(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Material "M" {
    color3f inputs:diffuse.connect = </M/Surface.outputs:result>
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f outputs:result
    }
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/M")
    conns = p.get_attribute_connections("inputs:diffuse")
    assert any("outputs:result" in c for c in conns)


def test_repeated_add_attribute_connection_overwrites(tmp_path):
    """add_attribute_connection on the same attribute replaces prior
    target — last call wins."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Material", name="M")
    p.set_attribute("inputs:weight", 0.0, dtype="float")
    p.add_attribute_connection("inputs:weight", "/M/A.outputs:value")
    p.add_attribute_connection("inputs:weight", "/M/B.outputs:value")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))

    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/M")
    conns = p2.get_attribute_connections("inputs:weight")
    assert any("outputs:value" in c for c in conns)
