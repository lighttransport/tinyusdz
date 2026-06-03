# UnregisteredValue Handling: OpenUSD vs TinyUSDZ

This document describes how "unregistered values" (properties/metadata not defined in any schema) are handled in both OpenUSD and TinyUSDZ, with emphasis on type inference rules.

## Background

In USD, properties and metadata can be either:
- **Registered**: Defined in a schema (e.g., `xformOp:translate` on an Xform prim). The type is known.
- **Unregistered**: Not in any schema. The parser must decide how to store these values without type guidance.

---

## OpenUSD Behavior

### Definition

`SdfUnregisteredValue` is defined in `pxr/usd/sdf/types.h:475-508`.

It wraps a `VtValue` that can hold one of three things:
- `std::string` — raw text representation of the value
- `VtDictionary` — fully parsed dictionary with typed sub-values
- `SdfUnregisteredValueListOp` — list operations (prepend/append/delete)

### Core Design: Unregistered Values Are Stored as Strings

When the USDA parser encounters an unregistered field, it does **not** perform type inference. Instead, it enters "string recording mode" and captures the raw text.

**Key code** (`pxr/usd/sdf/textParserHelpers.cpp:164-168`):
```cpp
else {
    // Prepare to parse only the string representation of this metadata
    // value, since it's an unregistered field.
    context.values.StartRecordingString();
}
```

The string recording mechanism (`pxr/usd/sdf/parserValueContext.cpp`) captures compound values character by character:

| Parser Event | Action |
|---|---|
| `BeginTuple()` | Appends `(` to recorded string |
| `EndTuple()` | Appends `)` to recorded string |
| `BeginList()` | Appends `[` to recorded string |
| `EndList()` | Appends `]` to recorded string |
| `AppendValue(v)` | Appends value text + comma separator |

### Examples of Unregistered Value Storage

```
customFloat = 3.14                    → SdfUnregisteredValue("3.14")
customVec   = (1.0, 2.0, 3.0)        → SdfUnregisteredValue("(1.0, 2.0, 3.0)")
customArray = [1, 2, 3]              → SdfUnregisteredValue("[1, 2, 3]")
customMat   = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))
    → SdfUnregisteredValue("((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))")
```

All stored as **plain strings**. No float/int/matrix type inference.

### The Exception: Dictionaries

Dictionaries are special because they carry inline type annotations:

```usda
customData = {
    int foo = 42
    float bar = 3.14
    double3 pos = (1, 2, 3)
}
```

When the parser encounters a dictionary inside an unregistered field, it **stops** string recording and parses the dictionary with full type information (`pxr/usd/sdf/textFileFormatParser.cpp:1470-1478`):

```cpp
if (context.values.IsRecordingString()) {
    context.values.StopRecordingString();
}
```

The resulting `VtDictionary` preserves all type information for its sub-values.

### Numeric Inference Rules (Registered Fields Only)

For **registered** fields where the schema provides a target type, OpenUSD's lexer first parses numeric literals into a variant type (`pxr/usd/sdf/parserHelpers.h:31`):

```cpp
using _Variant = std::variant<uint64_t, int64_t, double,
                              std::string, TfToken, SdfAssetPath>;
```

The inference rules in `_GetNumericValueFromString()` (`pxr/usd/sdf/textParserHelpers.cpp:471-530`):

| Condition | Inferred Variant Type |
|---|---|
| Contains `.`, `e`, or `E` | `double` |
| `-0`, `inf`, `-inf`, `nan` | `double` (special IEEE values) |
| Negative integer (starts with `-`) | `int64_t` (range: -2^63 to -1) |
| Non-negative integer | `uint64_t` (range: 0 to 2^64-1) |
| Out of int64/uint64 range | Falls back to `double` |

**Important**: OpenUSD uses **64-bit** integers in the parser variant, never 32-bit. Narrowing to the schema's target type (e.g., `int` or `uint`) happens later via `GfNumericCast<T>()`.

### Bool Inference (Registered Fields)

For registered bool fields (`pxr/usd/sdf/parserHelpers.h:157-192`):
- Numbers: nonzero → `true`, zero → `false`
- Strings (case-insensitive): `"true"`, `"yes"`, `"on"`, `"1"` → `true`; `"false"`, `"no"`, `"off"`, `"0"` → `false`

### Crate (USDC) Binary Format

Crate type IDs (`pxr/usd/usd/crateDataTypes.h:94-95`):
```cpp
xx(UnregisteredValue,       53, SdfUnregisteredValue,       false)
xx(UnregisteredValueListOp, 54, SdfUnregisteredValueListOp, false)
```

**Writing** (`pxr/usd/usd/crateFile.cpp:1437`):
```cpp
void Write(SdfUnregisteredValue const &urv) { Write(urv.GetValue()); }
```
Simply writes the inner `VtValue` (string or VtDictionary).

