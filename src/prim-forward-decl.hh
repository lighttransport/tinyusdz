// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// Forward declarations for prim-related types
// This file helps reduce header dependencies by providing forward declarations
// of commonly used types without requiring their full definitions.

#pragma once

#include <string>
#include <memory>
#include <vector>

namespace tinyusdz {

// Core USD types
class Prim;
class PrimNode;
class PrimSpec;
class Layer;
class Stage;
class Property;
class Attribute;
class Relationship;

// Variant-related types
struct Variant;
struct VariantSet;
struct VariantSetSpec;

// Metadata types
struct PrimMeta;
struct AttrMetas;

// Schema types
struct Model;
struct Xformable;
struct Klass;

// Range and iteration types
class PrimRange;

// Connection types
struct ConnectionPath;
template<typename T> class TypedConnection;

// Value types (minimal forward declarations)
namespace value {
  class Value;
  struct token;
  class AssetPath;
  struct TimeSamples;
}

// Path type
class Path;

// ListOp template
template<typename T> class ListOp;

// Enums that are frequently used
enum class Specifier;
enum class Purpose;
enum class Visibility;
enum class Interpolation;
enum class Orientation;
enum class Axis;
enum class Kind;

// Smart pointer typedefs
using PrimPtr = std::shared_ptr<Prim>;
using ConstPrimPtr = std::shared_ptr<const Prim>;
using LayerPtr = std::shared_ptr<Layer>;
using ConstLayerPtr = std::shared_ptr<const Layer>;

// Common container typedefs
using PrimVector = std::vector<Prim>;
using PathVector = std::vector<Path>;
using TokenVector = std::vector<value::token>;

} // namespace tinyusdz