# Complete USDC Writer Implementation Plan for TinyUSDZ

**Date**: 2025-11-01
**Version**: 1.0
**Target**: Full-featured USDC Crate Writer compatible with OpenUSD

## Executive Summary

This document provides a comprehensive plan to implement a production-ready USDC (Crate) binary format writer in TinyUSDZ. The implementation will progress through 5 major phases over approximately 14-16 weeks, culminating in a fully-featured writer capable of handling all USD data types, composition arcs, animation, and compression.

### Goals

1. **Complete USD Type Support**: Handle all 60+ Crate data types
2. **OpenUSD Compatibility**: 100% file format compatibility with OpenUSD
3. **Production Performance**: Comparable write speeds to OpenUSD
4. **File Size Parity**: Match OpenUSD compression ratios
5. **Robust & Safe**: Comprehensive validation and error handling
6. **Well-Tested**: Extensive unit, integration, and compatibility tests

### Current Status

- ✅ Core file structure and bootstrap (v0.1.0)
- ✅ All 6 structural sections
- ✅ Basic deduplication system
- ✅ Path sorting and tree encoding
- ✅ Basic value inlining (4 types)
- ❌ Most value types (56+ remaining)
- ❌ Compression (LZ4, integer, float)
- ❌ Testing infrastructure
- ❌ Performance optimization

---

## Phase 1: Value System Foundation (Weeks 1-3)

**Goal**: Implement complete value encoding/serialization for basic USD types

### 1.1 String/Token/AssetPath Values (Week 1)

#### Implementation Strategy

```cpp
// Extend TryInlineValue() for token/string indices
bool CrateWriter::TryInlineValue(const crate::CrateValue& value, crate::ValueRep* rep) {
  // Existing: int32, uint32, float, bool

  // Add: Token (always inlined as TokenIndex)
  if (auto* token_val = value.as<value::token>()) {
    TokenIndex idx = GetOrCreateToken(token_val->str());
    rep->SetType(CRATE_DATA_TYPE_TOKEN);
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Add: String (always inlined as StringIndex)
  if (auto* str_val = value.as<std::string>()) {
    StringIndex idx = GetOrCreateString(*str_val);
    rep->SetType(CRATE_DATA_TYPE_STRING);
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Add: AssetPath (always inlined as StringIndex for path string)
  if (auto* asset_val = value.as<value::AssetPath>()) {
    StringIndex idx = GetOrCreateString(asset_val->GetAssetPath());
    rep->SetType(CRATE_DATA_TYPE_ASSET_PATH);
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  return false;
}
```

#### Tasks

- [ ] Implement token value inlining
- [ ] Implement string value inlining
- [ ] Implement AssetPath value inlining
- [ ] Update `PackValue()` to handle these types
- [ ] Add test cases for string/token fields
- [ ] Verify with TinyUSDZ reader round-trip

#### Testing

```cpp
// Test: Write and read token value
tcrate::CrateValue token_value;
token_value.Set(value::token("xformOp:translate"));
// Write to file, read back, verify

// Test: Write and read string value
tcrate::CrateValue string_value;
string_value.Set(std::string("Hello USD"));
// Write to file, read back, verify

// Test: Write and read asset path
tcrate::CrateValue asset_value;
asset_value.Set(value::AssetPath("textures/albedo.png"));
// Write to file, read back, verify
```

### 1.2 Vector/Matrix/Quaternion Types (Week 2)

#### Implementation Strategy

**Inline Optimization**: Small vectors/matrices that fit in 48-bit payload

```cpp
// Vec3f with int8 components (common case: normalized values)
bool TryInlineVec3f(const value::float3& vec, crate::ValueRep* rep) {
  // Check if all components fit in int8 (-128 to 127)
  if (CanRepresentAsInt8(vec[0]) && CanRepresentAsInt8(vec[1]) && CanRepresentAsInt8(vec[2])) {
    int8_t x = static_cast<int8_t>(vec[0]);
    int8_t y = static_cast<int8_t>(vec[1]);
    int8_t z = static_cast<int8_t>(vec[2]);

    uint64_t payload = (static_cast<uint64_t>(x) << 16) |
                       (static_cast<uint64_t>(y) << 8) |
                       static_cast<uint64_t>(z);

    rep->SetType(CRATE_DATA_TYPE_VEC3F);
    rep->SetIsInlined();
    rep->SetPayload(payload);
    return true;
  }
  return false;
}

// Special case: Zero vectors (common default values)
bool TryInlineZeroVector(const value::float3& vec, crate::ValueRep* rep) {
  if (vec[0] == 0.0f && vec[1] == 0.0f && vec[2] == 0.0f) {
    rep->SetType(CRATE_DATA_TYPE_VEC3F);
    rep->SetIsInlined();
    rep->SetPayload(0);
    return true;
  }
  return false;
}
```

**Out-of-Line Serialization**: Full precision vectors/matrices

```cpp
int64_t CrateWriter::WriteValueData(const crate::CrateValue& value, std::string* err) {
  int64_t offset = Tell();

  // Vector types
  if (auto* vec2f = value.as<value::float2>()) {
    WriteBytes(vec2f->data(), sizeof(float) * 2);
  }
  else if (auto* vec3f = value.as<value::float3>()) {
    WriteBytes(vec3f->data(), sizeof(float) * 3);
  }
  else if (auto* vec4f = value.as<value::float4>()) {
    WriteBytes(vec4f->data(), sizeof(float) * 4);
  }
  // Similar for vec2/3/4 d/h/i variants

  // Matrix types (column-major order)
  else if (auto* mat4d = value.as<value::matrix4d>()) {
    WriteBytes(mat4d->data(), sizeof(double) * 16);
  }
  // Similar for matrix2d, matrix3d

  // Quaternion types
  else if (auto* quatf = value.as<value::quatf>()) {
    WriteBytes(&quatf->real, sizeof(float));
    WriteBytes(quatf->imaginary.data(), sizeof(float) * 3);
  }
  // Similar for quatd, quath

  return offset;
}
```

#### Type Coverage Matrix

