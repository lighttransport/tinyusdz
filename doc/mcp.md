# MCP(ModelContextProtocol)

## Status

W.I.P.

## Server

### C++ Server

C++ MCP Server(using Civetweb http server) is provided as Tydra module.

### JS Server(TODO)

If you are using TinyUSDZ on JS/WASM, MCP server in JS is provided in `<tinyusdz>/web`


## Core commands

* new_layer
  * Create new empty USD Layer
  * arg
    * layer_name(str) : Layer name(file name)
  * response
    * `result`: `true` upon success, `false` when failed(e.g. duplicated Layer name).

* load_layer
  * Load USD as Layer
  * arg
    * usd_name(str) : USD file name
    * layer_name(str) : (Unique) Layer name(e.g. basename(usd_name))
  * response
    * `result`: `true` upon success, `false` when failed(e.g. failed to load Layer).

* select_layer
  * Set current USD Layer
  * arg
    * layer_name(str) : Layer name(file name)
  * response
    * return selected USD Layer name(str)

* list_layers
  * List USD Layers
  * arg
    * N/A
  * response
    * Array of USD Layer names.

* delete_layers
  * List USD Layers
  * arg
    * N/A
  * response
    * Array of USD Layer names.

* get_layer_info
  * Get currently selected USD Layer info (Me
  * arg
    * N/A
* get_object_info
  * arg
    * object_path(str) : Absolute Object(USD PrimSpec) Path of currently selected Layer. Example: `/root`, `/root/xform/mesh0`

## Layer Composition and Structure commands

* list_primspecs
  * List root PrimSpecs in loaded USD Layer
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
  * response
    * Array of PrimSpec names in the root of the layer

* list_sublayers
  * List SubLayer USD paths in a loaded USD Layer
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
  * response
    * JSON array where each item contains:
      * `assetPath`: Path to the sublayer USD file
      * `layerOffset`: Object with `offset` and `scale` for animation time mapping

* list_references
  * List References in a loaded USD Layer (composition arcs to other USD files)
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primName(str) : Optional filter to get references from a specific prim
  * response
    * JSON array where each item contains:
      * `primName`: Name of the prim containing the reference
      * `assetPath`: Path to the referenced asset
      * `primPath`: Target prim path in the referenced asset
      * `listOp`: List edit operation qualifier (resetToExplicit, append, add, delete, prepend, order)
      * `layerOffset`: Object with `offset` and `scale` for animation time mapping

* list_payloads
  * List Payloads in a loaded USD Layer (deferred composition arcs)
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primName(str) : Optional filter to get payloads from a specific prim
  * response
    * JSON array where each item contains:
      * `primName`: Name of the prim containing the payload
      * `assetPath`: Path to the payload asset
      * `primPath`: Target prim path in the payload asset
      * `listOp`: List edit operation qualifier (resetToExplicit, append, add, delete, prepend, order)
      * `layerOffset`: Object with `offset` and `scale` for animation time mapping

* add_prim
  * Add PrimSpec to specified Layer as child PrimSpec at specified primPath
  * Input prim data can be in USDA (ASCII), USDC (binary), or USDJ (JSON) format
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primPath(str) : USD prim path where to add the prim (e.g., `/MyPrim` or `/Parent/Child`) - required
    * data(str) : Prim data in specified format - required
      * For USDA: escaped string representation of prim specification
      * For USDC: base64-encoded binary Crate format data
      * For USDJ: JSON string representation (not yet supported)
    * format(str) : Format of input data - required
      * Valid values: `usda`, `usdc`, `usdj`
  * response
    * Success message indicating the prim was added at the specified path
    * Error message if the operation failed (invalid path, parse error, etc.)

* del_prim
  * Delete a PrimSpec from specified Layer at specified primPath
  * Currently only supports deleting root-level prims (nested prim deletion not yet supported)
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primPath(str) : USD prim path of the prim to delete (e.g., `/MyPrim`) - required
  * response
    * Success message indicating the prim was deleted
    * Error message if the operation failed (prim not found, invalid path, etc.)

* get_prim
  * Get PrimSpec information as JSON (USDJ) from specified Layer at specified primPath
  * Returns comprehensive prim information including properties, metadata, and composition arcs
  * Currently only supports retrieving root-level prims (nested prim retrieval not yet supported)
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primPath(str) : USD prim path to retrieve (e.g., `/MyPrim`) - required
  * response
    * JSON object containing:
      * `name`: Prim name
      * `typeName`: Type name of the prim
      * `specifier`: Specifier type (def, over, or class)
      * `properties`: Object with property names and their type/variability info
      * `metadata`: Object with prim metadata (doc, comment, active, hidden, kind, etc.)
      * `references`: Array of reference composition arcs with assetPath and primPath
      * `payloads`: Array of payload composition arcs with assetPath and primPath
    * Error message if the prim was not found or path is invalid

## Property Management commands

* add_prop
  * Add property (attribute or relationship) to a PrimSpec at specified primPath
  * Currently only supports root-level prims (nested property modification not yet supported)
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primPath(str) : USD prim path (e.g., `/MyPrim`) - required
    * propertyName(str) : Name of the property to add - required
    * propertyType(str) : Type of property - required
      * Valid values: `attribute`, `relationship`
    * value(str) : Property value - required
      * For attributes: JSON representation of value (string, number, boolean)
      * For relationships: USD path string (e.g., `/other/prim`)
    * valueFormat(str) : Format of attribute value - optional, default "json"
      * Valid values: `json`, `ascii`
  * response
    * Success message indicating the property was added
    * Error message if the operation failed (invalid path, type error, etc.)

* del_prop
  * Delete property (attribute or relationship) from a PrimSpec at specified primPath
  * Currently only supports root-level prims
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primPath(str) : USD prim path (e.g., `/MyPrim`) - required
    * propertyName(str) : Name of the property to delete - required
  * response
    * Success message indicating the property was deleted
    * Error message if the operation failed (property not found, prim not found, etc.)

* get_prop
  * Get property (attribute or relationship) from a PrimSpec as JSON
  * Currently only supports root-level prims
  * arg
    * uuid(str) : Layer UUID (optional if name is provided)
    * name(str) : Layer name (optional if uuid is provided)
    * primPath(str) : USD prim path (e.g., `/MyPrim`) - required
    * propertyName(str) : Name of the property to get - required
  * response
    * JSON object containing:
      * `name`: Property name
      * `type`: Property type (`attribute` or `relationship`)
      * For attributes:
        * `dataType`: USD type name (e.g., `double`, `int64`, `token`)
        * `variability`: Variability qualifier (`uniform` or `varying`)
        * `value`: Property value (placeholder in current implementation)
      * For relationships:
        * `targets`: Array of target prim paths
    * Error message if the property was not found or path is invalid

## JS/Three.js specific commands


* `get_viewport_screenshot`
  * Screenshot of current viewport rendering.
  * arg
    * width(int)
    * format(str) : "png" or "jpeg"



