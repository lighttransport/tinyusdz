"""Cross-format chained conversions: USDA -> USDC -> USDA -> USDZ -> USDA."""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz
from conftest import assert_paths_present


def test_usda_usdc_usda_usdz_usda_chain(tmp_path: pathlib.Path,
                                        make_authored_stage):
    stage, expected_paths = make_authored_stage("xform_mesh")

    a1 = tmp_path / "1.usda"
    stage.save(str(a1), format="usda")

    s = tinyusdz.load(str(a1))
    c2 = tmp_path / "2.usdc"
    s.save(str(c2), format="usdc")

    s = tinyusdz.load(str(c2))
    a3 = tmp_path / "3.usda"
    s.save(str(a3), format="usda")

    s = tinyusdz.load(str(a3))
    z4 = tmp_path / "4.usdz"
    s.save(str(z4), format="usdz")

    s = tinyusdz.load(str(z4))
    a5 = tmp_path / "5.usda"
    s.save(str(a5), format="usda")

    final = tinyusdz.load(str(a5))
    assert_paths_present(final, expected_paths)


def test_existing_usdc_fixture_to_usda_back_to_usdc(
        tmp_path: pathlib.Path, usdc_fixture_dir: pathlib.Path):
    """Use a real USDC from tests/usdc/ and round-trip it through USDA."""
    candidates = sorted(usdc_fixture_dir.glob("apischema-000.usdc"))
    if not candidates:
        pytest.skip("apischema-000.usdc fixture not present")
    src = candidates[0]

    s = tinyusdz.load(str(src))
    n_roots = len(s.root_prims())
    assert n_roots >= 0  # successful load

    via_usda = tmp_path / "via.usda"
    s.save(str(via_usda), format="usda")
    s2 = tinyusdz.load(str(via_usda))
    via_usdc = tmp_path / "back.usdc"
    s2.save(str(via_usdc), format="usdc")

    s3 = tinyusdz.load(str(via_usdc))
    # Root-prim count is a stable structural invariant.
    assert len(s3.root_prims()) == n_roots
