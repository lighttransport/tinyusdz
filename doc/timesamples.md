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

## Time-sampled value deduplication

**Status:** implemented (see [`crate-writer.md`](crate-writer.md) for the full
detail). The USDC writer now deduplicates time-sample values both **per
attribute** (frames within one attribute that share an identical value collapse
to a single on-disk `ValueRep`, via `ComputeValueDedupDescriptor` in
`src/crate-writer-values.cc`) and **globally across the whole write** via a
writer-lifetime `value_dedup_map_` (`unordered_multimap<size_t, ValueDedupEntry>`
in `src/crate-writer.hh`) keyed by `NanAwareHash(wire_tag, bytes)` and shared by
every out-of-line value of the entire write, with a memory budget
(`GetValueDedupBudgetBytes`, default `max_memory_bytes/4` capped at 512 MB) so a
pathological all-unique scene cannot balloon writer memory.

This was originally proposed here as future work after the `bool[]` visibility-
mask blow-up (an animated `bool[]` whose 1500 identical frames inflated a 78 MB
root layer to 384 MB; per-attribute dedup brings it back to ~114 MB). The global
map additionally recovers sharing *across* attributes (identical rest poses /
identity transforms / shared sub-curves), which the per-attribute map misses.

## Future Work / TODO

### More compact scalar-timesample encoding

Even with global dedup, 1.4 M unique scalar samples each cost a `ValueRep`
(8 bytes) + an out-of-line value + a time (8 bytes). A column-oriented encoding
(one packed values-array per attribute instead of N independent `ValueRep`s, as
OpenUSD stores scalar time samples) would shrink the per-sample overhead. Larger
change to the crate layout; only worth it if global dedup proves insufficient.
