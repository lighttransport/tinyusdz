# PrimSpec JavaScript Functions Demo

This demonstrates the new JavaScript functions that have been added to the TinyUSDZ JavaScript scripting interface for working with PrimSpecs:

1. `findPrimSpecByPath()` - Search for PrimSpec by absolute path
2. `getPrimSpecMetadata()` - Get PrimSpec metadata by absolute path

## Function Overview

### 1. findPrimSpecByPath(pathString)

Searches for and retrieves basic PrimSpec information by absolute path string.

**Function Signature:**
```javascript
findPrimSpecByPath(pathString) -> PrimSpec object | null
```

- **Parameters**: 
  - `pathString` (string): Absolute USD path string (e.g., "/root/child/prim")
- **Returns**: 
  - PrimSpec object if found
  - `null` if not found or invalid path

### 2. getPrimSpecMetadata(pathString)

Retrieves detailed metadata information for a PrimSpec by absolute path string.

**Function Signature:**
```javascript
getPrimSpecMetadata(pathString) -> Metadata object | null
```

- **Parameters**: 
  - `pathString` (string): Absolute USD path string (e.g., "/root/child/prim")
- **Returns**: 
  - Metadata object if found
  - `null` if not found or invalid path

### PrimSpec Object Structure
When found, the function returns a JSON object with the following structure:
```javascript
{
  "name": "primName",           // Name of the PrimSpec
  "typeName": "Mesh",          // Type name (e.g., "Mesh", "Xform", etc.)
  "specifier": "def",          // Specifier: "def", "over", "class", or "invalid"
  "propertyCount": 5,          // Number of properties
  "childrenCount": 2,          // Number of child PrimSpecs
  "propertyNames": [           // Array of property names
    "points",
    "faceVertexIndices",
    "extent"
  ],
  "childrenNames": [           // Array of child PrimSpec names
    "material",
    "childPrim"
  ]
}
```

### Metadata Object Structure
When found, the `getPrimSpecMetadata()` function returns a JSON object with the following structure:
```javascript
{
  "active": true,                    // Active state (true/false/null)
  "hidden": false,                   // Hidden state (true/false/null)
  "instanceable": null,              // Instanceable state (true/false/null)
  "kind": "component",               // USD Kind (string)
  "documentation": "This is a mesh",  // Documentation (string/null)
  "comment": "Auto-generated",       // Comment (string/null)
  "displayName": "My Mesh",          // Display name (string/null)
  "sceneName": "Scene1",             // Scene name (string/null)
  "hasReferences": true,             // Whether prim has references
  "referencesCount": 2,              // Number of references
  "hasPayload": false,               // Whether prim has payload
  "payloadCount": 0,                 // Number of payloads
  "hasInherits": false,              // Whether prim has inherits
  "inheritsCount": 0,                // Number of inherits
  "hasVariants": true,               // Whether prim has variants
  "variantsCount": 2,                // Number of variant selections
  "variantNames": ["modelingVariant", "shadingVariant"],  // Variant names
  "hasVariantSets": true,            // Whether prim has variant sets
  "variantSetsCount": 1,             // Number of variant sets
  "variantSetNames": ["material"],   // Variant set names
  "hasCustomData": true,             // Whether prim has custom data
  "hasAssetInfo": false,             // Whether prim has asset info
  "unregisteredMetasCount": 3,       // Number of unregistered metadata
  "unregisteredMetaNames": ["myCustomMeta1", "myCustomMeta2"],  // Custom metadata names
  "authored": true                   // Whether metadata is authored
}
```

## Usage Example

```javascript
// Search for a specific PrimSpec
var rootPrim = findPrimSpecByPath("/Root");
if (rootPrim) {
    console.log("Found root prim:", rootPrim.name);
    console.log("Type:", rootPrim.typeName);
    console.log("Properties:", rootPrim.propertyNames);
    console.log("Children:", rootPrim.childrenNames);
} else {
    console.log("Root prim not found");
}

// Get metadata for the same PrimSpec
var rootMeta = getPrimSpecMetadata("/Root");
if (rootMeta) {
    console.log("Root prim is active:", rootMeta.active);
    console.log("Kind:", rootMeta.kind);
    console.log("Has references:", rootMeta.hasReferences);
    console.log("Variant sets:", rootMeta.variantSetNames);
    
    if (rootMeta.documentation) {
        console.log("Documentation:", rootMeta.documentation);
    }
    
    if (rootMeta.unregisteredMetasCount > 0) {
        console.log("Custom metadata:", rootMeta.unregisteredMetaNames);
    }
} else {
    console.log("Root prim metadata not found");
}

// Search for a nested PrimSpec and its metadata
var meshPrim = findPrimSpecByPath("/Root/Geometry/Mesh");
var meshMeta = getPrimSpecMetadata("/Root/Geometry/Mesh");

if (meshPrim && meshMeta) {
    console.log("Found mesh with", meshPrim.propertyCount, "properties");
    console.log("Mesh is instanceable:", meshMeta.instanceable);
    console.log("Mesh variants:", meshMeta.variantNames);
} else {
    console.log("Mesh or its metadata not found");
}

// Example: Check if a prim has composition arcs
var composedPrimMeta = getPrimSpecMetadata("/ComposedPrim");
if (composedPrimMeta) {
    var hasComposition = composedPrimMeta.hasReferences || 
                        composedPrimMeta.hasPayload || 
                        composedPrimMeta.hasInherits;
    
    if (hasComposition) {
        console.log("Prim has composition arcs:");
        if (composedPrimMeta.hasReferences) {
            console.log("- References:", composedPrimMeta.referencesCount);
        }
        if (composedPrimMeta.hasPayload) {
            console.log("- Payloads:", composedPrimMeta.payloadCount);
        }
        if (composedPrimMeta.hasInherits) {
            console.log("- Inherits:", composedPrimMeta.inheritsCount);
        }
    }
}
```

