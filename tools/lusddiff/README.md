# lusddiff - USD Layer Diff Tool

A command-line tool for computing and displaying differences between USD (Universal Scene Description) files.

## Description

`lusddiff` compares two USD files and reports differences in their structure, including:
- Added, deleted, and modified primitive specifications (PrimSpecs)
- Changes in primitive properties (attributes and relationships)
- Hierarchical differences in the USD scene graph

The tool supports both human-readable text output (similar to Unix `diff`) and structured JSON output for programmatic use.

## Usage

```bash
# Basic text diff
lusddiff file1.usd file2.usd

# Compare fully composed stages (OpenUSD usddiff-compatible spelling)
lusddiff --flatten scene1.usda scene2.usda

# JSON output
lusddiff --json scene1.usda scene2.usda

# Show help
lusddiff --help
```

### Command Line Options

- `--json` - Output differences in JSON format instead of text
- `-f`, `--flatten` - Compose both inputs before semantic comparison
- `-q`, `--brief` - Suppress diff output and use the exit status only
- `-n`, `--noeffect` - OpenUSD-compatible no-edit flag (LightUSD is always read-only)
- `--help`, `-h` - Display help information

### Supported File Formats

- `.usd` - USD (any format)
- `.usda` - USD ASCII format
- `.usdc` - USD Crate (binary) format
- `.usdz` - USD ZIP archive format

## Output Formats

### Text Output (Default)

Similar to Unix `diff` command with USD-specific annotations:

```
--- old_scene.usd
+++ new_scene.usd
- /RootPrim/DeletedChild (PrimSpec deleted)
+ /RootPrim/NewChild (PrimSpec added)
~ /RootPrim/ModifiedChild (PrimSpec modified)
- /RootPrim/SomePrim.deletedAttribute (Property deleted)
+ /RootPrim/SomePrim.newAttribute (Property added)
~ /RootPrim/SomePrim.modifiedAttribute (Property modified)
```

### JSON Output

Structured format suitable for programmatic processing:

```json
{
  "comparison": {
    "left": "old_scene.usd",
    "right": "new_scene.usd"
  },
  "primspec_diffs": {
    "/RootPrim": {
      "added": ["NewChild"],
      "deleted": ["DeletedChild"],
      "modified": ["ModifiedChild"]
    }
  },
  "property_diffs": {
    "/RootPrim/SomePrim": {
      "added": ["newAttribute"],
      "deleted": ["deletedAttribute"],
      "modified": ["modifiedAttribute"]
    }
  }
}
```

## Examples

### Compare Two Scene Files

```bash
lusddiff models/scene_v1.usd models/scene_v2.usd
```

### Export Differences as JSON

```bash
lusddiff --json old_model.usda new_model.usda > changes.json
```

### Using with Kitchen Set Example

```bash
# Compare different Kitchen Set configurations
lusddiff models/Kitchen_set/Kitchen_set.usd models/Kitchen_set/Kitchen_set_instanced.usd
```

### Cross-implementation comparison: normalizing package-anchored asset paths

When validating a LightUSD-produced `.usdz` against OpenUSD (pxr), a common
workflow is to flatten the same package with both `usdcat -f` (pxr) and
`lusdcat -f` (native) and diff the results. `lusdcat` cannot flatten a `.usdz`
directly (`--flatten is ignored for USDZ`), so unpack the archive first and
flatten its root layer:

```bash
unzip -q scene.usdz -d unpack
pxr/bin/usdcat -f scene.usdz       -o pxr-flat.usdc      # pxr loads the package directly
build/lusdcat -f unpack/root.usdc  -o lightusd-flat.usdc     # native flattens the unpacked root
build/lusddiff --low-mem pxr-flat.usdc lightusd-flat.usdc
```

**Gotcha — spurious `inputs:file` diffs.** pxr rewrites every texture
reference to the *package-anchored* absolute form, while a flatten of the
unpacked root keeps the *package-relative* form. Both resolve to the same
texture inside the package, but `lusddiff` reports each as a modified value:

```
~ /Root/.../diffuseTexture.inputs:file (Property modified: value)
  - @/abs/path/scene.usdz[Assets/.../BaseColor.png]@   # pxr: anchored to the package
  + @Assets/.../BaseColor.png@                          # native: package-relative
```

On a large package this can be the *entire* diff (thousands of such entries),
drowning out any real differences. To confirm the targets are equivalent,
strip the package-anchor prefix from both sides and compare the normalized
path multisets — an empty diff means every texture target matches 1:1:

```bash
# pxr form:  @<abs>.usdz[Assets/....png]@  ->  Assets/....png
build/lusdcat pxr-flat.usdc  | grep -oE 'inputs:file = @[^@]*@' \
  | sed -E 's/.*\.usdz\[//; s/\].*//; s/inputs:file = @//; s/@$//' | sort > pxr_norm.txt
# native form: @Assets/....png@           ->  Assets/....png
build/lusdcat lightusd-flat.usdc | grep -oE 'inputs:file = @[^@]*@' \
  | sed -E 's/inputs:file = @//; s/@$//' | sort > lightusd_norm.txt
diff pxr_norm.txt lightusd_norm.txt && echo "IDENTICAL: all texture targets match 1:1"
```

Note `lusddiff`'s text output truncates long values with `…`, so the pxr
anchored paths cannot be compared by basename straight from the diff — extract
and normalize from the flattened layers as above. After normalization, any
remaining diff entries (e.g. the stage `documentation` provenance string pxr
injects on flatten) are the genuine, non-asset-path differences.

## Building

The `lusddiff` tool is built when tools are enabled:

```bash
mkdir build && cd build
cmake -DLIGHTUSD_BUILD_TOOLS=ON ..
make lusddiff
```

The executable will be created at `build/lusddiff`.

## Implementation Details

- Uses LightUSD's Layer abstraction for efficient USD file processing
- Implements recursive diff algorithm with configurable depth limits
- Memory-safe implementation with proper error handling
- Supports all USD file formats through LightUSD's unified loader

## Limitations

- The default compares authored layers; use `--flatten` for composition-aware
  stage comparison.
- Directory-pair and recursive per-entry USDZ workflows from OpenUSD `usddiff`
  are not implemented yet.
- LightUSD never writes externally edited temporary USDA back to an input, so
  `--noeffect` is accepted but is always the effective behavior.

## Future Enhancements

- Directory and recursive per-entry USDZ comparison
- Visual diff output (HTML format)
- Performance optimization for large scene graphs
- Selective diffing (specific paths or property types)

## See Also

- [LightUSD Documentation](../../README.md)
- [USD Specification](https://openusd.org/)
- [Diff and Compare Implementation](../../src/tydra/diff-and-compare.cc)
