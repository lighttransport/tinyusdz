// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Lightweight USD library
// Master header - includes all public API headers

#pragma once

// Core types
#include "lightusd/types.hh"
#include "lightusd/result.hh"

// Debug and logging
#include "lightusd/debug.hh"

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

// PCP (Prim Cache Populate)
#include "lightusd/layer_registry.hh"
#include "lightusd/pcp_node.hh"
#include "lightusd/pcp_layer_stack.hh"
#include "lightusd/pcp_prim_index.hh"
#include "lightusd/pcp_cache.hh"

// Value Clips
#include "lightusd/clips.hh"

// Containers
#include "lightusd/typed_array.hh"

// Scene graph
#include "lightusd/prim.hh"
#include "lightusd/stage.hh"

// Primvar support
#include "lightusd/primvar.hh"

// Schema validation
#include "lightusd/schema.hh"
#include "lightusd/schema_registry.hh"

// USDA reader/writer
#include "lightusd/usda_reader.hh"
#include "lightusd/usda_writer.hh"

// USDC (Crate) reader
#include "lightusd/stream_reader.hh"
#include "lightusd/crate_format.hh"
#include "lightusd/usdc_reader.hh"

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
