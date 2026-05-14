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


# ---------------------------------------------------------------------------
# Authoring fixtures: programmatically build canonical Stages from the
# Python authoring API. Each factory returns a fresh Stage on each call
# so tests can save/reload without sharing state.
# ---------------------------------------------------------------------------

import tinyusdz


def _make_xform_only() -> "tinyusdz.Stage":
    s = tinyusdz.Stage()
    x = tinyusdz.Prim("Xform", name="World")
    s.add_root_prim(x)
    return s


def _make_xform_mesh() -> "tinyusdz.Stage":
    s = tinyusdz.Stage()
    world = tinyusdz.Prim("Xform", name="World")
    mesh = tinyusdz.Prim("Mesh", name="Mesh")
    mesh.set_attribute("faceVertexCounts", [3])
    mesh.set_attribute("faceVertexIndices", [0, 1, 2])
    mesh.set_attribute(
        "points",
        [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
        dtype="point3f[]",
    )
    world.add_child(mesh)
    s.add_root_prim(world)
    return s


def _make_sphere() -> "tinyusdz.Stage":
    # Note: GeomSphere.radius is a `double` per USD schema. The C API
    # currently only exposes float value constructors, so we can't author
    # `radius` from Python yet — leave it unauthored.
    s = tinyusdz.Stage()
    sph = tinyusdz.Prim("Sphere", name="Ball")
    s.add_root_prim(sph)
    return s


def _make_cube() -> "tinyusdz.Stage":
    s = tinyusdz.Stage()
    c = tinyusdz.Prim("Cube", name="Box")
    s.add_root_prim(c)
    return s


def _make_camera() -> "tinyusdz.Stage":
    s = tinyusdz.Stage()
    cam = tinyusdz.Prim("Camera", name="MainCam")
    cam.set_attribute("focalLength", 50.0)
    cam.set_attribute("horizontalAperture", 24.0)
    s.add_root_prim(cam)
    return s


def _make_multi_root() -> "tinyusdz.Stage":
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s.add_root_prim(tinyusdz.Prim("Xform", name="B"))
    s.add_root_prim(tinyusdz.Prim("Scope", name="C"))
    return s


def _make_nested() -> "tinyusdz.Stage":
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="Root")
    a = tinyusdz.Prim("Xform", name="A")
    b = tinyusdz.Prim("Xform", name="B")
    c = tinyusdz.Prim("Sphere", name="Tip")
    b.add_child(c)
    a.add_child(b)
    root.add_child(a)
    s.add_root_prim(root)
    return s


AUTHORED_STAGES = {
    "xform_only": (_make_xform_only, ["/World"]),
    "xform_mesh": (_make_xform_mesh, ["/World", "/World/Mesh"]),
    "sphere": (_make_sphere, ["/Ball"]),
    "cube": (_make_cube, ["/Box"]),
    "camera": (_make_camera, ["/MainCam"]),
    "multi_root": (_make_multi_root, ["/A", "/B", "/C"]),
    "nested": (_make_nested,
               ["/Root", "/Root/A", "/Root/A/B", "/Root/A/B/Tip"]),
}


@pytest.fixture
def make_authored_stage():
    """Return a callable: name -> (Stage, list_of_expected_prim_paths)."""
    def _builder(name: str):
        factory, paths = AUTHORED_STAGES[name]
        return factory(), list(paths)
    return _builder


def assert_paths_present(stage: "tinyusdz.Stage", paths) -> None:
    for p in paths:
        prim = stage.get_prim_at_path(p)
        assert prim is not None, f"prim {p!r} missing from stage"
