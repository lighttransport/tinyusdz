// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//
// Scene access API
//
#pragma once

#include <map>

#include "prim-types.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdSkel.hh"

namespace tinyusdz {
namespace tydra {

// key = fully absolute Prim path in string(e.g. "/xform/geom0")
template <typename T>
using PathPrimMap = std::map<std::string, const T *>;

//
// value = pair of Shader Prim which contains the Shader type T("info:id") and
// its concrete Shader type(UsdPreviewSurface)
//
template <typename T>
using PathShaderMap =
    std::map<std::string, std::pair<const Shader *, const T *>>;

///
/// List Prim of type T from the Stage.
/// Returns false when unsupported/unimplemented Prim type T is given.
///
template <typename T>
bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<T> &m /* output */);

///
/// List Shader of shader type T from the Stage.
/// Returns false when unsupported/unimplemented Shader type T is given.
/// TODO: User defined shader type("info:id")
///
template <typename T>
bool ListShaders(const tinyusdz::Stage &stage,
                 PathShaderMap<T> &m /* output */);

}  // namespace tydra
}  // namespace tinyusdz
