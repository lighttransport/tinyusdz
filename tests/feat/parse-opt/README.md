# Parse Optimization Benchmark

`bench-parse-opt` generates deterministic synthetic USDA numeric payloads and
measures parser throughput for the ASCII parser hot paths.

It has two workloads:

- Direct array literal parsing through `AsciiParser::ParseBasicTypeArray`
- Full in-memory USDA parsing through `LoadUSDFromMemory`

The quick profile is registered in `ctest`. The full profile is for manual
performance work.

## Covered Types

The benchmark currently covers:

- Integer arrays: `int[]`, `uint[]`, `int64[]`, `uint64[]`
- Floating scalar arrays: `half[]`, `float[]`, `double[]`
- Floating tuple arrays: `half3[]`, `float3[]`, `double3[]`
- Quaternion arrays: `quath[]`, `quatf[]`, `quatd[]`
- Matrix arrays: `matrix4d[]`
- A synthetic USDA file containing all of the numeric attributes above

Each direct parser case validates the parsed element count so parser failures
are visible in both manual runs and `ctest`.

## Building

```bash
cmake -S . -B build -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build build -j16 --target bench-parse-opt
```

## Running

Full profile:

```bash
./build/bench-parse-opt
```

Quick profile:

```bash
./build/bench-parse-opt --quick
```

Only direct array literals:

```bash
./build/bench-parse-opt --direct-only
```

Only synthetic USDA:

```bash
./build/bench-parse-opt --usda-only
```

The `ctest` target uses the quick profile:

```bash
ctest --test-dir build -R bench-parse-opt --output-on-failure
```

## Output

Each benchmark reports input size, average parse time, and throughput:

```text
=== Direct Array Literal Parsing ===
  float[]        bytes=   2345678 avg_ms=    5.123 MiB/s=    436.7 runs=3
```

Generation time is intentionally excluded from the timed region.
