# USD TimeSamples Evaluation Behavior

This document demonstrates how OpenUSD evaluates time samples at different time codes, including the interaction between default values and time samples.

## Table of Contents
- [Basic TimeSample Evaluation](#basic-timesample-evaluation)
- [Default Values vs TimeSamples](#default-values-vs-timesamples)
- [Key Insights](#key-insights)
- [Test Scripts](#test-scripts)

## Basic TimeSample Evaluation

When an attribute has time samples defined, USD evaluates them according to specific rules:

### Single TimeSample Behavior

When only one time sample exists at t=0 with value (0.1, 0.2, 0.3):

- **Time -10 (before samples)**: Returns (0.1, 0.2, 0.3) - holds the first sample value constant
- **Time 0 (at sample)**: Returns (0.1, 0.2, 0.3) - exact value at the sample
- **Time 10 (after samples)**: Returns (0.1, 0.2, 0.3) - holds the last sample value constant
- **Default time**: Returns None if no default value is set

**Key Behavior**: With a single time sample, USD holds that value constant for all time codes (no extrapolation).

### Multiple TimeSamples with Interpolation

With samples at t=-5 (0.1,0.1,0.1), t=0 (0.5,0.5,0.5), t=5 (1.0,1.0,1.0):

- **Time -10**: (0.1, 0.1, 0.1) - before first sample, holds first value
- **Time -5**: (0.1, 0.1, 0.1) - exactly at sample
- **Time -2.5**: (0.3, 0.3, 0.3) - linearly interpolated between samples
- **Time 0**: (0.5, 0.5, 0.5) - exactly at sample
- **Time 2.5**: (0.75, 0.75, 0.75) - linearly interpolated
- **Time 5**: (1.0, 1.0, 1.0) - exactly at sample
- **Time 10**: (1.0, 1.0, 1.0) - after last sample, holds last value

**Key Behavior**: USD linearly interpolates between time samples.

## Default Values vs TimeSamples

USD allows both default values and time samples to coexist on the same attribute. This enables switching between static and animated values.

### USDA Syntax

```usda
def Xform "Example"
{
    float3 xformOp:scale = (7, 8, 9)           # Default value
    float3 xformOp:scale.timeSamples = {       # Time samples
        0: (0.1, 0.2, 0.3),
    }
}
```

### Evaluation Behavior

When both default value (7, 8, 9) and time samples are authored:

1. **Default Value Only** (no time samples):
   - All time codes return (7, 8, 9)
   - `Usd.TimeCode.Default()` returns (7, 8, 9)

2. **Default + Single TimeSample**:
   - Numeric time codes (-10, 0, 10) return time sample values
   - `Usd.TimeCode.Default()` returns the default value (7, 8, 9)

3. **Default + Multiple TimeSamples**:
   - Numeric time codes use time samples with interpolation
   - `Usd.TimeCode.Default()` still returns (7, 8, 9)

## Key Insights

### Two Separate Value Spaces
- Default values and time samples are stored separately in USD
- They can coexist on the same attribute

### TimeCode Behavior
- **`Usd.TimeCode.Default()`**: Always returns the default/static value, even when time samples exist
- **Numeric time codes** (e.g., -10.0, 0.0, 10.0): Use time samples when they exist, ignoring the default value

### Practical Applications
- **Rest/Bind Pose**: Store as default value
- **Animation Data**: Store as time samples
- **Flexibility**: Query either static or animated state as needed

### Interpolation Rules
- **Before first sample**: Holds first sample value (no extrapolation)
- **After last sample**: Holds last sample value (no extrapolation)
- **Between samples**: Linear interpolation
- **Single sample**: Held constant for all time codes

## Test Scripts

### Script 1: Basic TimeSample Evaluation

```python
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
```

### Script 2: Default Values and TimeSamples Interaction

```python
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
```

## Example Output

### Single TimeSample at t=0
```
Raw TimeSamples defined in the file:
  Time samples: [0.0]
  Time 0.0: (0.1, 0.2, 0.3)

Time -10 (before samples): (0.1, 0.2, 0.3)
Time 0 (at sample): (0.1, 0.2, 0.3)
Time 10 (after samples): (0.1, 0.2, 0.3)
Default time: None
```

### Default Value (7,8,9) + TimeSample at t=0 (0.1,0.2,0.3)
```
Authored values:
  Default value: (7, 8, 9)
  Time samples: [0.0]
    Time 0.0: (0.1, 0.2, 0.3)

Time -10: (0.1, 0.2, 0.3)  → Uses time sample
Time 0: (0.1, 0.2, 0.3)    → Uses time sample
Time 10: (0.1, 0.2, 0.3)   → Uses time sample
Default: (7, 8, 9)         → Uses default value
```

## Use Cases

### Animation Systems
- Store bind/rest pose as default value
- Store animation keyframes as time samples
- Switch between static and animated states by using `Usd.TimeCode.Default()` vs numeric time codes

### Procedural Animation
- Use default values for base transformations
- Override with time samples for specific animated sequences
- Maintain fallback values when animation data is incomplete

### Asset Pipelines
- Author default values during modeling phase
- Add time samples during animation phase without losing original values
- Query either state for different pipeline stages