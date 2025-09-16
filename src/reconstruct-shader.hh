// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Shader and Material primitive reconstruction
#pragma once

#include <string>
#include "prim-types.hh"
#include "usdShade.hh"

namespace tinyusdz {
namespace prim {

struct PrimReconstructOptions;

// Generic shader reconstruction template
template <typename T>
bool ReconstructShader(
    const Specifier &spec,
    const PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);

// Shader and Material reconstruction functions
// Note: These are template specializations defined in reconstruct-shader.cc

// Additional shader specialization implementations are defined in reconstruct-shader.cc

// Helper function for material binding properties
bool ReconstructMaterialBindingProperties(
    const PropertyMap &properties,
    std::set<std::string> &table,
    MaterialBinding *materialBinding,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);

// Helper function for collection properties
bool ReconstructCollectionProperties(
    const PropertyMap &properties,
    std::set<std::string> &table,
    Collection *collection,
    std::string *collection_name,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);

} // namespace prim
} // namespace tinyusdz