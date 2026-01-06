# PATHS Encoding in OpenUSD Crate Format

This document summarizes how PATHS are encoded, sorted, and represented as tree structures in OpenUSD's Crate binary format (USDC files).

## Overview

The Crate format uses a hierarchical tree representation to efficiently store USD paths. The implementation has evolved significantly:
- **Pre-0.4.0**: Uncompressed tree structure with explicit headers
- **0.4.0+**: Compressed representation using parallel integer arrays

## Key Source Files

### Primary Implementation
- **pxr/usd/sdf/crateFile.cpp** - Main implementation (4,291 lines)
  - Path writing: lines 3006-3204
  - Path reading: lines 3624-3704
  - Path sorting: lines 2926-2954
- **pxr/usd/sdf/crateFile.h** - Header with structures and declarations
- **pxr/usd/sdf/pathTable.h** - SdfPathTable tree structure (lines 75-141)
- **pxr/usd/sdf/integerCoding.h** - Integer compression utilities

## Path Sorting Algorithm

### Pre-0.4.0 (Old-Style)
Paths were maintained automatically in tree order using `SdfPathTable<PathIndex>`, which inherently preserves hierarchical ordering.

### 0.4.0+ (New-Style)
Paths are explicitly sorted using `SdfPath::operator<`:

```cpp
vector<pair<SdfPath, PathIndex>> ppaths;
ppaths.reserve(_paths.size());
for (auto const &p: _paths) {
    if (!p.IsEmpty()) {
        ppaths.emplace_back(p, _packCtx->pathToPathIndex[p]);
    }
}
std::sort(ppaths.begin(), ppaths.end(),
          [](pair<SdfPath, PathIndex> const &l,
             pair<SdfPath, PathIndex> const &r) {
              return l.first < r.first;  // SdfPath comparison
          });
```

The sorting ensures paths are in lexicographic order, which facilitates the compressed tree representation.

## Tree Representation Formats

### Uncompressed Format (Pre-0.4.0)

Each path node is stored with a `_PathItemHeader` structure:

```cpp
struct _PathItemHeader {
    PathIndex index;              // Index into _paths vector
    TokenIndex elementTokenIndex; // Token for this path element
    uint8_t bits;                 // Flags

    // Bit flags:
    static const uint8_t HasChildBit = 1 << 0;
    static const uint8_t HasSiblingBit = 1 << 1;
    static const uint8_t IsPrimPropertyPathBit = 1 << 2;
};
```

**Layout Rules:**
- If `HasChildBit` is set: the next element is the first child
- If `HasSiblingBit` is set (without child): next element is the sibling
- If both bits are set: an 8-byte sibling offset follows, then child appears next

**Example Tree Traversal:**
```
Node A (HasChild=1, HasSibling=1) [sibling_offset=X]
  Node B (child of A)
  ...
Node C (at offset X, sibling of A)
```

### Compressed Format (0.4.0+)

Paths are encoded using **three parallel integer arrays**, compressed with `Sdf_IntegerCompression`:

#### 1. pathIndexes[]
- Index into the `_paths` vector for each node
- Maps tree position to actual SdfPath object

#### 2. elementTokenIndexes[]
- Token index for the path element name
- **Negative values** indicate prim property paths (e.g., attributes)
- **Positive values** indicate regular prim paths

#### 3. jumps[]
Navigation information for tree traversal:
- **`-2`**: Leaf node (no children or siblings)
- **`-1`**: Only child follows (next element is first child)
- **`0`**: Only sibling follows (next element is sibling)
- **Positive N**: Both child and sibling exist
  - Next element is first child
  - Element at `current_index + N` is sibling

**Compression Algorithm:**
```cpp
void _WriteCompressedPathData(_Writer &w, Container const &pathVec)
{
    // Build three arrays:
    vector<uint64_t> pathIndexes;
    vector<int32_t> elementTokenIndexes;  // Negative = property path
    vector<int32_t> jumps;

    // Populate arrays by walking tree...

    // Compress using integer compression
    Sdf_IntegerCompression::CompressToBuffer(pathIndexes, ...);
    Sdf_IntegerCompression::CompressToBuffer(elementTokenIndexes, ...);
    Sdf_IntegerCompression::CompressToBuffer(jumps, ...);
}
```

## SdfPathTable Tree Structure