| Type | Inline? | Size | Implementation Priority |
|------|---------|------|------------------------|
| `Vec2f/d/h/i` | Conditional | 8-16 bytes | High |
| `Vec3f/d/h/i` | Conditional | 12-24 bytes | High |
| `Vec4f/d/h/i` | Conditional | 16-32 bytes | High |
| `Matrix2d` | No | 32 bytes | Medium |
| `Matrix3d` | No | 72 bytes | Medium |
| `Matrix4d` | Identity only | 128 bytes | High |
| `Quatf/d/h` | No | 16-32 bytes | Medium |

#### Tasks

- [ ] Implement vector value inlining (zero and int8 cases)
- [ ] Implement vector out-of-line serialization
- [ ] Implement matrix out-of-line serialization (column-major)
- [ ] Implement quaternion out-of-line serialization
- [ ] Implement identity matrix inlining
- [ ] Add all 16 vector type variants
- [ ] Add all 3 matrix type variants
- [ ] Add all 3 quaternion type variants
- [ ] Write comprehensive tests for each type

### 1.3 Array Support (Week 3)

#### Implementation Strategy

**Array Header Format**:
```
[uint64_t array_size] [element_data...]
```

**Implementation**:

```cpp
int64_t CrateWriter::WriteArrayValue(const crate::CrateValue& value, std::string* err) {
  int64_t offset = Tell();

  // Write array size first
  if (auto* int_array = value.as<std::vector<int32_t>>()) {
    uint64_t size = int_array->size();
    Write(size);
    WriteBytes(int_array->data(), sizeof(int32_t) * size);
  }
  else if (auto* float_array = value.as<std::vector<float>>()) {
    uint64_t size = float_array->size();
    Write(size);
    WriteBytes(float_array->data(), sizeof(float) * size);
  }
  // ... handle all array types

  // Special case: Vec3f arrays (common for geometry)
  else if (auto* vec3f_array = value.as<std::vector<value::float3>>()) {
    uint64_t size = vec3f_array->size();
    Write(size);
    for (const auto& vec : *vec3f_array) {
      WriteBytes(&vec[0], sizeof(float) * 3);
    }
  }

  return offset;
}

crate::ValueRep CrateWriter::PackArrayValue(const crate::CrateValue& value, std::string* err) {
  crate::ValueRep rep;

  // Arrays are never inlined (too large)
  int64_t offset = WriteArrayValue(value, err);

  rep.SetType(GetArrayTypeId(value)); // e.g., CRATE_DATA_TYPE_INT for int[]
  rep.SetIsArray();
  rep.SetPayload(static_cast<uint64_t>(offset));

  return rep;
}
```

#### Tasks

- [ ] Implement array size header writing
- [ ] Implement scalar array serialization (int, uint, float, double, etc.)
- [ ] Implement vector array serialization (Vec2/3/4 variants)
- [ ] Implement matrix array serialization
- [ ] Handle empty arrays (inline with payload=0)
- [ ] Add array type ID mapping
- [ ] Test with geometry data (points, normals, uvs)
- [ ] Verify large array handling (>1M elements)

---

## Phase 2: Complex USD Types (Weeks 4-6)

### 2.1 Dictionary Support (Week 4)

#### Implementation Strategy

**VtDictionary Format**:
```
[uint64_t num_keys]
[StringIndex key1] [ValueRep value1]
[StringIndex key2] [ValueRep value2]
...
```

```cpp
int64_t CrateWriter::WriteDictionary(const value::dict& dict, std::string* err) {
  int64_t offset = Tell();

  // Write number of key-value pairs
  uint64_t size = dict.size();
  Write(size);

  // Sort keys for deterministic output
  std::vector<std::string> sorted_keys;
  for (const auto& [key, val] : dict) {
    sorted_keys.push_back(key);
  }
  std::sort(sorted_keys.begin(), sorted_keys.end());

  // Write each key-value pair
  for (const auto& key : sorted_keys) {
    StringIndex key_idx = GetOrCreateString(key);
    Write(key_idx);

    const auto& val = dict.at(key);
    crate::CrateValue crate_val;
    // Convert value::Value to crate::CrateValue
    ConvertValueToCrateValue(val, &crate_val);

    ValueRep val_rep = PackValue(crate_val, err);
    Write(val_rep);
  }

  return offset;
}
```

#### Tasks

- [ ] Implement dictionary serialization
- [ ] Handle nested dictionaries (recursive)
- [ ] Handle mixed value types in dictionary
- [ ] Implement value::Value → crate::CrateValue conversion
- [ ] Test with customData dictionaries
- [ ] Test with nested dictionary structures

### 2.2 ListOp Support (Week 5)

#### Implementation Strategy

**SdfListOp Format** (for all list op types):
```
[uint8_t flags]  // HasExplicit=1, HasAdded=2, HasDeleted=4, HasOrdered=8, HasPrepended=16, HasAppended=32
[uint32_t explicit_count] [items...]  // if HasExplicit
[uint32_t prepended_count] [items...] // if HasPrepended
[uint32_t appended_count] [items...]  // if HasAppended
[uint32_t deleted_count] [items...]   // if HasDeleted
[uint32_t ordered_count] [items...]   // if HasOrdered
```

