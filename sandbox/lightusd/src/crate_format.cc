// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Crate format implementation

#include "lightusd/crate_format.hh"

namespace lightusd {
namespace v1 {
namespace crate {

const char* crate_data_type_name(CrateDataTypeId id) {
    switch (id) {
        case CrateDataTypeId::Invalid: return "Invalid";
        case CrateDataTypeId::Bool: return "bool";
        case CrateDataTypeId::UChar: return "uchar";
        case CrateDataTypeId::Int: return "int";
        case CrateDataTypeId::UInt: return "uint";
        case CrateDataTypeId::Int64: return "int64";
        case CrateDataTypeId::UInt64: return "uint64";
        case CrateDataTypeId::Half: return "half";
        case CrateDataTypeId::Float: return "float";
        case CrateDataTypeId::Double: return "double";
        case CrateDataTypeId::String: return "string";
        case CrateDataTypeId::Token: return "token";
        case CrateDataTypeId::AssetPath: return "asset";
        case CrateDataTypeId::Matrix2d: return "matrix2d";
        case CrateDataTypeId::Matrix3d: return "matrix3d";
        case CrateDataTypeId::Matrix4d: return "matrix4d";
        case CrateDataTypeId::Quatd: return "quatd";
        case CrateDataTypeId::Quatf: return "quatf";
        case CrateDataTypeId::Quath: return "quath";
        case CrateDataTypeId::Vec2d: return "double2";
        case CrateDataTypeId::Vec2f: return "float2";
        case CrateDataTypeId::Vec2h: return "half2";
        case CrateDataTypeId::Vec2i: return "int2";
        case CrateDataTypeId::Vec3d: return "double3";
        case CrateDataTypeId::Vec3f: return "float3";
        case CrateDataTypeId::Vec3h: return "half3";
        case CrateDataTypeId::Vec3i: return "int3";
        case CrateDataTypeId::Vec4d: return "double4";
        case CrateDataTypeId::Vec4f: return "float4";
        case CrateDataTypeId::Vec4h: return "half4";
        case CrateDataTypeId::Vec4i: return "int4";
        case CrateDataTypeId::Dictionary: return "dictionary";
        case CrateDataTypeId::TokenListOp: return "TokenListOp";
        case CrateDataTypeId::StringListOp: return "StringListOp";
        case CrateDataTypeId::PathListOp: return "PathListOp";
        case CrateDataTypeId::ReferenceListOp: return "ReferenceListOp";
        case CrateDataTypeId::IntListOp: return "IntListOp";
        case CrateDataTypeId::Int64ListOp: return "Int64ListOp";
        case CrateDataTypeId::UIntListOp: return "UIntListOp";
        case CrateDataTypeId::UInt64ListOp: return "UInt64ListOp";
        case CrateDataTypeId::PathVector: return "PathVector";
        case CrateDataTypeId::TokenVector: return "TokenVector";
        case CrateDataTypeId::Specifier: return "Specifier";
        case CrateDataTypeId::Permission: return "Permission";
        case CrateDataTypeId::Variability: return "Variability";
        case CrateDataTypeId::VariantSelectionMap: return "VariantSelectionMap";
        case CrateDataTypeId::TimeSamples: return "TimeSamples";
        case CrateDataTypeId::Payload: return "Payload";
        case CrateDataTypeId::DoubleVector: return "DoubleVector";
        case CrateDataTypeId::LayerOffsetVector: return "LayerOffsetVector";
        case CrateDataTypeId::StringVector: return "StringVector";
        case CrateDataTypeId::ValueBlock: return "ValueBlock";
        case CrateDataTypeId::Value: return "Value";
        case CrateDataTypeId::UnregisteredValue: return "UnregisteredValue";
        case CrateDataTypeId::UnregisteredValueListOp: return "UnregisteredValueListOp";
        case CrateDataTypeId::PayloadListOp: return "PayloadListOp";
        case CrateDataTypeId::TimeCode: return "TimeCode";
        default: return "Unknown";
    }
}

const char* crate_data_type_name(int32_t id) {
    return crate_data_type_name(static_cast<CrateDataTypeId>(id));
}

const char* spec_type_name(SpecType type) {
    switch (type) {
        case SpecType::Unknown: return "Unknown";
        case SpecType::Attribute: return "Attribute";
        case SpecType::Connection: return "Connection";
        case SpecType::Expression: return "Expression";
        case SpecType::Mapper: return "Mapper";
        case SpecType::MapperArg: return "MapperArg";
        case SpecType::Prim: return "Prim";
        case SpecType::PseudoRoot: return "PseudoRoot";
        case SpecType::Relationship: return "Relationship";
        case SpecType::RelationshipTarget: return "RelationshipTarget";
        case SpecType::Variant: return "Variant";
        case SpecType::VariantSet: return "VariantSet";
        default: return "Unknown";
    }
}

} // namespace crate
} // namespace v1
} // namespace lightusd
