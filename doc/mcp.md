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

## JS/Three.js specific commands


* `get_viewport_screenshot`
  * Screenshot of current viewport rendering.
  * arg
    * width(int)
    * format(str) : "png" or "jpeg"



