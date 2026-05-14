# How to Add a New USD Schema / Prim Type

This document is the step-by-step procedure for adding support for a new USD schema to tinyusdz. It covers research, implementation, testing, and common pitfalls.

The Physics (`src/usdPhysics.hh`, `src/prim-reconstruct-physics.cc`, `src/pprint-physics.cc`, `src/sconv-physics.cc`) and AR (`src/usdAR.hh`, `src/prim-reconstruct-ar.cc`, `src/pprint-ar.cc`, `src/sconv-ar.cc`) implementations are the canonical references.

---

## 1. Research the Schema

Before writing any code, gather the exact property definitions.

### Where to look

| Source | What you get | Path / URL |
|--------|-------------|------------|
| **OpenUSD codebase** | Authoritative schema `.usda` with property names, types, defaults, allowed tokens | `pxr/usd/<domain>/schema.usda` |
| **Apple USDZ schemas** | Preliminary_* AR schemas | `schema/usdz/UsdInteractive/schema.usda`, `schema/usdz/UsdPhysics/schema.usda` in this repo |
| **OpenUSD docs** | Human-readable API docs | `openusd.org/dev/api/` |
| **Apple Developer docs** | AR schema semantics | `developer.apple.com/documentation/arkit/usdz_schemas_for_ar` |
| **AOUSD specs** | Emerging standards | `aousd.org` |

### What to extract

For each property in the schema `.usda`, note:
- **USD attribute name** (e.g. `physics:gravitationalForce:acceleration`)
- **USD type** (e.g. `uniform vector3d`) -- this determines the C++ type
- **Default value** (e.g. `(0, -9.81, 0)`)
- **Allowed tokens** (e.g. `["plane", "image", "face", "none"]`)
- **Whether it is a relationship** (`rel`) vs attribute

### Check existing support

```bash
grep -r "MyTypeName" src/
```

Partial stubs may already exist (e.g. old bare structs in `src/core/model-scope.hh`). These need to be replaced with properly typed versions.

---

## 2. Identify Schema Category

| Category | Example | Has TYPE_ID? | Has prim boilerplate? | Stored where? |
|----------|---------|-------------|----------------------|---------------|
| **Typed schema** (standalone prim) | `PhysicsScene`, `SpatialAudio` | Yes | Yes | Own prim node in scene graph |
| **API schema** (applied to host prim) | `PhysicsRigidBodyAPI`, `Preliminary_AnchoringAPI` | No | No | Host prim's generic `props` map; detected via `apiSchemas` metadata |
| **Multi-apply API schema** | `PhysicsDriveAPI:rotX` | No | No | `std::map<std::string, ApiStruct>` on host prim |

**Typed schema prim boilerplate** (every typed schema struct must have these members):

```cpp
std::string name;
Specifier spec{Specifier::Def};
int64_t parent_id{-1};

void set_name(const std::string &name_) { name = name_; }
const std::string &get_name() const { return name; }
Specifier &specifier() { return spec; }
const Specifier &specifier() const { return spec; }

// ... schema-specific attributes ...

std::pair<ListEditQual, std::vector<Reference>> references;
std::pair<ListEditQual, std::vector<Payload>> payload;
std::map<std::string, VariantSet> variantSet;
std::map<std::string, Property> props;

PrimMeta meta;
PrimMeta &metas() { return meta; }
const PrimMeta &metas() const { return meta; }

const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
const std::vector<value::token> &propertyNames() const { return _properties; }
std::vector<value::token> &primChildrenNames() { return _primChildren; }
std::vector<value::token> &propertyNames() { return _properties; }

private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
```

---

## 3. USD-to-C++ Type Mapping

Match USD types exactly. Using the wrong C++ type causes "Property type mismatch" errors at parse time.

| USD type in schema `.usda` | C++ type in tinyusdz |
|---------------------------|---------------------|
| `bool` | `bool` |
| `int` | `int` |
| `float` | `float` |
| `double` | `double` |
| `token` | `value::token` |
| `string` | `std::string` |
| `string[]` | `std::vector<std::string>` |
| `asset` | `value::AssetPath` |
| `float3` | `value::float3` |
| `double3` | `value::double3` |
| `vector3f` | `value::vector3f` |
| `vector3d` | `value::vector3d` |
| `point3f` | `value::point3f` |
| `point3d` | `value::point3d` |
| `quatf` | `value::quatf` |
| `timecode` | `double` (NOT `value::TimeCode`) |
| `rel` (relationship) | `RelationshipProperty` |

**Important**: `vector3d` and `double3` are distinct types in USD. `vector3d` has "vector" role semantics; `double3` is a plain tuple. Using the wrong one causes parse failures.

### Attribute wrapper types

