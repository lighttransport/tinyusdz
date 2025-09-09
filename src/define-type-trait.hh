// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.

///
/// @file define-type-trait.hh
/// @brief Type trait definitions for USD value types
///
/// Contains DEFINE_TYPE_TRAIT macro usage to register type traits for all
/// USD value types including primitives, complex types, and list operations.
/// These type traits enable type-safe value operations and serialization.
///
#pragma once

#include <string>
#include <vector>
#include "value-types.hh"

namespace tinyusdz {

// Forward declarations for types used in type traits
struct Reference;
struct Payload; 
struct LayerOffset;
struct Path;
struct Relationship;
struct Collection;
struct CollectionInstance;
struct Model;
struct Scope;
struct Extent;
struct VariantSelectionMap;
template<typename T> struct ListOp;
enum class Specifier;
enum class Permission;
enum class Variability;

namespace value {

// Include macro definitions for DEFINE_TYPE_TRAIT
#include "define-type-trait.inc"

///
/// Type trait definitions for USD types
/// These macros generate TypeTraits<T> specializations that provide:
/// - type_name(): Human-readable type name
/// - type_id(): Unique numeric type identifier  
/// - ncomp(): Number of components (e.g. extent = 2 for min/max)
/// - size(): Size in bytes
/// - Array and role type information
///

DEFINE_TYPE_TRAIT(Reference, "ref", TYPE_ID_REFERENCE, 1);
DEFINE_TYPE_TRAIT(Specifier, "specifier", TYPE_ID_SPECIFIER, 1);
DEFINE_TYPE_TRAIT(Permission, "permission", TYPE_ID_PERMISSION, 1);
DEFINE_TYPE_TRAIT(Variability, "variability", TYPE_ID_VARIABILITY, 1);

DEFINE_TYPE_TRAIT(VariantSelectionMap, "variants", TYPE_ID_VARIANT_SELECION_MAP,
                  0);

DEFINE_TYPE_TRAIT(Payload, "payload", TYPE_ID_PAYLOAD, 1);
DEFINE_TYPE_TRAIT(LayerOffset, "LayerOffset", TYPE_ID_LAYER_OFFSET, 1);

DEFINE_TYPE_TRAIT(ListOp<value::token>, "ListOpToken", TYPE_ID_LIST_OP_TOKEN,
                  1);
DEFINE_TYPE_TRAIT(ListOp<std::string>, "ListOpString", TYPE_ID_LIST_OP_STRING,
                  1);
DEFINE_TYPE_TRAIT(ListOp<Path>, "ListOpPath", TYPE_ID_LIST_OP_PATH, 1);
DEFINE_TYPE_TRAIT(ListOp<Reference>, "ListOpReference",
                  TYPE_ID_LIST_OP_REFERENCE, 1);
DEFINE_TYPE_TRAIT(ListOp<int32_t>, "ListOpInt", TYPE_ID_LIST_OP_INT, 1);
DEFINE_TYPE_TRAIT(ListOp<uint32_t>, "ListOpUInt", TYPE_ID_LIST_OP_UINT, 1);
DEFINE_TYPE_TRAIT(ListOp<int64_t>, "ListOpInt64", TYPE_ID_LIST_OP_INT64, 1);
DEFINE_TYPE_TRAIT(ListOp<uint64_t>, "ListOpUInt64", TYPE_ID_LIST_OP_UINT64, 1);
DEFINE_TYPE_TRAIT(ListOp<Payload>, "ListOpPayload", TYPE_ID_LIST_OP_PAYLOAD, 1);

DEFINE_TYPE_TRAIT(Path, "Path", TYPE_ID_PATH, 1);
DEFINE_TYPE_TRAIT(Relationship, "Relationship", TYPE_ID_RELATIONSHIP, 1);
// TODO(syoyo): Define as 1D array?
DEFINE_TYPE_TRAIT(std::vector<Path>, "PathVector", TYPE_ID_PATH_VECTOR, 1);

DEFINE_TYPE_TRAIT(std::vector<value::token>, "token[]", TYPE_ID_TOKEN_VECTOR,
                  1);

DEFINE_TYPE_TRAIT(value::TimeSamples, "TimeSamples", TYPE_ID_TIMESAMPLES, 1);

DEFINE_TYPE_TRAIT(Collection, "Collection", TYPE_ID_COLLECTION, 1);
DEFINE_TYPE_TRAIT(CollectionInstance, "CollectionInstance", TYPE_ID_COLLECTION_INSTANCE, 1);

DEFINE_TYPE_TRAIT(Model, "Model", TYPE_ID_MODEL, 1);
DEFINE_TYPE_TRAIT(Scope, "Scope", TYPE_ID_SCOPE, 1);


DEFINE_TYPE_TRAIT(Extent, "float3[]", TYPE_ID_EXTENT, 2);  // float3[2]

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz