"""Tests for prim-level metadata authoring + roundtrip:
kind, active, hidden, displayName, comment, doc.
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc"]


def test_prim_metadata_in_memory():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("kind", "component")
    p.set_metadata("displayName", "World Xform")
    p.set_metadata("active", False)
    p.set_metadata("hidden", True)
    p.set_metadata("comment", "owned by physics")
    p.set_metadata("doc", "root-level transform")

    assert p.get_metadata("kind") == "component"
    assert p.get_metadata("displayName") == "World Xform"
    assert p.get_metadata("active") is False
    assert p.get_metadata("hidden") is True


@pytest.mark.parametrize("fmt", FORMATS)
def test_prim_metadata_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("kind", "component")
    p.set_metadata("active", False)
    p.set_metadata("hidden", True)
    s.add_root_prim(p)

    out = tmp_path / f"m.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2 is not None
    assert p2.get_metadata("kind") == "component"
    assert p2.get_metadata("active") is False
    assert p2.get_metadata("hidden") is True


@pytest.mark.parametrize("fmt", FORMATS)
def test_displayName_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("displayName", "Display Name")
    s.add_root_prim(p)

    out = tmp_path / f"d.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2.get_metadata("displayName") == "Display Name"


def test_kind_appears_in_usda(tmp_path: pathlib.Path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="World")
    p.set_metadata("kind", "assembly")
    s.add_root_prim(p)
    out = tmp_path / "k.usda"
    s.save(str(out), format="usda")
    text = out.read_text()
    assert "kind = \"assembly\"" in text


def test_active_hidden_appear_in_usda(tmp_path: pathlib.Path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="W")
    p.set_metadata("active", False)
    p.set_metadata("hidden", True)
    s.add_root_prim(p)
    out = tmp_path / "a.usda"
    s.save(str(out), format="usda")
    text = out.read_text()
    assert "active = false" in text
    assert "hidden = true" in text


def test_invalid_metadata_value_type():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(TypeError):
        p.set_metadata("kind", 42)


def test_unknown_metadata_returns_none():
    p = tinyusdz.Prim("Xform", name="X")
    assert p.get_metadata("nope") is None


def test_specifier_in_memory():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("specifier", "class")
    assert p.get_metadata("specifier") == "class"
    p.set_metadata("specifier", "over")
    assert p.get_metadata("specifier") == "over"
    p.set_metadata("specifier", "def")
    assert p.get_metadata("specifier") == "def"


@pytest.mark.parametrize("spec", ["def", "over", "class"])
@pytest.mark.parametrize("fmt", FORMATS)
def test_specifier_roundtrip(tmp_path: pathlib.Path, fmt: str, spec: str):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("specifier", spec)
    s.add_root_prim(p)
    out = tmp_path / f"s.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2 is not None
    assert p2.get_metadata("specifier") == spec


def test_specifier_invalid_value():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(ValueError):
        p.set_metadata("specifier", "bogus")
