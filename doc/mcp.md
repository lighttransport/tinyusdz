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

## JS/Three.js specific commands


* `get_viewport_screenshot`
  * Screenshot of current viewport rendering.
  * arg
    * width(int)
    * format(str) : "png" or "jpeg"



