// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct concrete Prim from PropertyMap or PrimSpec.
// This is a refactored version that delegates to specialized modules.
//
#include "primspec.hh"
#include "prim-reconstruct.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"

// Include specialized reconstruction modules
#include "reconstruct-common.hh"
#include "reconstruct-geom.hh"
#include "reconstruct-light.hh"
#include "reconstruct-shader.hh"
#include "reconstruct-skeletal.hh"
#include "reconstruct-xform.hh"

#include "usdGeom.hh"
#include "usdSkel.hh"
#include "usdLux.hh"
#include "usdShade.hh"

#include "common-macros.inc"
#include "value-types.hh"

namespace tinyusdz {
namespace prim {

// Note: This file will be progressively refactored to move implementations
// to the specialized modules. For now, it includes the original implementations
// and will forward to the new modules as they are implemented.

// The original prim-reconstruct.cc implementations will be moved to their
// respective modules in phases:
// 1. Common utilities and helpers -> reconstruct-common.cc
// 2. Geometry primitives -> reconstruct-geom.cc
// 3. Light primitives -> reconstruct-light.cc
// 4. Shader/Material primitives -> reconstruct-shader.cc
// 5. Skeletal animation primitives -> reconstruct-skeletal.cc
// 6. Transform/Scene primitives -> reconstruct-xform.cc

// For now, include the original implementation from prim-reconstruct.cc
// This allows for gradual migration without breaking the build

} // namespace prim
} // namespace tinyusdz