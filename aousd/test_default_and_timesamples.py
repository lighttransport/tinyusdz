#!/usr/bin/env python3
"""
Test script to demonstrate how OpenUSD evaluates attributes when both
default values and timeSamples are authored.

This shows the distinction between static/default values and animated values.
"""

from pxr import Usd, UsdGeom, Gf, Sdf
import os
import sys


def create_test_stages():
    """Create test USD stages with different combinations of default and time samples."""

    print("Creating test stages with default values and time samples...")
    print("=" * 60)

    # Case 1: Only default value (no time samples)
    stage1_path = "test_default_only.usda"
    stage1 = Usd.Stage.CreateNew(stage1_path)
    stage1.SetFramesPerSecond(24.0)
    stage1.SetStartTimeCode(-10.0)
    stage1.SetEndTimeCode(10.0)

    xform1 = UsdGeom.Xform.Define(stage1, "/DefaultOnly")
    scale_op1 = xform1.AddScaleOp()
    # Set only default value (no time samples)
    scale_op1.Set(Gf.Vec3f(7.0, 8.0, 9.0))  # This sets the default value

    stage1.GetRootLayer().Save()
    print(f"Created: {stage1_path}")

    # Case 2: Both default value and time samples
    stage2_path = "test_default_and_timesamples.usda"
    stage2 = Usd.Stage.CreateNew(stage2_path)
    stage2.SetFramesPerSecond(24.0)
    stage2.SetStartTimeCode(-10.0)
    stage2.SetEndTimeCode(10.0)

    xform2 = UsdGeom.Xform.Define(stage2, "/DefaultAndTimeSamples")
    scale_op2 = xform2.AddScaleOp()

    # Set default value first
    scale_op2.Set(Gf.Vec3f(7.0, 8.0, 9.0))  # Default value

    # Then add time samples
    scale_op2.Set(Gf.Vec3f(0.1, 0.2, 0.3), 0.0)  # Time sample at t=0

    stage2.GetRootLayer().Save()
    print(f"Created: {stage2_path}")

    # Case 3: Default value with multiple time samples
    stage3_path = "test_default_and_multi_timesamples.usda"
    stage3 = Usd.Stage.CreateNew(stage3_path)
    stage3.SetFramesPerSecond(24.0)
    stage3.SetStartTimeCode(-10.0)
    stage3.SetEndTimeCode(10.0)

    xform3 = UsdGeom.Xform.Define(stage3, "/DefaultAndMultiTimeSamples")
    scale_op3 = xform3.AddScaleOp()

    # Set default value
    scale_op3.Set(Gf.Vec3f(7.0, 8.0, 9.0))  # Default value

    # Add multiple time samples
    scale_op3.Set(Gf.Vec3f(0.1, 0.1, 0.1), -5.0)
    scale_op3.Set(Gf.Vec3f(0.5, 0.5, 0.5), 0.0)
    scale_op3.Set(Gf.Vec3f(1.0, 1.0, 1.0), 5.0)

    stage3.GetRootLayer().Save()
    print(f"Created: {stage3_path}")

    return [stage1_path, stage2_path, stage3_path]


def evaluate_stage(stage_path, description):
    """Evaluate a stage at different time codes and show the results."""
    print(f"\n{description}")
    print("=" * 60)

    # Open the stage
    stage = Usd.Stage.Open(stage_path)

    # Get the xform prim
    prim_paths = [p.GetPath() for p in stage.Traverse()]
    if not prim_paths:
        print("ERROR: No prims found in stage")
        return

    xform_prim = stage.GetPrimAtPath(prim_paths[0])
    xform = UsdGeom.Xform(xform_prim)

    # Get the scale operation
    xform_ops = xform.GetOrderedXformOps()
    scale_op = None
    for op in xform_ops:
        if op.GetOpType() == UsdGeom.XformOp.TypeScale:
            scale_op = op
            break

    if not scale_op:
        print("ERROR: Could not find scale operation")
        return

    # Get the scale attribute
    scale_attr = scale_op.GetAttr()

    # Show raw authored values
    print("Authored values in the file:")

    # Check for default value
    if scale_attr.HasAuthoredValue():
        default_val = scale_attr.Get()  # Get without time code gets default
        print(f"  Default value: {default_val}")
    else:
        print("  Default value: None")

    # Show time samples
    time_samples = scale_attr.GetTimeSamples()
    if time_samples:
        print(f"  Time samples: {time_samples}")
        for t in time_samples:
            val = scale_attr.Get(t)
            print(f"    Time {t}: {val}")
    else:
        print("  Time samples: None")

    # Test evaluations
    print("\nEvaluation at different time codes:")
    print("-" * 40)

    test_times = [
        ("Time -10", -10.0),
        ("Time -5", -5.0),
        ("Time 0", 0.0),
        ("Time 5", 5.0),
        ("Time 10", 10.0),
        ("Default (Usd.TimeCode.Default())", Usd.TimeCode.Default())
    ]

    for desc, time_code in test_times:
        val = scale_op.Get(time_code)

        if isinstance(time_code, Usd.TimeCode):
            tc_str = "Default"
        else:
            tc_str = str(time_code)

        print(f"  {desc:35s}: {val}")

        # Add explanation for key cases
        if isinstance(time_code, Usd.TimeCode):
            print(f"    → Returns the default/static value")
        elif time_samples:
            if time_code < min(time_samples):
                print(f"    → Before first sample, holds first sample value")
            elif time_code > max(time_samples):
                print(f"    → After last sample, holds last sample value")
            elif time_code in time_samples:
                print(f"    → Exactly at a time sample")
            else:
                print(f"    → Between samples, interpolated")


def show_usda_content(file_path):
    """Display the content of a USDA file."""
    print(f"\nContent of {file_path}:")
    print("-" * 40)
    with open(file_path, 'r') as f:
        print(f.read())


def main():
    """Main function."""
    print("OpenUSD Default Value vs TimeSample Evaluation Test")
    print("=" * 60)

    # Change to aousd directory
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    # Create test stages
    stage_paths = create_test_stages()

    # Evaluate each stage
    evaluate_stage(stage_paths[0], "Case 1: Default value only (no time samples)")
    evaluate_stage(stage_paths[1], "Case 2: Both default value (7,8,9) and time sample at t=0 (0.1,0.2,0.3)")
    evaluate_stage(stage_paths[2], "Case 3: Default value (7,8,9) with multiple time samples")

    # Show the USDA files for reference
    print("\n" + "=" * 60)
    print("Generated USDA Files:")
    print("=" * 60)
    for path in stage_paths:
        show_usda_content(path)

    # Summary
    print("\n" + "=" * 60)
    print("KEY INSIGHTS:")
    print("=" * 60)
    print("1. Default value is returned when using Usd.TimeCode.Default()")
    print("2. When time samples exist, numeric time codes use the samples")
    print("3. Default and time samples can coexist:")
    print("   - Default value: Used for Usd.TimeCode.Default()")
    print("   - Time samples: Used for numeric time codes")
    print("4. This allows switching between static and animated values")
    print("=" * 60)

    print("\nTest complete!")


if __name__ == "__main__":
    main()