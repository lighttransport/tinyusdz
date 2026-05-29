# USD TimeSamples Evaluation

How OpenUSD (and TinyUSDZ) evaluates time-sampled attributes.

## Basic Evaluation Rules

### Single TimeSample

With one sample at t=0, value (0.1, 0.2, 0.3):
- All numeric time codes return (0.1, 0.2, 0.3) - held constant
- `TimeCode::Default()`: with no explicit default value authored, TinyUSDZ
  returns the **first time sample** (here (0.1, 0.2, 0.3)). An empty (no-sample)
  TimeSamples with no default returns no value.

### Multiple TimeSamples

With samples at t=-5, t=0, t=5:
- **Before first sample**: Holds first value (no backward extrapolation)
- **Between samples**: Linear interpolation
- **After last sample**: Holds last value (no forward extrapolation)

### Default Values vs TimeSamples

Default values and time samples coexist as two separate value spaces:

```usda
def Xform "Example"
{
    float3 xformOp:scale = (7, 8, 9)           # Default value
    float3 xformOp:scale.timeSamples = {        # Time samples
        0: (0.1, 0.2, 0.3),
    }
}
```

- **`TimeCode::Default()`**: Always returns the default value (7, 8, 9)
- **Numeric time codes**: Use time samples, ignoring the default value

### Interpolation Rules

| Condition | Behavior |
|-----------|----------|
| Before first sample | Hold first value |
| After last sample | Hold last value |
| Between samples | Linear interpolation |
| Single sample | Held constant for all times |
| Held interpolation mode | Step function (use earlier sample) |

## Practical Applications

- **Rest/Bind Pose**: Store as default value
- **Animation**: Store as time samples
- **Switching**: Query `TimeCode::Default()` for static, numeric for animated

## TinyUSDZ Compatibility

TinyUSDZ matches OpenUSD behavior for:

1. Single time sample held constant
2. `TimeCode::Default()` returns the authored default value even when time
   samples exist
3. Linear interpolation between samples
4. Hold extrapolation (no backward/forward extrapolation)
5. Held interpolation mode (step function)
6. Edge cases and boundary conditions

One deviation: when **no** default value is authored, `TimeCode::Default()`
falls back to the first time sample (OpenUSD returns no value instead).

### Test Location

Unit tests: `tests/unit/unit-timesamples.cc` (function `timesamples_test()`)

```bash
./build/unit-test-tinyusdz timesamples_test
```
