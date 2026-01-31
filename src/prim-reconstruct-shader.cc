// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct Shader Prims - split from prim-reconstruct.cc for parallel compilation
//
#include "prim-reconstruct-internal.hh"
#include "usdShade.hh"
#include "usdMtlx.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) if (err) { (*err) = s + (*err); }
#define PushWarn(s) if (warn) { (*warn) = s + (*err); }

#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))
#define PUSH_ERROR_AND_RETURN_F(s, ...) PUSH_ERROR_AND_RETURN(fmt::format(s, __VA_ARGS__))

namespace tinyusdz {
namespace prim {

// Include implementation helpers
#include "prim-reconstruct-impl.inc"

template <>
bool ReconstructShader<ShaderNode>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    ShaderNode *node,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)options;

  if (!node) {
    return false;
  }

  // NOTE: References are not commonly used for ShaderNode (shader connections
  // are handled via properties/connections). Ignoring references here.
  (void)references;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  // Add everything to props.
  for (auto &prop : properties) {
    ADD_PROPERTY(table, prop, ShaderNode, node->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  DCOUT("ShaderNode reconstructed.");
  return true;
}

template <>
bool ReconstructShader<UsdPreviewSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPreviewSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;

  // Use centralized enum handler
  auto OpacityModeHandler = enum_handler::OpacityMode;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>
  for (auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuseColor", UsdPreviewSurface,
                         surface->diffuseColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emissiveColor", UsdPreviewSurface,
                         surface->emissiveColor)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:roughness", UsdPreviewSurface,
                         surface->roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specularColor", UsdPreviewSurface,
                         surface->specularColor)  // specular workflow
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:metallic", UsdPreviewSurface,
                         surface->metallic)  // non specular workflow
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:clearcoat", UsdPreviewSurface,
                         surface->clearcoat)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:clearcoatRoughness",
                         UsdPreviewSurface, surface->clearcoatRoughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", UsdPreviewSurface,
                         surface->opacity)
    // From 2.6
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:opacityMode",
                       UsdPreviewSurface::OpacityMode, OpacityModeHandler, UsdPreviewSurface,
                       surface->opacityMode, options.strict_allowedToken_check)

    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacityThreshold",
                         UsdPreviewSurface, surface->opacityThreshold)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:ior", UsdPreviewSurface,
                         surface->ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", UsdPreviewSurface,
                         surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:dispacement", UsdPreviewSurface,
                         surface->displacement)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:occlusion", UsdPreviewSurface,
                         surface->occlusion)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:useSpecularWorkflow",
                         UsdPreviewSurface, surface->useSpecularWorkflow)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", UsdPreviewSurface,
                   surface->outputsSurface)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:displacement", UsdPreviewSurface,
                   surface->outputsDisplacement)
    ADD_PROPERTY(table, prop, UsdPreviewSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<UsdUVTexture>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdUVTexture *texture,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;

  // Use centralized enum handlers
  auto SourceColorSpaceHandler = enum_handler::SourceColorSpace;
  auto WrapHandler = enum_handler::TextureWrap;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    DCOUT("prop.name = " << prop.first);
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:file", UsdUVTexture, texture->file)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:st", UsdUVTexture,
                          texture->st)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:sourceColorSpace",
                       UsdUVTexture::SourceColorSpace, SourceColorSpaceHandler, UsdUVTexture,
                       texture->sourceColorSpace, options.strict_allowedToken_check)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:wrapS",
                       UsdUVTexture::Wrap, WrapHandler, UsdUVTexture,
                       texture->wrapS, options.strict_allowedToken_check)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, "inputs:wrapT",
                       UsdUVTexture::Wrap, WrapHandler, UsdUVTexture,
                       texture->wrapT, options.strict_allowedToken_check)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:r", UsdUVTexture,
                                  texture->outputsR)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:g", UsdUVTexture,
                                  texture->outputsG)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:b", UsdUVTexture,
                                  texture->outputsB)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:a", UsdUVTexture,
                                  texture->outputsA)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:rgb", UsdUVTexture,
                                  texture->outputsRGB)
    ADD_PROPERTY(table, prop, UsdUVTexture, texture->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  DCOUT("UsdUVTexture reconstructed.");
  return true;
}

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

template <>
bool ReconstructShader<UsdPrimvarReader_int>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_int *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float2>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float2 *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float3>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float3 *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_float4>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_float4 *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_string>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_string *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_vector>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_vector *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_normal>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_normal *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_point>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_point *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<UsdPrimvarReader_matrix>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdPrimvarReader_matrix *preader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  return ReconstructPrimvarReaderShaderImpl(spec, properties, references, preader, warn, err, options);
}

