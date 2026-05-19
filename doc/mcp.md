# MCP (Model Context Protocol) Interface

TinyUSDZ provides an MCP server for AI agents (Claude, GPT, etc.) to interact with USD data. The server exposes USD scene graph editing, composition arcs, querying, and scripting tools.

## Architecture

```
┌─ MCP Session ──────────────────────────────────────┐
│                                                      │
│   Context                                            │
│   ├── Stage (composed, for scene graph queries)      │
│   ├── Layers (uncomposed, for composition editing)   │
│   ├── Assets (for USDZ packaging & viewer)           │
│   ├── QuickJS Engine (persistent, per session)       │
│   └── Screenshots (for viewer workflow)              │
│                                                      │
│   Tools Layer                                        │
│   ├── Stage Tools      (load, create, export, info)  │
│   ├── Scene Graph      (prim list/get/create/remove) │
│   ├── Attributes       (list, get, set, block)       │
│   ├── Composition      (refs, payloads, variants)    │
│   ├── Query/Discover   (find by type, schema list)   │
│   ├── Scripting        (run_script: JS + tinyusdz.*) │
│   └── Legacy           (assets, screenshots)         │
│                                                      │
└──────────────────────────────────────────────────────┘
```

## Server

### C++ Native Server

A C++ MCP server using CivetWeb HTTP library. JSON-RPC 2.0 over HTTP POST on `/mcp` endpoint.

```bash
# Build with MCP server
cmake .. -DTINYUSDZ_WITH_MCP_SERVER=ON
make -j16

# Run the example MCP server
./build/examples/mcp_server/mcp_server
```

Default port: 8085. Configure via `MCPServerOptions`.

### JS/WASM Server

An Express-based MCP server bridging to TinyUSDZ WASM. See `web/mcp-server/`.

## Tools

### Stage Lifecycle

| Tool | Description | Key Args |
|------|-------------|----------|
| `stage_new` | Create new empty stage | `upAxis`, `defaultPrim`, `metersPerUnit` |
| `stage_load` | Load USD file into session | `uri`, `options` |
| `stage_load_data` | Load USD from base64 data | `data`, `format` |
| `stage_export` | Export stage to file | `uri`, `format` |
| `stage_to_string` | Export stage to USDA string | `format` |
| `stage_info` | Get stage metadata | — |

### Scene Graph

| Tool | Description | Key Args |
|------|-------------|----------|
| `prim_list` | List prims at/under path | `path`, `max_depth`, `include_attributes` |
| `prim_get` | Get full prim details | `path`, `include_attributes`, `include_metadata` |
| `prim_create` | Create a new prim | `path`, `type_name`, `specifier` |
| `prim_remove` | Remove a prim | `path` |
| `prim_rename` | Rename a prim | `path`, `new_name` |
| `prim_get_metadata` | Get prim metadata | `path` |

### Attributes

| Tool | Description | Key Args |
|------|-------------|----------|
| `attr_list` | List attributes on a prim | `path` |
| `attr_get` | Get attribute value | `path`, `attr_name`, `time` |
| `attr_set` | Set attribute value | `path`, `attr_name`, `value` |
| `attr_block` | Block (set to None) an attribute | `path`, `attr_name` |
| `attr_connections` | Get/set connections | `path`, `attr_name`, `set` |

### Composition Arcs

| Tool | Description | Key Args |
|------|-------------|----------|
| `reference_add` | Add reference arc | `path`, `asset_path`, `prim_path` |
| `reference_list` | List references | `path` |
| `reference_clear` | Clear all references | `path` |
| `payload_add` | Add payload arc | `path`, `asset_path` |
| `payload_list` | List payloads | `path` |
| `inherit_add` | Add inherit arc | `path`, `prim_path` |
| `specialize_add` | Add specialize arc | `path`, `prim_path` |
| `variant_list_sets` | List variant sets | `path` |
| `variant_get_selection` | Get variant selection | `path`, `variant_set` |
| `variant_set_selection` | Set variant selection | `path`, `variant_set`, `variant` |

### Query / Discovery

| Tool | Description | Key Args |
|------|-------------|----------|
| `query_prims_by_type` | Find prims by type | `type_name` |
| `schema_list_types` | List all registered prim types | — |
| `schema_get_type` | Get schema for a type | `type_name` |
| `search` | Search prim names | `query`, `scope` |

### Scripting

| Tool | Description | Key Args |
|------|-------------|----------|
| `run_script` | Execute JS against session stage | `script` (JS code) |

### Legacy (Viewer Workflow)

| Tool | Description |
|------|-------------|
| `store_asset` | Store asset with preview + geometry params |
| `read_asset` | Read stored asset |
| `read_asset_preview` | Read asset preview image |
| `get_asset_description` | Get asset description |
| `get_all_asset_descriptions` | Get all asset descriptions |
| `select_assets` | Select + arrange assets with transforms |
| `get_selected_assets` | Get currently selected assets |
| `save_screenshot` | Save a screenshot |
| `list_screenshots` | List screenshots |
| `read_screenshot` | Read screenshot image |
| `load_usd_layer_from_file` | Load USD as Layer (C++ native) |
| `load_usd_layer_from_data` | Load USD as Layer from base64 |
| `to_usda` | Convert Layer to USDA text |
| `list_primspecs` | List root PrimSpecs |

## JavaScript Scripting

The MCP server includes a QuickJS engine (when built with `-DTINYUSDZ_WITH_QJS=ON`). JS scripts can be executed via the `run_script` tool and have access to the `tinyusdz.*` API:

```javascript
// Stage info
var info = tinyusdz.stage.info();
console.log("Up Axis: " + info.upAxis);

// List children
var result = tinyusdz.prim.listChildren("/World");
for (var i = 0; i < result.prims.length; i++) {
    console.log(result.prims[i].name + " (" + result.prims[i].type + ")");
}

// Get prim details
var prim = tinyusdz.prim.get("/World/mesh0");
console.log("Type: " + prim.type + ", Children: " + prim.childCount);

// Find all meshes
var meshes = tinyusdz.query.findAllByType("Mesh");
console.log("Found " + meshes.count + " meshes");

// Export to USDA
var usda = tinyusdz.stage.exportToString();
```

## Value Format

Attributes and values use structured JSON format:

```json
// Scalar
{"type": "float", "value": 1.5}

// Compound
{"type": "float3", "value": [1.0, 2.0, 3.0]}
{"type": "color3f", "value": [0.5, 0.2, 0.8]}

// Array
{"type": "float3[]", "value": [[1,2,3], [4,5,6]]}

// Matrix
{"type": "matrix4d", "value": [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]}

// Quaternion
{"type": "quatf", "value": [0, 0, 0, 1]}

// Asset
{"type": "asset", "value": {"assetPath": "texture.png", "resolvedPath": ""}}

// Dictionary
{"type": "dictionary", "value": {"key1": {"type": "int", "value": 42}}}

// Blocked
{"type": "None"}
```

## Protocol

- Transport: HTTP POST to `/mcp`
- Protocol: JSON-RPC 2.0
- MCP spec version: 2025-03-26
- Session ID: `mcp-session-id` header (returned by `initialize`)
- Capabilities: `tools`, `resources`

## Security

- Max request body: 16 MB
- Max base64 input: 64 MB
- Max decoded payload: 48 MB
