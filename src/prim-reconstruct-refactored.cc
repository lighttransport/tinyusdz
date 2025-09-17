// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct concrete Prim from PropertyMap or PrimSpec.
// This is a refactored version that coordinates specialized modules.
//
// NOTE: This file temporarily includes the original prim-reconstruct.cc
// implementation to maintain compatibility while refactoring progresses.
//

// Include the original implementation
// This is a transitional approach - the original code will be progressively
// moved to the specialized reconstruct-*.cc modules
#include "prim-reconstruct.cc"

// The refactoring plan:
// 1. Common utilities and helpers -> reconstruct-common.cc (partial)
// 2. Geometry primitives -> reconstruct-geom.cc (partial)
// 3. Light primitives -> reconstruct-light.cc (partial)
// 4. Shader/Material primitives -> reconstruct-shader.cc (partial)
// 5. Skeletal animation primitives -> reconstruct-skeletal.cc (partial)
// 6. Transform/Scene primitives -> reconstruct-xform.cc (partial)