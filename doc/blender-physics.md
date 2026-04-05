# Blender USD Physics Export/Import Hook

`blender/usd_physics_hook.py` is a Blender addon that bridges Blender's rigid body
simulation with the USD Physics and MuJoCo mjcPhysics schemas.

## Requirements

- Blender 4.0+ (tested on 5.1)
- Blender must include bundled `pxr` Python modules (standard in official builds)

## Installation

1. Open Blender
2. Edit > Preferences > Add-ons > Install from Disk
3. Select `blender/usd_physics_hook.py`
4. Enable the addon

Or load directly from the script editor:
```python
import sys
sys.path.insert(0, "/path/to/tinyusdz/blender")
import usd_physics_hook
usd_physics_hook.register()
```

## Configuration

In Edit > Preferences > Add-ons > USD Physics Hook, set the **Physics Schema** mode:

| Mode | Description |
|---|---|
| **USD Physics Only** | Standard `UsdPhysics` schemas only (RigidBodyAPI, CollisionAPI, joints) |
| **MuJoCo Only** | MuJoCo `mjc:` custom attributes only |
| **USD + MuJoCo** (default) | Both standard and MuJoCo attributes |

## Export

The hook runs automatically during File > Export > USD (.usda/.usdc/.usdz).

### What gets exported

| Blender Feature | USD Schema | Attributes Written |
|---|---|---|
| Gravity | `PhysicsScene` | `physics:gravityDirection` (vector3f), `physics:gravityMagnitude` (float) |
| Rigid body (active) | `PhysicsRigidBodyAPI` + `PhysicsMassAPI` | `physics:rigidBodyEnabled`, `physics:mass` |
| Rigid body (passive) | `PhysicsRigidBodyAPI` | `physics:rigidBodyEnabled = false`, `physics:kinematicEnabled` |
| Collision shape | `PhysicsCollisionAPI` | Applied to mesh prims |
| Friction / restitution | `PhysicsMaterialAPI` | `physics:staticFriction`, `physics:dynamicFriction`, `physics:restitution` |
| Hinge constraint | `PhysicsRevoluteJoint` | `physics:axis`, `physics:lowerLimit`, `physics:upperLimit`, `physics:body0/body1` |
| Slider constraint | `PhysicsPrismaticJoint` | Same as hinge but linear limits |
| Fixed constraint | `PhysicsFixedJoint` | `physics:body0/body1` |
| Point constraint | `PhysicsSphericalJoint` | `physics:axis` |
| Generic spring | `PhysicsDistanceJoint` | `physics:body0/body1` |

### MuJoCo attributes (when mode includes MuJoCo)

| Blender Feature | MuJoCo Attributes |
|---|---|
| Rigid body collision | `mjc:condim`, `mjc:friction`, `mjc:solmix`, `mjc:margin` |
| Physics world | `mjc:option:timestep`, `mjc:option:iterations`, `mjc:flag:gravity` |
| Constraint spring | `mjc:damping`, `mjc:stiffness` |
| Joint group | `mjc:group` |

### Joint local positions

The hook computes `physics:localPos0` and `physics:localPos1` from the constraint
empty's world position relative to each body's origin.

## Import

The hook runs automatically during File > Import > USD.

### What gets imported

| USD Schema | Blender Feature |
|---|---|
| `PhysicsScene` | Scene gravity vector |
| `PhysicsRigidBodyAPI` | Rigid body (active/passive), mass, friction, restitution |
| `PhysicsRevoluteJoint` | Hinge constraint with angular limits |
| `PhysicsPrismaticJoint` | Slider constraint with linear limits |
| `PhysicsFixedJoint` | Fixed constraint |
| `PhysicsSphericalJoint` | Point constraint |
| `PhysicsDistanceJoint` | Generic spring constraint |
| `mjc:option:iterations` | Rigid body world solver iterations |

### Body resolution

The import hook matches USD prim names to Blender objects by:
1. Exact name match (`bpy.data.objects.get(prim_name)`)
2. Fallback: replacing `.` with `_` in object names

Joint `physics:body0`/`physics:body1` relationship targets are resolved to Blender
objects using the leaf prim name from the target path.

## Example

Export a scene with physics, then verify with TinyUSDZ:

```bash
# In Blender (with addon loaded):
# File > Export > Universal Scene Description (.usda)

# Parse with TinyUSDZ:
./build/tusdcat exported-scene.usda

# Should show PhysicsScene, joints, and physics attributes
```

See `models/blender-physics.usda` for an example exported scene.

## Limitations

- Blender soft body and cloth are not exported (USD Physics has no equivalent)
- Collision groups are not mapped (Blender uses layer-based filtering)
- Spring constraints with per-axis settings only export X-axis spring/damping
- MuJoCo actuators and tendons have no Blender equivalent — they are only
  exported/imported when the USD file was authored with those prims directly
