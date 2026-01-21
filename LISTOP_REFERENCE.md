# TinyUSDZ ListOp and Custom Attribute Types - Complete Reference

## 1. ListOp<T> Type Definition

### Location
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 1703-1764)

### Class Definition
```cpp
template <typename T>
class ListOp {
 public:
  ListOp() : is_explicit(false) {}

  void ClearAndMakeExplicit() {
    explicit_items.clear();
    added_items.clear();
    prepended_items.clear();
    appended_items.clear();
    deleted_items.clear();
    ordered_items.clear();
    is_explicit = true;
  }

  // Query methods
  bool IsExplicit() const { return is_explicit; }
  bool HasExplicitItems() const { return explicit_items.size(); }
  bool HasAddedItems() const { return added_items.size(); }
  bool HasPrependedItems() const { return prepended_items.size(); }
  bool HasAppendedItems() const { return appended_items.size(); }
  bool HasDeletedItems() const { return deleted_items.size(); }
  bool HasOrderedItems() const { return ordered_items.size(); }

  // Getter methods
  const std::vector<T> &GetExplicitItems() const { return explicit_items; }
  const std::vector<T> &GetAddedItems() const { return added_items; }
  const std::vector<T> &GetPrependedItems() const { return prepended_items; }
  const std::vector<T> &GetAppendedItems() const { return appended_items; }
  const std::vector<T> &GetDeletedItems() const { return deleted_items; }
  const std::vector<T> &GetOrderedItems() const { return ordered_items; }

  // Setter methods
  void SetExplicitItems(const std::vector<T> &v) { explicit_items = v; }
  void SetAddedItems(const std::vector<T> &v) { added_items = v; }
  void SetPrependedItems(const std::vector<T> &v) { prepended_items = v; }
  void SetAppendedItems(const std::vector<T> &v) { appended_items = v; }
  void SetDeletedItems(const std::vector<T> &v) { deleted_items = v; }
  void SetOrderedItems(const std::vector<T> &v) { ordered_items = v; }

 private:
  bool is_explicit{false};
  std::vector<T> explicit_items;
  std::vector<T> added_items;
  std::vector<T> prepended_items;
  std::vector<T> appended_items;
  std::vector<T> deleted_items;
  std::vector<T> ordered_items;
};
```

### ListOpHeader Structure
**Location:** Lines 1766-1801 (companion metadata structure)

```cpp
struct ListOpHeader {
  enum Bits {
    IsExplicitBit = 1 << 0,
    HasExplicitItemsBit = 1 << 1,
    HasAddedItemsBit = 1 << 2,
    HasDeletedItemsBit = 1 << 3,
    HasOrderedItemsBit = 1 << 4,
    HasPrependedItemsBit = 1 << 5,
    HasAppendedItemsBit = 1 << 6
  };
  // ... bit flag queries
};
```

---

## 2. ListEditQual Enum (List Edit Qualifiers)

### Location
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 271-279)

### Definition
```cpp
enum class ListEditQual {
  ResetToExplicit,  // "unqualified" (no qualifier, explicit reset)
  Append,           // "append" - add to end
  Add,              // "add" - add without order guarantee
  Delete,           // "delete" - remove items
  Prepend,          // "prepend" - add to beginning
  Order,            // "order" - reorder items
  Invalid
};
```

### Usage Pattern
ListEditQual is used with composition-related fields to indicate how list operations should be applied:
- **References:** `optional<pair<ListEditQual, vector<Reference>>>`
- **Payloads:** `optional<pair<ListEditQual, vector<Payload>>>`
- **Inherits:** `optional<pair<ListEditQual, vector<Path>>>`
- **Specializes:** `optional<pair<ListEditQual, vector<Path>>>`
- **VariantSets:** `optional<pair<ListEditQual, vector<string>>>`

---

## 3. Supported ListOp<T> Types

