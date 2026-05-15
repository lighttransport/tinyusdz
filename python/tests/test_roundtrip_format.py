"""Save authored stages to USDA / USDC / USDZ, reload, and verify the
prim graph survives.

USDC and USDZ readers may not preserve the schema-type-name on every
prim (see the empty `type_name` returned for prims read from binary
crates). We therefore assert on path presence and element_name, which
are stable across all three formats.
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz
from conftest import AUTHORED_STAGES, assert_paths_present


FORMATS = ["usda", "usdc", "usdz"]
STAGE_NAMES = list(AUTHORED_STAGES.keys())

# USD file magic-byte prefixes by format.
MAGIC = {
    "usda": b"#usda",
    "usdc": b"PXR-USDC",
    "usdz": b"PK\x03\x04",
}


@pytest.mark.parametrize("stage_name", STAGE_NAMES)
@pytest.mark.parametrize("fmt", FORMATS)
def test_roundtrip_paths(make_authored_stage, tmp_path: pathlib.Path,
                         stage_name: str, fmt: str):
    stage, expected_paths = make_authored_stage(stage_name)

    out = tmp_path / f"out.{fmt}"
    stage.save(str(out), format=fmt)
    assert out.exists() and out.stat().st_size > 0

    # File header / magic check.
    with open(out, "rb") as f:
        head = f.read(8)
    assert head.startswith(MAGIC[fmt]), (
        f"{fmt} file does not start with expected magic {MAGIC[fmt]!r}, "
        f"got {head!r}")

    # detect_format agrees.
    assert tinyusdz.detect_format(str(out)) == fmt

    # Reload and verify expected prim paths.
    reloaded = tinyusdz.load(str(out))
    assert_paths_present(reloaded, expected_paths)


@pytest.mark.parametrize("fmt", FORMATS)
def test_save_format_auto_detected_from_extension(
        make_authored_stage, tmp_path: pathlib.Path, fmt: str):
    """save(path) with no `format=` argument infers from the extension."""
    stage, _ = make_authored_stage("xform_only")
    out = tmp_path / f"auto.{fmt}"
    stage.save(str(out))  # no format arg
    with open(out, "rb") as f:
        head = f.read(8)
    assert head.startswith(MAGIC[fmt])


def test_save_unknown_format_raises(tmp_path: pathlib.Path,
                                    make_authored_stage):
    stage, _ = make_authored_stage("xform_only")
    with pytest.raises(ValueError):
        stage.save(str(tmp_path / "x.usda"), format="bogus")


def test_save_usda_attribute_values_survive(tmp_path: pathlib.Path,
                                            make_authored_stage):
    """USDA path is the most lossless — verify attribute values survive."""
    stage, _ = make_authored_stage("xform_mesh")
    out = tmp_path / "mesh.usda"
    stage.save(str(out), format="usda")

    s2 = tinyusdz.load(str(out))
    mesh = s2.get_prim_at_path("/World/Mesh")
    assert mesh is not None

    counts = mesh.get_attribute("faceVertexCounts")
    assert counts is not None
    assert list(memoryview(counts.value)) == [3]

    indices = mesh.get_attribute("faceVertexIndices")
    assert indices is not None
    assert list(memoryview(indices.value)) == [0, 1, 2]

    pts = mesh.get_attribute("points")
    assert pts is not None
    np = pytest.importorskip("numpy")
    arr = np.asarray(pts.value)
    assert arr.shape == (3, 3)
    assert arr.tolist() == [[0, 0, 0], [1, 0, 0], [0, 1, 0]]
