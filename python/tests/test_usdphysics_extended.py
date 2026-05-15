"""usdPhysics: rigid bodies, collision shapes, joints, scenes."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_physics_scene(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def PhysicsScene "World" {
    vector3f physics:gravityDirection = (0, -1, 0)
    float physics:gravityMagnitude = 9.81
}
''')
    assert "PhysicsScene" in txt


def test_rigid_body_api(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Body" (
    apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsMassAPI"]
) {
    bool physics:rigidBodyEnabled = 1
    bool physics:kinematicEnabled = 0
    float physics:mass = 5.0
}
''')
    assert "PhysicsRigidBodyAPI" in txt
    assert "PhysicsMassAPI" in txt


def test_collision_shape_cube(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Cube "Box" (
    apiSchemas = ["PhysicsCollisionAPI"]
) {
    bool physics:collisionEnabled = 1
}
''')
    assert "PhysicsCollisionAPI" in txt


def test_physics_revolute_joint_usda_only(tmp_path):
    """RevoluteJoint with body0/body1 rels — USDA fence (namespaced
    rels may drop on USDC)."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "A" {}
def Xform "B" {}
def PhysicsRevoluteJoint "Hinge" {
    rel physics:body0 = </A>
    rel physics:body1 = </B>
    uniform token physics:axis = "X"
    float physics:lowerLimit = -45
    float physics:upperLimit = 45
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "PhysicsRevoluteJoint" in txt
    assert "physics:body0" in txt


def test_physics_material_api(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "PhysMat" (
    apiSchemas = ["PhysicsMaterialAPI"]
) {
    float physics:dynamicFriction = 0.5
    float physics:staticFriction = 0.6
    float physics:restitution = 0.3
    float physics:density = 1000.0
}
''')
    assert "PhysicsMaterialAPI" in txt


def test_filtered_pairs_collection_usda_only(tmp_path):
    """PhysicsFilteredPairsAPI applied schema — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "World" (
    apiSchemas = ["PhysicsFilteredPairsAPI"]
) {
    rel physics:filteredPairs = [</A>, </B>]
    def Xform "A" {}
    def Xform "B" {}
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "PhysicsFilteredPairsAPI" in txt
