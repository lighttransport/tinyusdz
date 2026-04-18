"""Pytest shared configuration and fixtures."""
from __future__ import annotations

import pathlib

import pytest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
USDA_DIR = REPO_ROOT / "tests" / "usda"
USDC_DIR = REPO_ROOT / "tests" / "usdc"


@pytest.fixture(scope="session")
def tiny_usda(tmp_path_factory) -> pathlib.Path:
    """Write a small, known USDA fixture and return its path."""
    p = tmp_path_factory.mktemp("tinyusdz") / "mini.usda"
    p.write_text(
        """#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
""",
        encoding="utf-8",
    )
    return p


@pytest.fixture(scope="session")
def usda_fixture_dir() -> pathlib.Path:
    return USDA_DIR


@pytest.fixture(scope="session")
def usdc_fixture_dir() -> pathlib.Path:
    return USDC_DIR
