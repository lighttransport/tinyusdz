# tusddiff - USD Layer Diff Tool

A command-line tool for computing and displaying differences between USD (Universal Scene Description) files.

## Description

`tusddiff` compares two USD files and reports differences in their structure, including:
- Added, deleted, and modified primitive specifications (PrimSpecs)
- Changes in primitive properties (attributes and relationships)
- Hierarchical differences in the USD scene graph

The tool supports both human-readable text output (similar to Unix `diff`) and structured JSON output for programmatic use.

## Usage

```bash
# Basic text diff
tusddiff file1.usd file2.usd

# JSON output
tusddiff --json scene1.usda scene2.usda

# Show help
tusddiff --help
```

### Command Line Options

- `--json` - Output differences in JSON format instead of text
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
tusddiff models/scene_v1.usd models/scene_v2.usd
```

### Export Differences as JSON

```bash
tusddiff --json old_model.usda new_model.usda > changes.json
```

### Using with Kitchen Set Example

```bash
# Compare different Kitchen Set configurations
tusddiff models/Kitchen_set/Kitchen_set.usd models/Kitchen_set/Kitchen_set_instanced.usd
```

## Building

The `tusddiff` tool is built when tools are enabled:

```bash
mkdir build && cd build
cmake -DTINYUSDZ_BUILD_TOOLS=ON ..
make tusddiff
```

The executable will be created at `build/tusddiff`.

## Implementation Details

- Uses TinyUSDZ's Layer abstraction for efficient USD file processing
- Implements recursive diff algorithm with configurable depth limits
- Memory-safe implementation with proper error handling
- Supports all USD file formats through TinyUSDZ's unified loader

## Limitations

- Currently compares USD structure at the Layer level
- Property value comparison is basic (presence/absence, not deep value diff)
- Does not perform composition-aware diffing (compares pre-composition layers)

## Future Enhancements

- Deep value comparison for properties
- Composition-aware diffing
- Visual diff output (HTML format)
- Performance optimization for large scene graphs
- Selective diffing (specific paths or property types)

## See Also

- [TinyUSDZ Documentation](../../README.md)
- [USD Specification](https://openusd.org/)
- [Diff and Compare Implementation](../../src/tydra/diff-and-compare.cc)