template <>
bool ReconstructShader<MtlxAutodeskStandardSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    MtlxAutodeskStandardSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;
  (void)warn;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    // Base properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base", MtlxAutodeskStandardSurface,
                         surface->base)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_color", MtlxAutodeskStandardSurface,
                         surface->base_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:diffuse_roughness", MtlxAutodeskStandardSurface,
                         surface->diffuse_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:metalness", MtlxAutodeskStandardSurface,
                         surface->metalness)

    // Specular properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular", MtlxAutodeskStandardSurface,
                         surface->specular)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_color", MtlxAutodeskStandardSurface,
                         surface->specular_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness", MtlxAutodeskStandardSurface,
                         surface->specular_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_IOR", MtlxAutodeskStandardSurface,
                         surface->specular_IOR)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_anisotropy", MtlxAutodeskStandardSurface,
                         surface->specular_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_rotation", MtlxAutodeskStandardSurface,
                         surface->specular_rotation)

    // Transmission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission", MtlxAutodeskStandardSurface,
                         surface->transmission)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_color", MtlxAutodeskStandardSurface,
                         surface->transmission_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_depth", MtlxAutodeskStandardSurface,
                         surface->transmission_depth)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter", MtlxAutodeskStandardSurface,
                         surface->transmission_scatter)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter_anisotropy", MtlxAutodeskStandardSurface,
                         surface->transmission_scatter_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion", MtlxAutodeskStandardSurface,
                         surface->transmission_dispersion)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_extra_roughness", MtlxAutodeskStandardSurface,
                         surface->transmission_extra_roughness)

    // Subsurface properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface", MtlxAutodeskStandardSurface,
                         surface->subsurface)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_color", MtlxAutodeskStandardSurface,
                         surface->subsurface_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius", MtlxAutodeskStandardSurface,
                         surface->subsurface_radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scale", MtlxAutodeskStandardSurface,
                         surface->subsurface_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_anisotropy", MtlxAutodeskStandardSurface,
                         surface->subsurface_anisotropy)

    // Sheen properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen", MtlxAutodeskStandardSurface,
                         surface->sheen)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_color", MtlxAutodeskStandardSurface,
                         surface->sheen_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_roughness", MtlxAutodeskStandardSurface,
                         surface->sheen_roughness)

    // Coat properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat", MtlxAutodeskStandardSurface,
                         surface->coat)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_color", MtlxAutodeskStandardSurface,
                         surface->coat_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness", MtlxAutodeskStandardSurface,
                         surface->coat_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_anisotropy", MtlxAutodeskStandardSurface,
                         surface->coat_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_rotation", MtlxAutodeskStandardSurface,
                         surface->coat_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_IOR", MtlxAutodeskStandardSurface,
                         surface->coat_IOR)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_color", MtlxAutodeskStandardSurface,
                         surface->coat_affect_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_roughness", MtlxAutodeskStandardSurface,
                         surface->coat_affect_roughness)

    // Thin film properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_thickness", MtlxAutodeskStandardSurface,
                         surface->thin_film_thickness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_IOR", MtlxAutodeskStandardSurface,
                         surface->thin_film_IOR)

    // Emission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission", MtlxAutodeskStandardSurface,
                         surface->emission)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_color", MtlxAutodeskStandardSurface,
                         surface->emission_color)

    // Other properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", MtlxAutodeskStandardSurface,
                         surface->opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_walled", MtlxAutodeskStandardSurface,
                         surface->thin_walled)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", MtlxAutodeskStandardSurface,
                         surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:tangent", MtlxAutodeskStandardSurface,
                         surface->tangent)

    // Output
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:out", MtlxAutodeskStandardSurface,
                   surface->out)

    ADD_PROPERTY(table, prop, MtlxAutodeskStandardSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<MtlxOpenPBRSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    MtlxOpenPBRSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;
  (void)warn;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    // Base properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_weight", MtlxOpenPBRSurface,
                         surface->base_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_color", MtlxOpenPBRSurface,
                         surface->base_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_metalness", MtlxOpenPBRSurface,
                         surface->base_metalness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_diffuse_roughness", MtlxOpenPBRSurface,
                         surface->base_diffuse_roughness)

    // Specular properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_weight", MtlxOpenPBRSurface,
                         surface->specular_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_color", MtlxOpenPBRSurface,
                         surface->specular_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness", MtlxOpenPBRSurface,
                         surface->specular_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_ior", MtlxOpenPBRSurface,
                         surface->specular_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_anisotropy", MtlxOpenPBRSurface,
                         surface->specular_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_rotation", MtlxOpenPBRSurface,
                         surface->specular_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness_anisotropy", MtlxOpenPBRSurface,
                         surface->specular_roughness_anisotropy)

    // Transmission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_weight", MtlxOpenPBRSurface,
                         surface->transmission_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_color", MtlxOpenPBRSurface,
                         surface->transmission_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_depth", MtlxOpenPBRSurface,
                         surface->transmission_depth)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter", MtlxOpenPBRSurface,
                         surface->transmission_scatter)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter_anisotropy", MtlxOpenPBRSurface,
                         surface->transmission_scatter_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion", MtlxOpenPBRSurface,
                         surface->transmission_dispersion)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion_abbe_number", MtlxOpenPBRSurface,
                         surface->transmission_dispersion_abbe_number)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion_scale", MtlxOpenPBRSurface,
                         surface->transmission_dispersion_scale)

    // Subsurface properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_weight", MtlxOpenPBRSurface,
                         surface->subsurface_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_color", MtlxOpenPBRSurface,
                         surface->subsurface_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius", MtlxOpenPBRSurface,
                         surface->subsurface_radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius_scale", MtlxOpenPBRSurface,
                         surface->subsurface_radius_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scale", MtlxOpenPBRSurface,
                         surface->subsurface_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_anisotropy", MtlxOpenPBRSurface,
                         surface->subsurface_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scatter_anisotropy", MtlxOpenPBRSurface,
                         surface->subsurface_scatter_anisotropy)

    // Coat properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_weight", MtlxOpenPBRSurface,
                         surface->coat_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_color", MtlxOpenPBRSurface,
                         surface->coat_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness", MtlxOpenPBRSurface,
                         surface->coat_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_anisotropy", MtlxOpenPBRSurface,
                         surface->coat_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_rotation", MtlxOpenPBRSurface,
                         surface->coat_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness_anisotropy", MtlxOpenPBRSurface,
                         surface->coat_roughness_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_ior", MtlxOpenPBRSurface,
                         surface->coat_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_darkening", MtlxOpenPBRSurface,
                         surface->coat_darkening)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_color", MtlxOpenPBRSurface,
                         surface->coat_affect_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_roughness", MtlxOpenPBRSurface,
                         surface->coat_affect_roughness)

    // Fuzz properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_weight", MtlxOpenPBRSurface,
                         surface->fuzz_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_color", MtlxOpenPBRSurface,
                         surface->fuzz_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_roughness", MtlxOpenPBRSurface,
                         surface->fuzz_roughness)

    // Thin film properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_thickness", MtlxOpenPBRSurface,
                         surface->thin_film_thickness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_ior", MtlxOpenPBRSurface,
                         surface->thin_film_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_weight", MtlxOpenPBRSurface,
                         surface->thin_film_weight)

    // Emission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_luminance", MtlxOpenPBRSurface,
                         surface->emission_luminance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_color", MtlxOpenPBRSurface,
                         surface->emission_color)

    // Geometry properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_opacity", MtlxOpenPBRSurface,
                         surface->geometry_opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_thin_walled", MtlxOpenPBRSurface,
                         surface->geometry_thin_walled)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_normal", MtlxOpenPBRSurface,
                         surface->geometry_normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_tangent", MtlxOpenPBRSurface,
                         surface->geometry_tangent)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_coat_normal", MtlxOpenPBRSurface,
                         surface->geometry_coat_normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_coat_tangent", MtlxOpenPBRSurface,
                         surface->geometry_coat_tangent)

    // Output
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", MtlxOpenPBRSurface,
                   surface->surface)

    ADD_PROPERTY(table, prop, MtlxOpenPBRSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<OpenPBRSurface>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    OpenPBRSurface *surface,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)references;
  (void)options;
  (void)warn;

  std::set<std::string> table;
  table.insert("info:id"); // `info:id` is already parsed in ReconstructPrim<Shader>

  for (auto &prop : properties) {
    // Base layer properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_weight", OpenPBRSurface,
                         surface->base_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_color", OpenPBRSurface,
                         surface->base_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_roughness", OpenPBRSurface,
                         surface->base_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_metalness", OpenPBRSurface,
                         surface->base_metalness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:base_diffuse_roughness", OpenPBRSurface,
                         surface->base_diffuse_roughness)

    // Specular layer properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_weight", OpenPBRSurface,
                         surface->specular_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_color", OpenPBRSurface,
                         surface->specular_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_roughness", OpenPBRSurface,
                         surface->specular_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_ior", OpenPBRSurface,
                         surface->specular_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_ior_level", OpenPBRSurface,
                         surface->specular_ior_level)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_anisotropy", OpenPBRSurface,
                         surface->specular_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:specular_rotation", OpenPBRSurface,
                         surface->specular_rotation)

    // Transmission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_weight", OpenPBRSurface,
                         surface->transmission_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_color", OpenPBRSurface,
                         surface->transmission_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_depth", OpenPBRSurface,
                         surface->transmission_depth)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter", OpenPBRSurface,
                         surface->transmission_scatter)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_scatter_anisotropy", OpenPBRSurface,
                         surface->transmission_scatter_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:transmission_dispersion", OpenPBRSurface,
                         surface->transmission_dispersion)

    // Subsurface properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_weight", OpenPBRSurface,
                         surface->subsurface_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_color", OpenPBRSurface,
                         surface->subsurface_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_radius", OpenPBRSurface,
                         surface->subsurface_radius)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_scale", OpenPBRSurface,
                         surface->subsurface_scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:subsurface_anisotropy", OpenPBRSurface,
                         surface->subsurface_anisotropy)

    // Sheen properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_weight", OpenPBRSurface,
                         surface->sheen_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_color", OpenPBRSurface,
                         surface->sheen_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:sheen_roughness", OpenPBRSurface,
                         surface->sheen_roughness)

    // Fuzz properties (velvet/fabric-like appearance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_weight", OpenPBRSurface,
                         surface->fuzz_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_color", OpenPBRSurface,
                         surface->fuzz_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:fuzz_roughness", OpenPBRSurface,
                         surface->fuzz_roughness)

    // Thin film properties (iridescence from thin film interference)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_weight", OpenPBRSurface,
                         surface->thin_film_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_thickness", OpenPBRSurface,
                         surface->thin_film_thickness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:thin_film_ior", OpenPBRSurface,
                         surface->thin_film_ior)

    // Coat properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_weight", OpenPBRSurface,
                         surface->coat_weight)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_color", OpenPBRSurface,
                         surface->coat_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_roughness", OpenPBRSurface,
                         surface->coat_roughness)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_anisotropy", OpenPBRSurface,
                         surface->coat_anisotropy)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_rotation", OpenPBRSurface,
                         surface->coat_rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_ior", OpenPBRSurface,
                         surface->coat_ior)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_color", OpenPBRSurface,
                         surface->coat_affect_color)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:coat_affect_roughness", OpenPBRSurface,
                         surface->coat_affect_roughness)

    // Emission properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_luminance", OpenPBRSurface,
                         surface->emission_luminance)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:emission_color", OpenPBRSurface,
                         surface->emission_color)

    // Geometry properties
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:opacity", OpenPBRSurface,
                         surface->opacity)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:geometry_opacity", OpenPBRSurface,
                         surface->opacity)  // OpenPBR standard name, maps to same opacity field
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:normal", OpenPBRSurface,
                         surface->normal)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:tangent", OpenPBRSurface,
                         surface->tangent)

    // Outputs
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:surface", OpenPBRSurface,
                   surface->surface)

    ADD_PROPERTY(table, prop, OpenPBRSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructShader<UsdTransform2d>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    UsdTransform2d *transform,
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
    DCOUT("prop = " << prop.first);
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:in", UsdTransform2d,
                   transform->in)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:rotation", UsdTransform2d,
                   transform->rotation)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:scale", UsdTransform2d,
                   transform->scale)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:translation", UsdTransform2d,
                   transform->translation)
    PARSE_SHADER_TERMINAL_ATTRIBUTE(table, prop, "outputs:result",
                                  UsdTransform2d, transform->result)
    ADD_PROPERTY(table, prop, UsdPrimvarReader_float2, transform->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Shader>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Shader *shader,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)properties;
  (void)options;

  bool is_generic_shader{false};
  auto info_id_prop = properties.find("info:id");
  if (info_id_prop == properties.end()) {
    // Guess MatrialX shader. info:id will be resolved by importing referenced .mtlx.
    // Treat generic Shader at the moment.
    is_generic_shader = true;
    //PUSH_ERROR_AND_RETURN("`Shader` must contain `info:id` property.");
  }

  std::string shader_type;
  if (!is_generic_shader) {
    if (info_id_prop->second.is_attribute()) {
      const Attribute &attr = info_id_prop->second.get_attribute();
      if ((attr.type_name() == value::kToken)) {
        if (auto pv = attr.get_value<value::token>()) {
          shader_type = pv.value().str();
        } else {
          PUSH_ERROR_AND_RETURN("Internal errror. `info:id` has invalid type.");
        }
      } else {
        PUSH_ERROR_AND_RETURN("`info:id` attribute must be `token` type.");
      }

      // For some corrupted? USDZ file does not have `uniform` variability.
      if (attr.variability() != Variability::Uniform) {
        PUSH_WARN("`info:id` attribute must have `uniform` variability.");
      }
    } else {
      PUSH_ERROR_AND_RETURN("Invalid type or value for `info:id` property in `Shader`.");
    }

    DCOUT("info:id = " << shader_type);
  }


  if (shader_type.compare(kUsdPreviewSurface) == 0) {
    UsdPreviewSurface surface;
    if (!ReconstructShader<UsdPreviewSurface>(spec, properties, references,
                                              &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kUsdPreviewSurface);
    }
    shader->info_id = kUsdPreviewSurface;
    shader->value = surface;
    DCOUT("info_id = " << shader->info_id);
  } else if (shader_type.compare(kUsdUVTexture) == 0) {
    UsdUVTexture texture;
    if (!ReconstructShader<UsdUVTexture>(spec, properties, references,
                                         &texture, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kUsdUVTexture);
    }
    shader->info_id = kUsdUVTexture;
    shader->value = texture;
  } else if (shader_type.compare(kUsdPrimvarReader_int) == 0) {
    UsdPrimvarReader_int preader;
    if (!ReconstructShader<UsdPrimvarReader_int>(spec, properties, references,
                                                 &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_int);
    }
    shader->info_id = kUsdPrimvarReader_int;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float) == 0) {
    UsdPrimvarReader_float preader;
    if (!ReconstructShader<UsdPrimvarReader_float>(spec, properties, references,
                                                   &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float);
    }
    shader->info_id = kUsdPrimvarReader_float;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float2) == 0) {
    UsdPrimvarReader_float2 preader;
    if (!ReconstructShader<UsdPrimvarReader_float2>(spec, properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float2);
    }
    shader->info_id = kUsdPrimvarReader_float2;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float3) == 0) {
    UsdPrimvarReader_float3 preader;
    if (!ReconstructShader<UsdPrimvarReader_float3>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float3);
    }
    shader->info_id = kUsdPrimvarReader_float3;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_float4) == 0) {
    UsdPrimvarReader_float4 preader;
    if (!ReconstructShader<UsdPrimvarReader_float4>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_float4);
    }
    shader->info_id = kUsdPrimvarReader_float4;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_string) == 0) {
    UsdPrimvarReader_string preader;
    if (!ReconstructShader<UsdPrimvarReader_string>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_string);
    }
    shader->info_id = kUsdPrimvarReader_string;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_vector) == 0) {
    UsdPrimvarReader_vector preader;
    if (!ReconstructShader<UsdPrimvarReader_vector>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_vector);
    }
    shader->info_id = kUsdPrimvarReader_vector;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_normal) == 0) {
    UsdPrimvarReader_normal preader;
    if (!ReconstructShader<UsdPrimvarReader_normal>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_normal);
    }
    shader->info_id = kUsdPrimvarReader_normal;
    shader->value = preader;
  } else if (shader_type.compare(kUsdPrimvarReader_point) == 0) {
    UsdPrimvarReader_point preader;
    if (!ReconstructShader<UsdPrimvarReader_point>(spec,properties, references,
                                                    &preader, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdPrimvarReader_point);
    }
    shader->info_id = kUsdPrimvarReader_point;
    shader->value = preader;
  } else if (shader_type.compare(kUsdTransform2d) == 0) {
    UsdTransform2d transform;
    if (!ReconstructShader<UsdTransform2d>(spec,properties, references,
                                                    &transform, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                            << kUsdTransform2d);
    }
    shader->info_id = kUsdTransform2d;
    shader->value = transform;
  } else if (shader_type.compare(kOpenPBRSurface) == 0) {
    OpenPBRSurface surface;
    if (!ReconstructShader<OpenPBRSurface>(spec, properties, references,
                                           &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kOpenPBRSurface);
    }
    shader->info_id = kOpenPBRSurface;
    shader->value = surface;
  } else if (shader_type.compare(kMtlxAutodeskStandardSurface) == 0) {
    MtlxAutodeskStandardSurface surface;
    if (!ReconstructShader<MtlxAutodeskStandardSurface>(spec, properties, references,
                                                         &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kMtlxAutodeskStandardSurface);
    }
    shader->info_id = kMtlxAutodeskStandardSurface;
    shader->value = surface;
  } else if (shader_type.compare(kNdOpenPbrSurfaceSurfaceshader) == 0) {
    // Blender v4.5 MaterialX OpenPBR Surface export
    MtlxOpenPBRSurface surface;
    if (!ReconstructShader<MtlxOpenPBRSurface>(spec, properties, references,
                                                &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kNdOpenPbrSurfaceSurfaceshader);
    }
    shader->info_id = kNdOpenPbrSurfaceSurfaceshader;
    shader->value = surface;
  } else {
    // Reconstruct as generic ShaderNode
    ShaderNode surface;
    if (!ReconstructShader<ShaderNode>(spec,properties, references,
                                              &surface, warn, err, options)) {
      PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << shader_type);
    }
    if (shader_type.size()) {
      shader->info_id = shader_type;
    }
    shader->value = surface;
  }

  DCOUT("Shader reconstructed.");

  return true;
}

