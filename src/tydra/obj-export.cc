// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
#include "obj-export.hh"
#include "common-macros.inc"
#include "tiny-format.hh"

namespace tinyusdz {
namespace tydra {

#define PushError(msg) { \
  if (err) { \
    (*err) += msg + "\n"; \
  } \
}

bool export_to_obj(const RenderScene &scene, const int mesh_id,
  std::string &obj_str, std::string &mtl_str, std::string *warn, std::string *err) {

  (void)obj_str;
  (void)mtl_str;
  (void)warn;

  if (mesh_id < 0) {
    PUSH_ERROR_AND_RETURN("Invalid mesh_id");
  } else if (size_t(mesh_id) < scene.meshes.size()) {
    PUSH_ERROR_AND_RETURN(fmt::format("mesh_id {} is out-of-range. scene.meshes.size {}", mesh_id, scene.meshes.size()));
  }

  PUSH_ERROR_AND_RETURN("TODO");
}


} // namespace tydra
} // namespace tinyusdz