```cpp
template<typename T>
int64_t CrateWriter::WriteListOp(const ListOp<T>& listop, std::string* err) {
  int64_t offset = Tell();

  // Write flags
  uint8_t flags = 0;
  if (listop.IsExplicit()) flags |= 0x01;
  if (!listop.GetPrependedItems().empty()) flags |= 0x10;
  if (!listop.GetAppendedItems().empty()) flags |= 0x20;
  if (!listop.GetDeletedItems().empty()) flags |= 0x04;
  if (!listop.GetOrderedItems().empty()) flags |= 0x08;
  Write(flags);

  // Write each list (only if present)
  auto writeItemList = [&](const std::vector<T>& items) {
    uint32_t count = items.size();
    Write(count);
    for (const auto& item : items) {
      WriteListOpItem(item); // Type-specific serialization
    }
  };

  if (listop.IsExplicit()) writeItemList(listop.GetExplicitItems());
  if (flags & 0x10) writeItemList(listop.GetPrependedItems());
  if (flags & 0x20) writeItemList(listop.GetAppendedItems());
  if (flags & 0x04) writeItemList(listop.GetDeletedItems());
  if (flags & 0x08) writeItemList(listop.GetOrderedItems());

  return offset;
}

// Specialized for different item types
void CrateWriter::WriteListOpItem(const value::token& item) {
  TokenIndex idx = GetOrCreateToken(item.str());
  Write(idx);
}

void CrateWriter::WriteListOpItem(const std::string& item) {
  StringIndex idx = GetOrCreateString(item);
  Write(idx);
}

void CrateWriter::WriteListOpItem(const Path& item) {
  PathIndex idx = GetOrCreatePath(item);
  Write(idx);
}

void CrateWriter::WriteListOpItem(const Reference& item) {
  // Write asset path, prim path, layer offset, custom data
  WriteReference(item);
}
```

#### ListOp Type Coverage

- [ ] `TokenListOp` - API schemas, applied schemas
- [ ] `StringListOp` - Less common
- [ ] `PathListOp` - Relationships, connections
- [ ] `ReferenceListOp` - Composition references
- [ ] `PayloadListOp` - Lazy-loaded payloads
- [ ] `IntListOp` - Rare
- [ ] `Int64ListOp` - Rare
- [ ] `UIntListOp` - Rare
- [ ] `UInt64ListOp` - Rare

#### Tasks

- [ ] Implement ListOp flag encoding
- [ ] Implement TokenListOp serialization
- [ ] Implement PathListOp serialization
- [ ] Implement ReferenceListOp serialization
- [ ] Implement PayloadListOp serialization
- [ ] Implement integer ListOp variants
- [ ] Test with USD composition (references, payloads)
- [ ] Test with apiSchemas
- [ ] Test with relationship targets

### 2.3 Reference/Payload Support (Week 6)

#### Implementation Strategy

**Reference Format**:
```
[StringIndex asset_path]      // Asset path (empty if internal reference)
[PathIndex prim_path]          // Target prim path
[int64_t layer_offset_offset]  // File offset to LayerOffset (0 if none)
[int64_t custom_data_offset]   // File offset to customData dict (0 if none)
```

**Payload Format**: Same as Reference

```cpp
int64_t CrateWriter::WriteReference(const Reference& ref, std::string* err) {
  int64_t offset = Tell();

  // Asset path
  StringIndex asset_idx = ref.asset_path.empty() ?
    StringIndex(0) : GetOrCreateString(ref.asset_path);
  Write(asset_idx);

  // Prim path
  PathIndex prim_idx = GetOrCreatePath(Path(ref.prim_path, ""));
  Write(prim_idx);

  // Layer offset (if present)
  int64_t layer_offset_offset = 0;
  if (ref.layerOffset.IsValid()) {
    layer_offset_offset = WriteLayerOffset(ref.layerOffset, err);
  }
  Write(layer_offset_offset);

  // Custom data (if present)
  int64_t custom_data_offset = 0;
  if (!ref.customData.empty()) {
    custom_data_offset = WriteDictionary(ref.customData, err);
  }
  Write(custom_data_offset);

  return offset;
}

int64_t CrateWriter::WriteLayerOffset(const LayerOffset& offset, std::string* err) {
  int64_t file_offset = Tell();

  Write(offset.offset);  // double
  Write(offset.scale);   // double

  return file_offset;
}
```

#### Tasks

- [ ] Implement Reference serialization
- [ ] Implement Payload serialization
- [ ] Implement LayerOffset serialization
- [ ] Handle internal vs external references
- [ ] Handle reference with custom data
- [ ] Test with USD composition hierarchies
- [ ] Test with external file references
- [ ] Test with payload arcs

---

## Phase 3: Animation & TimeSamples (Weeks 7-8)

### 3.1 TimeSamples Implementation (Week 7)

#### Format Strategy

**TimeSamples Structure**:
```
[uint64_t num_samples]
[double time1] [ValueRep value1]
[double time2] [ValueRep value2]
...
```

**Time Array Deduplication**: Many attributes share same time samples

```cpp
class CrateWriter {
  // Add time array deduplication table
  std::unordered_map<std::vector<double>, int64_t, TimeArrayHasher> time_array_offsets_;
};

int64_t CrateWriter::WriteTimeSamples(const value::TimeSamples& ts, std::string* err) {
  int64_t offset = Tell();

  // Extract time array
  std::vector<double> times;
  ts.get_times(&times);

  // Check if we've already written this time array
  auto it = time_array_offsets_.find(times);
  int64_t time_array_offset;

  if (it != time_array_offsets_.end()) {
    // Reuse existing time array
    time_array_offset = it->second;
  } else {
    // Write new time array
    time_array_offset = Tell();

    uint64_t num_samples = times.size();
    Write(num_samples);
    WriteBytes(times.data(), sizeof(double) * num_samples);

    time_array_offsets_[times] = time_array_offset;
  }

  // Write reference to time array
  Write(time_array_offset);

  // Write value array
  uint64_t num_samples = times.size();
  for (size_t i = 0; i < num_samples; ++i) {
    value::Value val;
    ts.get(times[i], &val);

    crate::CrateValue crate_val;
    ConvertValueToCrateValue(val, &crate_val);

    ValueRep val_rep = PackValue(crate_val, err);
    Write(val_rep);
  }

  return offset;
}
```

#### Tasks

- [ ] Implement TimeSamples format
- [ ] Implement time array deduplication
- [ ] Implement value array serialization
- [ ] Handle different value types in TimeSamples
- [ ] Test with animated transforms
- [ ] Test with animated geometry (points)
- [ ] Test time array reuse (verify deduplication)
- [ ] Benchmark deduplication effectiveness

### 3.2 TimeCode Support (Week 8)

**TimeCode** is a special type representing a frame number:

