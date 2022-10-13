// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// Scene access API
//
#pragma once

#include <map>
#include "prim-types.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "stage.hh"

namespace tinyusdz {
namespace tydra {

//
// Find and make a map of specified concrete Prim type from Stage.
//

// key = fully absolute Prim path
using PathMaterialMap = std::map<std::string, const tinyusdz::Material *>;
bool ListMaterials(const tinyusdz::Stage &stage, PathMaterialMap &m);


} // namespace tydra
} // namespace tinyusdz

