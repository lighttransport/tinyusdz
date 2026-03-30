// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Internal header for prim-reconstruct split files.
// Contains common constants, helper templates, and macros.
//
#pragma once

#include "prim-reconstruct.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"
#include "enum-handlers.hh"
#include "prim-property-tables.hh"
#include "common-macros.inc"
#include "value-types.hh"

namespace tinyusdz {
namespace prim {

// Common constants used in property parsing
constexpr auto kProxyPrim = "proxyPrim";
constexpr auto kVisibility = "visibility";
constexpr auto kExtent = "extent";
constexpr auto kPurpose = "purpose";
constexpr auto kMaterialBinding = "material:binding";
constexpr auto kMaterialBindingCollection = "material:binding:collection";
constexpr auto kMaterialBindingPreview = "material:binding:preview";
constexpr auto kSkelSkeleton = "skel:skeleton";
constexpr auto kSkelAnimationSource = "skel:animationSource";
constexpr auto kSkelBlendShapes = "skel:blendShapes";
constexpr auto kSkelBlendShapeTargets = "skel:blendShapeTargets";
constexpr auto kInputsVarname = "inputs:varname";
constexpr auto kCollectionPrefix = "collection:";

// Forward declaration of ReconstructShader template
template <typename T>
bool ReconstructShader(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);

// ReconstructGPrimProperties is defined in prim-reconstruct-impl.inc
// which is included by each split file in an anonymous namespace.

}  // namespace prim
}  // namespace tinyusdz