| Pattern | C++ wrapper | When to use |
|---------|------------|-------------|
| Optional attribute, no default | `TypedAttribute<T>` | Most attributes |
| Attribute with a fallback default | `TypedAttributeWithFallback<T>` | Attributes with `= defaultValue` in schema |
| Animatable attribute | `TypedAttribute<Animatable<T>>` | Time-varying attributes |
| Relationship | `RelationshipProperty` | `rel` declarations |

---

## 4. Implementation Checklist (in dependency order)

### Phase A -- Type definitions (header)

| File | Action |
|------|--------|
| **`src/usdFoo.hh`** (new) | Define structs with typed attributes. Add `constexpr auto kFoo = "Foo";` per typed schema. Add `DEFINE_TYPE_TRAIT(Foo, kFoo, TYPE_ID_FOO, 1);` in `namespace value {}` at bottom. Include `value-types.hh`, `core/prim-enums.hh`, `core/composition-types.hh`, `core/prim-metas.hh`, `core/typed-attribute.hh`, `core/relationship.hh`, `core/property.hh`, `core/variant-types.hh`. |
| **`src/value-types.hh`** | Add `TYPE_ID_FOO` entries in enum (typed schemas only). Insert a new block between existing `_END` / `_BEGIN` markers. |
| **`src/core/model-scope.hh`** | Remove old placeholder structs if any exist. |

### Phase B -- Property tables

| File | Action |
|------|--------|
| **`src/prim-property-tables.hh`** | Add X-macro tables. One `#define FOO_TYPED_ATTRS(X)` for attributes and one `#define FOO_RELS(X)` for relationships per typed schema. Map USD attribute name string to struct member name. |

Example:
```cpp
#define FOO_TYPED_ATTRS(X) \
  X("myNamespace:myAttr", myAttr) \
  X("otherAttr", otherAttr)

#define FOO_RELS(X) \
  X("myRel", myRel)
```

### Phase C -- Parser integration

| File | Action |
|------|--------|
| **`src/prim-reconstruct-foo.cc`** (new) | `ReconstructPrim<T>` specializations. Must define `PushError`, `PushWarn`, `PUSH_WARN_F` macros. Use `EXPAND_TYPED_ATTR`, `EXPAND_SINGLE_REL`, `ADD_PROPERTY`, `PARSE_PROPERTY_END_MAKE_WARN` in the property loop. |
| **`src/usda-reader.cc`** | `#include "usdFoo.hh"`. Add 3 things: `RECONSTRUCT_PRIM_DECL(Foo)` (near line 125), `DEFINE_PRIM_TYPE(Foo, kFoo, value::TYPE_ID_FOO)` (near line 626), `RegisterReconstructCallback<Foo>()` (near line 2237). |
| **`src/usdc-reader-prim.cc`** | `#include "usdFoo.hh"`. Add `INSTANTIATE_RECONSTRUCT_PRIM(Foo)` (near line 158) and `RECONSTRUCT_PRIM(Foo, typeName, prim_name, spec)` dispatch (near line 495). |

### Phase D -- Writer integration

| File | Action |
|------|--------|
| **`src/pprint-foo.cc`** (new) | `to_string()` overloads using `PRINT_PRIM_HEADER`/`PRINT_PRIM_FOOTER`, `print_typed_attr()`, `print_rel_prop()`. |
| **`src/pprinter.hh`** | `#include "usdFoo.hh"`. Add `to_string()` declarations. |
| **`src/value-pprint.cc`** | Add typed schemas to `CASE_GPRIM_LIST` macro. |
| **`src/sconv-foo.cc`** (new) | `CrateWriter::ExtractFooProperties()` using `EXTRACT_TYPED`, `EXTRACT_FALLBACK`, `EXTRACT_TOKEN`, `EXTRACT_REL` macros. |
| **`src/crate-writer.hh`** | Add `ExtractFooProperties()` method declaration. |
| **`src/stage-converter.cc`** | Add `else if (type_name == "Foo")` dispatch entry (near line 1010). |

### Phase E -- Prim infrastructure

| File | Action |
|------|--------|
| **`src/prim-types.cc`** | `#include "usdFoo.hh"`. Add `EXTRACT_NAME_AND_RETURN_PATH(Foo)` (near line 897) and `SET_ELEMENT_NAME(elementName, Foo)` (near line 963). |

### Phase F -- Build system

| File | Action |
|------|--------|
| **`CMakeLists.txt`** | Add new `.cc` files: `sconv-foo.cc` (near line 449), `prim-reconstruct-foo.cc` (near line 459), `pprint-foo.cc` (near line 501), `tydra/foo-to-json.cc` (near line 562). |

### Phase G -- API schema enum (only for new applied API schemas)

