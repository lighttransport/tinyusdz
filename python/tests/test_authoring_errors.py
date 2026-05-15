"""Negative-path tests for the authoring API."""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


def test_save_to_invalid_dir_raises(tmp_path: pathlib.Path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    bad = tmp_path / "no" / "such" / "dir" / "out.usda"
    with pytest.raises(tinyusdz.UsdIoError):
        s.save(str(bad), format="usda")


def test_save_unknown_format_raises():
    s = tinyusdz.Stage()
    with pytest.raises(ValueError):
        s.save("/tmp/x.usda", format="bogus")


def test_loads_invalid_text_raises():
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.loads("this is not usd content")


def test_set_attribute_unsupported_value_raises():
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises(TypeError):
        p.set_attribute("attr", object())


def test_add_root_prim_wrong_type_raises():
    s = tinyusdz.Stage()
    with pytest.raises(TypeError):
        s.add_root_prim("not a prim")  # type: ignore[arg-type]


def test_invalid_prim_type_identifier_raises():
    with pytest.raises(ValueError):
        tinyusdz.Prim("Bad Name!")
