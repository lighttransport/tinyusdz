// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Simple RenderScene -> glTF-like JSON exporter
//
#pragma once

#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

///
/// Export RenderScene to JSON.
/// JSON schema is TinyUSDZ specific, but Three.js' JSON Object scene format in mind.
///
/// https://github.com/mrdoob/three.js/wiki/JSON-Object-Scene-format-4
///
/// When `asset_as_binary` is set to true, Export asset data(Mesh vertex attributes, Texture images, etc) as separated binary data(like glTF's GLB format).
/// Otherwise all asserts are embedded to JSON(Use BASE64 string for texture image data)
///
/// If you want to export a USD scene with lots of geometry(e.g. 1M+ tris) + textures, binary output is recommended.
///
/// @param[in] scene RenderScene
/// @param[in] asset_as_binary Export asset data(Mesh, Texture, etc) as binary data?
/// @param[out] json_str exported JSON string
/// @param[out] binary_str Binary data string
/// @param[out] warn warning message
/// @param[out] err error message
///
/// @return true upon success.
///
bool export_to_json(const RenderScene &scene, bool asset_as_binary,
                   std::string &json_str, std::string &binary_str,
                   std::string *warn, std::string *err);

}  // namespace tydra
}  // namespace tinyusdz