| File | Action |
|------|--------|
| **`src/core/composition-types.hh`** | Add to `APISchemas::APIName` enum. |
| **`src/enum-handlers.cc`** | Add string-to-enum mapping. |
| **`src/pprint-enum.cc`** | Add print case. |

### Phase H -- Tydra utilities (optional)

| File | Action |
|------|--------|
| **`src/tydra/foo-to-json.hh`** / **`.cc`** | JSON export. Use `TraversePrims()` + `prim.as<Foo>()`. Follow `physics-to-json.cc`. |

---

## 5. Create Test Files

Place USDA test fixtures in `tests/usda/`. These are **auto-discovered** by:
- `usda-parser-unit-test` -- tries to parse every `tests/usda/*.usda`
- `usda-roundtrip-test` -- parse -> print -> reparse -> compare
- `usdc-roundtrip-test` -- parse -> write USDC -> reparse -> compare

Create at least:
```
tests/usda/foo-basic.usda        # Typed prim with all attributes set
tests/usda/foo-api-schema.usda   # Host prim with applied API schema (if applicable)
```

**Note**: `tests/usda/*.usda` may be gitignored. Use `git add -f` to track new test files.

### Test file template

```usda
#usda 1.0
(
    defaultPrim = "Root"
    upAxis = "Y"
)

def Xform "Root"
{
    def MyTypeName "MyPrim"
    {
        token myAttr = "value"
        float myFloat = 1.5
        rel myRel = </Root/OtherPrim>
    }
}
```

---

## 6. Build and Verify

```bash
# Build
cd build && cmake .. -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
make -j16

# All tests must pass
ctest --output-on-failure

# Double-roundtrip verification (output must be empty)
./build/tusdcat tests/usda/foo-basic.usda > /tmp/r1.usda
./build/tusdcat /tmp/r1.usda > /tmp/r2.usda
diff /tmp/r1.usda /tmp/r2.usda

# Individual test suites
ctest -R usda-parser-unit-test --output-on-failure   # Parse test
ctest -R usda-roundtrip-test --output-on-failure      # USDA roundtrip
ctest -R usdc-roundtrip-test --output-on-failure      # USDC roundtrip
ctest -R unit-test-tinyusdz --output-on-failure       # Unit tests
```

---

## 7. Common Pitfalls

| Problem | Symptom | Fix |
|---------|---------|-----|
| Wrong C++ type for USD type | "Property type mismatch. X expects type `float3` but defined as type `vector3f`" | Check the schema `.usda` for the exact USD type and use the matching C++ type from the mapping table above |
| `print_typed_attr` for `std::string` | Strings printed without quotes, roundtrip parse fails | Use `quote()` from `str-util.hh` manually for string attributes in `pprint-*.cc` |
| `EXTRACT_FALLBACK` for `std::string` in USDC writer | USDC roundtrip fails: "expects type `string` but defined as type `uint`" | Use `CrateValue::Set(value)` directly instead of the `EXTRACT_FALLBACK` macro |
| Missing `PUSH_WARN_F` macro | Compile error: "no arguments to 'PUSH_WARN_F' that depend on a template parameter" | Add `#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))` in the reconstruct `.cc` file |
| Using `value::TimeCode` for timecode attributes | Compile error: "incomplete type `TypeTraits<TimeCode>`" | Use `double` for timecode-valued attributes |
| Adding TYPE_ID for API schemas | Unnecessary, wastes ID space | Only typed schemas (standalone prims) need TYPE_ID entries |
| Test file gitignored | `git add` silently skips the file | Use `git add -f tests/usda/foo.usda` |

---

## 8. Reference Implementations

| Domain | Header | Reconstruct | PPrint | USDC Extract | Tydra |
|--------|--------|-------------|--------|-------------|-------|
| Physics | `src/usdPhysics.hh` | `src/prim-reconstruct-physics.cc` | `src/pprint-physics.cc` | `src/sconv-physics.cc` | `src/tydra/physics-to-json.cc` |
| AR/Interactive | `src/usdAR.hh` | `src/prim-reconstruct-ar.cc` | `src/pprint-ar.cc` | `src/sconv-ar.cc` | `src/tydra/ar-to-json.cc` |
| Media | `src/usdMedia.hh` | `src/prim-reconstruct-media.cc` | `src/pprint-media.cc` | `src/sconv-media.cc` | -- |
| Geometry | `src/usdGeom.hh` | `src/prim-reconstruct-geom.cc` | `src/pprint-geom.cc` | `src/sconv-geom.cc` | `src/tydra/render-data.cc` |
| Shader | `src/usdShade.hh` | `src/prim-reconstruct-shader.cc` | `src/pprint-shader.cc` | `src/sconv-shader.cc` | `src/tydra/shader-network.cc` |
