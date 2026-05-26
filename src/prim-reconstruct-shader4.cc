// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Shader/Material/NodeGraph reconstruction specializations.
// Split from prim-reconstruct.cc
//
#include "prim-reconstruct.hh"

#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"
#include "enum-handlers.hh"
#include "prim-property-tables.hh"

#include "usdShade.hh"
#include "usdMtlx.hh"

#include "common-macros.inc"
#include "value-types.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) \
  if (err) { \
    (*err) = (s) + (err->empty() ? std::string() : std::string("\n")) + (*err); \
  }
#define PushWarn(s) \
  if (warn) { \
    (*warn) = (s) + (warn->empty() ? std::string() : std::string("\n")) + (*warn); \
  }

// __VA_ARGS__ does not allow empty, thus # of args must be 2+
#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))

namespace tinyusdz {
namespace prim {

[[maybe_unused]] constexpr auto kInputsVarname = "inputs:varname";
[[maybe_unused]] constexpr auto kPurpose = "purpose";

// MaterialX Validation Helpers
// ==========================================================================



template <typename T>
bool ReconstructShader(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);

#include "prim-reconstruct-common.inc"

// Helper macro for parsing inputs:varname with backwards compatibility
// Supports both token (older spec) and string (current spec) types
#define PARSE_PRIMVAR_READER_VARNAME(__table, __prop, __varname_attr, __err_msg_prefix) \
  if ((__prop.first == kInputsVarname) && !__table.count(kInputsVarname)) {             \
    /* Support older spec: token type for varname */                                    \
    TypedAttribute<Animatable<value::token>> tok_attr;                                  \
    auto ret = ParseTypedAttribute(__table, __prop.first, __prop.second, kInputsVarname, tok_attr); \
    if (ret.code == ParseResult::ResultCode::Success) {                                 \
      if (!ConvertTokenAttributeToStringAttribute(tok_attr, __varname_attr)) {          \
        PUSH_ERROR_AND_RETURN(__err_msg_prefix "Failed to convert inputs:varname token type to string type."); \
      }                                                                                  \
      continue;                                                                          \
    } else if (ret.code == ParseResult::ResultCode::TypeMismatch) {                     \
      /* Try parsing as string type */                                                  \
      ret = ParseTypedAttribute(__table, __prop.first, __prop.second, "inputs:varname", __varname_attr); \
      if (ret.code == ParseResult::ResultCode::Success) {                               \
        continue;                                                                        \
      } else {                                                                           \
        PUSH_ERROR_AND_RETURN(fmt::format(__err_msg_prefix "Failed to parse inputs:varname: {}", ret.err)); \
      }                                                                                  \
    }                                                                                    \
  }

// ============================================================================
// Generic PrimvarReader Shader Reconstruction
// ============================================================================
// All PrimvarReader variants (int, float, float2, float3, float4, string,
// vector, normal, point, matrix) follow identical logic - only the type differs.
// This helper eliminates ~220 lines of duplication.

template<typename PrimvarReaderT>
static bool ReconstructPrimvarReaderShaderImpl(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    PrimvarReaderT *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;
  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>
  for (auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fallback", PrimvarReaderT,
                   preader->fallback)
    PARSE_PRIMVAR_READER_VARNAME(table, prop, preader->varname, "")
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:result",
                                  PrimvarReaderT, preader->result)
    ADD_PROPERTY(table, prop, PrimvarReaderT, preader->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
  return true;
}

// All PrimvarReader variants delegate to the shared template impl above.
#define RECONSTRUCT_PRIMVAR_READER_SHADER(__type) \
template <> \
bool ReconstructShader<__type>( \
    const Specifier &spec, PropertyMap &properties, \
    const ReferenceList &references, __type *preader, \
    std::string *warn, std::string *err, \
    const PrimReconstructOptions &options) { \
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options); \
}

RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_int)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_float)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_float2)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_float3)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_float4)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_string)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_vector)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_normal)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_point)
RECONSTRUCT_PRIMVAR_READER_SHADER(UsdPrimvarReader_matrix)

#undef RECONSTRUCT_PRIMVAR_READER_SHADER

}  // namespace prim
}  // namespace tinyusdz
