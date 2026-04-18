"""Stage-level tests."""
from __future__ import annotations

import pathlib

import tinyusdz


def test_get_prim_at_path(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    mesh = stage.get_prim_at_path("/World/Mesh")
    assert mesh is not None
    assert mesh.type_name == "Mesh"
    assert mesh.name == "Mesh"


def test_get_prim_at_path_missing(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    assert stage.get_prim_at_path("/Nothing/Here") is None


def test_root_prims_iteration(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    roots = stage.root_prims()
    assert len(roots) == 1
    world = roots[0]
    # Children reachable
    kids = world.children()
    assert any(k.name == "Mesh" for k in kids)


def test_traverse_yields_all_prims(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    names = [p.name for p in tinyusdz.traverse(stage)]
    # Both World and Mesh should appear.
    assert "World" in names
    assert "Mesh" in names


def test_export_to_string_roundtrip(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    s = stage.export_to_string()
    assert isinstance(s, str)
    assert "Mesh" in s
    assert "World" in s


def test_stage_save_usda_roundtrip(tiny_usda: pathlib.Path, tmp_path: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    out = tmp_path / "out.usda"
    stage.save(str(out))
    assert out.exists() and out.stat().st_size > 0
    # Reload
    reloaded = tinyusdz.load(str(out))
    assert reloaded.get_prim_at_path("/World/Mesh") is not None
