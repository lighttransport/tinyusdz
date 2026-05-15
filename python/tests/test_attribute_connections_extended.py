"""Extended attribute-connection coverage — multiple targets,
listop qualifiers, dtype propagation through USDC roundtrip.
"""
import tinyusdz


def test_single_connection_roundtrip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_attribute_connection("inputs:diffuse",
                                "/Materials/Mat.outputs:rgb")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "inputs:diffuse.connect" in txt
    assert "/Materials/Mat.outputs:rgb" in txt


def test_multiple_connections_on_same_attribute(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_attribute_connection(
        "inputs:src",
        ["/A.outputs:r", "/B.outputs:g", "/C.outputs:b"])
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "/A.outputs:r" in txt
    assert "/B.outputs:g" in txt
    assert "/C.outputs:b" in txt


def test_get_attribute_connections(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_attribute_connection("inputs:src",
                                ["/A.outputs:r", "/B.outputs:g"])
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2 is not None
    targets = p2.get_attribute_connections("inputs:src")
    assert targets is not None
    assert "/A.outputs:r" in targets
    assert "/B.outputs:g" in targets


def test_connection_with_dtype_hint(tmp_path):
    """Connections can carry a dtype hint that types the underlying
    attribute even with no authored value."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_attribute_connection("inputs:scale",
                                "/Other.outputs:result",
                                dtype="float")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "inputs:scale.connect" in txt or "float inputs:scale" in txt


def test_connection_on_nonexistent_attribute_returns_none():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)
    assert p.get_attribute_connections("does:not:exist") is None


def test_connection_target_with_namespaced_attr_name(tmp_path):
    """Connection target paths often include namespaced attribute
    names like `inputs:rgb` or `outputs:surface`. Requires a dtype
    hint when there is no authored value, otherwise the USDA writer
    has nothing to emit a typed declaration with."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_attribute_connection(
        "outputs:surface",
        "/World/Mat/PreviewSurface.outputs:surface",
        dtype="token")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "/World/Mat/PreviewSurface.outputs:surface" in txt
