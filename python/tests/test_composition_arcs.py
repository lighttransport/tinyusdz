"""Composition arc authoring (references / payload / inherits / specializes).

Phase C.1: Python API exposed via Prim.add_reference / add_payload /
add_inherit / add_specialize plus their clear_* counterparts.
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc"]


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
def test_add_reference_external(tmp_path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./other.usda", "/Foo", offset=10.0, scale=2.0,
                    qualifier="prepend")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "references" in txt
    assert "./other.usda" in txt
    assert "/Foo" in txt
    assert "offset = 10" in txt
    assert "scale = 2" in txt


@pytest.mark.parametrize("fmt", FORMATS)
def test_add_reference_internal(tmp_path, fmt):
    s = tinyusdz.Stage()
    base = tinyusdz.Prim("Xform", name="Base")
    s.add_root_prim(base)
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("", "/Base")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "references = </Base>" in txt or "references = </Base" in txt


@pytest.mark.parametrize("fmt", FORMATS)
def test_add_payload(tmp_path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_payload("./pay.usda", "/Root")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "payload" in txt
    assert "./pay.usda" in txt


@pytest.mark.parametrize("fmt", FORMATS)
def test_add_inherit_specialize(tmp_path, fmt):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_inherit("/Class/Base")
    p.add_specialize("/Specs/Mid")
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "inherits = </Class/Base>" in txt
    assert "specializes = </Specs/Mid>" in txt


def test_qualifier_variants():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./a.usda", qualifier="prepend")
    p.add_reference("./b.usda", qualifier="append")
    p.add_reference("./c.usda", qualifier="add")
    p.add_reference("./d.usda", qualifier="delete")
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "prepend references" in txt
    assert "append references" in txt
    assert "add references" in txt
    assert "delete references" in txt


def test_invalid_qualifier_raises():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(ValueError):
        p.add_reference("./a.usda", qualifier="bogus")


def test_clear_arcs():
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./a.usda")
    p.add_payload("./b.usda")
    p.add_inherit("/A")
    p.add_specialize("/B")
    p.clear_references()
    p.clear_payload()
    p.clear_inherits()
    p.clear_specializes()
    s = tinyusdz.Stage()
    s.add_root_prim(p)
    txt = s.export_to_string()
    assert "references" not in txt
    assert "payload" not in txt
    assert "inherits" not in txt
    assert "specializes" not in txt
