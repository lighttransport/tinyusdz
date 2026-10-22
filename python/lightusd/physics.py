# SPDX-License-Identifier: Apache-2.0
"""USD physics inspection and authoring, including MuJoCo and Newton metadata.

These helpers author descriptive USD data; they do not run a physics engine.
All angles authored here use USD units (degrees), lengths use stage units,
and quaternions use USD order ``(real, i, j, k)``.
"""
from __future__ import annotations
from dataclasses import dataclass
from types import MappingProxyType
from typing import Any, Mapping

__all__ = ["PhysicsRecord", "PhysicsData", "inspect", "apply_schema", "define_prim"]


@dataclass(frozen=True)
class PhysicsRecord:
    prim_path: str
    type_name: str
    api_schemas: tuple[str, ...]
    attributes: Mapping[str, Any]
    relationships: Mapping[str, tuple[str, ...]]


@dataclass(frozen=True)
class PhysicsData:
    scenes: tuple[PhysicsRecord, ...] = ()
    rigid_bodies: tuple[PhysicsRecord, ...] = ()
    colliders: tuple[PhysicsRecord, ...] = ()
    joints: tuple[PhysicsRecord, ...] = ()
    materials: tuple[PhysicsRecord, ...] = ()
    articulation_roots: tuple[PhysicsRecord, ...] = ()
    filtered_pairs: tuple[PhysicsRecord, ...] = ()
    actuators: tuple[PhysicsRecord, ...] = ()
    sites: tuple[PhysicsRecord, ...] = ()
    tendons: tuple[PhysicsRecord, ...] = ()
    equalities: tuple[PhysicsRecord, ...] = ()
    sensors: tuple[PhysicsRecord, ...] = ()
    contacts: tuple[PhysicsRecord, ...] = ()
    keyframes: tuple[PhysicsRecord, ...] = ()
    extensions: tuple[PhysicsRecord, ...] = ()


def _freeze(value):
    from . import Array
    if isinstance(value, Array):
        value = memoryview(value).tolist()
    if isinstance(value, (list, tuple)):
        return tuple(_freeze(v) for v in value)
    if isinstance(value, dict):
        return MappingProxyType({k: _freeze(v) for k, v in value.items()})
    return value


def inspect(stage, *, time: float = 0.0) -> PhysicsData:
    """Take an immutable snapshot of authored physics values and targets.

    A prim may appear in several groups (for example mass and collision APIs
    on a rigid body). Unknown engine properties remain in the snapshot.
    """
    groups = {name: [] for name in PhysicsData.__dataclass_fields__}
    for prim in stage:
        schemas = tuple(prim.metadata("apiSchemas") or ())
        attributes = {name: _freeze(prim.get(name, time=time))
                      for name in prim.attributes
                      if name.startswith(("physics:", "mjc:", "newton:", "physx", "state:", "urdf:"))}
        relationships = {name: tuple(prim.relationship(name).targets)
                         for name in prim.relationships
                         if name.startswith(("physics:", "mjc:", "newton:", "physx", "state:", "urdf:"))}
        if not attributes and not relationships and not (
                prim.type_name.startswith(("Physics", "Mjc", "Newton")) or
                any(s.startswith(("Physics", "Mjc", "Newton")) for s in schemas)):
            continue
        record = PhysicsRecord(prim.path, prim.type_name, schemas,
                               MappingProxyType(attributes), MappingProxyType(relationships))
        names = set()
        if prim.type_name == "PhysicsScene": names.add("scenes")
        if "PhysicsRigidBodyAPI" in schemas or "PhysicsMassAPI" in schemas: names.add("rigid_bodies")
        if "PhysicsCollisionAPI" in schemas or "PhysicsMeshCollisionAPI" in schemas: names.add("colliders")
        if prim.type_name.startswith("Physics") and prim.type_name.endswith("Joint"): names.add("joints")
        if "PhysicsMaterialAPI" in schemas: names.add("materials")
        if "PhysicsArticulationRootAPI" in schemas: names.add("articulation_roots")
        if "PhysicsFilteredPairsAPI" in schemas: names.add("filtered_pairs")
        for marker, group in (("Actuator", "actuators"), ("Tendon", "tendons"),
                              ("Sensor", "sensors"), ("ContactPair", "contacts"),
                              ("Keyframe", "keyframes")):
            if marker in prim.type_name: names.add(group)
        if "MjcSiteAPI" in schemas: names.add("sites")
        if any(s.startswith("MjcEquality") for s in schemas): names.add("equalities")
        if not names: names.add("extensions")
        for name in names: groups[name].append(record)
    return PhysicsData(**{k: tuple(v) for k, v in groups.items()})


