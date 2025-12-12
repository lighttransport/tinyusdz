// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Lightweight USD library
// Master header - includes all public API headers

#pragma once

// Core types
#include "lightusd/types.hh"
#include "lightusd/result.hh"

// Error reporting
#include "lightusd/cursor.hh"
#include "lightusd/diagnostic.hh"

// String types
#include "lightusd/token.hh"
#include "lightusd/path.hh"

// Value system
#include "lightusd/value.hh"
#include "lightusd/timesamples.hh"

// Properties
#include "lightusd/attribute.hh"
#include "lightusd/relationship.hh"
#include "lightusd/property.hh"

// Variants and Composition
#include "lightusd/variant.hh"
#include "lightusd/composition.hh"
#include "lightusd/layer.hh"
#include "lightusd/prim_index.hh"

// Scene graph
#include "lightusd/prim.hh"
#include "lightusd/stage.hh"

// USDA reader/writer
#include "lightusd/usda_reader.hh"
#include "lightusd/usda_writer.hh"

/// LightUSD namespace
namespace lightusd {

// Bring v1 into lightusd namespace
using namespace v1;

/// Library version
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

/// Get version string
inline const char* version_string() {
    return "0.1.0";
}

} // namespace lightusd