template <>
bool ReconstructPrim<Material>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Material *material,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)options;
  std::set<std::string> table;

  // NOTE: Properties with 'inputs' and 'outputs' namespace are handled as
  // generic properties and stored in Material::props. Terminal outputs like
  // `outputs:surface` are treated as input attributes with connections.

  // Check if MaterialXConfigAPI is applied
  bool hasMaterialXConfig = false;
  for (auto &prop : properties) {
    if (prop.first == "config:mtlx:version" ||
        prop.first == "config:mtlx:namespace" ||
        prop.first == "config:mtlx:colorspace" ||
        prop.first == "config:mtlx:sourceUri") {
      hasMaterialXConfig = true;
      break;
    }
  }

  // Initialize MaterialXConfigAPI if needed
  if (hasMaterialXConfig) {
    material->materialXConfig = MaterialXConfigAPI();
  }

  // For `Material`, `outputs` are terminal attribute and treated as input attribute with connection(Should be "token output:surface.connect = </path/to/shader>").
  for (auto &prop : properties) {
    // Parse MaterialXConfigAPI properties
    if (hasMaterialXConfig) {
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:version", Material,
                           material->materialXConfig->mtlx_version)
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:namespace", Material,
                           material->materialXConfig->mtlx_namespace)
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:colorspace", Material,
                           material->materialXConfig->mtlx_colorspace)
      PARSE_TYPED_ATTRIBUTE(table, prop, "config:mtlx:sourceUri", Material,
                           material->materialXConfig->mtlx_sourceUri)
    }

    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:surface",
                                  Material, material->surface)
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:displacement",
                                  Material, material->displacement)
    PARSE_SHADER_INPUT_CONNECTION_PROPERTY(table, prop, "outputs:volume",
                                  Material, material->volume)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, Material,
                       material->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, Material, material->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
  return true;
}

template <>
bool ReconstructPrim<NodeGraph>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    NodeGraph *nodegraph,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options)
{
  (void)spec;
  (void)references;
  (void)warn;
  std::set<std::string> table;

  // NodeGraph can have arbitrary outputs (e.g., outputs:result, outputs:normal, etc.)
  // They are stored in the props map, so we just add all properties
  for (auto &prop : properties) {
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, NodeGraph,
                       nodegraph->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, NodeGraph, nodegraph->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }
  return true;
}

// PrimSpec versions
#define RECONSTRUCT_PRIM_PRIMSPEC_IMPL(__prim_ty) \
template <> \
bool ReconstructPrim<__prim_ty>( \
    PrimSpec &primspec, \
    __prim_ty *prim, \
    std::string *warn, \
    std::string *err, \
    const PrimReconstructOptions &options) { \
  ReferenceList references; \
  return ReconstructPrim<__prim_ty>(primspec.specifier(), primspec.props(), references, prim, warn, err, options); \
}

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Shader)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Material)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(NodeGraph)

}  // namespace prim
}  // namespace tinyusdz