# Canonical USD property types. Extension properties outside this table can
# be authored explicitly as (USD_type_name, value); no guessed array types.
_TYPES = {}

def _types(type_name, *names):
    _TYPES.update(dict.fromkeys(names, type_name))

_types("bool", "physics:rigidBodyEnabled", "physics:kinematicEnabled", "physics:startsAsleep",
       "physics:collisionEnabled", "physics:jointEnabled", "newton:gravityEnabled",
       "newton:selfCollisionEnabled", "newton:mimicEnabled", "mjc:freeJoint", "mjc:mocap")
_types("float", "physics:mass", "physics:density", "physics:gravityMagnitude",
       "physics:lowerLimit", "physics:upperLimit", "physics:staticFriction",
       "physics:dynamicFriction", "physics:restitution", "physics:coneAngle0Limit",
       "physics:coneAngle1Limit", "physics:minDistance", "physics:maxDistance")
_types("vector3f", "physics:velocity", "physics:angularVelocity", "physics:gravityDirection")
_types("point3f", "physics:centerOfMass", "physics:localPos0", "physics:localPos1")
_types("float3", "physics:diagonalInertia")
_types("quatf", "physics:principalAxes", "physics:localRot0", "physics:localRot1")
_types("token", "physics:axis", "physics:approximation", "mjc:type", "mjc:dynType",
       "mjc:gainType", "mjc:biasType", "mjc:trnType", "mjc:inertia", "mjc:objtype",
       "mjc:objname", "mjc:reftype", "mjc:refname", "mjc:plugin", "mjc:instance")
_types("int", "newton:timeStepsPerSecond", "newton:maxSolverIterations", "newton:maxHullVertices",
       "newton:delaySteps", "mjc:group", "mjc:condim", "mjc:geomContype", "mjc:geomConaffinity", "mjc:priority")
for _name in ("contactMargin", "contactGap", "torsionalFriction", "rollingFriction", "mimicCoef0",
              "mimicCoef1", "kp", "kd", "ki", "integralMax", "constEffort", "maxEffort",
              "maxMotorEffort", "saturationEffort", "velocityLimit"):
    _types("float", "newton:" + _name)
_types("float[]", "newton:lookupPositions", "newton:lookupEfforts")
_types("asset", "newton:modelPath")
_types("int[]", "mjc:path:indices", "mjc:path:segments", "mjc:sideSites:indices")
for _name in ("damping", "stiffness", "armature", "frictionloss", "margin", "gap", "solmix",
              "width", "cutoff", "noise", "torsionalfriction", "rollingfriction", "timestep"):
    _types("double", "mjc:" + _name)
for _name in ("solref", "solimp", "geomFriction", "geomSize", "gainPrm", "biasPrm", "gear",
              "lengthRange", "ctrlRange", "forceRange", "springlength", "qpos", "qvel", "act",
              "ctrl", "mpos", "mquat", "path:coef", "path:divisors"):
    _types("double[]", "mjc:" + _name)
for _name in ("timestep", "impratio", "density", "viscosity", "o_margin", "tolerance", "ls_tolerance",
              "noslip_tolerance", "ccd_tolerance"):
    _types("double", "mjc:option:" + _name)
for _name in ("iterations", "ls_iterations", "noslip_iterations", "ccd_iterations", "sdf_iterations", "sdf_initpoints"):
    _types("int", "mjc:option:" + _name)