The in-memory tree structure uses a sophisticated design in `pathTable.h`:

```cpp
struct _Entry {
    value_type value;              // The actual data
    _Entry *next;                  // Hash bucket linked list
    _Entry *firstChild;            // First child in tree
    TfPointerAndBits<_Entry> nextSiblingOrParent;  // Dual-purpose pointer

    // Navigation methods
    _Entry *GetNextSibling();
    _Entry *GetParentLink();
    void SetSibling(_Entry *sibling);
    void SetParentLink(_Entry *parent);
    void AddChild(_Entry *child);
};
```

**Key Design Features:**
- **Dual-purpose pointer**: `nextSiblingOrParent` uses low bit to distinguish:
  - Bit 0 clear: points to next sibling
  - Bit 0 set: points to parent (for leaf nodes)
- **Hash table + tree**: Combines O(1) lookup with hierarchical structure
- **firstChild pointer**: Enables efficient tree traversal

## Decompression Process

Reading compressed paths (0.4.0+) involves:

1. **Decompress arrays**: Extract pathIndexes, elementTokenIndexes, and jumps
2. **Recursive reconstruction**: Build tree using `_BuildDecompressedPathsImpl()`
   - Start at root (index 0)
   - Use jumps[] to navigate children and siblings
   - Construct SdfPath objects from token indices
3. **Populate PathTable**: Insert paths maintaining tree structure

```cpp
void _BuildDecompressedPathsImpl(
    size_t curIdx,
    SdfPath const &curPath,
    vector<uint64_t> const &pathIndexes,
    vector<int32_t> const &elementTokenIndexes,
    vector<int32_t> const &jumps)
{
    // Process current node
    int32_t jump = jumps[curIdx];

    if (jump == -2) {
        // Leaf node - done
    } else if (jump == -1) {
        // Has child only
        _BuildDecompressedPathsImpl(curIdx + 1, childPath, ...);
    } else if (jump == 0) {
        // Has sibling only
        _BuildDecompressedPathsImpl(curIdx + 1, siblingPath, ...);
    } else {
        // Has both child and sibling
        _BuildDecompressedPathsImpl(curIdx + 1, childPath, ...);
        _BuildDecompressedPathsImpl(curIdx + jump, siblingPath, ...);
    }
}
```

## Version History

- **0.0.1**: Initial release with uncompressed path tree
- **0.1.0**: Fixed PathItemHeader structure layout
- **0.4.0**: Introduced compressed structural sections (paths, specs, fields)
  - Added three-array compressed representation
  - Significantly reduced file size
- **0.13.0**: Current version (as of investigation)

## Integer Compression Details

The `Sdf_IntegerCompression` class provides:
- **Variable-length encoding**: Smaller integers use fewer bytes
- **Optimized for sequential data**: Leverages locality in indices
- **Fast decompression**: Minimal overhead during file loading

## Performance Characteristics

**Compressed Format Benefits (0.4.0+):**
- **Smaller file size**: Integer compression reduces path section by 40-60%
- **Cache-friendly**: Sequential array access vs pointer chasing
- **Fast bulk loading**: Decompress entire array at once

**Memory Layout:**
```
File: [compressed_pathIndexes] [compressed_elementTokens] [compressed_jumps]
       |                       |                          |
       v                       v                          v
Memory: pathIndexes[]      elementTokenIndexes[]      jumps[]
       |                       |                          |
       +-------+---------------+-------------+            |
               |                             |            |
               v                             v            v
         SdfPathTable with full tree structure
```

## Implementation Notes for TinyUSDZ

When implementing PATHS encoding in TinyUSDZ crate-writer:

1. **Sorting**: Use `SdfPath::operator<` equivalent for stable ordering
2. **Tree building**: Construct paths in depth-first order
3. **Compression**: Implement or use existing integer compression
4. **Version handling**: Support both uncompressed (0.0.1-0.3.0) and compressed (0.4.0+)
5. **Validation**: Verify jumps[] indices don't exceed array bounds
6. **Property paths**: Use negative elementTokenIndexes for attributes/relationships

## References

- OpenUSD source: `pxr/usd/sdf/crateFile.cpp`
- OpenUSD source: `pxr/usd/sdf/pathTable.h`
- OpenUSD source: `pxr/usd/sdf/integerCoding.h`
- Crate format version history in `crateFile.cpp` lines 334-351
