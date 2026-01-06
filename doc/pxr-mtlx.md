# MaterialX Parser Logic Summary

This document summarizes the node graph parsing logic in `pxr/usd/usdMtlx/parser.cpp`.

## Overview

The parser converts MaterialX node definitions into USD's Sdr (Shader Definition Registry) shader nodes. It implements `SdrParserPlugin` to integrate with USD's shader discovery system.

## Key Components

### 1. ShaderBuilder (lines 78-136)

A builder pattern class that accumulates data for constructing `SdrShaderNode`:

- **Fields**: `discoveryResult`, `valid`, `definitionURI`, `implementationURI`, `context`, `properties`, `metadata`
- **AddPropertyNameRemapping()**: Maps MaterialX property names to USD names
- **AddProperty()**: Converts MaterialX typed elements to `SdrShaderProperty`
- **Build()**: Creates the final `SdrShaderNodeUniquePtr`

### 2. Property Parsing (AddProperty, lines 224-391)

Converts MaterialX inputs/outputs to Sdr properties:

1. **Type Resolution**: Maps MaterialX types to USD Sdf types via `UsdMtlxGetUsdType()`
2. **Default Values**: Extracts via `UsdMtlxGetUsdValue()`
3. **Metadata Extraction**:
   - `defaultinput` for outputs
   - `target` for inputs
   - `colorspace` for inputs/outputs
   - UI metadata: `uiname`, `doc`, `uifolder`, `uimin`, `uimax`, `uisoftmin`, `uisoftmax`, `uistep`
   - Unit info: `unit`, `unittype`
4. **Primvar Handling**: Tracks `defaultgeomprop` references, replacing `UV0` with the configured primary UV set name

### 3. ParseElement (lines 436-551)

Main NodeDef parsing function:

1. **Context Determination** (lines 444-454):
   - Checks type's semantic for "shader" to get context
   - Falls back to standard typedefs
   - Defaults to `SdrNodeContext->Pattern`

2. **Node Metadata** (lines 462-467):
   - `Label` = nodeDef's node string
   - `Category` = nodeDef's type
   - `Help` from `doc` attribute
   - `Target` from `target` attribute
   - `Role` from `nodegroup` attribute

3. **Primvar Collection** (lines 471-535):
   - `ND_geompropvalue*` nodes: add `$geomprop`
   - `ND_texcoord_vector2`: add primary UV set name
   - **Implementation NodeGraph traversal**:
     - `geompropvalue` nodes: extract primvar from `geomprop` input
     - `texcoord` nodes: add primary UV set
     - `image`/`tiledimage` nodes: add primary UV set if no texcoord already added
   - `internalgeomprops` attribute: parse and add primvars

4. **Property Collection** (lines 538-544):
   - Iterates `nodeDef->getActiveInputs()` for input properties
   - Iterates `nodeDef->getActiveOutputs()` for output properties

### 4. UsdMtlxParserPlugin (lines 556-643)

The main parser plugin:

**ParseShaderNode()** (lines 567-622):
1. Load MaterialX document from `resolvedUri` or `sourceCode`
2. Look up NodeDef by `identifier` or `subIdentifier`
3. Create `ShaderBuilder` and call `ParseElement()`
4. Return built shader node

**GetDiscoveryTypes()**: Returns `["mtlx"]`

**GetSourceType()**: Returns empty string (default source type)

## Data Flow

```
MaterialX Document
       │
       ▼
   NodeDef lookup
       │
       ▼
   ShaderBuilder
       │
       ├─── ParseElement() ───┬─── Extract context
       │                      ├─── Extract metadata
       │                      ├─── Collect primvars
       │                      └─── Parse inputs/outputs
       │
       ▼
   AddProperty() for each input/output
       │
       ├─── Type conversion (UsdMtlxGetUsdType)
       ├─── Default value (UsdMtlxGetUsdValue)
       ├─── Metadata extraction
       └─── Primvar tracking
       │
       ▼
   ShaderBuilder.Build()
       │
       ▼
   SdrShaderNode
```

## Key Tokens (lines 32-56)

Private tokens for attribute names:
- `mtlx`, `defaultgeomprop`, `defaultinput`, `doc`, `enum`, `enumvalues`
- `nodecategory`, `nodegroup`, `target`, `uifolder`, `uimax`, `uimin`
- `uiname`, `uisoftmax`, `uisoftmin`, `uistep`, `unit`, `unittype`, `UV0`

## Environment Settings

- `USDMTLX_PRIMARY_UV_NAME`: Override the primary UV set name (defaults to USD's `UsdUtilsGetPrimaryUVSetName()`, typically "st")

## Role Normalization (lines 405-414)

For stdlib texture nodes, `texture2d` and `texture3d` roles are normalized to `texture` for Sdr compatibility.