for _name in ("integrator", "cone", "jacobian", "solver"):
    _types("token", "mjc:option:" + _name)


def _prepare(attributes, relationships):
    result = []
    for name, value in (attributes or {}).items():
        type_name = _TYPES.get(name)
        if isinstance(value, tuple) and len(value) == 2 and isinstance(value[0], str):
            type_name, value = value
        if type_name is None:
            raise ValueError(f"unknown property type for {name!r}; pass (type_name, value)")
        result.append((name, value, type_name))
    targets = {}
    for name, paths in (relationships or {}).items():
        if isinstance(paths, str): paths = (paths,)
        paths = tuple(paths)
        if not all(isinstance(p, str) and p.startswith("/") for p in paths):
            raise ValueError("physics relationship targets must be absolute prim paths")
        targets[name] = paths
    return result, targets


_SCHEMAS = frozenset(('MjcCollisionAPI', 'MjcEqualityAPI', 'MjcEqualityConnectAPI', 'MjcEqualityJointAPI', 'MjcEqualityWeldAPI', 'MjcImageableAPI', 'MjcJointAPI', 'MjcMaterialAPI', 'MjcMeshCollisionAPI', 'MjcSceneAPI', 'MjcSiteAPI', 'NewtonActuatorClampingBaseAPI', 'NewtonActuatorControlBaseAPI', 'NewtonActuatorDelayAPI', 'NewtonArticulationRootAPI', 'NewtonCollisionAPI', 'NewtonDCMotorClampingAPI', 'NewtonKaminoSceneAPI', 'NewtonMaterialAPI', 'NewtonMaxEffortClampingAPI', 'NewtonMeshCollisionAPI', 'NewtonMimicAPI', 'NewtonNeuralControlAPI', 'NewtonPDControlAPI', 'NewtonPIDControlAPI', 'NewtonPositionBasedClampingAPI', 'NewtonSceneAPI', 'NewtonXpbdSceneAPI', 'PhysicsArticulationRootAPI', 'PhysicsCollisionAPI', 'PhysicsDriveAPI', 'PhysicsFilteredPairsAPI', 'PhysicsLimitAPI', 'PhysicsMassAPI', 'PhysicsMaterialAPI', 'PhysicsMeshCollisionAPI', 'PhysicsRigidBodyAPI'))

def _check_schema(name):
    if not isinstance(name, str) or name.split(":", 1)[0] not in _SCHEMAS:
        raise ValueError(f"unknown physics API schema: {name!r}")


def apply_schema(prim, schema_name: str, *, attributes=None, relationships=None):
    """Apply a physics API schema, then author typed properties and targets.

    Property names are canonical USD names. Supply ``(type_name, value)`` for
    an extension property without a built-in type. Existing APIs are kept.
    """
    _check_schema(schema_name)
    values, targets = _prepare(attributes, relationships)
    schemas = list(prim.metadata("apiSchemas") or ())
    if schema_name not in schemas: schemas.append(schema_name)
    prim.set_metadata("apiSchemas", schemas)
    for name, value, type_name in values:
        prim.set(name, value, type=type_name, uniform=name.startswith(("newton:", "mjc:")) or name == "physics:axis")
    for name, paths in targets.items(): prim.add_relationship(name, paths)
    return prim


def define_prim(stage, path: str, type_name: str, *, api_schemas=(), attributes=None, relationships=None):
    """Define a physics prim with canonical USD property types and targets."""
    if not isinstance(type_name, str) or not type_name.startswith(("Physics", "Mjc", "Newton")):
        raise ValueError("expected a Physics, Mjc or Newton prim type")
    for schema in api_schemas: _check_schema(schema)
    values, targets = _prepare(attributes, relationships)
    prim = stage.define_prim(path, type_name)
    if api_schemas: prim.set_metadata("apiSchemas", list(api_schemas))
    for name, value, type_name in values: prim.set(name, value, type=type_name, uniform=name.startswith(("newton:", "mjc:")) or name == "physics:axis")
    for name, paths in targets.items(): prim.add_relationship(name, paths)
    return prim
