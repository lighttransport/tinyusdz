#!/usr/bin/env python3
"""
Test script to demonstrate how OpenUSD evaluates timeSamples at different time codes.

This script creates a USD stage with a transform that has scale animation defined
at time 0, then evaluates the scale at various time codes to show USD's behavior.
"""

from pxr import Usd, UsdGeom, Gf, Sdf
import os
import sys


def create_test_stage():
    """Create a USD stage with animated scale values."""
    # Create a new stage
    stage_path = "test_scale_timesamples.usda"
    stage = Usd.Stage.CreateNew(stage_path)

    # Set the stage's time codes per second (frame rate)
    stage.SetFramesPerSecond(24.0)
    stage.SetStartTimeCode(-10.0)
    stage.SetEndTimeCode(10.0)

    # Create a transform prim
    xform_prim = UsdGeom.Xform.Define(stage, "/TestXform")

    # Add scale operation
    scale_op = xform_prim.AddScaleOp()

    # Set time samples for scale
    # Only set value at time 0
    scale_op.Set(Gf.Vec3f(0.1, 0.2, 0.3), 0.0)

    # Save the stage
    stage.GetRootLayer().Save()

    print(f"Created USD stage: {stage_path}")
    print("=" * 60)

    return stage_path


def evaluate_timesamples(stage_path):
    """Load the stage and evaluate scale at different time codes."""
    # Open the stage
    stage = Usd.Stage.Open(stage_path)

    # Get the xform prim
    xform_prim = stage.GetPrimAtPath("/TestXform")
    xform = UsdGeom.Xform(xform_prim)

    # Get the scale attribute directly
    xform_ops = xform.GetOrderedXformOps()
    scale_op = None
    for op in xform_ops:
        if op.GetOpType() == UsdGeom.XformOp.TypeScale:
            scale_op = op
            break

    if not scale_op:
        print("ERROR: Could not find scale operation")
        return

    # Print the raw time samples
    scale_attr = scale_op.GetAttr()
    time_samples = scale_attr.GetTimeSamples()
    print("Raw TimeSamples defined in the file:")
    print(f"  Time samples: {time_samples}")
    for t in time_samples:
        val = scale_attr.Get(t)
        print(f"  Time {t}: {val}")
    print()

    # Test time codes to evaluate
    test_times = [
        ("Time -10 (before samples)", -10.0),
        ("Time 0 (at sample)", 0.0),
        ("Time 10 (after samples)", 10.0),
        ("Default time (Usd.TimeCode.Default())", Usd.TimeCode.Default())
    ]

    print("Evaluation Results:")
    print("=" * 60)

    for description, time_code in test_times:
        # Evaluate at specific time
        if isinstance(time_code, Usd.TimeCode):
            val = scale_op.Get(time_code)
            tc_str = "Default"
        else:
            val = scale_op.Get(time_code)
            tc_str = str(time_code)

        print(f"\n{description}:")
        print(f"  TimeCode: {tc_str}")
        print(f"  Value: {val}")

        # Check if value is authored at this time
        if isinstance(time_code, Usd.TimeCode):
            has_value = scale_attr.HasValue()
            is_varying = scale_attr.ValueMightBeTimeVarying()
        else:
            has_value = scale_attr.HasAuthoredValue()
            is_varying = scale_attr.ValueMightBeTimeVarying()

        print(f"  Has authored value: {has_value}")
        print(f"  Is time-varying: {is_varying}")

        # Get interpolation info
        if not isinstance(time_code, Usd.TimeCode):
            # Check if this time is within the authored range
            if time_samples:
                first_sample = min(time_samples)
                last_sample = max(time_samples)
                print(f"  Sample range: [{first_sample}, {last_sample}]")

                if time_code < first_sample:
                    print(f"  → Time is BEFORE first sample (held constant)")
                elif time_code > last_sample:
                    print(f"  → Time is AFTER last sample (held constant)")
                elif time_code in time_samples:
                    print(f"  → Time is EXACTLY at a sample")
                else:
                    print(f"  → Time is BETWEEN samples (would interpolate if multiple samples existed)")

    print("\n" + "=" * 60)
    print("USD TimeSample Evaluation Behavior:")
    print("  • When only one time sample exists, USD holds that value constant")
    print("  • Before the first sample: returns the first sample value")
    print("  • After the last sample: returns the last sample value")
    print("  • Default time: returns the default/static value if set,")
    print("    otherwise the earliest time sample")
    print("=" * 60)


def create_multi_sample_example():
    """Create an example with multiple time samples to show interpolation."""
    print("\n\nCreating Multi-Sample Example for Comparison:")
    print("=" * 60)

    stage_path = "test_scale_multi_timesamples.usda"
    stage = Usd.Stage.CreateNew(stage_path)

    # Set frame rate and time codes
    stage.SetFramesPerSecond(24.0)
    stage.SetStartTimeCode(-10.0)
    stage.SetEndTimeCode(10.0)

    # Create transform with multiple time samples
    xform_prim = UsdGeom.Xform.Define(stage, "/TestXformMulti")
    scale_op = xform_prim.AddScaleOp()

    # Set multiple time samples
    scale_op.Set(Gf.Vec3f(0.1, 0.1, 0.1), -5.0)
    scale_op.Set(Gf.Vec3f(0.5, 0.5, 0.5), 0.0)
    scale_op.Set(Gf.Vec3f(1.0, 1.0, 1.0), 5.0)

    stage.GetRootLayer().Save()

    # Evaluate at various times
    xform = UsdGeom.Xform(stage.GetPrimAtPath("/TestXformMulti"))
    xform_ops = xform.GetOrderedXformOps()
    scale_op = xform_ops[0]

    print(f"Created stage with multiple time samples: {stage_path}")
    print("TimeSamples: {-5: (0.1,0.1,0.1), 0: (0.5,0.5,0.5), 5: (1.0,1.0,1.0)}")
    print()

    test_times = [
        ("Time -10", -10.0),
        ("Time -5", -5.0),
        ("Time -2.5", -2.5),
        ("Time 0", 0.0),
        ("Time 2.5", 2.5),
        ("Time 5", 5.0),
        ("Time 10", 10.0),
    ]

    print("Multi-sample evaluation (shows interpolation):")
    for desc, t in test_times:
        val = scale_op.Get(t)
        print(f"  {desc:12s}: {val}")

    print("\nNote: With multiple samples, USD linearly interpolates between them")


def main():
    """Main function."""
    print("OpenUSD TimeSample Evaluation Test")
    print("=" * 60)

    # Change to aousd directory
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    # Create and test single sample case (as requested)
    stage_path = create_test_stage()
    evaluate_timesamples(stage_path)

    # Show multi-sample case for comparison
    create_multi_sample_example()

    print("\nTest complete!")


if __name__ == "__main__":
    main()