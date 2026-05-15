"""Tests for authoring USD Physics schemas from Python.

Covers PhysicsScene, PhysicsRevoluteJoint, PhysicsCollisionGroup, plus
prim metadata (kind, doc, displayName) — all round-tripped through both
USDA and USDC.

UsdPhysics joints have relationships (physics:body0/body1) and quat /
point3f / float typed-builtin attributes; the binding's typed-field
fallback in c-tinyusd-helpers.cc covers the readback path for these.

Note: relationship authoring (rel physics:body0 = </Box0>) is not yet
exposed in the Python API — those tests load existing USDA fixtures
to verify reads.
"""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


FORMATS = ["usda", "usdc"]
PHYSICS_SCHEMA_FORMATS = ["usda", "usdc"]


# ---------------------------------------------------------------------------
# PhysicsScene authoring
# ---------------------------------------------------------------------------

def _make_physics_scene_stage() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    scene = tinyusdz.Prim("PhysicsScene", name="World")
    scene.set_attribute(
        "physics:gravityDirection", (0.0, 0.0, -1.0), dtype="vector3f")
    scene.set_attribute("physics:gravityMagnitude", 9.81)
    s.add_root_prim(scene)
    return s


def test_physics_scene_in_memory_authoring():
    s = _make_physics_scene_stage()
    sc = s.get_prim_at_path("/World")
    assert sc is not None
    assert sc.type_name == "PhysicsScene"

    mag = sc.get_attribute("physics:gravityMagnitude")
    assert mag is not None
    assert pytest.approx(mag.value.as_scalar(), rel=1e-5) == 9.81

    gdir = sc.get_attribute("physics:gravityDirection")
    assert gdir is not None
    # In-memory authored attribute round-trips via the props fallback.
    assert gdir.value is not None


@pytest.mark.parametrize("fmt", PHYSICS_SCHEMA_FORMATS)
def test_physics_scene_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = _make_physics_scene_stage()
    out = tmp_path / f"scene.{fmt}"
    s.save(str(out), format=fmt)
    assert out.exists() and out.stat().st_size > 0

    reloaded = tinyusdz.load(str(out))
    sc = reloaded.get_prim_at_path("/World")
    assert sc is not None

    mag = sc.get_attribute("physics:gravityMagnitude")
    assert mag is not None and mag.value is not None
    assert pytest.approx(mag.value.as_scalar(), rel=1e-5) == 9.81

    # gravityDirection survives as a packed vector3f. The Value's
    # type_name reporting is unrelated to attribute fidelity; we
    # verify via to_string() containing the components.
    gdir = sc.get_attribute("physics:gravityDirection")
    assert gdir is not None and gdir.value is not None
    s_repr = gdir.value.to_string()
    assert "-1" in s_repr  # Z component


def test_physics_scene_usda_export_contains_physics_attrs():
    s = _make_physics_scene_stage()
    text = s.export_to_string()
    assert "PhysicsScene" in text
    assert "physics:gravityMagnitude" in text
    assert "9.81" in text
    assert "physics:gravityDirection" in text


# ---------------------------------------------------------------------------
# PhysicsRevoluteJoint authoring
# ---------------------------------------------------------------------------

def _make_revolute_joint_stage() -> tinyusdz.Stage:
    s = tinyusdz.Stage()
    world = tinyusdz.Prim("Xform", name="World")
    joint = tinyusdz.Prim("PhysicsRevoluteJoint", name="Hinge")
    # Schema-builtin typed attributes
    joint.set_attribute("physics:localPos0",
                        (0.0, -0.5, 0.0), dtype="point3f")
    joint.set_attribute("physics:localPos1",
                        (0.0,  0.25, 0.0), dtype="point3f")
    # Custom (props-resident) attributes — joint-axis specific
    joint.set_attribute("physics:lowerLimit", -90.0)
    joint.set_attribute("physics:upperLimit",  90.0)
    joint.set_attribute("physics:axis", "X", dtype="token")
    joint.set_attribute("physics:breakForce", 1000.0)
    world.add_child(joint)
    s.add_root_prim(world)
    return s


def test_revolute_joint_in_memory():
    s = _make_revolute_joint_stage()
    j = s.get_prim_at_path("/World/Hinge")
    assert j is not None
    assert j.type_name == "PhysicsRevoluteJoint"

    lo = j.get_attribute("physics:lowerLimit")
    hi = j.get_attribute("physics:upperLimit")
    assert lo is not None and hi is not None
    assert pytest.approx(lo.value.as_scalar(), rel=1e-5) == -90.0
    assert pytest.approx(hi.value.as_scalar(), rel=1e-5) ==  90.0

    axis = j.get_attribute("physics:axis")
    assert axis is not None
    assert axis.value.as_scalar() == "X"


@pytest.mark.parametrize("fmt", PHYSICS_SCHEMA_FORMATS)
def test_revolute_joint_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = _make_revolute_joint_stage()
    out = tmp_path / f"joint.{fmt}"
    s.save(str(out), format=fmt)
    reloaded = tinyusdz.load(str(out))
    j = reloaded.get_prim_at_path("/World/Hinge")
    assert j is not None

    bf = j.get_attribute("physics:breakForce")
    assert bf is not None and bf.value is not None
    assert pytest.approx(bf.value.as_scalar(), rel=1e-5) == 1000.0

    # Custom (non-schema-builtin) limits live in props and survive the
    # write/read cycle for both USDA and USDC.
    lo = j.get_attribute("physics:lowerLimit")
    assert lo is not None and lo.value is not None
    assert pytest.approx(lo.value.as_scalar(), rel=1e-5) == -90.0


