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
#include "list-op.hh"

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
enum class Specifier;
enum class Permission;
enum class Variability;

namespace value {

///
/// Type trait definitions for USD types
/// These template specializations provide:
/// - type_name(): Human-readable type name
/// - type_id(): Unique numeric type identifier  
/// - ncomp(): Number of components (e.g. extent = 2 for min/max)
/// - size(): Size in bytes
/// - Array and role type information
///

// Reference type
template <>
struct TypeTraits<Reference> {
  using value_type = Reference;
  using value_underlying_type = Reference;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Reference); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_REFERENCE; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_REFERENCE; }
  static std::string type_name() { return "ref"; }
  static std::string underlying_type_name() { return "ref"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Specifier type  
template <>
struct TypeTraits<Specifier> {
  using value_type = Specifier;
  using value_underlying_type = Specifier;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Specifier); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_SPECIFIER; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_SPECIFIER; }
  static std::string type_name() { return "specifier"; }
  static std::string underlying_type_name() { return "specifier"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Permission type
template <>
struct TypeTraits<Permission> {
  using value_type = Permission;
  using value_underlying_type = Permission;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Permission); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_PERMISSION; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_PERMISSION; }
  static std::string type_name() { return "permission"; }
  static std::string underlying_type_name() { return "permission"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Variability type
template <>
struct TypeTraits<Variability> {
  using value_type = Variability;
  using value_underlying_type = Variability;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Variability); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_VARIABILITY; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_VARIABILITY; }
  static std::string type_name() { return "variability"; }
  static std::string underlying_type_name() { return "variability"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// VariantSelectionMap type
template <>
struct TypeTraits<VariantSelectionMap> {
  using value_type = VariantSelectionMap;
  using value_underlying_type = VariantSelectionMap;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(VariantSelectionMap); }
  static constexpr uint32_t ncomp() { return 0; }
  static constexpr uint32_t type_id() { return TYPE_ID_VARIANT_SELECION_MAP; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_VARIANT_SELECION_MAP; }
  static std::string type_name() { return "variants"; }
  static std::string underlying_type_name() { return "variants"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Payload type
template <>
struct TypeTraits<Payload> {
  using value_type = Payload;
  using value_underlying_type = Payload;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Payload); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_PAYLOAD; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_PAYLOAD; }
  static std::string type_name() { return "payload"; }
  static std::string underlying_type_name() { return "payload"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// LayerOffset type
template <>
struct TypeTraits<LayerOffset> {
  using value_type = LayerOffset;
  using value_underlying_type = LayerOffset;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(LayerOffset); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LAYER_OFFSET; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LAYER_OFFSET; }
  static std::string type_name() { return "LayerOffset"; }
  static std::string underlying_type_name() { return "LayerOffset"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Path type
template <>
struct TypeTraits<Path> {
  using value_type = Path;
  using value_underlying_type = Path;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Path); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_PATH; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_PATH; }
  static std::string type_name() { return "Path"; }
  static std::string underlying_type_name() { return "Path"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Relationship type
template <>
struct TypeTraits<Relationship> {
  using value_type = Relationship;
  using value_underlying_type = Relationship;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Relationship); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_RELATIONSHIP; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_RELATIONSHIP; }
  static std::string type_name() { return "Relationship"; }
  static std::string underlying_type_name() { return "Relationship"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Collection type
template <>
struct TypeTraits<Collection> {
  using value_type = Collection;
  using value_underlying_type = Collection;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Collection); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_COLLECTION; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_COLLECTION; }
  static std::string type_name() { return "Collection"; }
  static std::string underlying_type_name() { return "Collection"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// CollectionInstance type
template <>
struct TypeTraits<CollectionInstance> {
  using value_type = CollectionInstance;
  using value_underlying_type = CollectionInstance;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(CollectionInstance); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_COLLECTION_INSTANCE; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_COLLECTION_INSTANCE; }
  static std::string type_name() { return "CollectionInstance"; }
  static std::string underlying_type_name() { return "CollectionInstance"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Model type
template <>
struct TypeTraits<Model> {
  using value_type = Model;
  using value_underlying_type = Model;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Model); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_MODEL; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_MODEL; }
  static std::string type_name() { return "Model"; }
  static std::string underlying_type_name() { return "Model"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Scope type
template <>
struct TypeTraits<Scope> {
  using value_type = Scope;
  using value_underlying_type = Scope;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Scope); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_SCOPE; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_SCOPE; }
  static std::string type_name() { return "Scope"; }
  static std::string underlying_type_name() { return "Scope"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Extent type (float3[2])
template <>
struct TypeTraits<Extent> {
  using value_type = Extent;
  using value_underlying_type = Extent;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(Extent); }
  static constexpr uint32_t ncomp() { return 2; } // float3[2]
  static constexpr uint32_t type_id() { return TYPE_ID_EXTENT; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_EXTENT; }
  static std::string type_name() { return "float3[]"; }
  static std::string underlying_type_name() { return "float3[]"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// ListOp template specializations
template <>
struct TypeTraits<ListOp<value::token>> {
  using value_type = ListOp<value::token>;
  using value_underlying_type = ListOp<value::token>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<value::token>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_TOKEN; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_TOKEN; }
  static std::string type_name() { return "ListOpToken"; }
  static std::string underlying_type_name() { return "ListOpToken"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<std::string>> {
  using value_type = ListOp<std::string>;
  using value_underlying_type = ListOp<std::string>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<std::string>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_STRING; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_STRING; }
  static std::string type_name() { return "ListOpString"; }
  static std::string underlying_type_name() { return "ListOpString"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<Path>> {
  using value_type = ListOp<Path>;
  using value_underlying_type = ListOp<Path>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<Path>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_PATH; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_PATH; }
  static std::string type_name() { return "ListOpPath"; }
  static std::string underlying_type_name() { return "ListOpPath"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<Reference>> {
  using value_type = ListOp<Reference>;
  using value_underlying_type = ListOp<Reference>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<Reference>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_REFERENCE; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_REFERENCE; }
  static std::string type_name() { return "ListOpReference"; }
  static std::string underlying_type_name() { return "ListOpReference"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<int32_t>> {
  using value_type = ListOp<int32_t>;
  using value_underlying_type = ListOp<int32_t>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<int32_t>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_INT; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_INT; }
  static std::string type_name() { return "ListOpInt"; }
  static std::string underlying_type_name() { return "ListOpInt"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<uint32_t>> {
  using value_type = ListOp<uint32_t>;
  using value_underlying_type = ListOp<uint32_t>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<uint32_t>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_UINT; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_UINT; }
  static std::string type_name() { return "ListOpUInt"; }
  static std::string underlying_type_name() { return "ListOpUInt"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<int64_t>> {
  using value_type = ListOp<int64_t>;
  using value_underlying_type = ListOp<int64_t>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<int64_t>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_INT64; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_INT64; }
  static std::string type_name() { return "ListOpInt64"; }
  static std::string underlying_type_name() { return "ListOpInt64"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<uint64_t>> {
  using value_type = ListOp<uint64_t>;
  using value_underlying_type = ListOp<uint64_t>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<uint64_t>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_UINT64; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_UINT64; }
  static std::string type_name() { return "ListOpUInt64"; }
  static std::string underlying_type_name() { return "ListOpUInt64"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

template <>
struct TypeTraits<ListOp<Payload>> {
  using value_type = ListOp<Payload>;
  using value_underlying_type = ListOp<Payload>;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(ListOp<Payload>); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_LIST_OP_PAYLOAD; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_LIST_OP_PAYLOAD; }
  static std::string type_name() { return "ListOpPayload"; }
  static std::string underlying_type_name() { return "ListOpPayload"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

// Vector types
template <>
struct TypeTraits<std::vector<Path>> {
  using value_type = std::vector<Path>;
  using value_underlying_type = std::vector<Path>;
  static constexpr uint32_t ndim() { return 1; }
  static constexpr size_t size() { return sizeof(Path); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_PATH_VECTOR; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_PATH_VECTOR; }
  static std::string type_name() { return "PathVector"; }
  static std::string underlying_type_name() { return "PathVector"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return true; }
};

template <>
struct TypeTraits<std::vector<value::token>> {
  using value_type = std::vector<value::token>;
  using value_underlying_type = std::vector<value::token>;
  static constexpr uint32_t ndim() { return 1; }
  static constexpr size_t size() { return sizeof(value::token); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_TOKEN_VECTOR; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_TOKEN_VECTOR; }
  static std::string type_name() { return "token[]"; }
  static std::string underlying_type_name() { return "token[]"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return true; }
};

// TimeSamples type
template <>
struct TypeTraits<value::TimeSamples> {
  using value_type = value::TimeSamples;
  using value_underlying_type = value::TimeSamples;
  static constexpr uint32_t ndim() { return 0; }
  static constexpr size_t size() { return sizeof(value::TimeSamples); }
  static constexpr uint32_t ncomp() { return 1; }
  static constexpr uint32_t type_id() { return TYPE_ID_TIMESAMPLES; }
  static constexpr uint32_t underlying_type_id() { return TYPE_ID_TIMESAMPLES; }
  static std::string type_name() { return "TimeSamples"; }
  static std::string underlying_type_name() { return "TimeSamples"; }
  static bool is_role_type() { return false; }
  static bool is_array() { return type_id() & TYPE_ID_1D_ARRAY_BIT; }
};

}  // namespace value