**Reading** (`pxr/usd/usd/crateFile.cpp:1182-1196`):
```cpp
VtValue val = Read<VtValue>();
if (val.IsHolding<string>())
    return SdfUnregisteredValue(val.UncheckedGet<string>());
if (val.IsHolding<VtDictionary>())
    return SdfUnregisteredValue(val.UncheckedGet<VtDictionary>());
if (val.IsHolding<SdfUnregisteredValueListOp>())
    return SdfUnregisteredValue(val.UncheckedGet<SdfUnregisteredValueListOp>());
```

Roundtrip is lossless — strings stay as strings, dictionaries stay as typed dictionaries.

### Writing Back to USDA

The USDA writer handles `SdfUnregisteredValue` in `Sdf_WriteSimpleField()` (`pxr/usd/sdf/fileIO_Common.h:259-280`):

```cpp
bool isUnregisteredValue = value.IsHolding<SdfUnregisteredValue>();

if (isUnregisteredValue) {
    const VtValue &boxedValue = value.Get<SdfUnregisteredValue>().GetValue();

    if (boxedValue.IsHolding<SdfUnregisteredValueListOp>()) {
        Sdf_FileIOUtility::WriteListOp(...);
    }
    else if (boxedValue.IsHolding<VtDictionary>()) {
        Sdf_FileIOUtility::WriteDictionary(out, indent, true, boxedValue.Get<VtDictionary>());
    }
    else if (boxedValue.IsHolding<std::string>()) {
        // Written verbatim — NO quoting, NO re-parsing
        Sdf_FileIOUtility::Write(out, 0, "%s\n", boxedValue.Get<std::string>().c_str());
    }
} else {
    // Regular values go through StringFromVtValue() which applies Quote()
    Sdf_FileIOUtility::Write(out, 0, "%s\n",
        Sdf_FileIOUtility::StringFromVtValue(value).c_str());
}
```

The three cases:

| Inner Type | Writer Action |
|---|---|
| `std::string` | Direct `%s` output — **unquoted, verbatim** |
| `VtDictionary` | `WriteDictionary()` with type annotations |
| `SdfUnregisteredValueListOp` | `WriteListOp()` (add/delete/prepend/append syntax) |

**Key distinction from regular strings**: Normal string values go through `StringFromVtValue()` → `Quote()` (`pxr/usd/sdf/fileIO_Common.cpp:945-1030`) which adds quotes and escapes special characters. UnregisteredValue strings bypass this entirely — the raw text goes straight to output.

**No type recovery**: The writer does NOT attempt to re-parse `"42"` back to int or `"(1, 2, 3)"` back to a vector. The exact text captured during parsing is emitted verbatim.

**Roundtrip examples**:
```
USDA input:   custom_field = 42            → parse → SdfUnregisteredValue("42")            → write → custom_field = 42
USDA input:   custom_vec = (1, 2, 3)       → parse → SdfUnregisteredValue("(1, 2, 3)")    → write → custom_vec = (1, 2, 3)
USDA input:   custom_str = "hello"         → parse → SdfUnregisteredValue("\"hello\"")     → write → custom_str = "hello"
```

Note: quoted strings in the original USDA are stored with quotes as part of the recorded string, so the quotes survive the roundtrip.

### Summary Table (OpenUSD)

| Value in USDA | Storage in SdfUnregisteredValue | Written to USDA |
|---|---|---|
| `3.14` | `string("3.14")` | `3.14` (verbatim) |
| `(1, 2, 3)` | `string("(1, 2, 3)")` | `(1, 2, 3)` (verbatim) |
| `[1, 2, 3]` | `string("[1, 2, 3]")` | `[1, 2, 3]` (verbatim) |
| `((1,0,0,0),...)` | `string("((1, 0, 0, 0), ...)")` | `((1, 0, 0, 0), ...)` (verbatim) |
| `{ int x = 1 }` | `VtDictionary` | `{ int x = 1 }` (typed dict) |
| `prepend [...]` | `SdfUnregisteredValueListOp` | `prepend [...]` (list op) |

---

## TinyUSDZ Behavior

### Key Types

**CustomDataType / Dictionary** (`src/core/meta-variable.hh`):
```cpp
class MetaVariable;
using CustomDataType = std::map<std::string, MetaVariable>;
using Dictionary = CustomDataType;  // alias to CustomDataType
```

**MetaVariable** (`src/core/meta-variable.hh`, `class MetaVariable`):
- Wraps a `value::Value` object (the accepted value-type set is broad — see "Supported types" below)
- Hardened for metadata: no `custom` keyword, no TimeSamples, no Connections, no Relationships; the value must be assigned
- Can be a string-only entry (then the name is interpreted as a comment)
- Can contain nested dictionaries

**any_value** (`src/value-types.hh`, `class any_value`):
- Purpose-built type-erased container (replaced linb::any)
- 48-byte Small Buffer Optimization (SBO)
- Direct `type_id`/`underlying_type_id` members