### Location
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/crate-format.hh` (lines 450-458)

The following types can be used as template arguments to ListOp:

```cpp
ListOp<value::token>
ListOp<std::string>
ListOp<Path>
ListOp<Reference>
ListOp<int32_t>
ListOp<uint32_t>
ListOp<int64_t>
ListOp<uint64_t>
ListOp<Payload>
```

---

## 4. Creating and Using ListOp Attributes - Practical Examples

### Example 1: Connection Paths (Explicit List)
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/stage-converter.cc` (lines 2226-2232)

```cpp
// Create a ListOp<Path> for attribute connections
ListOp<Path> connection_paths_listop;
connection_paths_listop.ClearAndMakeExplicit();
connection_paths_listop.SetExplicitItems(connections);

crate::CrateValue conn_value;
conn_value.Set(connection_paths_listop);
fields.push_back({"connectionPaths", conn_value});
```

### Example 2: References with ListEditQual (Dynamic Behavior)
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/stage-converter.cc` (lines 3631-3660)

```cpp
ListOp<Reference> ref_listop;

// Set based on the composition qualification
switch (qual) {
  case ListEditQual::ResetToExplicit:
    ref_listop.ClearAndMakeExplicit();
    ref_listop.SetExplicitItems(ref_list);
    break;
  case ListEditQual::Append:
    ref_listop.SetAppendedItems(ref_list);
    break;
  case ListEditQual::Prepend:
    ref_listop.SetPrependedItems(ref_list);
    break;
  case ListEditQual::Add:
    ref_listop.SetAddedItems(ref_list);
    break;
  case ListEditQual::Delete:
    ref_listop.SetDeletedItems(ref_list);
    break;
  default:
    ref_listop.ClearAndMakeExplicit();
    ref_listop.SetExplicitItems(ref_list);
    break;
}

crate::CrateValue ref_value;
ref_value.Set(ref_listop);
fields.push_back({"references", ref_value});
```

### Example 3: Payloads
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/stage-converter.cc` (lines 3673-3679)

```cpp
ListOp<Payload> payload_listop;

switch (qual) {
  case ListEditQual::ResetToExplicit:
    payload_listop.ClearAndMakeExplicit();
    payload_listop.SetExplicitItems(payload_list);
    break;
  // ... other cases
}

crate::CrateValue payload_value;
payload_value.Set(payload_listop);
fields.push_back({"payload", payload_value});
```

---

## 5. Dictionary Attribute Support

### Overview
Dictionary (aka CustomDataType) is used for metadata and custom data that can contain mixed types.

### Type Definition
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 758-763)

```cpp
// Dictionary is an alias for CustomDataType
using CustomDataType = std::map<std::string, MetaVariable>;
using Dictionary = CustomDataType;
```

### MetaVariable Class
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 789-901)

MetaVariable is a type-erased value container that can hold any supported USD type:

```cpp
class MetaVariable {
 public:
  // Constructors
  MetaVariable() = default;
  template <typename T>
  MetaVariable(const T &v) { set_value(v); }
  template <typename T>
  MetaVariable(const std::string &name, const T &v) { set_value(name, v); }

  // Value operations
  template <typename T>
  void set_value(const T &v) {
    _value = v;
    _name = std::string();  // empty
  }

  template <typename T>
  void set_value(const std::string &name, const T &v) {
    _value = v;
    _name = name;
  }

  template <typename T>
  bool get_value(T *dst) const {
    if (const T *v = _value.as<T>()) {
      (*dst) = *v;
      return true;
    }
    return false;
  }

  template <typename T>
  nonstd::optional<T> get_value() const {
    if (const T *v = _value.as<T>()) {
      return *v;
    }
    return nonstd::nullopt;
  }

  // Metadata
  void set_name(const std::string &name) { _name = name; }
  const std::string &get_name() const { return _name; }
  const std::string type_name() const { return TypeName(*this); }
  uint32_t type_id() const { return TypeId(*this); }
  bool is_blocked() const { return type_id() == value::TYPE_ID_VALUEBLOCK; }

 private:
  value::Value _value;  // Type-erased container
  std::string _name;
};
```

