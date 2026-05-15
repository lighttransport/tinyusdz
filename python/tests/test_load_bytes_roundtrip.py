"""`load_bytes` and `loads` in-memory roundtrip coverage.

The file-based `load(path)` is well-tested elsewhere; here we focus on
the in-memory entry points that take raw bytes / strings.
"""
import pytest

import tinyusdz


def test_load_bytes_usdc_roundtrip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 42)
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    raw = out.read_bytes()
    s2 = tinyusdz.load_bytes(raw)
    txt = s2.export_to_string()
    assert "int a = 42" in txt
    assert 'def Xform "X"' in txt


def test_load_bytes_usda_roundtrip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("greeting", "hello")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    raw = out.read_bytes()
    s2 = tinyusdz.load_bytes(raw)
    txt = s2.export_to_string()
    assert 'string greeting = "hello"' in txt


def test_loads_from_string():
    src = '''#usda 1.0
def Xform "X" {
    custom int n = 7
}
'''
    s = tinyusdz.loads(src)
    txt = s.export_to_string()
    assert "int n = 7" in txt


def test_load_bytes_empty_raises():
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load_bytes(b"")


def test_load_bytes_garbage_raises():
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load_bytes(b"this is not usd")


def test_load_bytes_format_hint(tmp_path):
    """Explicit format hint should be respected."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 1)
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    raw = out.read_bytes()
    s2 = tinyusdz.load_bytes(raw, format="usdc")
    assert s2.get_prim_at_path("/X") is not None


def test_load_bytes_then_resave_yields_same_content(tmp_path):
    src = '''#usda 1.0
def Sphere "ball" {
    double radius = 2.5
}
'''
    s = tinyusdz.loads(src)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load_bytes(out.read_bytes())
    txt = s2.export_to_string()
    assert "double radius = 2.5" in txt