## C++ Integration

To use these functions from C++, you need to:

1. Create or load a USD Layer
2. Use `RunJSScriptWithLayer()` to execute JavaScript code with access to the Layer

```cpp
#include "src/tydra/js-script.hh"

// Assuming you have a Layer object
tinyusdz::Layer layer;
// ... populate layer with PrimSpecs ...

std::string jsCode = R"(
    // Find PrimSpec basic info
    var prim = findPrimSpecByPath("/myPrim");
    if (prim) {
        console.log("Found:", prim.name, "type:", prim.typeName);
        console.log("Properties:", prim.propertyCount);
        console.log("Children:", prim.childrenCount);
    }
    
    // Get detailed metadata
    var meta = getPrimSpecMetadata("/myPrim");
    if (meta) {
        console.log("Active:", meta.active);
        console.log("Kind:", meta.kind);
        console.log("Has composition:", 
            meta.hasReferences || meta.hasPayload || meta.hasInherits);
        
        if (meta.hasVariants) {
            console.log("Variants:", meta.variantNames);
        }
    }
)";

std::string err;
bool success = tinyusdz::tydra::RunJSScriptWithLayer(jsCode, &layer, err);
if (!success) {
    std::cerr << "JavaScript error: " << err << std::endl;
}
```

## Implementation Details

Both functions are implemented in `src/tydra/js-script.cc` and include:

### Common Features:
- **Path validation**: Ensures the input string is a valid USD path
- **Layer search**: Uses the existing `Layer::find_primspec_at()` method
- **Error handling**: Returns `null` for invalid paths or missing PrimSpecs

### findPrimSpecByPath():
- **JSON conversion**: Converts basic PrimSpec data to JSON
- **Structure info**: Provides name, type, properties, and children info

### getPrimSpecMetadata():
- **Comprehensive metadata**: Extracts all USD metadata fields
- **Composition info**: Details about references, payloads, inherits
- **Variant info**: Information about variants and variant sets
- **Custom metadata**: Handles unregistered metadata fields
- **Boolean flags**: Convenient flags for common checks

## Requirements

- TinyUSDZ must be built with `TINYUSDZ_WITH_QJS=ON` to enable QuickJS support
- The functions are only available when using `RunJSScriptWithLayer()`

## Error Cases

Both functions return `null` in these cases:
- Invalid path string (empty, malformed)
- Path not found in the Layer
- Relative paths (not yet supported)
- No Layer context available

## Metadata Fields Reference

The `getPrimSpecMetadata()` function provides access to the following USD metadata:

### Core Metadata:
- `active` - Whether the prim is active in the scene
- `hidden` - Whether the prim is hidden from traversal
- `instanceable` - Whether the prim can be instanced
- `kind` - USD Kind classification (model, component, assembly, etc.)

### Documentation:
- `documentation` - Formal documentation string
- `comment` - Informal comment string
- `displayName` - Human-readable display name (extension)
- `sceneName` - Scene name (USDZ extension)

### Composition Arcs:
- `hasReferences` / `referencesCount` - Reference composition
- `hasPayload` / `payloadCount` - Payload composition  
- `hasInherits` / `inheritsCount` - Inheritance composition

### Variants:
- `hasVariants` / `variantsCount` / `variantNames` - Variant selections
- `hasVariantSets` / `variantSetsCount` / `variantSetNames` - Variant set definitions

### Custom Data:
- `hasCustomData` - Whether prim has custom data dictionary
- `hasAssetInfo` - Whether prim has asset info dictionary
- `unregisteredMetasCount` / `unregisteredMetaNames` - Custom metadata fields
- `authored` - Whether any metadata is authored