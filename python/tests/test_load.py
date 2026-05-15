"""Basic load / parse tests."""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


def test_module_symbols_present():
    assert hasattr(tinyusdz, "Stage")
    assert hasattr(tinyusdz, "Prim")
    assert hasattr(tinyusdz, "Attribute")
    assert hasattr(tinyusdz, "Value")
    assert hasattr(tinyusdz, "load")
    assert issubclass(tinyusdz.UsdParseError, tinyusdz.UsdError)
    assert issubclass(tinyusdz.UsdIoError, tinyusdz.UsdError)


def test_load_usda_smoke(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    assert isinstance(stage, tinyusdz.Stage)
    roots = stage.root_prims()
    assert len(roots) == 1
    assert roots[0].type_name == "Xform"
    assert roots[0].name == "World"


def test_is_usd_and_detect_format(tiny_usda: pathlib.Path):
    assert tinyusdz.is_usd(str(tiny_usda))
    assert tinyusdz.detect_format(str(tiny_usda)) == "usda"


def test_load_missing_raises(tmp_path: pathlib.Path):
    missing = tmp_path / "does_not_exist.usda"
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load(str(missing))


def test_load_explicit_format(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda), format="usda")
    assert isinstance(stage, tinyusdz.Stage)