### Using Dictionary in Prim Metadata
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 990-1007)

```cpp
struct PrimMetas {
  nonstd::optional<Dictionary> assetInfo;    // 'assetInfo'
  nonstd::optional<Dictionary> customData;   // 'customData'
  nonstd::optional<Dictionary> sdrMetadata;  // 'sdrMetadata'
  nonstd::optional<Dictionary> clips;        // 'clips'
  Dictionary meta;                           // other metadata values
  // ...
};
```

### Using Dictionary in Attribute Metadata
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 1100-1110)

```cpp
struct AttrMetas {
  nonstd::optional<Dictionary> customData;   // 'customData'
  nonstd::optional<Dictionary> sdrMetadata;  // 'sdrMetadata'
  std::map<std::string, MetaVariable> meta;  // other metadata
};
```

### Creating Dictionary Entries
```cpp
Dictionary customData;

// Add a float value
customData["myFloat"] = MetaVariable(3.14f);

// Add a string value
customData["myString"] = MetaVariable("hello");

// Add an int value
customData["myInt"] = MetaVariable(42);

// Add with explicit names
customData.insert({"key1", MetaVariable("key1", value::token("myToken"))});
```

---

## 6. Complete List of Supported Value Types for Attributes/Metadata

### Scalar Types
- `bool`
- `int`, `int32_t`, `int64_t`
- `uint32_t`, `uint64_t`
- `float`, `double`
- `half`
- `char`, `char2`, `char3`, `char4` (signed 8-bit integers)
- `uchar`, `uchar2`, `uchar3`, `uchar4` (unsigned 8-bit integers)
- `short`, `short2`, `short3`, `short4` (signed 16-bit integers)
- `ushort`, `ushort2`, `ushort3`, `ushort4` (unsigned 16-bit integers)

### Vector/Matrix Types
- `float2`, `float3`, `float4`
- `double2`, `double3`, `double4`
- `half2`, `half3`, `half4`
- `int2`, `int3`, `int4`
- `uint2`, `uint3`, `uint4`
- `matrix2f`, `matrix3f`, `matrix4f`
- `matrix2d`, `matrix3d`, `matrix4d`

### Specialized Vector Types
- `vector3f`, `vector3d`, `vector3h` (geometric vectors)
- `vector4f`, `vector4d`, `vector4h`
- `point3f`, `point3d`, `point3h` (positions)
- `normal3f`, `normal3d`, `normal3h` (normals)
- `color3f`, `color3d`, `color3h` (colors)
- `color4f`, `color4d`, `color4h`
- `texcoord2f`, `texcoord2d`, `texcoord2h` (texture coordinates)
- `texcoord3f`, `texcoord3d`, `texcoord3h`
- `quath`, `quatf`, `quatd` (quaternions)

### String/Path Types
- `token` (lightweight string identifier)
- `string` (full UTF-8 string)
- `Path` (USD object path)
- `asset` (asset path)

### Special Types
- `timecode` (time value)
- `dictionary` (nested key-value pairs)
- `None` (ValueBlock - represents unset/blocked value)

### Array Types
Arrays are supported via TypedArray templates, using the same base types with `[]` suffix in USDA format:
- `float[]`, `double[]`, `int[]`, etc.
- Represented internally with TypedArray<T> in C++

---

## 7. Reference and Payload Structures

### Reference Structure
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 967-973)

```cpp
struct Reference {
  value::AssetPath asset_path;    // Path to referenced file
  Path prim_path;                 // Path within the file
  LayerOffset layerOffset;        // Time/frame offset
  Dictionary customData;          // Additional metadata
};
```

### Payload Structure
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 975-987)

```cpp
struct Payload {
  value::AssetPath asset_path;    // Path to payload file
  Path prim_path;                 // Path within the file
  LayerOffset layerOffset;        // Time/frame offset
  // Note: No customData for Payload
};
```