```cpp
int64_t CrateWriter::WriteTimeCode(const value::TimeCode& tc, std::string* err) {
  // TimeCode is just a double, can be inlined
  crate::ValueRep rep;
  rep.SetType(CRATE_DATA_TYPE_TIME_CODE);
  rep.SetIsInlined();

  // Pack double into 48 bits if possible, otherwise out-of-line
  if (CanInlineDouble(tc.value)) {
    rep.SetPayload(PackDoubleToPayload(tc.value));
    rep.SetIsInlined();
  } else {
    int64_t offset = Tell();
    Write(tc.value);
    rep.SetPayload(offset);
  }

  return rep;
}
```

#### Tasks

- [ ] Implement TimeCode value encoding
- [ ] Handle inline vs out-of-line TimeCode
- [ ] Test with time-sampled attributes
- [ ] Integrate with TimeSamples support

---

## Phase 4: Compression (Weeks 9-11)

### 4.1 LZ4 Structural Compression (Week 9)

#### Strategy

Compress the following sections:
- TOKENS (string blob)
- FIELDS (Field array)
- FIELDSETS (index lists)
- PATHS (tree arrays)
- SPECS (Spec array)

**Format**:
```
[uint64_t uncompressed_size]
[uint64_t compressed_size]
[compressed_data...]
```

#### Implementation

```cpp
// Add LZ4 library to dependencies
#include <lz4.h>

bool CrateWriter::WriteCompressedSection(const std::vector<uint8_t>& data,
                                          std::string* err) {
  // Compression threshold: only compress if > 256 bytes
  if (data.size() < 256) {
    // Write uncompressed
    WriteBytes(data.data(), data.size());
    return true;
  }

  // Allocate compression buffer
  size_t max_compressed_size = LZ4_compressBound(data.size());
  std::vector<uint8_t> compressed(max_compressed_size);

  // Compress
  int compressed_size = LZ4_compress_default(
    reinterpret_cast<const char*>(data.data()),
    reinterpret_cast<char*>(compressed.data()),
    data.size(),
    max_compressed_size
  );

  if (compressed_size <= 0) {
    if (err) *err = "LZ4 compression failed";
    return false;
  }

  // Only use compression if it actually reduces size
  if (compressed_size < data.size()) {
    Write(static_cast<uint64_t>(data.size()));        // Uncompressed size
    Write(static_cast<uint64_t>(compressed_size));    // Compressed size
    WriteBytes(compressed.data(), compressed_size);
  } else {
    // Write uncompressed (set compressed_size = 0 as indicator)
    Write(static_cast<uint64_t>(data.size()));
    Write(static_cast<uint64_t>(0));  // 0 = not compressed
    WriteBytes(data.data(), data.size());
  }

  return true;
}
```

#### Modified Section Writing

```cpp
bool CrateWriter::WriteTokensSection(std::string* err) {
  int64_t section_start = Tell();

  // Build token blob
  std::ostringstream blob;
  for (const auto& token : tokens_) {
    blob << token << '\0';
  }
  std::string token_blob = blob.str();

  // Write token count (uncompressed)
  uint64_t token_count = tokens_.size();
  Write(token_count);

  // Compress and write blob
  std::vector<uint8_t> data(token_blob.begin(), token_blob.end());
  if (!WriteCompressedSection(data, err)) {
    return false;
  }

  int64_t section_end = Tell();

  crate::Section section(kTokensSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}
```

#### Tasks

- [ ] Add LZ4 library dependency
- [ ] Implement compressed section writing
- [ ] Update TOKENS section for compression
- [ ] Update FIELDS section for compression
- [ ] Update FIELDSETS section for compression
- [ ] Update PATHS section for compression
- [ ] Update SPECS section for compression
- [ ] Add compression statistics logging
- [ ] Benchmark compression ratios
- [ ] Compare file sizes with OpenUSD

### 4.2 Integer Compression (Week 10)

#### Strategy

Use delta encoding + variable-length encoding for monotonic integer sequences (PathIndex, TokenIndex, FieldIndex).

**Variable-Length Encoding**:
- 1 byte: 0-127 (7 bits)
- 2 bytes: 128-16,383 (14 bits)
- 3 bytes: 16,384-2,097,151 (21 bits)
- 4 bytes: 2,097,152-268,435,455 (28 bits)
- 5 bytes: anything larger

```cpp
class IntegerCompressor {
public:
  // Compress array of uint32 values
  std::vector<uint8_t> Compress(const std::vector<uint32_t>& values) {
    if (values.empty()) return {};

    std::vector<uint8_t> result;

    // Try delta encoding (for sorted/monotonic sequences)
    if (IsMostlyMonotonic(values)) {
      result = CompressWithDelta(values);
    } else {
      result = CompressRaw(values);
    }

    return result;
  }

private:
  std::vector<uint8_t> CompressWithDelta(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> result;

    // Write first value
    WriteVarint(values[0], result);

    // Write deltas
    for (size_t i = 1; i < values.size(); ++i) {
      int64_t delta = static_cast<int64_t>(values[i]) - static_cast<int64_t>(values[i-1]);
      WriteSignedVarint(delta, result);
    }

    return result;
  }

  void WriteVarint(uint64_t value, std::vector<uint8_t>& out) {
    while (value >= 128) {
      out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
      value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
  }

  void WriteSignedVarint(int64_t value, std::vector<uint8_t>& out) {
    // ZigZag encoding: maps signed to unsigned
    uint64_t zigzag = (value << 1) ^ (value >> 63);
    WriteVarint(zigzag, out);
  }
};
```

#### Tasks

- [ ] Implement variable-length integer encoding
- [ ] Implement delta encoding for sorted sequences
- [ ] Add compression format detection (delta vs raw)
- [ ] Compress PathIndex arrays in SPECS section
- [ ] Compress TokenIndex arrays in FIELDS section
- [ ] Compress FieldIndex arrays in FIELDSETS section
- [ ] Test compression ratios
- [ ] Verify correctness with round-trip tests

### 4.3 Float Array Compression (Week 11)

#### Strategy

Two compression schemes:
1. **As-Integer**: When floats are exactly representable as integers
2. **Lookup Table**: When many duplicate values exist

```cpp
class FloatArrayCompressor {
public:
  enum CompressionMethod {
    NONE,
    AS_INTEGER,
    LOOKUP_TABLE
  };

  struct CompressedFloatArray {
    CompressionMethod method;
    std::vector<uint8_t> data;
  };

  CompressedFloatArray Compress(const std::vector<float>& values) {
    // Try as-integer encoding
    if (AllFloatsAreIntegers(values)) {
      return CompressAsInteger(values);
    }

    // Try lookup table encoding
    size_t unique_count = CountUniqueValues(values);
    if (unique_count < 1024 && unique_count < values.size() * 0.25) {
      return CompressWithLookupTable(values);
    }

    // No compression
    return CompressedFloatArray{NONE, {}};
  }

private:
  CompressedFloatArray CompressAsInteger(const std::vector<float>& values) {
    std::vector<int32_t> int_values;
    int_values.reserve(values.size());

    for (float f : values) {
      int_values.push_back(static_cast<int32_t>(f));
    }

    // Use integer compression
    IntegerCompressor int_comp;
    std::vector<uint8_t> compressed = int_comp.Compress(
      reinterpret_cast<const std::vector<uint32_t>&>(int_values)
    );

    return CompressedFloatArray{AS_INTEGER, compressed};
  }

  CompressedFloatArray CompressWithLookupTable(const std::vector<float>& values) {
    // Build unique value table
    std::vector<float> table;
    std::unordered_map<float, uint32_t> value_to_index;

    for (float f : values) {
      if (value_to_index.find(f) == value_to_index.end()) {
        value_to_index[f] = table.size();
        table.push_back(f);
      }
    }

    // Build index array
    std::vector<uint32_t> indices;
    indices.reserve(values.size());
    for (float f : values) {
      indices.push_back(value_to_index[f]);
    }

    // Serialize: [table_size][table_values...][compressed_indices...]
    std::vector<uint8_t> result;

    uint32_t table_size = table.size();
    // Write table_size, table, then compressed indices

    return CompressedFloatArray{LOOKUP_TABLE, result};
  }
};
```

#### Tasks

- [ ] Implement as-integer float compression
- [ ] Implement lookup table float compression
- [ ] Add compression method detection
- [ ] Apply to float arrays in value section
- [ ] Test with geometry data (large point arrays)
- [ ] Benchmark compression ratios
- [ ] Compare with OpenUSD compression

---

## Phase 5: Production Readiness (Weeks 12-16)

### 5.1 Validation & Error Handling (Week 12)

#### Input Validation

```cpp
class CrateWriter {
  // Add validation mode
  Options options_;

  bool ValidateSpec(const Path& path, const FieldValuePairVector& fields, std::string* err) {
    // Validate path
    if (!path.is_valid()) {
      if (err) *err = "Invalid path: " + path.full_path_name();
      return false;
    }

    // Validate field names
    for (const auto& field : fields) {
      if (field.first.empty()) {
        if (err) *err = "Empty field name for path: " + path.full_path_name();
        return false;
      }

      // Check for reserved field names
      if (!IsValidFieldName(field.first)) {
        if (err) *err = "Invalid field name: " + field.first;
        return false;
      }
    }

    // Type-specific validation
    if (path.is_property_path()) {
      // Properties must have specific fields
      if (!HasRequiredPropertyFields(fields)) {
        if (err) *err = "Property missing required fields: " + path.full_path_name();
        return false;
      }
    }

    return true;
  }
};
```

#### Error Recovery

```cpp
class CrateWriter {
  // Add transaction-like behavior
  struct WriteTransaction {
    std::string temp_filepath;
    bool committed = false;
  };

  WriteTransaction* current_transaction_ = nullptr;

  bool Begin Transaction(std::string* err) {
    if (current_transaction_) {
      if (err) *err = "Transaction already in progress";
      return false;
    }

    current_transaction_ = new WriteTransaction();
    current_transaction_->temp_filepath = filepath_ + ".tmp";

    // Open temp file
    file_.open(current_transaction_->temp_filepath, std::ios::binary | std::ios::out);

    return true;
  }

  bool CommitTransaction(std::string* err) {
    if (!current_transaction_) {
      if (err) *err = "No transaction in progress";
      return false;
    }

    file_.close();

    // Atomic rename
    if (std::rename(current_transaction_->temp_filepath.c_str(), filepath_.c_str()) != 0) {
      if (err) *err = "Failed to commit transaction";
      return false;
    }

    current_transaction_->committed = true;
    delete current_transaction_;
    current_transaction_ = nullptr;

    return true;
  }

  void RollbackTransaction() {
    if (current_transaction_) {
      file_.close();
      std::remove(current_transaction_->temp_filepath.c_str());
      delete current_transaction_;
      current_transaction_ = nullptr;
    }
  }
};
```

#### Tasks

- [ ] Implement path validation
- [ ] Implement field name validation
- [ ] Implement type consistency checking
- [ ] Add transaction support
- [ ] Add rollback on error
- [ ] Implement detailed error messages
- [ ] Add error location tracking (which spec failed)
- [ ] Test error handling paths

### 5.2 Performance Optimization (Week 13)

#### Async I/O

```cpp
class BufferedAsyncWriter {
  static constexpr size_t kBufferSize = 512 * 1024;  // 512KB buffers
  static constexpr size_t kNumBuffers = 4;

  struct Buffer {
    std::vector<uint8_t> data;
    size_t used = 0;
    bool writing = false;
    std::future<void> write_future;
  };

  std::array<Buffer, kNumBuffers> buffers_;
  size_t current_buffer_idx_ = 0;
  int fd_;

public:
  void Write(const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = size;

    while (remaining > 0) {
      Buffer& buf = buffers_[current_buffer_idx_];

      // Wait if buffer is being written
      if (buf.writing && buf.write_future.valid()) {
        buf.write_future.wait();
        buf.writing = false;
        buf.used = 0;
      }

      // Copy to buffer
      size_t to_copy = std::min(remaining, kBufferSize - buf.used);
      std::memcpy(buf.data.data() + buf.used, ptr, to_copy);
      buf.used += to_copy;
      ptr += to_copy;
      remaining -= to_copy;

      // Flush if buffer full
      if (buf.used == kBufferSize) {
        FlushBuffer(current_buffer_idx_);
        current_buffer_idx_ = (current_buffer_idx_ + 1) % kNumBuffers;
      }
    }
  }

private:
  void FlushBuffer(size_t idx) {
    Buffer& buf = buffers_[idx];
    buf.writing = true;

    // Launch async write
    buf.write_future = std::async(std::launch::async, [this, idx]() {
      Buffer& b = buffers_[idx];
      ::write(fd_, b.data.data(), b.used);
    });
  }
};
```

#### Parallel Token Processing

```cpp
void CrateWriter::BuildTokenTable() {
  // Collect all unique tokens in parallel
  std::vector<std::string> all_tokens;

  // Extract tokens from all sources
  #pragma omp parallel
  {
    std::vector<std::string> local_tokens;

    #pragma omp for
    for (size_t i = 0; i < spec_data_.size(); ++i) {
      ExtractTokensFromSpec(spec_data_[i], local_tokens);
    }

    #pragma omp critical
    {
      all_tokens.insert(all_tokens.end(), local_tokens.begin(), local_tokens.end());
    }
  }

  // Sort and deduplicate
  std::sort(all_tokens.begin(), all_tokens.end());
  all_tokens.erase(std::unique(all_tokens.begin(), all_tokens.end()), all_tokens.end());

  // Build token index map
  for (size_t i = 0; i < all_tokens.size(); ++i) {
    token_to_index_[all_tokens[i]] = crate::TokenIndex(i);
  }
  tokens_ = std::move(all_tokens);
}
```

#### Tasks

- [ ] Implement buffered async I/O
- [ ] Implement parallel token table construction
- [ ] Implement parallel value packing (where possible)
- [ ] Add memory pooling for frequently allocated objects
- [ ] Optimize deduplication map lookups
- [ ] Profile and optimize hot paths
- [ ] Benchmark write performance vs OpenUSD
- [ ] Target: Within 20% of OpenUSD write speed

### 5.3 Testing Infrastructure (Weeks 14-15)

#### Unit Tests

```cpp
// Test framework structure
class CrateWriterTest : public ::testing::Test {
protected:
  void SetUp() override {
    temp_file_ = CreateTempFile();
    writer_ = std::make_unique<CrateWriter>(temp_file_);
  }

  void TearDown() override {
    writer_.reset();
    std::remove(temp_file_.c_str());
  }

  std::string temp_file_;
  std::unique_ptr<CrateWriter> writer_;
};

TEST_F(CrateWriterTest, WriteBasicPrim) {
  ASSERT_TRUE(writer_->Open());

  Path prim_path("/World", "");
  tcrate::FieldValuePairVector fields;

  tcrate::CrateValue specifier;
  specifier.Set(Specifier::Def);
  fields.push_back({"specifier", specifier});

  ASSERT_TRUE(writer_->AddSpec(prim_path, SpecType::Prim, fields));
  ASSERT_TRUE(writer_->Finalize());

  // Verify file was written
  ASSERT_TRUE(std::filesystem::exists(temp_file_));
  ASSERT_GT(std::filesystem::file_size(temp_file_), 64);  // At least bootstrap
}

TEST_F(CrateWriterTest, ValueInlining) {
  // Test int32 inlining
  tcrate::CrateValue int_val;
  int_val.Set(static_cast<int32_t>(42));

  crate::ValueRep rep;
  ASSERT_TRUE(writer_->TryInlineValue(int_val, &rep));
  ASSERT_TRUE(rep.IsInlined());
  ASSERT_EQ(rep.GetPayload(), 42);
}

TEST_F(CrateWriterTest, TokenDeduplication) {
  writer_->Open();

  // Add same token multiple times
  auto idx1 = writer_->GetOrCreateToken("xformOp:translate");
  auto idx2 = writer_->GetOrCreateToken("xformOp:translate");
  auto idx3 = writer_->GetOrCreateToken("xformOp:translate");

  // Should all return same index
  ASSERT_EQ(idx1.value, idx2.value);
  ASSERT_EQ(idx2.value, idx3.value);

  // Token table should have only one entry
  ASSERT_EQ(writer_->GetTokenCount(), 1);
}
```

#### Integration Tests

```cpp
TEST(CrateWriterIntegrationTest, RoundTripSimpleScene) {
  std::string temp_file = CreateTempFile();

  // Write file
  {
    CrateWriter writer(temp_file);
    writer.Open();

    // Add prims
    writer.AddSpec(Path("/World", ""), SpecType::Prim, ...);
    writer.AddSpec(Path("/World/Geom", ""), SpecType::Prim, ...);
    writer.AddSpec(Path("/World/Geom", "points"), SpecType::Attribute, ...);

    writer.Finalize();
  }

  // Read file back with TinyUSDZ
  Stage stage;
  std::string warn, err;
  bool ret = LoadUSDFromFile(temp_file, &stage, &warn, &err);

  ASSERT_TRUE(ret) << "Failed to read: " << err;

  // Verify structure
  ASSERT_TRUE(stage.GetPrimAtPath(Path("/World", "")).is_valid());
  ASSERT_TRUE(stage.GetPrimAtPath(Path("/World/Geom", "")).is_valid());

  // Verify attribute
  auto geom_prim = stage.GetPrimAtPath(Path("/World/Geom", ""));
  Attribute points_attr;
  ASSERT_TRUE(geom_prim.GetAttribute("points", &points_attr));
}

TEST(CrateWriterIntegrationTest, CompareWithOpenUSD) {
  // Write same scene with both writers
  std::string tinyusdz_file = "test_tinyusdz.usdc";
  std::string openusd_file = "test_openusd.usdc";

  // Write with TinyUSDZ
  WriteSampleSceneWithTinyUSDZ(tinyusdz_file);

  // Write with OpenUSD
  WriteSampleSceneWithOpenUSD(openusd_file);

  // Read both with OpenUSD and compare
  auto tinyusdz_stage = pxr::UsdStage::Open(tinyusdz_file);
  auto openusd_stage = pxr::UsdStage::Open(openusd_file);

  ASSERT_TRUE(tinyusdz_stage);
  ASSERT_TRUE(openusd_stage);

  // Compare structure
  CompareStages(tinyusdz_stage, openusd_stage);
}
```

#### Compatibility Tests

```bash
#!/bin/bash
# Test compatibility with USD tools

FILE="test_output.usdc"

# Create test file with TinyUSDZ
./test_writer "$FILE"

# Verify with OpenUSD tools
echo "Testing with usdcat..."
usdcat "$FILE" -o /tmp/test.usda
if [ $? -ne 0 ]; then
    echo "FAIL: usdcat failed"
    exit 1
fi

echo "Testing with usdchecker..."
usdchecker "$FILE"
if [ $? -ne 0 ]; then
    echo "FAIL: usdchecker found issues"
    exit 1
fi

echo "Testing with usddumpcrate..."
usddumpcrate "$FILE"
if [ $? -ne 0 ]; then
    echo "FAIL: usddumpcrate failed"
    exit 1
fi

echo "All compatibility tests passed!"
```

#### Tasks

- [ ] Set up testing framework (Google Test)
- [ ] Write unit tests for each component
  - [ ] Bootstrap writing
  - [ ] Section writing
  - [ ] Value encoding
  - [ ] Deduplication
  - [ ] Compression
- [ ] Write integration tests
  - [ ] Round-trip with TinyUSDZ reader
  - [ ] Comparison with OpenUSD writer
- [ ] Write compatibility tests
  - [ ] Test with `usdcat`
  - [ ] Test with `usdchecker`
  - [ ] Test with `usddumpcrate`
  - [ ] Test with DCC tools (if available)
- [ ] Set up CI/CD for automated testing
- [ ] Achieve >90% code coverage

### 5.4 Documentation & Polish (Week 16)

#### API Documentation

```cpp
///
/// @class CrateWriter
/// @brief Writes USD Layer/PrimSpec data to USDC (Crate) binary format.
///
/// Example usage:
/// @code
/// CrateWriter writer("output.usdc");
///
/// CrateWriter::Options opts;
/// opts.enable_compression = true;
/// writer.SetOptions(opts);
///
/// writer.Open();
///
/// // Add prims
/// Path prim_path("/World", "");
/// tcrate::FieldValuePairVector fields;
/// // ... populate fields
/// writer.AddSpec(prim_path, SpecType::Prim, fields);
///
/// writer.Finalize();
/// writer.Close();
/// @endcode
///
/// @note Thread-safety: CrateWriter is not thread-safe. Do not call methods
///       from multiple threads simultaneously.
///
/// @see LoadUSDFromFile() for reading USDC files
///
class CrateWriter {
public:
  ///
  /// @brief Configuration options for the writer.
  ///
  struct Options {
    uint8_t version_major = 0;  ///< Target crate version (major)
    uint8_t version_minor = 8;  ///< Target crate version (minor)
    uint8_t version_patch = 0;  ///< Target crate version (patch)

    bool enable_compression = true;      ///< Enable LZ4 compression for sections
    bool enable_deduplication = true;    ///< Enable value deduplication
    bool enable_validation = true;       ///< Validate inputs
    bool enable_async_io = true;         ///< Use async buffered I/O

    size_t buffer_size = 512 * 1024;    ///< I/O buffer size (bytes)
    size_t num_buffers = 4;              ///< Number of async buffers
  };

  /// @brief Create a writer for the specified file.
  /// @param filepath Output file path
  explicit CrateWriter(const std::string& filepath);

  /// ... rest of API documentation
};
```

#### User Guide

```markdown
# TinyUSDZ USDC Writer Guide

## Overview

The TinyUSDZ USDC (Crate) writer provides a complete implementation for
writing USD scene data to OpenUSD-compatible binary files.

## Quick Start

### Basic Usage

\`\`\`cpp
#include "crate-writer.hh"

using namespace tinyusdz;

CrateWriter writer("output.usdc");
writer.Open();

// Add a root prim
Path root("/World", "");
tcrate::FieldValuePairVector fields;

tcrate::CrateValue specifier;
specifier.Set(Specifier::Def);
fields.push_back({"specifier", specifier});

writer.AddSpec(root, SpecType::Prim, fields);

writer.Finalize();
writer.Close();
\`\`\`

### Configuration

\`\`\`cpp
CrateWriter::Options opts;
opts.enable_compression = true;      // Enable LZ4 compression
opts.enable_async_io = true;         // Use async I/O
opts.version_minor = 8;              // Target Crate v0.8.0
writer.SetOptions(opts);
\`\`\`

## Supported Features

### Data Types

✅ All primitive types (int, float, bool, etc.)
✅ Strings, tokens, asset paths
✅ Vectors, matrices, quaternions
✅ Arrays of all types
✅ Dictionaries
✅ ListOps (all variants)
✅ TimeSamples (animation)
✅ References and Payloads

### Compression

✅ LZ4 compression for structural sections
✅ Integer delta encoding
✅ Float compression (as-integer and lookup table)

### Performance

- Write speed: Within 20% of OpenUSD
- File sizes: Match OpenUSD compression ratios
- Memory usage: Efficient deduplication

## Best Practices

### Memory Management

For large scenes, use staged writing:

\`\`\`cpp
writer.Open();

// Write in batches
for (const auto& batch : scene_batches) {
  for (const auto& spec : batch) {
    writer.AddSpec(spec.path, spec.type, spec.fields);
  }
  // Deduplication tables are maintained
}

writer.Finalize();
\`\`\`

...
```

#### Tasks

