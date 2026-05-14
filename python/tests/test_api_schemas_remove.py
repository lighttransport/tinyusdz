"""apply_api_schema and api_schemas() Python API."""
import tinyusdz


def test_apply_single_schema():
    p = tinyusdz.Prim("Xform", name="X")
    p.apply_api_schema("MaterialBindingAPI")
    schemas = p.api_schemas()
    assert "MaterialBindingAPI" in schemas


def test_apply_multi_schemas():
    p = tinyusdz.Prim("Xform", name="X")
    p.apply_api_schema("MaterialBindingAPI")
    p.apply_api_schema("PhysicsRigidBodyAPI")
    schemas = p.api_schemas()
    assert "MaterialBindingAPI" in schemas
    assert "PhysicsRigidBodyAPI" in schemas


def test_apply_schema_repeats_appends():
    """apply_api_schema appends each call; dedup is the caller's job."""
    p = tinyusdz.Prim("Xform", name="X")
    p.apply_api_schema("MaterialBindingAPI")
    p.apply_api_schema("MaterialBindingAPI")
    schemas = p.api_schemas()
    assert schemas.count("MaterialBindingAPI") >= 1


def test_apply_with_instance_then_more():
    p = tinyusdz.Prim("Xform", name="X")
    p.apply_api_schema("CollectionAPI", instance_name="lights")
    p.apply_api_schema("CollectionAPI", instance_name="cameras")
    schemas = p.api_schemas()
    assert "CollectionAPI:lights" in schemas
    assert "CollectionAPI:cameras" in schemas


def test_api_schemas_round_trip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.apply_api_schema("MaterialBindingAPI")
    p.apply_api_schema("PhysicsCollisionAPI")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    schemas = p2.api_schemas()
    assert "MaterialBindingAPI" in schemas
    assert "PhysicsCollisionAPI" in schemas
