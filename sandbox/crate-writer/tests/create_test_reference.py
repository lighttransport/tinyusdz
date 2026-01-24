#!/usr/bin/env python3
"""
Create Reference USDC File using OpenUSD

This script creates a simple USDC file using OpenUSD's Python API.
This serves as a reference for what crate-writer should produce.

Usage:
    source ../../aousd/setup_env_monolithic.sh
    python3 create_test_reference.py
"""

import sys

try:
    from pxr import Usd, UsdGeom, Sdf, Tf
except ImportError:
    print("ERROR: OpenUSD Python bindings not found!")
    print("Please run: source ../../aousd/setup_env_monolithic.sh")
    sys.exit(1)

def create_simple_scene(filename="openusd_reference.usdc"):
    """Create a simple USD scene with geometry"""
    print(f"Creating reference USDC file: {filename}")
    print(f"USD Version: {'.'.join(map(str, Usd.GetVersion()))}")
    print()

    # Create stage
    stage = Usd.Stage.CreateNew(filename)

    # Set metadata
    stage.SetMetadata('comment', 'Reference file created by OpenUSD')
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)

    # Create root prim
    print("Creating /World prim...")
    world = UsdGeom.Xform.Define(stage, "/World")
    world.AddTranslateOp().Set((0, 0, 0))

    # Create geometry prim
    print("Creating /World/Geom prim...")
    geom = UsdGeom.Xform.Define(stage, "/World/Geom")
    geom.AddTranslateOp().Set((1.0, 2.0, 3.0))

    # Create a sphere
    print("Creating /World/Geom/Sphere...")
    sphere = UsdGeom.Sphere.Define(stage, "/World/Geom/Sphere")
    sphere.GetRadiusAttr().Set(1.5)

    # Add some attributes
    print("Creating attributes...")
    color_attr = sphere.GetPrim().CreateAttribute("primvars:displayColor", Sdf.ValueTypeNames.Color3fArray)
    color_attr.Set([(0.8, 0.2, 0.2)])

    # Create a cube
    print("Creating /World/Geom/Cube...")
    cube = UsdGeom.Cube.Define(stage, "/World/Geom/Cube")
    cube.GetSizeAttr().Set(2.0)
    cube.AddTranslateOp().Set((5.0, 0.0, 0.0))

    # Save the stage
    print(f"\nSaving to {filename}...")
    stage.GetRootLayer().Save()

    print(f"✓ File created successfully!")
    print(f"\nFile statistics:")
    print(f"  Total prims: {len(list(stage.Traverse()))}")
    print(f"  File size: {len(open(filename, 'rb').read())} bytes")

    return filename


def create_complex_scene(filename="openusd_complex.usdc"):
    """Create a more complex scene with various USD features"""
    print(f"\nCreating complex USDC file: {filename}")
    print()

    stage = Usd.Stage.CreateNew(filename)

    # Metadata
    stage.SetMetadata('comment', 'Complex reference file with various USD features')
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    stage.SetStartTimeCode(1.0)
    stage.SetEndTimeCode(10.0)
    stage.SetTimeCodesPerSecond(24.0)
    stage.SetFramesPerSecond(24.0)

    # Root
    world = UsdGeom.Xform.Define(stage, "/World")

    # Create geometry with attributes
    print("Creating geometry with various attributes...")
    mesh = UsdGeom.Mesh.Define(stage, "/World/Mesh")

    # Mesh topology
    mesh.GetPointsAttr().Set([
        (-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)
    ])
    mesh.GetFaceVertexCountsAttr().Set([4])
    mesh.GetFaceVertexIndicesAttr().Set([0, 1, 2, 3])

    # Normals
    mesh.GetNormalsAttr().Set([
        (0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1)
    ])

    # UVs
    uvs = mesh.GetPrim().CreateAttribute("primvars:st", Sdf.ValueTypeNames.TexCoord2fArray)
    uvs.Set([(0, 0), (1, 0), (1, 1), (0, 1)])

    # Animated transform
    print("Creating animated transform...")
    anim_xform = UsdGeom.Xform.Define(stage, "/World/AnimatedSphere")
    translate_op = anim_xform.AddTranslateOp()

    for frame in range(1, 11):
        x = float(frame - 1)
        translate_op.Set((x, 0, 0), time=frame)

    # Sphere under animated transform
    anim_sphere = UsdGeom.Sphere.Define(stage, "/World/AnimatedSphere/Sphere")
    anim_sphere.GetRadiusAttr().Set(0.5)

    # Save
    print(f"Saving to {filename}...")
    stage.GetRootLayer().Save()

    print(f"✓ Complex file created successfully!")
    print(f"\nFile statistics:")
    print(f"  Total prims: {len(list(stage.Traverse()))}")
    print(f"  File size: {len(open(filename, 'rb').read())} bytes")

    return filename


def main():
    print("="*70)
    print("OpenUSD Reference File Creator")
    print("="*70)

    # Create simple reference
    simple_file = create_simple_scene()

    # Create complex reference
    complex_file = create_complex_scene()

    print("\n" + "="*70)
    print("Summary")
    print("="*70)
    print(f"Created:")
    print(f"  1. {simple_file} (simple scene)")
    print(f"  2. {complex_file} (complex scene with animation)")
    print()
    print(f"To validate these files:")
    print(f"  python3 test_openusd_validation.py {simple_file}")
    print(f"  python3 test_openusd_validation.py {complex_file}")
    print("="*70)


if __name__ == '__main__':
    main()