- [ ] Write comprehensive API documentation
- [ ] Create user guide with examples
- [ ] Document all supported types
- [ ] Create migration guide (from experimental to v1.0)
- [ ] Document performance characteristics
- [ ] Create troubleshooting guide
- [ ] Add inline code examples
- [ ] Generate Doxygen documentation

---

## Integration with TinyUSDZ

### High-Level Integration Plan

```cpp
// Add to tinyusdz.hh
namespace tinyusdz {

///
/// Save Stage to USDC binary file
///
/// @param stage Stage to save
/// @param filename Output file path
/// @param warn Warning messages (output)
/// @param err Error messages (output)
/// @return true on success
///
bool SaveUSDCToFile(const Stage& stage,
                    const std::string& filename,
                    std::string* warn = nullptr,
                    std::string* err = nullptr);

///
/// Save Layer to USDC binary file
///
/// @param layer Layer to save
/// @param filename Output file path
/// @param warn Warning messages (output)
/// @param err Error messages (output)
/// @return true on success
///
bool SaveLayerToUSDC(const Layer& layer,
                     const std::string& filename,
                     std::string* warn = nullptr,
                     std::string* err = nullptr);

} // namespace tinyusdz
```

### Implementation

```cpp
bool SaveUSDCToFile(const Stage& stage, const std::string& filename,
                    std::string* warn, std::string* err) {
  experimental::CrateWriter writer(filename);

  experimental::CrateWriter::Options opts;
  opts.enable_compression = true;
  opts.enable_deduplication = true;
  writer.SetOptions(opts);

  if (!writer.Open(err)) {
    return false;
  }

  // Extract root layer
  const Layer& root_layer = stage.GetRootLayer();

  // Write all prims from stage
  if (!WriteStageToWriter(stage, writer, err)) {
    return false;
  }

  if (!writer.Finalize(err)) {
    return false;
  }

  writer.Close();
  return true;
}

bool WriteStageToWriter(const Stage& stage, experimental::CrateWriter& writer, std::string* err) {
  // Traverse stage hierarchy
  std::function<bool(const Prim&)> traversePrim = [&](const Prim& prim) -> bool {
    // Convert Prim to specs
    Path prim_path = prim.GetPath();
    tcrate::FieldValuePairVector prim_fields;

    // Extract prim metadata
    if (!ExtractPrimFields(prim, prim_fields, err)) {
      return false;
    }

    // Write prim spec
    if (!writer.AddSpec(prim_path, SpecType::Prim, prim_fields, err)) {
      return false;
    }

    // Write attribute specs
    for (const auto& attr : prim.GetAttributes()) {
      Path attr_path(prim_path.prim_part(), attr.name);
      tcrate::FieldValuePairVector attr_fields;

      if (!ExtractAttributeFields(attr, attr_fields, err)) {
        return false;
      }

      if (!writer.AddSpec(attr_path, SpecType::Attribute, attr_fields, err)) {
        return false;
      }
    }

    // Write relationship specs
    for (const auto& rel : prim.GetRelationships()) {
      Path rel_path(prim_path.prim_part(), rel.name);
      tcrate::FieldValuePairVector rel_fields;

      if (!ExtractRelationshipFields(rel, rel_fields, err)) {
        return false;
      }

      if (!writer.AddSpec(rel_path, SpecType::Relationship, rel_fields, err)) {
        return false;
      }
    }

    // Recurse to children
    for (const auto& child : prim.GetChildren()) {
      if (!traversePrim(child)) {
        return false;
      }
    }

    return true;
  };

  // Start traversal from root
  return traversePrim(stage.GetPseudoRoot());
}
```

---

## Success Metrics

### Version 1.0.0 Release Criteria

#### Functionality

- [ ] All 60+ Crate data types supported
- [ ] All USD composition arcs (references, payloads, variants)
- [ ] TimeSamples for animation
- [ ] Full compression support (LZ4, integer, float)
- [ ] 100% OpenUSD file format compatibility

#### Performance

- [ ] Write speed within 20% of OpenUSD
- [ ] File sizes match OpenUSD (±5%)
- [ ] Memory usage < 2x input data size
- [ ] Handle files up to 10GB

#### Quality

- [ ] >90% code coverage
- [ ] All unit tests passing
- [ ] All integration tests passing
- [ ] Files validated by `usdchecker`
- [ ] Files readable by all major DCC tools

#### Documentation

- [ ] Complete API documentation
- [ ] User guide with examples
- [ ] Migration guide
- [ ] Performance tuning guide

---

## Timeline Summary

| Phase | Duration | Key Deliverables |
|-------|----------|------------------|
| **Phase 1: Value System** | 3 weeks | String/Token, Vectors/Matrices, Arrays |
| **Phase 2: Complex Types** | 3 weeks | Dictionary, ListOps, References/Payloads |
| **Phase 3: Animation** | 2 weeks | TimeSamples, TimeCode |
| **Phase 4: Compression** | 3 weeks | LZ4, Integer, Float compression |
| **Phase 5: Production** | 5 weeks | Testing, Optimization, Documentation |
| **Total** | **16 weeks** | Production-ready v1.0.0 |

---

## Risk Analysis

### Technical Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| LZ4 integration issues | Medium | High | Early prototyping, fallback to miniz |
| Compression ratio below OpenUSD | Low | Medium | Extensive testing, algorithm tuning |
| Performance below target | Medium | High | Profiling, optimization passes |
| Compatibility issues with OpenUSD | Low | Critical | Extensive validation testing |

### Resource Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Underestimated complexity | Medium | High | Agile approach, adjust timeline |
| Insufficient testing resources | Medium | High | Automated CI/CD, community testing |
| Documentation lag | High | Medium | Write docs alongside code |

---

## Conclusion

This plan provides a comprehensive roadmap to implement a production-ready USDC writer for TinyUSDZ. The phased approach ensures incremental progress with testable milestones at each stage. Upon completion, TinyUSDZ will have full read/write capability for USD binary files, matching OpenUSD's functionality and performance.

**Estimated Timeline**: 14-16 weeks
**Estimated Effort**: 1-2 full-time developers
**Target Release**: TinyUSDZ v1.1.0 with complete USDC write support