# ---------------------------------------------------------------------------
# PhysicsCollisionGroup
# ---------------------------------------------------------------------------

def test_collision_group_construction():
    s = tinyusdz.Stage()
    g1 = tinyusdz.Prim("PhysicsCollisionGroup", name="GroupA")
    g2 = tinyusdz.Prim("PhysicsCollisionGroup", name="GroupB")
    s.add_root_prim(g1)
    s.add_root_prim(g2)
    text = s.export_to_string()
    assert "PhysicsCollisionGroup" in text
    assert '"GroupA"' in text
    assert '"GroupB"' in text


@pytest.mark.parametrize("fmt", FORMATS)
def test_collision_group_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("PhysicsCollisionGroup", name="GroupA"))
    s.add_root_prim(tinyusdz.Prim("PhysicsCollisionGroup", name="GroupB"))
    out = tmp_path / f"groups.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/GroupA") is not None
    assert s2.get_prim_at_path("/GroupB") is not None


# ---------------------------------------------------------------------------
# Prim metadata: kind, doc, displayName
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("fmt", FORMATS)
def test_prim_metadata_kind_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Root")
    p.set_metadata("kind", "component")
    s.add_root_prim(p)
    out = tmp_path / f"meta.{fmt}"
    s.save(str(out), format=fmt)

    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/Root")
    assert p2 is not None
    assert p2.get_metadata("kind") == "component"


@pytest.mark.parametrize("fmt", FORMATS)
def test_prim_metadata_doc_roundtrip(tmp_path: pathlib.Path, fmt: str):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Root")
    p.set_metadata("doc", "Documentation string for testing")
    s.add_root_prim(p)
    out = tmp_path / f"doc.{fmt}"
    s.save(str(out), format=fmt)
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/Root")
    assert p2 is not None
    assert "Documentation" in (p2.get_metadata("doc") or "")


def test_prim_metadata_in_memory():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_metadata("kind", "assembly")
    p.set_metadata("displayName", "My Friendly Name")
    assert p.get_metadata("kind") == "assembly"
    assert p.get_metadata("displayName") == "My Friendly Name"
    assert p.get_metadata("does_not_exist") is None


def test_physics_scene_with_metadata_roundtrip(tmp_path: pathlib.Path):
    """Combine schema-typed attributes + prim metadata in one prim."""
    s = tinyusdz.Stage()
    scene = tinyusdz.Prim("PhysicsScene", name="World")
    scene.set_attribute("physics:gravityMagnitude", 9.81)
    scene.set_metadata("kind", "component")
    scene.set_metadata("doc", "Default Earth-like gravity scene")
    s.add_root_prim(scene)

    out = tmp_path / "scene_with_meta.usda"
    s.save(str(out), format="usda")
    s2 = tinyusdz.load(str(out))
    sc = s2.get_prim_at_path("/World")
    assert sc is not None
    assert sc.get_metadata("kind") == "component"
    assert "Earth" in (sc.get_metadata("doc") or "")
    mag = sc.get_attribute("physics:gravityMagnitude")
    assert mag is not None
    assert pytest.approx(mag.value.as_scalar(), rel=1e-5) == 9.81


# ---------------------------------------------------------------------------
# Reading existing UsdPhysics fixtures from disk
# ---------------------------------------------------------------------------

def test_read_existing_physics_scene_fixture(usda_fixture_dir: pathlib.Path):
    """Load tests/usda/physics-scene-001.usda and read the gravity attrs."""
    f = usda_fixture_dir / "physics-scene-001.usda"
    if not f.exists():
        pytest.skip("physics-scene-001.usda fixture missing")
    s = tinyusdz.load(str(f))
    sc = s.get_prim_at_path("/World")
    assert sc is not None
    assert sc.type_name == "PhysicsScene"

    mag = sc.get_attribute("physics:gravityMagnitude")
    assert mag is not None and mag.value is not None
    assert pytest.approx(mag.value.as_scalar(), rel=1e-5) == 9.81

    gdir = sc.get_attribute("physics:gravityDirection")
    assert gdir is not None and gdir.value is not None
    # vector3f -> packed string repr should contain the Z component
    assert "-1" in gdir.value.to_string()


def test_read_existing_revolute_joint_fixture(usda_fixture_dir: pathlib.Path):
    f = usda_fixture_dir / "physics-revolute-joint-001.usda"
    if not f.exists():
        pytest.skip("physics-revolute-joint-001.usda fixture missing")
    s = tinyusdz.load(str(f))
    j = s.get_prim_at_path("/World/Hinge")
    assert j is not None
    assert j.type_name == "PhysicsRevoluteJoint"

    # Custom-resident attributes from the fixture: lowerLimit/upperLimit
    lo = j.get_attribute("physics:lowerLimit")
    hi = j.get_attribute("physics:upperLimit")
    assert lo is not None and hi is not None
    assert pytest.approx(lo.value.as_scalar(), rel=1e-5) == -90.0
    assert pytest.approx(hi.value.as_scalar(), rel=1e-5) ==  90.0
