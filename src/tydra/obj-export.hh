// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Simple RenderMesh/RenderMaterial -> wavefront .obj exporter
//
#pragma once

#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

///
/// Export RenderMesh/RenderMaterial to .obj.
/// Requires RenderScene to export Material/Texture correctly.
///
/// @param[in] scene RenderScene
/// @param[in] mesh_id Mesh id in RenderScene
/// @param[out] obj_str .obj string
/// @param[out] warn warning message
/// @param[out] err error message
///
/// @return true upon success.
///
bool export_to_obj(const RenderScene &scene, const int mesh_id,
                   std::string &obj_str, std::string &mtl_str,
                   std::string *warn, std::string *err);

}  // namespace tydra
}  // namespace tinyusdz