### LayerOffset Structure
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 961-965)

```cpp
struct LayerOffset {
  double _offset{0.0};            // Time offset
  double _scale{1.0};             // Time scale factor
};
```

---

## 8. Attribute Class

### Basic Attribute Construction
**File:** `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 2152-2247)

```cpp
// Create attribute from typed value
template <typename T>
Attribute(const T &v, bool varying = true) {
  set_value(v);
  variability() = varying ? Variability::Varying : Variability::Uniform;
}

// Create uniform attribute
template <typename T>
static Attribute Uniform(const T &v) {
  Attribute attr;
  attr.set_value(v);
  attr.variability() = Variability::Uniform;
  return attr;
}

// Set value
template <typename T>
void set_value(const T &v) {
  if (_type_name.empty()) {
    _type_name = value::TypeTraits<T>::type_name();
  }
  _var.set_value(v);
}
```

### Attribute Variability
```cpp
enum class Variability {
  Varying,   // Changes per frame (default)
  Uniform,   // Constant over time
  Config,    // Configuration value
  Invalid
};
```

---

## 9. Practical Pattern Summary

### Pattern 1: Create a Simple ListOp (Explicit)
```cpp
ListOp<value::token> my_listop;
my_listop.ClearAndMakeExplicit();
std::vector<value::token> items = {value::token("item1"), value::token("item2")};
my_listop.SetExplicitItems(items);
```

### Pattern 2: Create a ListOp (Append Operation)
```cpp
ListOp<value::token> my_listop;
std::vector<value::token> to_append = {value::token("new_item")};
my_listop.SetAppendedItems(to_append);
```

### Pattern 3: Create a Dictionary Attribute
```cpp
Dictionary custom_data;
custom_data["version"] = MetaVariable("version", 1);
custom_data["author"] = MetaVariable("author", value::token("john"));
custom_data["metadata"] = MetaVariable("metadata", 3.14f);

nonstd::optional<Dictionary> opt_dict = custom_data;
prim_metas.customData = opt_dict;
```

### Pattern 4: Using ListOp in Composition (References)
```cpp
std::vector<Reference> refs = {
  Reference{value::AssetPath("ref.usd"), Path("/ref_prim"), LayerOffset()}
};

ListOp<Reference> ref_op;
// Using prepend for composition
ref_op.SetPrependedItems(refs);

// Or explicit (reset) for complete specification
ref_op.ClearAndMakeExplicit();
ref_op.SetExplicitItems(refs);
```

---

## 10. Key Files for Reference

1. **`src/prim-types.hh`** (primary) - ListOp, ListEditQual, Dictionary, MetaVariable definitions
2. **`src/value-types.hh`** - Value type definitions and type traits
3. **`src/crate-format.hh`** - Crate binary format serialization support
4. **`src/stage-converter.cc`** - Real-world examples of ListOp creation
5. **`src/crate-reader.cc`** - Reading ListOp structures from crate files
6. **`src/crate-writer.cc`** - Writing ListOp structures to crate files

---

## 11. Important Notes

### ListOp vs Attribute Constraints
- **Attributes cannot have ListEdit qualifiers** - Attributes use explicit values only
- **Composition fields (references, payloads, etc.) use ListOp with qualifiers**
- Only composition-related fields support Prepend, Append, Add, Delete operations

### Dictionary Support
- Dictionaries store `MetaVariable` instances (type-erased containers)
- Primarily used for metadata and customData fields
- Support optional initialization with `nonstd::optional<Dictionary>`

### Type Safety
- ListOp is a template class supporting only specific types (see section 3)
- MetaVariable uses type-erased Value container internally
- Type checking happens at runtime for dictionary/metadata operations

### Performance Considerations
- ordered_dict in prim-types preserves insertion order for metadata
- Dictionary is a std::map, so keys are sorted alphabetically
- ListOp maintains separate vectors for different operation types (explicit, add, append, etc.)
