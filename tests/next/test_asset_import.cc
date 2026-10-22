// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <cstring>
#include <iostream>

#include "c-api/lightusd-import-c.h"

static lightusd_stage* Import(const char* text, uint32_t format) {
  lightusd_import_options options;
  lightusd_import_options_init(&options);
  options.format = format;
  lightusd_stage* stage = nullptr;
  auto status = lightusd_import_memory(reinterpret_cast<const uint8_t*>(text),
                                       std::strlen(text), "", &options, &stage);
  if (status != LIGHTUSD_OK) std::cerr << lightusd_last_error() << "\n";
  assert(status == LIGHTUSD_OK && stage);
  return stage;
}
int main() {
  auto* stage = Import("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n",
                       LIGHTUSD_IMPORT_OBJ);
  auto mesh = lightusd_stage_prim_at_path(stage, "/World/Links/Geometry/mesh");
  assert(lightusd_prim_is_valid(mesh));
  lightusd_value_view counts{};
  assert(lightusd_attr_get(mesh, "faceVertexCounts", &counts) == LIGHTUSD_OK);
  assert(counts.count == 1 && static_cast<const int32_t*>(counts.data)[0] == 4);
  lightusd_stage_destroy(stage);
  stage = Import("<robot><link name='base'/></robot>", LIGHTUSD_IMPORT_URDF);
  auto base = lightusd_stage_prim_at_path(stage, "/World/Links/base");
  lightusd_value* schemas = nullptr;
  assert(lightusd_prim_get_metadata(base, "apiSchemas", &schemas) ==
         LIGHTUSD_OK);
  lightusd_value_destroy(schemas);
  lightusd_stage_destroy(stage);
  lightusd_import_options options;
  lightusd_import_options_init(&options);
  options.format = LIGHTUSD_IMPORT_OBJ;
  options.max_memory_bytes = 4;
  stage = reinterpret_cast<lightusd_stage*>(uintptr_t(1));
  assert(lightusd_import_memory(reinterpret_cast<const uint8_t*>("invalid"), 7,
                                "", &options,
                                &stage) == LIGHTUSD_ERR_RESOURCE_LIMIT);
  assert(!stage);
  options.struct_size = 0;
  assert(lightusd_import_memory(nullptr, 0, "", &options, &stage) ==
         LIGHTUSD_ERR_INVALID_ARG);
  assert(!stage);
  std::cout << "Native import C API tests passed\n";
}
