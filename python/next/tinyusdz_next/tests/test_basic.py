"""Basic tests for the tinyusdz_next Python bindings."""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from tinyusdz_next import load, Stage, Prim, UsdError

passed = 0
failed = 0

def test(name, fn):
    global passed, failed
    try:
        fn()
        passed += 1
        print(f"  {name} ... PASS")
    except Exception as e:
        failed += 1
        print(f"  {name} ... FAIL: {e}")

def test_stage_create():
    s = Stage()
    assert s is not None
    assert s.default_prim is None

def test_stage_load():
    s = load("/tmp/test_roundtrip_schema.usdc")
    assert s is not None
    assert s.default_prim == "World"

def test_get_prim():
    s = load("/tmp/test_roundtrip_schema.usdc")
    world = s.get_prim_at_path("/World")
    assert world is not None
    assert world.name == "World"
    assert world.type_name == "Xform"
    assert world.path == "/World"

def test_get_nonexistent_prim():
    s = load("/tmp/test_roundtrip_schema.usdc")
    p = s.get_prim_at_path("/Nonexistent")
    assert p is None

def test_property_access():
    s = load("/tmp/test_roundtrip_schema.usdc")
    cam = s.get_prim_at_path("/Camera1")
    assert cam is not None
    fl = cam.get_property("focalLength")
    assert fl == 50.0

def test_property_names():
    s = load("/tmp/test_roundtrip_schema.usdc")
    cube = s.get_prim_at_path("/Cube")
    assert cube is not None
    names = cube.get_property_names()
    assert names is not None
    assert len(names) > 0
    assert "points" in names

def test_get_properties():
    s = load("/tmp/test_roundtrip_schema.usdc")
    mat = s.get_prim_at_path("/Material1")
    assert mat is not None
    props = mat.get_properties()
    assert props is not None
    assert "inputs:metallic" in props

def test_traverse():
    s = load("/tmp/test_roundtrip_schema.usdc")
    prims = s.traverse()
    assert len(prims) >= 10  # We have 12 prims

def test_has_property():
    s = load("/tmp/test_roundtrip_schema.usdc")
    cube = s.get_prim_at_path("/Cube")
    assert cube.has_property("points")
    assert not cube.has_property("nonexistent")

def test_get_children():
    s = load("/tmp/test_roundtrip_schema.usdc")
    world = s.get_prim_at_path("/World")
    children = world.get_children()
    assert children is not None

def test_has_relationship():
    s = load("/tmp/test_roundtrip_schema.usdc")
    shader = s.get_prim_at_path("/Shader1")
    assert shader is not None
    # May or may not have relationships
    assert isinstance(shader.has_relationship("material:binding"), bool)

def test_has_time_samples():
    s = load("/tmp/test_roundtrip_schema.usdc")
    cube = s.get_prim_at_path("/Cube")
    assert cube is not None
    assert isinstance(cube.has_time_samples("points"), bool)

def test_module_load():
    s = load("/tmp/test_roundtrip_schema.usdc")
    assert s is not None

def test_repr():
    s = load("/tmp/test_roundtrip_schema.usdc")
    r = repr(s)
    assert "Stage" in r
    assert "World" in r

def test_prim_repr():
    s = load("/tmp/test_roundtrip_schema.usdc")
    world = s.get_prim_at_path("/World")
    r = repr(world)
    assert "Prim" in r
    assert "World" in r

if __name__ == "__main__":
    print("TinyUSDZ Next Python Tests")
    print("=========================\n")

    tests = [
        ("Stage create", test_stage_create),
        ("Stage load", test_stage_load),
        ("Get prim at path", test_get_prim),
        ("Get nonexistent prim", test_get_nonexistent_prim),
        ("Property access", test_property_access),
        ("Property names", test_property_names),
        ("Get properties dict", test_get_properties),
        ("Traverse", test_traverse),
        ("Has property", test_has_property),
        ("Get children", test_get_children),
        ("Has relationship", test_has_relationship),
        ("Has time samples", test_has_time_samples),
        ("Module load", test_module_load),
        ("Stage repr", test_repr),
        ("Prim repr", test_prim_repr),
    ]

    for name, fn in tests:
        test(name, fn)

    print(f"\n{passed}/{passed + failed} tests passed")
    sys.exit(0 if failed == 0 else 1)