**Crate Type IDs** (`src/crate-format.hh`):
```
CRATE_DATA_TYPE_UNREGISTERED_VALUE = 53
CRATE_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP = 54
```

### Dictionary Parsing (USDA)

The ASCII parser handles dictionaries in `AsciiParser::ParseDictElement()` (`src/ascii-parser.cc`).

Each dictionary entry must have an **explicit type declaration**:
```usda
customData = {
    int myInt = 42
    float myFloat = 3.14
    double3 pos = (1, 2, 3)
    dictionary nested = {
        bool flag = 1
    }
}
```

Supported types (the `APPLY_TO_METAVARIABLE_TYPE` macro in `src/ascii-parser.cc`, plus a few extra cases in the `ParseDictElement` switch):
- Scalars: `bool`, `token`, `int`/`uint`, `int64`/`uint64`, `half`, `float`, `double`, `string`, `asset`
- Vector/role types: `half2-4`, `int2-4`, `uint2-4`, `float2-4`, `double2-4`, `normal3{h,f,d}`, `vector3{h,f,d}`, `point3{h,f,d}`, `color3{f,d}`/`color4{f,d}`, `texcoord2/3{h,f,d}`
- Matrices `matrix2-4{f,d}`, quaternions `quat{h,f,d}`
- Arrays of the above (with `[]` suffix)
- Nested `dictionary` (depth limited to 64)

(`timecode` is *not* among the accepted dict element types.)

### Crate Reader: Unregistered Values

(`src/crate-reader-values.cc`, `CRATE_DATA_TYPE_UNREGISTERED_VALUE` case)

The crate reader handles unregistered values based on the inner value type:
1. **STRING**: Stores as string in the value
2. **DICTIONARY**: Calls `CrateReader::ReadCustomData()` to deserialize into `CustomDataType`

```
// Source comment:
// "Should be STRING or DICTIONARY for UNREGISTERED_VALUE."
```

### Numeric Parsing

TinyUSDZ parses numbers in `src/ascii-parser-basetype-impl.inc`:
- `parseInt()`: Integer parsing with overflow checks
- `ParseFloat()`: Uses `fast_float::from_chars()`
- `ParseDouble()`: Uses `fast_float::from_chars()`

Float detection (`src/ascii-parser.cc`, the token-reading number path):
- Contains `.` → float/double
- Contains `e` or `E` → float/double (exponential notation)

**Key difference**: TinyUSDZ does NOT infer types from literal values. Types must always be explicitly declared in USDA syntax for dictionary/metadata values.

### Writers

**USDA Writer** (`print_customData()` in `src/pprint-meta.cc`):
- Writes customData from the `CustomDataType` map with type annotations preserved

**Crate Writer** (`src/crate-writer.cc` + `src/crate-writer-values.cc`):
- Serializes `CustomDataType` like a dictionary, with per-entry `value::Value` type dispatch (value packing in `crate-writer-values.cc`)

---

## Comparison

| Aspect | OpenUSD | TinyUSDZ |
|---|---|---|
| **Unregistered non-dict values** | Stored as raw string | Stored as string (via crate reader) |
| **Dictionary values** | Fully typed via `VtDictionary` | Fully typed via `CustomDataType` (map of MetaVariable) |
| **Type inference for unreg. fields** | None (string recording) | None (explicit type required) |
| **Parser numeric variant** | `variant<uint64_t, int64_t, double, ...>` | Separate `parseInt`/`ParseFloat`/`ParseDouble` functions |
| **Integer width in parser** | 64-bit (`int64_t`/`uint64_t`) | Per declared dict type: 32-bit (`int`/`uint`) or 64-bit (`int64`/`uint64`) |
| **Dict sub-value types** | Broad (most USD types) | Broad (numeric, vector, matrix, quat, role types; nested dict) — see list above; no `timecode` |
| **Type erasure** | `VtValue` (boost/std any) | `any_value` (48-byte SBO, purpose-built) |
| **Crate type ID** | 53 (UnregisteredValue) | 53 (same) |
| **Security model** | General purpose | `MetaVariable` disallows TimeSamples / Connections / Relationships / `custom`; value must be assigned |

### Key Takeaways

1. **Neither library infers types for unregistered values** — OpenUSD stores them as strings, TinyUSDZ behaves similarly for crate-sourced unregistered values.

2. **Dictionaries are the exception** in both libraries — they carry inline type annotations, so sub-values are fully typed.

3. **OpenUSD's numeric inference (int64/uint64/double)** only applies to registered fields during lexing. The 32-bit narrowing happens later via `GfNumericCast` based on the schema type.

4. **TinyUSDZ's `MetaVariable` constraints** (no TimeSamples/Connections/Relationships, no `custom`, value-must-be-assigned) are a deliberate hardening choice for metadata, not a limitation to fix. The accepted *value* types for dictionary entries are broad.
