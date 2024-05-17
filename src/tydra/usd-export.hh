// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Simple RenderScene -> USD exporter.
// For debugging whether RenderScene is correctly constructed from USD :-)
//
// Supports USDA only at the moment.
//
// - Features
//   - [ ] RenderMesh
//   -  [x] Polygon mesh
//   -  [ ] Subdivision mesh
//   - [x] RenderMaterial/Texture(export texture filename only)
//   - [ ] Skinning
//   - [ ] BlendShapes
//   - [ ] Animations
//   - [ ] Hair
//
#pragma once

#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

///
/// Export RenderScene to USDA(USD ASCII).
///
/// @param[in] scene RenderScene
/// @param[out] usda_str USDA string
/// @param[out] warn warning message
/// @param[out] err error message
///
/// @return true upon success.
///
bool export_to_usda(const RenderScene &scene, 
                   std::string &usda_str,
                   std::string *warn, std::string *err);

}  // namespace tydra
}  // namespace tinyusdz
