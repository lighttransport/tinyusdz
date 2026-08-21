// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// TODO:
//   - [ ] Subdivision surface to polygon mesh conversion.
//     - [ ] Correctly handle primvar with 'vertex' interpolation(Use the basis
//     function of subd surface)
//   - [ ] Support material binding collection(Collection API)
//   - [ ] Support multiple skel animation
//   https://github.com/PixarAnimationStudios/OpenUSD/issues/2246
//   - [ ] Adjust normal vector computation with handness?
//   - [ ] Node xform animation
//   - [ ] Better build of index buffer
//     - [ ] Preserve the order of 'points' variable(mesh.points, Skin
//     indices/weights, BlendShape points, ...) as much as possible.
//     - Implement spatial hash
//
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_map>

#include "common-utils.hh"
#include "common-types.hh"
#include "color-management.hh"
#include "image-loader.hh"
#include "image-util.hh"
#include "image-types.hh"
#include "io-util.hh"
#include "texture-util.hh"
#include "linear-algebra.hh"
#include "math-util.inc"
#include "pprint-enum.hh"
#include "core/prim.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdVol.hh"  // OpenVDB (.vdb) loader
#include "usdShade.hh"
#include "usdLux.hh"
#include "usdMtlx.hh"
#include "value-pprint.hh"
#include "bone-util.hh"
#include "shape-to-mesh.hh"
#include "materialx-to-json.hh"
#include "mmap-array-ref.hh"
#include "security-policy.hh"
#include "shape-to-mesh.hh"

#include "../safe-arithmetic.hh"

//
#include "common-macros.inc"
#include "math-util.inc"


//
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-internal.hh"
#include "tydra/render-data-material-internal.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"
#include "value-types.hh"  // value::Mult
#include "xform.hh"        // tinyusdz::inverse

namespace tinyusdz {

namespace tydra {

namespace {

using TydraPerfClock = std::chrono::steady_clock;

static double ElapsedMs(const TydraPerfClock::time_point &start) {
  return double(std::chrono::duration_cast<std::chrono::microseconds>(
                    TydraPerfClock::now() - start)
                    .count()) *
         0.001;
}

static double NsToMs(const uint64_t ns) {
  return double(ns) * 0.000001;
}

static void AppendFloat(std::ostringstream &ss, const float v) {
  ss << std::setprecision(std::numeric_limits<float>::max_digits10) << v;
}

static void AppendVec2(std::ostringstream &ss, const vec2 &v) {
  AppendFloat(ss, v[0]);
  ss << ",";
  AppendFloat(ss, v[1]);
}

static void AppendVec3(std::ostringstream &ss, const vec3 &v) {
  AppendFloat(ss, v[0]);
  ss << ",";
  AppendFloat(ss, v[1]);
  ss << ",";
  AppendFloat(ss, v[2]);
}

static void AppendVec4(std::ostringstream &ss, const vec4 &v) {
  AppendFloat(ss, v[0]);
  ss << ",";
  AppendFloat(ss, v[1]);
  ss << ",";
  AppendFloat(ss, v[2]);
  ss << ",";
  AppendFloat(ss, v[3]);
}

static void AppendMat3(std::ostringstream &ss, const mat3 &m) {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (r || c) {
        ss << ",";
      }
      AppendFloat(ss, m.m[r][c]);
    }
  }
}

static void AppendValue(std::ostringstream &ss, const float v) {
  AppendFloat(ss, v);
}

static void AppendValue(std::ostringstream &ss, const vec3 &v) {
  AppendVec3(ss, v);
}

static void AppendTextureSignature(
    std::ostringstream &ss, const int32_t texture_id,
    const std::vector<UVTexture> &textures) {
  if (texture_id < 0 || size_t(texture_id) >= textures.size()) {
    ss << "tex:-1";
    return;
  }

  const UVTexture &t = textures[size_t(texture_id)];
  ss << "tex:image=" << t.texture_image_id
     << ",wrap=" << to_string(t.wrapS) << "/" << to_string(t.wrapT)
     << ",udim=" << (t.is_udim ? 1 : 0)
     << ",udimId=" << t.udim_texture_id
     << ",udimScale=";
  AppendVec2(ss, t.udim_uv_scale);
  ss << ",udimOffset=";
  AppendVec2(ss, t.udim_uv_offset);
  ss << ",scale=";
  AppendVec4(ss, t.scale);
  ss << ",bias=";
  AppendVec4(ss, t.bias);
  ss << ",channel=" << to_string(t.connectedOutputChannel)
     << ",varname=" << t.varname_uv
     << ",hasXform=" << (t.has_transform2d ? 1 : 0)
     << ",xform=";
  AppendMat3(ss, t.transform);
}

static std::string TextureSignature(const UVTexture &texture) {
  std::ostringstream ss;
  ss << "tex:image=" << texture.texture_image_id
     << ",wrap=" << to_string(texture.wrapS) << "/" << to_string(texture.wrapT)
     << ",udim=" << (texture.is_udim ? 1 : 0)
     << ",udimId=" << texture.udim_texture_id
     << ",udimScale=";
  AppendVec2(ss, texture.udim_uv_scale);
  ss << ",udimOffset=";
  AppendVec2(ss, texture.udim_uv_offset);
  ss << ",scale=";
  AppendVec4(ss, texture.scale);
  ss << ",bias=";
  AppendVec4(ss, texture.bias);
  ss << ",channel=" << to_string(texture.connectedOutputChannel)
     << ",varname=" << texture.varname_uv
     << ",hasXform=" << (texture.has_transform2d ? 1 : 0)
     << ",xform=";
  AppendMat3(ss, texture.transform);
  return ss.str();
}

template <typename T>
static void AppendShaderParam(std::ostringstream &ss, const char *name,
                              const ShaderParam<T> &param,
                              const std::vector<UVTexture> &textures) {
  ss << name << "=";
  AppendValue(ss, param.value);
  ss << ";";
  ss << name << "Texture=";
  AppendTextureSignature(ss, param.texture_id, textures);
  ss << ";";
}

static std::string MaterialSignature(
    const RenderMaterial &mat, const std::vector<UVTexture> &textures) {
  std::ostringstream ss;
  ss << "tag=" << int(mat.materialTag) << ";";
  ss << "disp=" << (mat.has_displacement ? 1 : 0) << ";";
  ss << "volume=" << (mat.has_volume ? 1 : 0) << ";";
  ss << "mtlxConfig=" << (mat.materialXConfig.authored ? 1 : 0) << ";"
     << mat.materialXConfig.version << ";"
     << mat.materialXConfig.name_space << ";"
     << mat.materialXConfig.colorspace << ";"
     << mat.materialXConfig.source_uri << ";";
  if (mat.surfaceShader.has_value()) {
    const PreviewSurfaceShader &s = *mat.surfaceShader;
    ss << "preview{";
    ss << "useSpecularWorkflow=" << (s.useSpecularWorkflow ? 1 : 0) << ";";
    AppendShaderParam(ss, "diffuseColor", s.diffuseColor, textures);
    AppendShaderParam(ss, "emissiveColor", s.emissiveColor, textures);
    AppendShaderParam(ss, "specularColor", s.specularColor, textures);
    AppendShaderParam(ss, "metallic", s.metallic, textures);
    AppendShaderParam(ss, "roughness", s.roughness, textures);
    AppendShaderParam(ss, "clearcoat", s.clearcoat, textures);
    AppendShaderParam(ss, "clearcoatRoughness", s.clearcoatRoughness,
                      textures);
    AppendShaderParam(ss, "opacity", s.opacity, textures);
    AppendShaderParam(ss, "opacityThreshold", s.opacityThreshold, textures);
    AppendShaderParam(ss, "ior", s.ior, textures);
    AppendShaderParam(ss, "normal", s.normal, textures);
    AppendShaderParam(ss, "displacement", s.displacement, textures);
    AppendShaderParam(ss, "occlusion", s.occlusion, textures);
    ss << "}";
  }
  if (mat.openPBRShader.has_value()) {
    const OpenPBRSurfaceShader &s = *mat.openPBRShader;
    ss << "openpbr{";
#define TINYUSDZ_APPEND_OPENPBR_PARAM(name) \
    AppendShaderParam(ss, #name, s.name, textures)
    TINYUSDZ_APPEND_OPENPBR_PARAM(base_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(base_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(base_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(base_metalness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(base_diffuse_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_ior);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_ior_level);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_rotation);
    TINYUSDZ_APPEND_OPENPBR_PARAM(specular_roughness_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_depth);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_scatter);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_scatter_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_dispersion);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_dispersion_abbe_number);
    TINYUSDZ_APPEND_OPENPBR_PARAM(transmission_dispersion_scale);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_radius);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_radius_scale);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_scale);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(subsurface_scatter_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(sheen_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(sheen_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(sheen_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(fuzz_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(fuzz_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(fuzz_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(thin_film_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(thin_film_thickness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(thin_film_ior);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_weight);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_rotation);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_ior);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_affect_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_affect_roughness);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_roughness_anisotropy);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_darkening);
    TINYUSDZ_APPEND_OPENPBR_PARAM(emission_luminance);
    TINYUSDZ_APPEND_OPENPBR_PARAM(emission_color);
    TINYUSDZ_APPEND_OPENPBR_PARAM(opacity);
    TINYUSDZ_APPEND_OPENPBR_PARAM(normal);
    TINYUSDZ_APPEND_OPENPBR_PARAM(tangent);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_normal);
    TINYUSDZ_APPEND_OPENPBR_PARAM(coat_tangent);
    TINYUSDZ_APPEND_OPENPBR_PARAM(displacement);
#undef TINYUSDZ_APPEND_OPENPBR_PARAM
    ss << "tangentRotation=";
    AppendFloat(ss, s.tangent_rotation);
    ss << ";normalMapScale=";
    AppendFloat(ss, s.normal_map_scale);
    ss << ";coatTangentRotation=";
    AppendFloat(ss, s.coat_tangent_rotation);
    ss << ";coatNormalMapScale=";
    AppendFloat(ss, s.coat_normal_map_scale);
    ss << ";nodeGraph=" << s.nodeGraphJson;
    ss << "}";
  }
  return ss.str();
}

static void RemapMaterialId(int &id, const std::vector<int> &remap) {
  if (id >= 0 && size_t(id) < remap.size()) {
    id = remap[size_t(id)];
  }
}

static void RemapMaterialSubsetIds(MaterialSubset &subset,
                                   const std::vector<int> &remap) {
  RemapMaterialId(subset.material_id, remap);
  RemapMaterialId(subset.backface_material_id, remap);
}

template <typename T>
static void RemapShaderParamTexture(ShaderParam<T> &param,
                                    const std::vector<int> &remap) {
  if (param.texture_id >= 0 && size_t(param.texture_id) < remap.size()) {
    param.texture_id = remap[size_t(param.texture_id)];
  }
}

static void RemapMaterialTextureIds(RenderMaterial &mat,
                                    const std::vector<int> &remap) {
  if (mat.surfaceShader.has_value()) {
    PreviewSurfaceShader &s = *mat.surfaceShader;
    RemapShaderParamTexture(s.diffuseColor, remap);
    RemapShaderParamTexture(s.emissiveColor, remap);
    RemapShaderParamTexture(s.specularColor, remap);
    RemapShaderParamTexture(s.metallic, remap);
    RemapShaderParamTexture(s.roughness, remap);
    RemapShaderParamTexture(s.clearcoat, remap);
    RemapShaderParamTexture(s.clearcoatRoughness, remap);
    RemapShaderParamTexture(s.opacity, remap);
    RemapShaderParamTexture(s.opacityThreshold, remap);
    RemapShaderParamTexture(s.ior, remap);
    RemapShaderParamTexture(s.normal, remap);
    RemapShaderParamTexture(s.displacement, remap);
    RemapShaderParamTexture(s.occlusion, remap);
  }

  if (mat.openPBRShader.has_value()) {
    OpenPBRSurfaceShader &s = *mat.openPBRShader;
#define TINYUSDZ_REMAP_OPENPBR_PARAM(name) RemapShaderParamTexture(s.name, remap)
    TINYUSDZ_REMAP_OPENPBR_PARAM(base_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(base_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(base_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(base_metalness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(base_diffuse_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_ior);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_ior_level);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_rotation);
    TINYUSDZ_REMAP_OPENPBR_PARAM(specular_roughness_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_depth);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_scatter);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_scatter_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_dispersion);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_dispersion_abbe_number);
    TINYUSDZ_REMAP_OPENPBR_PARAM(transmission_dispersion_scale);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_radius);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_radius_scale);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_scale);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(subsurface_scatter_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(sheen_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(sheen_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(sheen_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(fuzz_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(fuzz_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(fuzz_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(thin_film_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(thin_film_thickness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(thin_film_ior);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_weight);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_rotation);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_ior);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_affect_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_affect_roughness);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_roughness_anisotropy);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_darkening);
    TINYUSDZ_REMAP_OPENPBR_PARAM(emission_luminance);
    TINYUSDZ_REMAP_OPENPBR_PARAM(emission_color);
    TINYUSDZ_REMAP_OPENPBR_PARAM(opacity);
    TINYUSDZ_REMAP_OPENPBR_PARAM(normal);
    TINYUSDZ_REMAP_OPENPBR_PARAM(tangent);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_normal);
    TINYUSDZ_REMAP_OPENPBR_PARAM(coat_tangent);
    TINYUSDZ_REMAP_OPENPBR_PARAM(displacement);
#undef TINYUSDZ_REMAP_OPENPBR_PARAM
  }
}

}  // namespace

// Copy authored displayColor / displayOpacity primvars from an analytic Gprim
// (Cube/Sphere/Cone/Cylinder/Capsule) onto the tessellated temp GeomMesh so
// ConvertMesh applies them. Without this, analytic primitives always render
// with the default material even when the prim authors primvars:displayColor.
static void CopyDisplayPrimvarsToTempMesh(const GPrim &src, GeomMesh *dst) {
  if (!dst) return;
  GeomPrimvar pv;
  if (src.get_primvar("displayColor", &pv)) dst->set_primvar(pv);
  GeomPrimvar po;
  if (src.get_primvar("displayOpacity", &po)) dst->set_primvar(po);
}

//
// Convert GeomCube to RenderMesh by generating tessellated geometry
//
bool RenderSceneConverter::ConvertCube(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomCube &cube, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  // Extract cube size
  double size;
  if (!cube.size.get_value().get_scalar(&size)) {
    size = 2.0;  // Use default value if not available
  }

  // Generate cube mesh geometry
  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GenerateCubeMesh(size, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  // Create temporary GeomMesh with generated data
  GeomMesh temp_mesh;

  // Convert points from float3 to point3f
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);

  // Copy properties from cube
  temp_mesh.orientation = cube.orientation;
  temp_mesh.doubleSided = cube.doubleSided;

  // Set normals as face-varying primvar
  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::FaceVarying);
  }

  // Set UVs as st primvar (face-varying)
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  // Forward to ConvertMesh
  CopyDisplayPrimvarsToTempMesh(cube, &temp_mesh);
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomSphere to RenderMesh by generating tessellated geometry
//
bool RenderSceneConverter::ConvertSphere(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomSphere &sphere, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  // Extract sphere radius
  double radius;
  if (!sphere.radius.get_value().get_scalar(&radius)) {
    radius = 1.0;  // UsdGeomSphere schema fallback
  }

  // Generate sphere mesh geometry
  // Default to icosphere with 2 subdivisions (4 divisions as per user request seems to mean subdivisions)
  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  int subdivisions = env.mesh_config.sphere_subdivisions;
  if (env.mesh_config.sphere_tessellation == SphereTessellation::UV) {
    GenerateUVSphereMesh(radius, subdivisions, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);
  } else {
    GenerateIcosphereMesh(radius, subdivisions, points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);
  }

  // Create temporary GeomMesh with generated data
  GeomMesh temp_mesh;

  // Convert points from float3 to point3f
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);

  // Copy properties from sphere
  temp_mesh.orientation = sphere.orientation;
  temp_mesh.doubleSided = sphere.doubleSided;

  // Set normals as face-varying primvar
  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::FaceVarying);
  }

  // Set UVs as st primvar (face-varying)
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  // Forward to ConvertMesh
  CopyDisplayPrimvarsToTempMesh(sphere, &temp_mesh);
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomCylinder to RenderMesh
//
bool RenderSceneConverter::ConvertCylinder(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomCylinder &cylinder, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  double radius = 1.0;
  cylinder.radius.get_value().get_scalar(&radius);
  double height = 2.0;
  cylinder.height.get_value().get_scalar(&height);

  int radialSegs = 24;
  int heightSegs = 1;

  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GenerateCylinderMesh(radius, height, radialSegs, heightSegs,
                       points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  GeomMesh temp_mesh;
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);
  temp_mesh.orientation = cylinder.orientation;
  temp_mesh.doubleSided = cylinder.doubleSided;

  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::Vertex);
  }
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::Vertex);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  CopyDisplayPrimvarsToTempMesh(cylinder, &temp_mesh);
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomCone to RenderMesh
//
bool RenderSceneConverter::ConvertCone(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomCone &cone, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  double radius = 1.0;
  cone.radius.get_value().get_scalar(&radius);
  double height = 2.0;
  cone.height.get_value().get_scalar(&height);

  int radialSegs = 24;

  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GenerateConeMesh(radius, height, radialSegs,
                   points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  GeomMesh temp_mesh;
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);
  temp_mesh.orientation = cone.orientation;
  temp_mesh.doubleSided = cone.doubleSided;

  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::Vertex);
  }
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::Vertex);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  CopyDisplayPrimvarsToTempMesh(cone, &temp_mesh);
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomCapsule to RenderMesh
//
bool RenderSceneConverter::ConvertCapsule(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomCapsule &capsule, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  double radius = 0.5;
  capsule.radius.get_value().get_scalar(&radius);
  double height = 2.0;
  capsule.height.get_value().get_scalar(&height);

  int radialSegs = 24;
  int heightSegs = 1;

  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GenerateCapsuleMesh(radius, height, radialSegs, heightSegs,
                      points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  GeomMesh temp_mesh;
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);
  temp_mesh.orientation = capsule.orientation;
  temp_mesh.doubleSided = capsule.doubleSided;

  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::Vertex);
  }
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::Vertex);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  CopyDisplayPrimvarsToTempMesh(capsule, &temp_mesh);
  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

//
// Convert GeomPlane to RenderMesh
//
bool RenderSceneConverter::ConvertPlane(
    const RenderSceneConverterEnv &env, const Path &abs_prim_path,
    const GeomPlane &plane, const MaterialPath &material_path,
    const std::map<std::string, MaterialPath> &subset_material_path_map,
    const StringAndIdMap &rmaterial_map,
    const std::vector<const tinyusdz::GeomSubset *> &material_subsets,
    const std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> &blendshapes,
    RenderMesh *dstMesh) {

  double width = 1.0;
  plane.width.get_value().get_scalar(&width);
  double length = 1.0;
  plane.length.get_value().get_scalar(&length);

  int widthSegs = 1;
  int lengthSegs = 1;

  std::vector<value::float3> points_f3;
  std::vector<int> faceVertexCounts;
  std::vector<int> faceVertexIndices;
  std::vector<value::float3> normals_f3;
  std::vector<value::float2> uvs_f2;

  GeneratePlaneMesh(width, length, widthSegs, lengthSegs,
                    points_f3, faceVertexCounts, faceVertexIndices, normals_f3, uvs_f2);

  GeomMesh temp_mesh;
  std::vector<value::point3f> points;
  for (const auto &p : points_f3) {
    points.push_back(value::point3f{p[0], p[1], p[2]});
  }
  temp_mesh.points.set_value(points);
  temp_mesh.faceVertexCounts.set_value(faceVertexCounts);
  temp_mesh.faceVertexIndices.set_value(faceVertexIndices);
  temp_mesh.orientation = plane.orientation;
  temp_mesh.doubleSided = plane.doubleSided;

  {
    std::vector<value::normal3f> normal3f_data;
    for (const auto &n : normals_f3) {
      normal3f_data.push_back(value::normal3f{n[0], n[1], n[2]});
    }
    temp_mesh.normals.set_value(normal3f_data);
    temp_mesh.normals.metas().set_interpolation_enum(Interpolation::FaceVarying);
  }
  {
    GeomPrimvar primvar;
    primvar.set_name("st");
    primvar.set_interpolation(Interpolation::FaceVarying);
    std::vector<value::texcoord2f> uv_data;
    for (const auto &uv : uvs_f2) {
      uv_data.push_back(value::texcoord2f{uv[0], uv[1]});
    }
    primvar.set_value(uv_data);
    temp_mesh.set_primvar(primvar);
  }

  return ConvertMesh(env, abs_prim_path, temp_mesh, material_path,
                     subset_material_path_map, rmaterial_map,
                     material_subsets, blendshapes, dstMesh);
}

// Helper to get NodeCategory from NodeType
static NodeCategory GetNodeCategoryFromType(NodeType nodeType) {
  switch (nodeType) {
    case NodeType::Xform:
      return NodeCategory::Group;
    case NodeType::Volume:
      return NodeCategory::Geom;
    case NodeType::Mesh:
      return NodeCategory::Geom;
    case NodeType::Camera:
      return NodeCategory::Camera;
    case NodeType::SkelRoot:
    case NodeType::Skeleton:
      return NodeCategory::Skeleton;
    case NodeType::PointLight:
    case NodeType::DirectionalLight:
    case NodeType::EnvmapLight:
    case NodeType::RectLight:
    case NodeType::DiskLight:
    case NodeType::CylinderLight:
    case NodeType::GeometryLight:
      return NodeCategory::Light;
  }
  return NodeCategory::Group;  // Default
}

// Extract the common FieldAsset attributes (filePath / fieldName) from a
// field-asset prim (OpenVDBAsset / Field3DAsset / FieldAsset). Returns false if
// the prim is not a field-asset type.
static bool GetFieldAssetInfo(const tinyusdz::Prim &prim,
                              value::AssetPath *filePath,
                              std::string *fieldName) {
  const FieldAsset *fa = nullptr;
  if (const auto *openvdb_asset = prim.as<OpenVDBAsset>()) {
    fa = openvdb_asset;
  } else if (const auto *field3d_asset = prim.as<Field3DAsset>()) {
    fa = field3d_asset;
  } else if (const auto *field_asset = prim.as<FieldAsset>()) {
    fa = field_asset;
  }
  if (!fa) return false;

  if (auto fpv = fa->filePath.get_value()) {
    fpv.value().get_scalar(filePath);
  }
  if (auto fnv = fa->fieldName.get_value()) {
    value::token tk;
    if (fnv.value().get_scalar(&tk)) {
      *fieldName = tk.str();
    }
  }
  return true;
}

static const Attribute *FindShadeInput(const UsdShadePrim &shader,
                                       const char *name) {
  const auto it = shader.props.find(name);
  if (it == shader.props.end() || !it->second.is_attribute()) return nullptr;
  return it->second.get_attribute_or_null();
}

static const UsdShadePrim *GetShadeNodeData(const Prim *prim) {
  if (!prim) return nullptr;
  if (const Shader *shader = prim->as<Shader>()) {
    return shader->value.as<ShaderNode>();
  }
  if (const NodeGraph *graph = prim->as<NodeGraph>()) return graph;
  if (const Material *material = prim->as<Material>()) return material;
  return nullptr;
}

static const Attribute *ResolveShadeInput(const Stage &stage,
                                          const UsdShadePrim &node,
                                          const char *name, int depth = 0) {
  if (depth > 16) return nullptr;
  const Attribute *attr = FindShadeInput(node, name);
  if (!attr) return nullptr;
  if (!attr->has_connections()) return attr;
  const auto &connections = attr->connections();
  if (connections.size() != 1) return nullptr;
  const Path &target = connections[0];
  const Prim *target_prim = nullptr;
  if (!stage.find_prim_at_path(Path(target.prim_part(), ""), target_prim) ||
      !target_prim) return nullptr;
  const UsdShadePrim *target_node = GetShadeNodeData(target_prim);
  if (!target_node) return nullptr;
  const std::string property = target.prop_part();
  if (!property.empty()) {
    if (const Attribute *resolved = ResolveShadeInput(
            stage, *target_node, property.c_str(), depth + 1)) {
      return resolved;
    }
  }
  // Generic MaterialX constant nodes declare outputs:out without a value.
  if (const Shader *shader = target_prim->as<Shader>()) {
    if (shader->info_id.find("constant") != std::string::npos ||
        shader->info_id.find("Constant") != std::string::npos) {
      return ResolveShadeInput(stage, *target_node, "inputs:value", depth + 1);
    }
  }
  return nullptr;
}

static void ApplyVolumeShaderConstants(const Stage &stage,
                                       const UsdShadePrim &shader,
                                       float *density_scale,
                                       float albedo[3],
                                       float emission_color[3],
                                       float *emission_scale) {
  auto scalar = [&](const char *name, float *out) {
    if (const Attribute *source = FindShadeInput(shader, name)) {
      if (source->connections().size() == 1) {
        auto evaluated = EvaluateMtlxNodeGraphAsConstant(
            stage, source->connections()[0], "lin_rec709");
        if (evaluated && evaluated->n >= 1) {
          *out = evaluated->v[0];
          return;
        }
      }
    }
    const Attribute *attr = ResolveShadeInput(stage, shader, name);
    if (!attr || attr->has_timesamples()) return;
    if (auto v = attr->get_value<float>()) *out = v.value();
    else if (auto d = attr->get_value<double>()) *out = float(d.value());
  };
  auto color = [&](const char *name, float out[3]) {
    if (const Attribute *source = FindShadeInput(shader, name)) {
      if (source->connections().size() == 1) {
        auto evaluated = EvaluateMtlxNodeGraphAsConstant(
            stage, source->connections()[0], "lin_rec709");
        if (evaluated && evaluated->n >= 3) {
          out[0] = evaluated->v[0]; out[1] = evaluated->v[1];
          out[2] = evaluated->v[2];
          return;
        }
      }
    }
    const Attribute *attr = ResolveShadeInput(stage, shader, name);
    if (!attr || attr->has_timesamples()) return;
    if (auto v = attr->get_value<value::color3f>()) {
      out[0] = (*v)[0]; out[1] = (*v)[1]; out[2] = (*v)[2];
    } else if (auto v = attr->get_value<value::float3>()) {
      out[0] = (*v)[0]; out[1] = (*v)[1]; out[2] = (*v)[2];
    }
  };
  scalar("inputs:density", density_scale);
  color("inputs:scattering_color", albedo);
  color("inputs:scatter_color", albedo);
  color("inputs:emission_color", emission_color);
  color("inputs:emissionColor", emission_color);
  scalar("inputs:emission", emission_scale);
  scalar("inputs:emission_intensity", emission_scale);
  scalar("inputs:emissionIntensity", emission_scale);
  *density_scale = std::max(0.0f, *density_scale);
  *emission_scale = std::max(0.0f, *emission_scale);
}

bool RenderSceneConverter::ConvertVolume(
    const RenderSceneConverterEnv &env, const std::string &volume_abs_path,
    const Volume &volume, RenderVolume *dst) {
  if (!dst) return false;

  dst->abs_path = volume_abs_path;

  Path material_path;
  const Material *material = nullptr;
  std::string material_err;
  GetBoundMaterialCached(env.stage, Path(volume_abs_path, ""), "",
                         &material_path, &material, &material_err);
  // Dynamic UsdVol reconstruction predates MaterialBindingAPI support. Keep a
  // relationship fallback so a directly authored binding is not lost even
  // when the applied API instance was not reconstructed on this schema.
  if (!material) {
    Relationship direct_binding;
    bool has_direct_binding =
        volume.get_materialBinding(value::token(""), &direct_binding);
    if (has_direct_binding) {
      const Relationship &rel = direct_binding;
      Path target;
      if (!rel.targetPathVector.empty()) target = rel.targetPathVector[0];
      else target = rel.targetPath;
      const Prim *mat_prim = nullptr;
      if (target.is_valid() &&
          env.stage.find_prim_at_path(Path(target.prim_part(), ""), mat_prim) &&
          mat_prim) {
        material = mat_prim->as<Material>();
      }
    }
  }
  if (material) {
    std::vector<Path> connections;
    if (material->volume.authored()) {
      connections = material->volume.get_connections();
    } else {
      const auto output = material->props.find("outputs:volume");
      if (output != material->props.end() && output->second.is_attribute()) {
        if (const Attribute *attr = output->second.get_attribute_or_null()) {
          connections = attr->connections();
        }
      }
    }
    if (connections.size() == 1) {
      const Prim *shader_prim = nullptr;
      std::string lookup_err;
      if (env.stage.find_prim_at_path(
              Path(connections[0].prim_part(), ""), shader_prim,
              &lookup_err) && shader_prim) {
        if (const Shader *shader = shader_prim->as<Shader>()) {
          if (const ShaderNode *node = shader->value.as<ShaderNode>()) {
            ApplyVolumeShaderConstants(env.stage, *node,
                                        &dst->density_scale, dst->albedo,
                                        dst->emission_color,
                                        &dst->emission_scale);
          }
        }
      }
    }
  }

  const AssetResolutionResolver &assetResolver = env.asset_resolver;

  for (const auto &item : volume.fieldRelationships) {
    const std::string &field_name = item.first;
    const Relationship &rel = item.second;

    // Resolve the field-asset prim path from the relationship target.
    Path target_path;
    if (!rel.targetPathVector.empty()) {
      target_path = rel.targetPathVector[0];
    } else {
      target_path = rel.targetPath;
    }
    const std::string target_prim = target_path.prim_part();
    if (target_prim.empty()) {
      DCOUT("field:" << field_name << " relationship has no target; skip.");
      continue;
    }

    const Prim *fieldPrim = nullptr;
    {
      auto pv = env.stage.GetPrimAtPath(Path(target_prim, /* prop */ ""));
      if (pv) {
        fieldPrim = pv.value();
      }
    }
    if (!fieldPrim) {
      DCOUT("field-asset prim not found: " << target_prim);
      continue;
    }

    value::AssetPath filePath;
    std::string vdb_field_name = field_name;  // default to the rel name
    if (!GetFieldAssetInfo(*fieldPrim, &filePath, &vdb_field_name)) {
      DCOUT("target prim is not a field-asset: " << target_prim);
      continue;
    }
    if (filePath.GetAssetPath().empty()) {
      DCOUT("field-asset has empty filePath: " << target_prim);
      continue;
    }

    // Resolve + open the .vdb asset.
    std::string sanitized = utils::SanitizeAssetPath(
        filePath.GetAssetPath(), assetResolver.get_allow_parent_relative_paths());
    if (sanitized.empty()) {
      DCOUT("Unsafe vdb asset path: " << filePath.GetAssetPath());
      continue;
    }
    std::string resolvedPath = assetResolver.resolve(sanitized);
    if (resolvedPath.empty()) {
      DCOUT("Failed to resolve vdb asset path: " << filePath.GetAssetPath());
      continue;
    }

    Asset asset;
    std::string aerr, awarn;
    if (!assetResolver.open_asset(resolvedPath, sanitized, &asset, &awarn,
                                  &aerr)) {
      DCOUT("Failed to open vdb asset: " << resolvedPath << " : " << aerr);
      continue;
    }

    // Decode the .vdb into dense float grids.
    std::vector<usdVol::VDBGrid> grids;
    std::string vwarn, verr;
    if (!usdVol::ReadVDBFromMemory(asset.data(), asset.size(), resolvedPath,
                                   &grids, &vwarn, &verr)) {
      DCOUT("Failed to decode vdb: " << resolvedPath << " : " << verr);
      continue;
    }
    if (grids.empty()) continue;

    // Pick the grid whose name matches the requested field, else the first.
    const usdVol::VDBGrid *g = nullptr;
    for (const auto &gg : grids) {
      if (gg.name == vdb_field_name) {
        g = &gg;
        break;
      }
    }
    if (!g) g = &grids[0];
    if (g->data.empty() || g->dim[0] <= 0 || g->dim[1] <= 0 || g->dim[2] <= 0) {
      continue;
    }

    RenderVolumeField f;
    f.field_name = field_name;
    f.field_data_type = g->value_type;
    f.background = g->background;
    for (int a = 0; a < 3; a++) {
      f.dim[a] = g->dim[a];
      f.origin[a] = g->origin[a];
      f.voxel_size[a] = float(g->voxel_size[a]);
      f.world_translation[a] = float(g->world_translation[a]);
      // Object-space AABB of the grid.
      f.bounds_min[a] =
          float(g->origin[a]) * f.voxel_size[a] + f.world_translation[a];
      f.bounds_max[a] = float(g->origin[a] + g->dim[a]) * f.voxel_size[a] +
                        f.world_translation[a];
    }

    // Store the dense float voxels in a BufferData.
    BufferData buf;
    buf.componentType = ComponentType::Float;
    size_t byte_size;
    if (!safe::mul(g->data.size(), sizeof(float), &byte_size)) {
      DCOUT("VDB grid data size overflow.");
      continue;
    }
    std::vector<uint8_t> bytes(byte_size);
    std::memcpy(bytes.data(), g->data.data(), bytes.size());
    SetBufferDataBytes(buf, std::move(bytes));
    f.buffer_id = int64_t(buffers.size());
    buffers.push_back(std::move(buf));

    dst->fields.push_back(std::move(f));
  }

  return true;
}

bool RenderSceneConverter::BuildSingleNode(
    const RenderSceneConverterEnv &env, const std::string &primPath,
    const XformNode &node, Node &out_rnode) {
  Node rnode;

  const tinyusdz::Prim *prim = node.prim;
  if (prim) {
    rnode.prim_name = prim->element_name();
    rnode.abs_path = primPath;
    rnode.display_name = prim->metas().has_displayName() ? prim->metas().get_displayName() : "";

    DCOUT("rnode.prim_name " << rnode.prim_name);
    DCOUT("node.local_mat " << node.get_local_matrix());
    DCOUT("node.has_resetXform " << node.has_resetXformStack());
    DCOUT("prim.type_name " << prim->type_name());
    DCOUT("prim.type_id " << prim->type_id());
    DCOUT("xform " << value::TYPE_ID_GEOM_XFORM);

    // NOTE: this ~13-branch else-if chain on prim->type_id() was converted
    // to standalone ifs -- same MSVC C1061 ("blocks nested too deeply")
    // risk class already fixed for the same reason elsewhere in this
    // codebase. Unlike some sibling fixes, EVERY condition here (not just
    // the final fallback) needs an explicit `!matched &&` prefix: the range
    // check below (prim->type_id() > TYPE_ID_MODEL_BEGIN && < GEOM_END) can
    // structurally overlap several of the specific-type branches above it
    // (Mesh/Volume/Camera/Xform/Scope/Model/parametric prims are all
    // plausibly within that range), and -- unlike a loop with break/continue
    // -- nothing else here would stop a naive flatten from double-executing
    // node setup for a type_id() that satisfies both. The `!matched &&`
    // guard reproduces the original chain's exact "first match wins"
    // semantics regardless of any such overlap.
    bool matched = false;

    if (prim->type_id() == value::TYPE_ID_GEOM_MESH) {
      matched = true;
      // GeomMesh(GPrim) also has xform.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();

      if (auto mesh_it = meshMap.find(primPath); mesh_it != meshMap.s_end()) {
        rnode.id = int32_t(mesh_it->second);
      } else {
        rnode.id = -1;
      }

      // Note: MeshLightAPI is now handled in ConvertMesh, which sets
      // mesh.is_area_light = true and stores light properties directly in RenderMesh
    }
    if (!matched && prim->type_id() == value::TYPE_ID_VOLUME) {
      matched = true;
      // UsdVol Volume: decode referenced .vdb field(s) into a RenderVolume.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Volume;

      const Volume *vol = prim->as<Volume>();
      if (vol) {
        RenderVolume rvol;
        rvol.prim_name = prim->element_name();
        rvol.abs_path = primPath;
        rvol.display_name = prim->metas().has_displayName()
                                ? prim->metas().get_displayName()
                                : "";
        rvol.world_matrix = node.get_world_matrix();
        // Best-effort: keep the node even if some/all fields fail to decode.
        ConvertVolume(env, primPath, *vol, &rvol);

        size_t vol_id = volumes.size();
        volumeMap.add(primPath, vol_id);
        volumes.push_back(std::move(rvol));
        rnode.id = int32_t(vol_id);
      } else {
        rnode.id = -1;
      }
    }
    if (!matched && prim->type_id() == value::TYPE_ID_GEOM_CAMERA) {
      matched = true;
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Camera;

      const GeomCamera *geomCamera = prim->as<GeomCamera>();
      if (geomCamera) {
        RenderCamera rcam;
        rcam.name = prim->element_name();
        rcam.abs_path = primPath;
        rcam.display_name = prim->metas().has_displayName() ? prim->metas().get_displayName() : "";

        // Extract lens properties
        float val_f;
        if (geomCamera->focalLength.get_value().get_scalar(&val_f)) {
          rcam.focalLength = val_f;
        }
        if (geomCamera->verticalAperture.get_value().get_scalar(&val_f)) {
          rcam.verticalAperture = val_f;
        }
        if (geomCamera->horizontalAperture.get_value().get_scalar(&val_f)) {
          rcam.horizontalAperture = val_f;
        }
        if (geomCamera->horizontalApertureOffset.get_value().get_scalar(&val_f)) {
          rcam.horizontalApertureOffset = val_f;
        }
        if (geomCamera->verticalApertureOffset.get_value().get_scalar(&val_f)) {
          rcam.verticalApertureOffset = val_f;
        }
        if (geomCamera->exposure.get_value().get_scalar(&val_f)) {
          rcam.exposure = val_f;
        }
        if (geomCamera->focusDistance.get_value().get_scalar(&val_f)) {
          rcam.focusDistance = val_f;
        }
        if (geomCamera->fStop.get_value().get_scalar(&val_f)) {
          rcam.fStop = val_f;
        }

        value::float2 range_val;
        if (geomCamera->clippingRange.get_value().get_scalar(&range_val)) {
          rcam.znear = range_val[0];
          rcam.zfar = range_val[1];
        }

        GeomCamera::Projection proj_val;
        if (geomCamera->projection.get_value().get_scalar(&proj_val)) {
          rcam.projection = proj_val;
        }
        rcam.stereoRole = geomCamera->stereoRole.get_value();
        geomCamera->shutterOpen.get_value().get_scalar(&rcam.shutterOpen);
        geomCamera->shutterClose.get_value().get_scalar(&rcam.shutterClose);
        if (geomCamera->clippingPlanes.authored()) {
          auto planes = geomCamera->clippingPlanes.get_value();
          if (planes.has_value()) {
            planes->get_default(&rcam.clippingPlanes);
          }
        }

        size_t cam_id = cameras.size();
        cameraMap.add(primPath, cam_id);
        cameras.push_back(std::move(rcam));
        rnode.id = int32_t(cam_id);
      } else {
        rnode.id = -1;
      }
    }
    if (!matched && prim->type_id() == value::TYPE_ID_GEOM_XFORM) {
      matched = true;
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      DCOUT("rnode.local_matrix " << rnode.local_matrix);
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }
    if (!matched && prim->type_id() == value::TYPE_ID_SCOPE) {
      matched = true;
      // NOTE: get_local_matrix() should return identity matrix.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }
    if (!matched && prim->type_id() == value::TYPE_ID_MODEL) {
      matched = true;
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }
    if (!matched &&
        (prim->type_id() == value::TYPE_ID_GEOM_CUBE || prim->type_id() == value::TYPE_ID_GEOM_SPHERE ||
         prim->type_id() == value::TYPE_ID_GEOM_CYLINDER || prim->type_id() == value::TYPE_ID_GEOM_CONE ||
         prim->type_id() == value::TYPE_ID_GEOM_CAPSULE || prim->type_id() == value::TYPE_ID_GEOM_PLANE)) {
      matched = true;
      // Parametric primitives are converted to meshes
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.nodeType = NodeType::Mesh;
      rnode.has_resetXform = node.has_resetXformStack();

      if (auto mesh_it = meshMap.find(primPath); mesh_it != meshMap.s_end()) {
        rnode.id = int32_t(mesh_it->second);
      } else {
        rnode.id = -1;
      }
    }
    if (!matched && (prim->type_id() > value::TYPE_ID_MODEL_BEGIN) && (prim->type_id() < value::TYPE_ID_GEOM_END)) {
      matched = true;
      // Other Geom prims (e.g. GeomCone, GeomCylinder) - not yet converted to meshes
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }
    if (!matched && IsLightPrim(*prim)) {
      matched = true;
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();

      // Convert USD light to RenderLight and add to scene
      RenderLight rlight;
      bool light_converted = false;
      std::string light_abs_path = primPath;
      Path lightPath(light_abs_path, /* prop_part */ "");

      if (prim->type_id() == value::TYPE_ID_LUX_SPHERE) {
        const SphereLight *sphereLight = prim->as<SphereLight>();
        if (sphereLight) {
          if (!ConvertSphereLight(env, lightPath, *sphereLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::PointLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DISTANT) {
        const DistantLight *distantLight = prim->as<DistantLight>();
        if (distantLight) {
          if (!ConvertDistantLight(env, lightPath, *distantLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::DirectionalLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DOME) {
        const DomeLight *domeLight = prim->as<DomeLight>();
        if (domeLight) {
          if (!ConvertDomeLight(env, lightPath, *domeLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::EnvmapLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_RECT) {
        const RectLight *rectLight = prim->as<RectLight>();
        if (rectLight) {
          if (!ConvertRectLight(env, lightPath, *rectLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::RectLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_DISK) {
        const DiskLight *diskLight = prim->as<DiskLight>();
        if (diskLight) {
          if (!ConvertDiskLight(env, lightPath, *diskLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::DiskLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_CYLINDER) {
        const CylinderLight *cylinderLight = prim->as<CylinderLight>();
        if (cylinderLight) {
          if (!ConvertCylinderLight(env, lightPath, *cylinderLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::CylinderLight;
          light_converted = true;
        }
      } else if (prim->type_id() == value::TYPE_ID_LUX_GEOMETRY) {
        const GeometryLight *geometryLight = prim->as<GeometryLight>();
        if (geometryLight) {
          if (!ConvertGeometryLight(env, lightPath, *geometryLight, &rlight)) {
            return false;
          }
          rnode.nodeType = NodeType::GeometryLight;
          light_converted = true;
        }
      } else {
        // Unsupported light type
        DCOUT("Unsupported light type: " << prim->type_name());
        rnode.nodeType = NodeType::Xform;
      }

      if (light_converted) {
        // Copy world transform to the light
        // rnode.global_matrix is a matrix4d, rlight.transform is mat4 (float)
        const auto &m = rnode.global_matrix;
        rlight.transform.m[0][0] = float(m.m[0][0]);
        rlight.transform.m[0][1] = float(m.m[0][1]);
        rlight.transform.m[0][2] = float(m.m[0][2]);
        rlight.transform.m[0][3] = float(m.m[0][3]);
        rlight.transform.m[1][0] = float(m.m[1][0]);
        rlight.transform.m[1][1] = float(m.m[1][1]);
        rlight.transform.m[1][2] = float(m.m[1][2]);
        rlight.transform.m[1][3] = float(m.m[1][3]);
        rlight.transform.m[2][0] = float(m.m[2][0]);
        rlight.transform.m[2][1] = float(m.m[2][1]);
        rlight.transform.m[2][2] = float(m.m[2][2]);
        rlight.transform.m[2][3] = float(m.m[2][3]);
        rlight.transform.m[3][0] = float(m.m[3][0]);
        rlight.transform.m[3][1] = float(m.m[3][1]);
        rlight.transform.m[3][2] = float(m.m[3][2]);
        rlight.transform.m[3][3] = float(m.m[3][3]);

        // Extract position from transform (translation column)
        rlight.position[0] = float(m.m[3][0]);
        rlight.position[1] = float(m.m[3][1]);
        rlight.position[2] = float(m.m[3][2]);

        // Extract direction from transform (light faces -Z in local space)
        // Direction is the negative of the Z column (third column) of the rotation part
        rlight.direction[0] = -float(m.m[2][0]);
        rlight.direction[1] = -float(m.m[2][1]);
        rlight.direction[2] = -float(m.m[2][2]);

        // Add light to the lights array
        size_t light_id = lights.size();
        lightMap.add(light_abs_path, light_id);
        lights.push_back(std::move(rlight));
        rnode.id = int32_t(light_id);
      } else {
        rnode.id = -1;
      }
    }
    if (!matched && prim->type_id() == value::TYPE_ID_SKEL_ROOT) {
      matched = true;
      // UsdSkelRoot: encapsulation prim for skinned subtree.
      // SkelRoot is Xformable and its world transform (skelLocalToWorld)
      // positions the skinned result in world space.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::SkelRoot;
    }
    if (!matched && prim->type_id() == value::TYPE_ID_SKELETON) {
      matched = true;
      // UsdSkeleton: joint hierarchy with bindTransforms and restTransforms.
      // Skeleton is Xformable; its world transform contributes to
      // skelLocalToWorld for positioning skinned results.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Skeleton;
    }
    if (!matched && prim->type_id() == value::TYPE_ID_GEOM_POINT_INSTANCER) {
      matched = true;
      // UsdGeomPointInstancer: the instancer prim itself is an Xform node with
      // no directly-attached geometry (id == -1). Its instances are expanded
      // into RenderScene::instances (see ExpandPointInstancer); the instancer's
      // world transform recorded here is used as the instance space origin.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }
    if (!matched) {
      // ignore other node types.
      DCOUT("Unknown/Unsupported prim. " << prim->type_name());

      // Setup as xform for now.
      rnode.local_matrix = node.get_local_matrix();
      rnode.global_matrix = node.get_world_matrix();
      rnode.has_resetXform = node.has_resetXformStack();
      rnode.nodeType = NodeType::Xform;
    }

    // Set category based on nodeType
    rnode.category = GetNodeCategoryFromType(rnode.nodeType);

    // AOUSD Spec 11.3.3: Mark instance prims
    if (prim->IsInstance() && prim->HasCompositionArcs()) {
      rnode.is_instance = true;
      int proto_idx = env.stage.GetPrototypeIndex(
          Path(primPath, /* prop_part */ ""));
      rnode.prototype_index = proto_idx;
    }
  }

  out_rnode = std::move(rnode);

  return true;
}

bool RenderSceneConverter::BuildNodeHierarchyIterative(
    const RenderSceneConverterEnv &env, const std::string &parentPrimPath,
    const XformNode &root_node, Node &out_rnode) {

  // Use a post-order iterative approach:
  // 1. Process nodes in DFS order, building a flat list of (node_data, child_count)
  // 2. Then assemble the tree from leaves up using a result stack.

  struct FlatEntry {
    Node node;           // node data (without children)
    size_t child_count;  // number of direct children
  };

  // Phase 1: DFS to build flat entries in pre-order
  struct WorkItem {
    const XformNode* xform_node;
    std::string parent_path;
  };

  std::vector<FlatEntry> flat;
  std::vector<WorkItem> stack;
  stack.push_back({&root_node, parentPrimPath});

  constexpr size_t kMaxIter = 1024 * 1024;
  size_t iter = 0;

  while (!stack.empty() && iter++ < kMaxIter) {
    WorkItem item = std::move(stack.back());
    stack.pop_back();

    std::string primPath;
    if (item.parent_path.empty()) {
      primPath = "/" + item.xform_node->element_name;
    } else {
      primPath = item.parent_path + "/" + item.xform_node->element_name;
    }

    Node rnode;
    if (!BuildSingleNode(env, primPath, *item.xform_node, rnode)) {
      return false;
    }

    flat.push_back({std::move(rnode), item.xform_node->children.size()});

    // Push children in reverse order so first child is processed first
    const auto& children = item.xform_node->children;
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back({&(*it), primPath});
    }
  }

  // Phase 2: Assemble tree from flat list using a result stack.
  // flat is in pre-order. We process from the end (leaves first).
  // Each entry knows its child_count; we pop that many from the result stack.
  std::vector<Node> result_stack;
  for (size_t i = flat.size(); i > 0; --i) {
    auto& entry = flat[i - 1];
    // The last child_count items on result_stack are this node's children
    // (in reverse order because we process right-to-left)
    entry.node.children.resize(entry.child_count);
    for (size_t c = 0; c < entry.child_count; c++) {
      entry.node.children[c] = std::move(result_stack.back());
      result_stack.pop_back();
    }
    result_stack.push_back(std::move(entry.node));
  }

  if (!result_stack.empty()) {
    out_rnode = std::move(result_stack.back());
  }

  return true;
}

//

bool RenderSceneConverter::BuildNodeHierarchy(
    const RenderSceneConverterEnv &env, const XformNode &root) {
  std::string defaultRootNode = env.stage.metas().defaultPrim.str();

  default_node = -1;

  for (const auto &rootNode : root.children) {
    Node root_node;
    if (!BuildNodeHierarchyIterative(env, /* root */ "", rootNode, root_node)) {
      return false;
    }

    if (defaultRootNode == rootNode.element_name) {
      default_node = int(root_nodes.size());
    }

    root_nodeMap.add("/" + rootNode.element_name, root_nodes.size());
    root_nodes.push_back(root_node);
  }

  return true;
}

bool RenderSceneConverter::ResolveBlendShapeAnimationTargets() {
  struct MeshNodeRef {
    int32_t node_index{-1};
    int32_t mesh_id{-1};
    std::string abs_path;
  };

  std::vector<MeshNodeRef> mesh_nodes;
  int32_t node_index = 0;
  std::function<void(const Node &, int)> collectMeshNodes =
      [&](const Node &node, int depth) {
        if (depth > 4096) return;
        const int32_t current_index = node_index++;
        if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
            size_t(node.id) < meshes.size()) {
          MeshNodeRef ref;
          ref.node_index = current_index;
          ref.mesh_id = node.id;
          ref.abs_path = node.abs_path;
          mesh_nodes.push_back(std::move(ref));
        }
        for (const Node &child : node.children) {
          collectMeshNodes(child, depth + 1);
        }
      };

  for (const Node &root : root_nodes) {
    collectMeshNodes(root, /*depth*/ 0);
  }

  auto meshMatchesChannel = [&](const RenderMesh &mesh,
                                const AnimationChannel &channel) {
    if (mesh.skel_id != channel.skeleton_id) {
      return false;
    }
    if (channel.blendshape_target_names.empty()) {
      return !mesh.targets.empty();
    }
    for (const std::string &name : channel.blendshape_target_names) {
      if (mesh.targets.find(name) == mesh.targets.end()) {
        return false;
      }
    }
    return true;
  };

  size_t resolved_count = 0;
  for (AnimationClip &clip : animations) {
    const size_t original_channel_count = clip.channels.size();
    std::vector<AnimationChannel> extra_channels;

    for (size_t ch_idx = 0; ch_idx < original_channel_count; ch_idx++) {
      AnimationChannel &channel = clip.channels[ch_idx];
      if (channel.path != AnimationPath::Weights ||
          channel.target_type != ChannelTargetType::SceneNode ||
          channel.target_node >= 0 || channel.skeleton_id < 0) {
        continue;
      }

      std::vector<MeshNodeRef> matches;
      for (const MeshNodeRef &ref : mesh_nodes) {
        const RenderMesh &mesh = meshes[size_t(ref.mesh_id)];
        if (meshMatchesChannel(mesh, channel)) {
          matches.push_back(ref);
        }
      }

      if (matches.empty()) {
        PushWarn(fmt::format(
            "Could not resolve blendShapeWeights target for animation {} "
            "skeleton_id {}.",
            clip.abs_path, channel.skeleton_id));
        continue;
      }

      channel.target_node = matches[0].node_index;
      channel.target_prim_path = matches[0].abs_path;
      resolved_count++;

      for (size_t i = 1; i < matches.size(); i++) {
        AnimationChannel duplicate = channel;
        duplicate.target_node = matches[i].node_index;
        duplicate.target_prim_path = matches[i].abs_path;
        extra_channels.push_back(std::move(duplicate));
        resolved_count++;
      }
    }

    if (!extra_channels.empty()) {
      clip.channels.insert(clip.channels.end(), extra_channels.begin(),
                           extra_channels.end());
    }

    std::set<int32_t> animated_nodes;
    for (const AnimationChannel &channel : clip.channels) {
      if (channel.target_type == ChannelTargetType::SceneNode &&
          channel.target_node >= 0) {
        animated_nodes.insert(channel.target_node);
      }
    }
    if (!animated_nodes.empty()) {
      clip.num_animated_nodes = int32_t(animated_nodes.size());
    }
  }

  if (resolved_count > 0) {
    PushInfo("Resolved " + std::to_string(resolved_count) +
             " blendShapeWeights animation target(s).");
  }

  return true;
}

bool RenderSceneConverter::GetBoundMaterialCached(
    const Stage &stage, const Path &abs_path,
    const std::string &purpose, Path *materialPath,
    const Material **material, std::string *err) {
  // Build cache key: "prim_path\0purpose"
  std::string key = abs_path.full_path_name();
  key.push_back('\0');
  key += purpose;

  auto it = _materialBindingCache.find(key);
  if (it != _materialBindingCache.end()) {
    if (!it->second.error.empty()) {
      if (err) {
        (*err) += it->second.error;
      }
      return false;
    }

    if (it->second.found) {
      *materialPath = it->second.materialPath;
      *material = it->second.material;
    }
    return it->second.found;
  }

  std::string local_err;
  bool found = GetBoundMaterial(stage, abs_path, purpose,
                                materialPath, material, &local_err);

  MaterialBindingCacheEntry entry;
  entry.found = found;
  if (found) {
    entry.materialPath = *materialPath;
    entry.material = *material;
  }
  entry.error = local_err;
  _materialBindingCache[key] = entry;

  if (!local_err.empty() && err) {
    (*err) += local_err;
  }

  return found;
}

namespace {
// Clears the converter's streaming-sink pointer on every exit path of
// ConvertToRenderSceneImpl (works without exceptions). Declared at the top of
// the function so all `return`s (incl. PUSH_ERROR_AND_RETURN) run the dtor.
struct SinkScopeGuard {
  const RenderSceneSink **slot;
  ~SinkScopeGuard() { *slot = nullptr; }
};
}  // namespace

bool RenderSceneConverter::ConvertToRenderScene(
    const RenderSceneConverterEnv &env, RenderScene *scene) {
  return ConvertToRenderSceneImpl(env, scene, /* sink */ nullptr);
}

bool RenderSceneConverter::ConvertToRenderSceneStreaming(
    const RenderSceneConverterEnv &env, const RenderSceneSink &sink,
    RenderScene *scene) {
  return ConvertToRenderSceneImpl(env, scene, &sink);
}

bool RenderSceneConverter::ConvertToRenderSceneImpl(
    const RenderSceneConverterEnv &env, RenderScene *scene,
    const RenderSceneSink *sink) {
  _sink = sink;
  SinkScopeGuard _sink_guard{&_sink};

  if (!scene) {
    PUSH_ERROR_AND_RETURN("nullptr for RenderScene argument.");
  }

  color_management::RenderingColorConfig rendering_color;
  std::string color_warning;
  if (!color_management::ResolveRenderingColorConfig(
          env.stage, env.material_config.render_settings_path,
          &rendering_color, &color_warning)) {
    PUSH_ERROR_AND_RETURN("Failed to resolve rendering color configuration.");
  }
  _working_color_space = rendering_color.working_space;
  if (!color_warning.empty()) PushWarn(color_warning);

  const auto total_start = TydraPerfClock::now();
  double count_ms = 0.0;
  double skel_map_ms = 0.0;
  double xform_ms = 0.0;
  double visit_prims_ms = 0.0;
  double standalone_skel_ms = 0.0;
  double skel_anim_ms = 0.0;
  double hierarchy_ms = 0.0;
  double xform_anim_ms = 0.0;
  double merge_ms = 0.0;
  double instance_map_ms = 0.0;
  double stage_meta_ms = 0.0;

  // Reset progress state
  _progress_info = DetailedProgressInfo{};
  _timing_info.clear();

  // Clear lookup caches from previous conversion
  _skelPathToIndex.clear();
  _animPathToIndex.clear();
  _skelNameToIndexCache.clear();
  _skelRootToSkeleton.clear();
  _uvNameCache.clear();
  _materialBindingCache.clear();
  _materialSourceSignatureCache.clear();
  _value_clip_layer_cache.clear();
  _value_clip_stage_cache.clear();
  ResetConnectionResolveCache(env.stage);

  // Report initial progress
  if (!CallProgressCallback(0.0f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // Count meshes and materials before conversion for accurate progress reporting
  // Single-pass traversal: walk the stage tree once and classify prims by type_id
  DCOUT("[Tydra] Counting primitives...");
  PathPrimMap<GeomMesh> meshPrimMap;
  PathPrimMap<GeomCube> cubePrimMap;
  PathPrimMap<GeomSphere> spherePrimMap;
  PathPrimMap<GeomCylinder> cylinderPrimMap;
  PathPrimMap<GeomCone> conePrimMap;
  PathPrimMap<GeomCapsule> capsulePrimMap;
  PathPrimMap<GeomPlane> planePrimMap;
  PathPrimMap<Material> materialPrimMap;
  PathPrimMap<Skeleton> allSkeletons;
  PathPrimMap<SkelRoot> allSkelRoots;
  PathPrimMap<SkelAnimation> allAnimations;
  PathPrimMap<GeomPointInstancer> pointInstancerPrimMap;

  {
    const auto phase_start = TydraPerfClock::now();
    // Iterative stack-based traversal visiting each prim exactly once
    struct StackEntry {
      const Prim *parent;
      size_t child_idx;
      size_t parent_path_len;
    };
    std::vector<StackEntry> stack;
    stack.reserve(64);
    std::string path_buf;
    path_buf.reserve(256);

    auto classifyPrim = [&](const Prim &prim) {
      switch (prim.type_id()) {
        case value::TYPE_ID_GEOM_MESH:
          if (const auto *p = prim.as<GeomMesh>()) meshPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_CUBE:
          if (const auto *p = prim.as<GeomCube>()) cubePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_SPHERE:
          if (const auto *p = prim.as<GeomSphere>()) spherePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_CYLINDER:
          if (const auto *p = prim.as<GeomCylinder>()) cylinderPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_CONE:
          if (const auto *p = prim.as<GeomCone>()) conePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_CAPSULE:
          if (const auto *p = prim.as<GeomCapsule>()) capsulePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_PLANE:
          if (const auto *p = prim.as<GeomPlane>()) planePrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_MATERIAL:
          if (const auto *p = prim.as<Material>()) materialPrimMap[path_buf] = p;
          break;
        case value::TYPE_ID_SKELETON:
          if (const auto *p = prim.as<Skeleton>()) allSkeletons[path_buf] = p;
          break;
        case value::TYPE_ID_SKEL_ROOT:
          if (const auto *p = prim.as<SkelRoot>()) allSkelRoots[path_buf] = p;
          break;
        case value::TYPE_ID_SKELANIMATION:
          if (const auto *p = prim.as<SkelAnimation>()) allAnimations[path_buf] = p;
          break;
        case value::TYPE_ID_GEOM_POINT_INSTANCER:
          if (const auto *p = prim.as<GeomPointInstancer>())
            pointInstancerPrimMap[path_buf] = p;
          break;
        default:
          break;
      }
    };

    for (const auto &root_prim : env.stage.root_prims()) {
      path_buf = "/" + root_prim.local_path().full_path_name();
      classifyPrim(root_prim);

      if (!root_prim.children().empty()) {
        stack.push_back({&root_prim, 0, 0});
      }

      size_t iter = 0;
      while (!stack.empty()) {
        if (iter++ >= kMaxDefaultTraversalLimit) {
          PUSH_WARN("Prim traversal exceeded max iteration limit during pre-processing.");
          break;
        }
        auto &top = stack.back();
        if (top.child_idx >= top.parent->children().size()) {
          path_buf.resize(top.parent_path_len);
          stack.pop_back();
          continue;
        }

        const Prim &child = top.parent->children()[top.child_idx];
        ++top.child_idx;

        size_t cur_len = path_buf.size();
        path_buf += "/";
        path_buf += child.local_path().full_path_name();

        classifyPrim(child);

        if (!child.children().empty()) {
          stack.push_back({&child, 0, cur_len});
        } else {
          path_buf.resize(cur_len);
        }
      }
    }
    count_ms = ElapsedMs(phase_start);
  }
  DCOUT("[Tydra] Pre-discovered " << allSkeletons.size() << " skeletons, "
        << allSkelRoots.size() << " skelroots, " << allAnimations.size() << " animations");

  {
    const auto phase_start = TydraPerfClock::now();
    SkelRootSkeletonResolver::BuildMap(allSkeletons, allSkelRoots,
                                       &_skelRootToSkeleton);
    skel_map_ms = ElapsedMs(phase_start);
  }
  DCOUT("Precomputed SkelRoot->Skeleton entries: " << _skelRootToSkeleton.size());

  // Total meshes includes GeomMesh and all parametric primitives (all converted to meshes)
  const size_t total_meshes = meshPrimMap.size() + cubePrimMap.size() + spherePrimMap.size() +
                              cylinderPrimMap.size() + conePrimMap.size() +
                              capsulePrimMap.size() + planePrimMap.size();
  const size_t total_materials = materialPrimMap.size();
  DCOUT("[Tydra] Found " << total_meshes << " meshes ("
        << meshPrimMap.size() << " mesh, " << cubePrimMap.size() << " cube, "
        << spherePrimMap.size() << " sphere, "
        << cylinderPrimMap.size() << " cylinder, " << conePrimMap.size() << " cone, "
        << capsulePrimMap.size() << " capsule, " << planePrimMap.size() << " plane), "
        << total_materials << " materials");

  // Report counting complete via detailed progress
  _progress_info.stage = DetailedProgressInfo::Stage::CountingPrims;
  _progress_info.meshes_total = total_meshes;
  _progress_info.materials_total = total_materials;
  _progress_info.message = "Counted " + std::to_string(total_meshes) + " meshes, " +
                           std::to_string(total_materials) + " materials";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // 1. Convert Xform
  // 2. Convert Material/Texture
  // 3. Convert Mesh/SkinWeights/BlendShapes
  // 4. Convert Skeleton(bones)
  // 5. Build node hierarchy (includes lights and cameras)

  //
  // 1. Build Xform at specified time.
  //    Each Prim in Stage is converted to XformNode.
  //
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingXforms;
  _progress_info.progress = 0.1f;
  _progress_info.message = "Building xform hierarchy";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  XformNode xform_node;
  {
    const auto phase_start = TydraPerfClock::now();
    if (!BuildXformNodeFromStage(env.stage, &xform_node, env.timecode)) {
      PUSH_ERROR_AND_RETURN("Failed to build Xform node hierarchy.\n");
    }
    xform_ms = ElapsedMs(phase_start);
  }

  // Report progress after xform building (20%)
  if (!CallProgressCallback(0.2f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  std::string err;

  //
  // 2. Convert Material/Texture
  // 3. Convert Mesh/SkinWeights/BlendShapes
  // 4. Convert Skeleton(bones) and SkelAnimation
  //
  // Material conversion will be done in MeshVisitor.
  //
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingMeshes;
  _progress_info.progress = 0.2f;
  _progress_info.message = "Converting meshes and materials";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }
  if (!EmitPhase(StreamPhase::MaterialsAndMeshes)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  MeshVisitorEnv menv;
  menv.env = &env;
  menv.converter = this;
  menv.meshes_total = total_meshes;
  menv.materials_total = total_materials;
  menv.allSkeletons = &allSkeletons;
  menv.allSkelRoots = &allSkelRoots;
  menv.allAnimations = &allAnimations;

  // Store pre-discovered maps in converter for use by ConvertMesh
  _allSkeletons = &allSkeletons;
  _allSkelRoots = &allSkelRoots;
  _allAnimations = &allAnimations;

  {
    const auto phase_start = TydraPerfClock::now();
    bool ret = tydra::VisitPrims(env.stage, MeshVisitor, &menv, &err);

    visit_prims_ms = ElapsedMs(phase_start);
    if (!ret) {
      PUSH_ERROR_AND_RETURN(err);
    }
  }

  // Add standalone skeletons (not referenced by any mesh) to the render scene.
  // This ensures skeletons with SkelAnimations but no bound meshes are still
  // available for visualization (e.g. bone hierarchy display).
  {
    const auto phase_start = TydraPerfClock::now();
    for (const auto &skelEntry : allSkeletons) {
      const std::string &skelPathStr = skelEntry.first;
      if (_skelPathToIndex.find(skelPathStr) != _skelPathToIndex.end()) {
        continue;  // Already added by a mesh binding
      }
      const Skeleton *skelPtr = skelEntry.second;
      if (!skelPtr) continue;

      int32_t skel_id = int32_t(skeletons.size());
      SkelHierarchy skel;

      std::string primName = skelPathStr;
      size_t lastSlash = primName.rfind('/');
      if (lastSlash != std::string::npos) {
        primName = primName.substr(lastSlash + 1);
      }
      if (!ConvertSkeletonFromPtr(env, Path(skelPathStr, ""), *skelPtr, primName, &skel)) {
        PushWarn(fmt::format(
            "Skipping invalid standalone skeleton {}: {}\n",
            skelPathStr, GetError()));
        _err.clear();
        continue;
      }

      _skelPathToIndex[skelPathStr] = skel_id;
      skeletons.emplace_back(std::move(skel));
      DCOUT("Added standalone skeleton: " << skelPathStr);
    }
    standalone_skel_ms = ElapsedMs(phase_start);
  }

  // Convert all SkelAnimation prims now that all skeletons have been discovered.
  // This supports multiple animations per skeleton (when animationSource is a pathvector).
  DCOUT("Converting all SkelAnimation prims...");
  {
    const auto phase_start = TydraPerfClock::now();
    if (!ConvertAllSkelAnimations(env)) {
      PUSH_ERROR_AND_RETURN("Failed to convert SkelAnimation prims");
    }
    skel_anim_ms = ElapsedMs(phase_start);
  }
  DCOUT("SkelAnimation conversion complete");

  // Clear temporary pointers
  _allSkeletons = nullptr;
  _allSkelRoots = nullptr;
  _allAnimations = nullptr;
  _skelRootToSkeleton.clear();
  _materialBindingCache.clear();
  _materialSourceSignatureCache.clear();

  // Report progress after mesh/material conversion (70%)
  _progress_info.stage = DetailedProgressInfo::Stage::BuildingHierarchy;
  _progress_info.progress = 0.7f;
  _progress_info.meshes_processed = menv.meshes_processed;
  _progress_info.message = "Mesh conversion complete (" +
      std::to_string(menv.meshes_processed) + " meshes)";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  if (!CallProgressCallback(0.7f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 5. Build node hierarchy from XformNode and meshes, materials, skeletons,
  // etc.
  //
  _progress_info.message = "Building node hierarchy";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }
  if (!EmitPhase(StreamPhase::Hierarchy)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  {
    const auto phase_start = TydraPerfClock::now();
    if (!BuildNodeHierarchy(env, xform_node)) {
      return false;
    }
    if (!ResolveBlendShapeAnimationTargets()) {
      return false;
    }
    hierarchy_ms = ElapsedMs(phase_start);
  }

  // Stream cameras, lights and the node tree now that world matrices are known.
  // (Skipped entirely without a streaming sink.)
  if (_sink) {
    for (size_t i = 0; i < cameras.size(); i++) {
      if (!EmitCamera(i, cameras[i].abs_path)) {
        PushError("Conversion cancelled by user.\n");
        return false;
      }
    }
    for (size_t i = 0; i < lights.size(); i++) {
      if (!EmitLight(i, lights[i].abs_path)) {
        PushError("Conversion cancelled by user.\n");
        return false;
      }
    }
    for (size_t i = 0; i < root_nodes.size(); i++) {
      if (!EmitRootNode(i)) {
        PushError("Conversion cancelled by user.\n");
        return false;
      }
    }
  }

  // Report progress after node hierarchy building (85%)
  _progress_info.stage = DetailedProgressInfo::Stage::ExtractingAnimations;
  _progress_info.progress = 0.85f;
  _progress_info.message = "Hierarchy complete, extracting animations";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  if (!CallProgressCallback(0.85f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 6. Extract xformOp animations from nodes with time-sampled transforms
  //
  {
    const auto phase_start = TydraPerfClock::now();
    // Single-pass depth-first traversal with stable node indices.
    // This avoids repeatedly counting subtree sizes.
    std::function<void(const XformNode&, int32_t&, int32_t)> extractAnimationsFromNode;
    extractAnimationsFromNode = [&](const XformNode& node, int32_t& next_node_index, int32_t depth) {
      if (size_t(depth) >= kMaxDefaultTraversalLimit) return;
      const int32_t node_index = next_node_index++;

      if (node.prim) {
        const Path &prim_path = node.absolute_path;

        // Check if this node has a prim with xformOps.
        if (IsXformablePrim(*node.prim)) {
          const Xformable *xformable = nullptr;
          if (CastToXformable(*node.prim, &xformable) && xformable) {
            AnimationClip anim;
            bool converted = false;

            // Prefer value clip animation baking when enabled.
            if (env.scene_config.enable_value_clips &&
                ConvertValueClipAnimation(env, *node.prim, prim_path,
                                          node_index, &anim)) {
              converted = true;
            }

            // Fallback to direct xformOp sampling when no clip animation exists.
            if (!converted && xformable->has_timesamples()) {
              if (ExtractXformOpAnimation(env, prim_path, node.element_name,
                                          *xformable, node_index, &anim)) {
                converted = true;
              }
            }

            if (converted) {
              // Check if animation with this path already exists via O(1) lookup
              const auto &anim_abs_path = anim.abs_path;
              if (_animPathToIndex.find(anim_abs_path) ==
                  _animPathToIndex.end()) {
                DCOUT("Extracted animation from: " << anim_abs_path);
                _animPathToIndex[anim_abs_path] =
                    int32_t(animations.size());
                animations.emplace_back(std::move(anim));
              }
            }
          }
        }

        AnimationClip property_anim;
        if (ExtractPrimPropertyAnimation(env, *node.prim, prim_path,
                                         node_index, &property_anim)) {
          const std::string property_anim_key =
              property_anim.abs_path + "#properties";
          if (_animPathToIndex.find(property_anim_key) ==
              _animPathToIndex.end()) {
            DCOUT("Extracted property animation from: "
                  << property_anim.abs_path);
            _animPathToIndex[property_anim_key] =
                int32_t(animations.size());
            animations.emplace_back(std::move(property_anim));
          }
        }
      }

      for (const auto& child : node.children) {
        extractAnimationsFromNode(child, next_node_index, depth + 1);
      }
    };

    int32_t current_node_index = 0;
    for (const auto& root : xform_node.children) {
      extractAnimationsFromNode(root, current_node_index, 0);
    }
    xform_anim_ms = ElapsedMs(phase_start);
  }

  // Report progress after animation extraction (90%)
  if (!CallProgressCallback(0.9f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  //
  // 7. Merge meshes with same material (optional optimization)
  //
  if (env.scene_config.dedup_materials_by_texture_identity) {
    const size_t before_textures = textures.size();
    const size_t removed_textures = DeduplicateTexturesByIdentityImpl();
    if (removed_textures > 0) {
      PushInfo("Texture deduplication before material deduplication: " +
               std::to_string(before_textures) + " -> " +
               std::to_string(textures.size()) + ".");
    }

    const size_t before_materials = materials.size();
    const size_t removed = DeduplicateMaterialsByTextureIdentityImpl();
    if (removed > 0) {
      PushInfo("Material deduplication before merge: " +
               std::to_string(before_materials) + " -> " +
               std::to_string(materials.size()) + ".");
    }
  }

  if (env.scene_config.merge_meshes) {
    const auto phase_start = TydraPerfClock::now();
    if (!MergeMeshesImpl(env)) {
      PushWarn("Mesh merging encountered issues, but conversion continues.\n");
    }
    merge_ms = ElapsedMs(phase_start);
  }

  // Report progress after mesh merging (95%)
  if (!CallProgressCallback(0.95f)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  // Flatten the built Node hierarchy into an abs_path -> world matrix map.
  // Used to look up per-prim world transforms for instance expansion
  // (scenegraph instances in 7b, PointInstancer instances in 7c).
  std::unordered_map<std::string, value::matrix4d> path_to_global;
  {
    const auto phase_start = TydraPerfClock::now();
    std::function<void(const Node &)> collect = [&](const Node &n) {
      if (!n.abs_path.empty()) {
        path_to_global[n.abs_path] = n.global_matrix;
      }
      for (const auto &c : n.children) {
        collect(c);
      }
    };
    for (const auto &n : root_nodes) {
      collect(n);
    }
    instance_map_ms = ElapsedMs(phase_start);
  }

  //
  // 7b. Build instance registry from Stage (AOUSD Spec 11.3.3)
  //
  {
    // BuildInstancePrototypes must be called on the Stage first.
    // It's safe to call multiple times (idempotent after first call).
    Stage &mutable_stage = const_cast<Stage &>(env.stage);
    size_t num_protos = mutable_stage.BuildInstancePrototypes();
    if (num_protos > 0) {
      DCOUT("[Tydra] Found " << num_protos << " instance prototypes");

      // Build prototype_index -> mesh_id mapping from meshMap
      // Instance prims are typically Xform parents of mesh children,
      // so we look up child mesh paths for each prototype source.
      std::unordered_map<int, int32_t> proto_to_mesh;

      // For each instance prim, create a RenderInstance
      for (size_t proto_idx = 0; proto_idx < num_protos; proto_idx++) {
        auto inst_paths = mutable_stage.GetInstancesForPrototype(
            static_cast<int>(proto_idx));
        for (const auto &inst_path : inst_paths) {
          const std::string &path_str = inst_path.prim_part();

          // Find mesh_id for this instance's children (if any)
          int32_t found_mesh_id = -1;
          for (auto it = meshMap.s_begin(); it != meshMap.s_end(); ++it) {
            // Check if mesh path starts with instance path
            if (it->first.size() > path_str.size() &&
                it->first.compare(0, path_str.size(), path_str) == 0 &&
                it->first[path_str.size()] == '/') {
              found_mesh_id = static_cast<int32_t>(it->second);
              break;
            }
          }

          RenderInstance rinst;
          rinst.abs_path = path_str;
          rinst.prototype_index = static_cast<int32_t>(proto_idx);
          rinst.mesh_id = found_mesh_id;

          // Populate the instance transform from the instance prim's node.
          auto mit = path_to_global.find(path_str);
          if (mit != path_to_global.end()) {
            rinst.global_matrix = mit->second;
            rinst.local_matrix = mit->second;
          }

          // Extract prim name from path
          size_t last_slash = path_str.rfind('/');
          if (last_slash != std::string::npos) {
            rinst.prim_name = path_str.substr(last_slash + 1);
          }

          instances.emplace_back(std::move(rinst));
        }
      }
      DCOUT("[Tydra] Created " << instances.size() << " render instances");
    }
  }

  //
  // 7c. Expand PointInstancer prims into RenderScene::instances.
  //
  if (env.scene_config.expand_point_instancers &&
      !pointInstancerPrimMap.empty()) {
    for (const auto &kv : pointInstancerPrimMap) {
      const std::string &pi_path = kv.first;
      const GeomPointInstancer *pi = kv.second;
      if (!pi) continue;

      value::matrix4d instancer_world = value::matrix4d::identity();
      auto mit = path_to_global.find(pi_path);
      if (mit != path_to_global.end()) {
        instancer_world = mit->second;
      }

      if (!ExpandPointInstancer(env, pi_path, *pi, instancer_world,
                                path_to_global)) {
        PushWarn("PointInstancer expansion failed for " + pi_path +
                 "; continuing.\n");
      }
    }
    DCOUT("[Tydra] Total render instances after PointInstancer expansion: "
          << instances.size());
  }

  if (env.scene_config.flatten_optimized_render_tree) {
    const size_t before_nodes = root_nodes.size();
    const size_t kept_nodes = FlattenOptimizedRenderTreeImpl();
    PushInfo("Flattened optimized render tree: roots " +
             std::to_string(before_nodes) + " -> 1, render nodes " +
             std::to_string(kept_nodes) + ".");
  }

  // render_scene.meshMap = std::move(meshMap);
  // render_scene.materialMap = std::move(materialMap);
  // render_scene.textureMap = std::move(textureMap);
  // render_scene.imageMap = std::move(imageMap);
  // render_scene.bufferMap = std::move(bufferMap);

  // Stream skeletons/animations then instances (fast tail phases) before the
  // member arrays are moved into the RenderScene. Skipped without a sink.
  if (_sink) {
    if (!EmitPhase(StreamPhase::Animations)) {
      PushError("Conversion cancelled by user.\n");
      return false;
    }
    for (size_t i = 0; i < skeletons.size(); i++) {
      if (!EmitSkeleton(i, skeletons[i].abs_path)) {
        PushError("Conversion cancelled by user.\n");
        return false;
      }
    }
    for (size_t i = 0; i < animations.size(); i++) {
      if (!EmitAnimation(i, animations[i].abs_path)) {
        PushError("Conversion cancelled by user.\n");
        return false;
      }
    }
    if (!EmitPhase(StreamPhase::Instances)) {
      PushError("Conversion cancelled by user.\n");
      return false;
    }
    for (size_t i = 0; i < instances.size(); i++) {
      if (!EmitInstance(i, instances[i].abs_path)) {
        PushError("Conversion cancelled by user.\n");
        return false;
      }
    }
  }

  RenderScene render_scene;
  render_scene.usd_filename = env.usd_filename;
  render_scene.default_root_node = 0;
  if (default_node > -1) {
    if (size_t(default_node) >= root_nodes.size()) {
      PushWarn("Invalid default_node id. Use 0 for default_node id.");
    } else {
      render_scene.default_root_node = uint32_t(default_node);
    }
  }

  render_scene.nodes = std::move(root_nodes);
  render_scene.meshes = std::move(meshes);
  render_scene.textures = std::move(textures);
  render_scene.udim_textures = std::move(udim_textures);
  render_scene.images = std::move(images);
  render_scene.buffers = std::move(buffers);
  render_scene.materials = std::move(materials);
  render_scene.cameras = std::move(cameras);
  render_scene.lights = std::move(lights);
  render_scene.skeletons = std::move(skeletons);
  render_scene.animations = std::move(animations);
  render_scene.instances = std::move(instances);
  render_scene.volumes = std::move(volumes);

  // Populate scene metadata from Stage
  {
    const auto phase_start = TydraPerfClock::now();
    const auto &stage_metas = env.stage.metas();

    render_scene.meta.renderSettingsPrimPath =
        rendering_color.render_settings_path;
    render_scene.meta.workingColorSpace = rendering_color.working_space;
    color::ColorSpaceDesc display_linear;
    color::ColorTransform display_transform;
    if (color::GetBuiltinColorSpace("lin_rec709_scene", &display_linear) &&
        color::BuildColorTransform(rendering_color.working_definition,
                                   display_linear, &display_transform)) {
      std::copy(display_transform.matrix, display_transform.matrix + 9,
                render_scene.meta.workingToDisplayLinear.begin());
    }

    // upAxis
    if (stage_metas.upAxis.authored()) {
      render_scene.meta.upAxis = to_string(stage_metas.upAxis.get_value());
    }

    // metersPerUnit
    if (stage_metas.metersPerUnit.authored()) {
      render_scene.meta.metersPerUnit = stage_metas.metersPerUnit.get_value();
    }

    // framesPerSecond
    if (stage_metas.framesPerSecond.authored()) {
      render_scene.meta.framesPerSecond = stage_metas.framesPerSecond.get_value();
    }

    // timeCodesPerSecond
    if (stage_metas.timeCodesPerSecond.authored()) {
      render_scene.meta.timeCodesPerSecond = stage_metas.timeCodesPerSecond.get_value();
    }

    // startTimeCode
    if (stage_metas.startTimeCode.authored()) {
      render_scene.meta.startTimeCode = stage_metas.startTimeCode.get_value();
    }

    // endTimeCode
    if (stage_metas.endTimeCode.authored()) {
      render_scene.meta.endTimeCode = stage_metas.endTimeCode.get_value();
    }

    // autoPlay
    if (stage_metas.autoPlay.authored()) {
      render_scene.meta.autoPlay = stage_metas.autoPlay.get_value();
    }

    // comment
    if (!stage_metas.comment.value.empty()) {
      render_scene.meta.comment = stage_metas.comment.value;
    }

    // copyright - Check if customLayerData contains copyright info
    auto it = stage_metas.customLayerData.find("copyright");
    if (it != stage_metas.customLayerData.end()) {
      // Try to extract string value from MetaVariable
      auto copyright_val = it->second.get_value<std::string>();
      if (copyright_val) {
        render_scene.meta.copyright = copyright_val.value();
      }
    }
    stage_meta_ms = ElapsedMs(phase_start);
  }

  (*scene) = std::move(render_scene);

  {
    std::ostringstream ss;
    ss << "total=" << ElapsedMs(total_start)
       << " count=" << count_ms
       << " skelMap=" << skel_map_ms
       << " xform=" << xform_ms
       << " visitPrims=" << visit_prims_ms
       << " materialResolve=" << NsToMs(menv.resolve_material_ns)
       << "/" << menv.material_resolve_calls
       << " materialFound=" << menv.material_resolve_found
       << " materialConvert=" << NsToMs(menv.convert_material_ns)
       << " materialCache=" << menv.material_cache_hits << "/"
       << menv.material_cache_misses
       << " meshConvert=" << NsToMs(menv.convert_mesh_ns)
       << " meshProgress=" << NsToMs(menv.progress_ns)
       << " standaloneSkel=" << standalone_skel_ms
       << " skelAnim=" << skel_anim_ms
       << " hierarchy=" << hierarchy_ms
       << " xformAnim=" << xform_anim_ms
       << " merge=" << merge_ms
       << " instanceMap=" << instance_map_ms
       << " metadata=" << stage_meta_ms
       << " meshes=" << scene->meshes.size()
       << " materials=" << scene->materials.size()
       << " textures=" << scene->textures.size()
       << " images=" << scene->images.size()
       << " instances=" << scene->instances.size();
    _timing_info = ss.str();
  }

  // Report completion (100%)
  _progress_info.stage = DetailedProgressInfo::Stage::Complete;
  _progress_info.progress = 1.0f;
  _progress_info.message = "Conversion complete";
  if (!CallDetailedProgressCallback(_progress_info)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }
  CallProgressCallback(1.0f);

  // Light-link collections depend on the final RenderScene mesh table. Resolve
  // them after conversion/merge and before the streaming completion callback,
  // so both monolithic consumers and sinks observe identical records.
  ResolveLightLinking(env.stage, scene);

  if (!EmitPhase(StreamPhase::Complete)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }
  if (!EmitComplete(*scene)) {
    PushError("Conversion cancelled by user.\n");
    return false;
  }

  DCOUT("[Tydra] Conversion complete: " << scene->meshes.size() << " meshes, "
        << scene->materials.size() << " materials, " << scene->textures.size() << " textures");

  return true;
}

bool DefaultTextureImageLoaderFunction(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, TextureImage *texImageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  if (!texImageOut) {
    if (err) {
      (*err) = "`imageOut` argument is nullptr\n";
    }
    return false;
  }

  if (!imageData) {
    if (err) {
      (*err) = "`imageData` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string sanitized_path = utils::SanitizeAssetPath(
      assetPath.GetAssetPath(), assetResolver.get_allow_parent_relative_paths());
  if (sanitized_path.empty()) {
    if (err) {
      (*err) += fmt::format("Unsafe asset path: {}\n", assetPath.GetAssetPath());
    }
    return false;
  }

  std::string resolvedPath = assetResolver.resolve(sanitized_path);

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  Asset asset;
  bool ret = assetResolver.open_asset(resolvedPath, sanitized_path,
                                      &asset, warn, err);
  if (!ret) {
    if (err) {
      (*err) += fmt::format("Failed to open asset: {}", resolvedPath);
    }
    return false;
  }

  if (asset.size() > security_policy::GetMaxAssetReadBytes()) {
    if (err) {
      (*err) += fmt::format("Resolved asset exceeds max bytes ({} > {}).",
                            asset.size(), security_policy::GetMaxAssetReadBytes());
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);

  auto result = tinyusdz::image::LoadImageFromMemory(asset.data(), asset.size(),
                                                     resolvedPath);
  if (!result) {
    if (err) {
      (*err) += "Failed to load image file: " + result.error() + "\n";
    }
    return false;
  }

  TextureImage texImage;

  texImage.asset_identifier = resolvedPath;
  texImage.channels = result.value().image.channels;

  const auto &imgret = result.value();

  if (imgret.image.bpp == 8) {
    // assume uint8
    texImage.assetTexelComponentType = ComponentType::UInt8;
  } else if (imgret.image.bpp == 16) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt16;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int16;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Half;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }

  } else if (imgret.image.bpp == 32) {
    if (imgret.image.format == Image::PixelFormat::UInt) {
      texImage.assetTexelComponentType = ComponentType::UInt32;
    } else if (imgret.image.format == Image::PixelFormat::Int) {
      texImage.assetTexelComponentType = ComponentType::Int32;
    } else if (imgret.image.format == Image::PixelFormat::Float) {
      texImage.assetTexelComponentType = ComponentType::Float;
    } else {
      if (err) {
        (*err) += "Invalid image.pixelformat: " + tinyusdz::to_string(imgret.image.format) + "\n";
      }
      return false;
    }
  } else {
    DCOUT("Unsupported bpp = " << result.value().image.bpp);
    if (err) {
      (*err) += "Unsupported bpp: " +
               std::to_string(result.value().image.bpp) + "\n";
    }
    return false;
  }

  texImage.channels = result.value().image.channels;
  texImage.width = result.value().image.width;
  texImage.height = result.value().image.height;

  // `imageData` receives the decoder output as-is, so the buffer's texel type
  // equals the asset's texel type (HDR/EXR = Float32, 16-bit PNG = UInt16,
  // ...). Without this, float buffers were tagged UInt8 and every consumer
  // read the raw float bytes as 8-bit texels (garbage for HDR envmaps).
  texImage.texelComponentType = texImage.assetTexelComponentType;

  (*texImageOut) = texImage;

  // raw image data
  (*imageData) = result.value().image.data;

  return true;
}

// NOTE: this used to be a ~30-branch `else if` chain on tok.str(). Same
// class of MSVC C1061 ("blocks nested too deeply") risk already hit once in
// crate-writer-values.cc and fixed there -- an else-if chain nests one level
// deeper per link, while a lookup table has no such growth. Every original
// branch was a pure string->enum mapping with no side effects, so a table is
// both safe and a cleaner fit than the standalone-if pattern used elsewhere.
bool InferColorSpace(const value::token &tok, ColorSpace *cty) {
  if (!cty) {
    return false;
  }

  static const std::unordered_map<std::string, ColorSpace> kColorSpaceMap = {
      {"raw", ColorSpace::Raw},
      {"Raw", ColorSpace::Raw},
      {"srgb", ColorSpace::sRGB},
      {"srgb_rec709_scene", ColorSpace::sRGB},
      {"sRGB", ColorSpace::sRGB},
      {"srgb_texture", ColorSpace::sRGB_Texture},  // MaterialX texture colorspace
      {"linear", ColorSpace::Lin_sRGB},  // guess linear_srgb
      {"lin_srgb", ColorSpace::Lin_sRGB},
      {"rec709", ColorSpace::Rec709},
      {"lin_rec709", ColorSpace::Lin_Rec709},  // MaterialX/OpenUSD linear Rec.709
      {"lin_rec709_scene", ColorSpace::Lin_Rec709},
      {"g22_rec709", ColorSpace::g22_Rec709},  // MaterialX/OpenUSD gamma 2.2 Rec.709
      {"g22_rec709_scene", ColorSpace::g22_Rec709},
      {"g18_rec709", ColorSpace::g18_Rec709},  // MaterialX/OpenUSD gamma 1.8 Rec.709
      {"g18_rec709_scene", ColorSpace::g18_Rec709},
      {"lin_rec2020", ColorSpace::Lin_Rec2020},  // Linear Rec.2020
      {"lin_rec2020_scene", ColorSpace::Lin_Rec2020},
      {"acescg", ColorSpace::Lin_ACEScg},  // Alternative ACES CG naming
      {"lin_ap1", ColorSpace::Lin_ACEScg},  // Linear AP1 (same as ACEScg)
      {"lin_ap1_scene", ColorSpace::Lin_ACEScg},
      {"aces2065-1", ColorSpace::ACES2065_1},  // ACES 2065-1
      {"lin_ap0_scene", ColorSpace::ACES2065_1},
      {"ocio", ColorSpace::OCIO},
      {"lin_displayp3", ColorSpace::Lin_DisplayP3},
      {"lin_p3d65_scene", ColorSpace::Lin_DisplayP3},
      {"srgb_displayp3", ColorSpace::sRGB_DisplayP3},
      {"srgb_p3d65_scene", ColorSpace::sRGB_DisplayP3},
      // seen in Apple's USDZ model (or OCIO?)
      {"ACES - ACEScg", ColorSpace::Lin_ACEScg},
      {"Input - Texture - sRGB - Display P3", ColorSpace::sRGB_DisplayP3},
      {"Input - Texture - sRGB - sRGB", ColorSpace::sRGB},
      {"custom", ColorSpace::Custom},
  };

  const auto it = kColorSpaceMap.find(tok.str());
  if (it == kColorSpaceMap.end()) {
    return false;
  }
  (*cty) = it->second;
  return true;
}

namespace {

// Decode a single (resolved) image asset into an 8-bit `Image`.
bool UDIMDecodeImageAsset(const std::string &assetPath,
                          const AssetResolutionResolver &assetResolver,
                          Image *out, std::string *warn, std::string *err) {
  std::vector<uint8_t> direct_data;
  if (io::FileExists(assetPath)) {
    const size_t max_bytes = security_policy::GetMaxAssetReadBytes();
    if (!io::ReadWholeFile(&direct_data, err, assetPath, max_bytes)) {
      if (err) (*err) += fmt::format("Failed to read asset: {}\n", assetPath);
      return false;
    }
    auto result = tinyusdz::image::LoadImageFromMemory(direct_data.data(),
                                                       direct_data.size(),
                                                       assetPath);
    if (!result) {
      if (err) (*err) += "Failed to load image file: " + result.error() + "\n";
      return false;
    }
    (*out) = result.value().image;
    return true;
  }

  std::string sanitized = utils::SanitizeAssetPath(
      assetPath, assetResolver.get_allow_parent_relative_paths());
  if (sanitized.empty()) {
    if (err) (*err) += fmt::format("Unsafe asset path: {}\n", assetPath);
    return false;
  }

  std::string resolved = assetResolver.resolve(sanitized);
  if (resolved.empty()) {
    if (err) (*err) += fmt::format("Failed to resolve asset path: {}\n", assetPath);
    return false;
  }

  Asset asset;
  if (!assetResolver.open_asset(resolved, sanitized, &asset, warn, err)) {
    if (err) (*err) += fmt::format("Failed to open asset: {}\n", resolved);
    return false;
  }

  if (asset.size() > security_policy::GetMaxAssetReadBytes()) {
    if (err) {
      (*err) += fmt::format("Resolved asset exceeds max bytes ({} > {}).\n",
                            asset.size(),
                            security_policy::GetMaxAssetReadBytes());
    }
    return false;
  }

  auto result =
      tinyusdz::image::LoadImageFromMemory(asset.data(), asset.size(), resolved);
  if (!result) {
    if (err) (*err) += "Failed to load image file: " + result.error() + "\n";
    return false;
  }

  (*out) = result.value().image;
  return true;
}

// Expand `src` (1-4 channels, 8-bit or fp32) into a 4-channel RGBA8 `Image`.
bool UDIMToRGBA8(const Image &src, Image *dst) {
  if (src.bpp != 8 && src.bpp != 32) return false;
  if (src.channels < 1 || src.channels > 4) return false;

  const size_t npixels = size_t(src.width) * size_t(src.height);
  dst->width = src.width;
  dst->height = src.height;
  dst->channels = 4;
  dst->bpp = 8;
  dst->format = Image::PixelFormat::UInt;
  dst->colorspace = src.colorspace;
  dst->data.assign(npixels * 4, 0);

  const int sc = src.channels;
  for (size_t i = 0; i < npixels; i++) {
    uint8_t *d = dst->data.data() + i * 4;
    if (src.bpp == 32) {
      const float *s = reinterpret_cast<const float *>(src.data.data()) +
                       i * size_t(sc);
      auto q = [](float v) -> uint8_t {
        if (!(v > 0.0f)) return 0;
        if (v >= 1.0f) return 255;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
      };
      if (sc == 1) {
        d[0] = d[1] = d[2] = q(s[0]);
        d[3] = 255;
      } else if (sc == 2) {
        d[0] = d[1] = d[2] = q(s[0]);
        d[3] = q(s[1]);
      } else if (sc == 3) {
        d[0] = q(s[0]); d[1] = q(s[1]); d[2] = q(s[2]);
        d[3] = 255;
      } else {
        d[0] = q(s[0]); d[1] = q(s[1]); d[2] = q(s[2]); d[3] = q(s[3]);
      }
      continue;
    }

    const uint8_t *s = src.data.data() + i * size_t(sc);
    if (sc == 1) {
      d[0] = d[1] = d[2] = s[0];
      d[3] = 255;
    } else if (sc == 2) {  // luminance + alpha
      d[0] = d[1] = d[2] = s[0];
      d[3] = s[1];
    } else if (sc == 3) {
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
      d[3] = 255;
    } else {  // 4
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
    }
  }
  return true;
}

// Largest power-of-two <= n (n >= 1).
uint32_t UDIMFloorPow2(uint32_t n) {
  if (n < 1) return 1;
  uint32_t p = 1;
  while ((p << 1) <= n) p <<= 1;
  return p;
}

}  // namespace

bool ExpandUDIMTiles(const std::string &udimAssetPath,
                     const AssetResolutionResolver &assetResolver,
                     int max_tiles, std::vector<UDIMTile> *tilesOut,
                     std::string *warn, std::string *err) {
  (void)warn;
  if (!tilesOut) {
    if (err) (*err) = "`tilesOut` argument is nullptr\n";
    return false;
  }

  std::string prefix, suffix;
  if (!io::SplitUDIMPath(udimAssetPath, &prefix, &suffix)) {
    if (err) {
      (*err) += fmt::format("Not a UDIM asset path (no <UDIM> token): {}\n",
                            udimAssetPath);
    }
    return false;
  }

  // UDIM ids 1001..1100 (10x10 grid).
  constexpr uint32_t kUDIMStart = 1001;
  constexpr uint32_t kUDIMEnd = 1100;

  int cap = max_tiles;
  if (cap <= 0 || cap > 100) cap = 100;

  tilesOut->clear();
  for (uint32_t id = kUDIMStart; id <= kUDIMEnd; id++) {
    const std::string tilePath = prefix + std::to_string(id) + suffix;
    bool found = io::FileExists(tilePath);
    if (!found) {
      const std::string sanitized = utils::SanitizeAssetPath(
          tilePath, assetResolver.get_allow_parent_relative_paths());
      if (sanitized.empty()) {
        continue;
      }
      const std::string resolved = assetResolver.resolve(sanitized);
      if (resolved.empty()) {
        continue;
      }
      found = true;
    }
    if (!found) {
      continue;
    }

    UDIMTile tile;
    tile.udim_id = id;
    tile.u = (id - kUDIMStart) % 10;
    tile.v = (id - kUDIMStart) / 10;
    tile.asset_path = tilePath;
    tilesOut->push_back(tile);

    if (int(tilesOut->size()) >= cap) {
      break;
    }
  }

  if (tilesOut->empty()) {
    if (err) {
      (*err) += fmt::format("No UDIM tiles resolved for: {}\n", udimAssetPath);
    }
    return false;
  }

  return true;
}

bool BuildUDIMAtlas(const std::vector<UDIMTile> &tiles,
                    const AssetResolutionResolver &assetResolver,
                    int max_atlas_size, bool srgb, UDIMAtlas *atlasOut,
                    std::string *warn, std::string *err) {
  if (!atlasOut) {
    if (err) (*err) = "`atlasOut` argument is nullptr\n";
    return false;
  }
  if (tiles.empty()) {
    if (err) (*err) = "No UDIM tiles to combine\n";
    return false;
  }

  // Grid bounds from present tiles.
  uint32_t min_u = 9, max_u = 0, min_v = 9, max_v = 0;
  for (const auto &t : tiles) {
    min_u = (std::min)(min_u, t.u);
    max_u = (std::max)(max_u, t.u);
    min_v = (std::min)(min_v, t.v);
    max_v = (std::max)(max_v, t.v);
  }
  const uint32_t cols = max_u - min_u + 1;
  const uint32_t rows = max_v - min_v + 1;

  // Per-tile cell size derived from the max atlas longest edge.
  int atlas_cap = max_atlas_size > 0 ? max_atlas_size : 4096;
  uint32_t per_tile_max =
      uint32_t((std::max)(1, atlas_cap / int((std::max)(cols, rows))));
  const uint32_t per_tile = UDIMFloorPow2(per_tile_max);

  const uint32_t atlas_w = per_tile * cols;
  const uint32_t atlas_h = per_tile * rows;

  Image atlas;
  atlas.width = int(atlas_w);
  atlas.height = int(atlas_h);
  atlas.channels = 4;
  atlas.bpp = 8;
  atlas.format = Image::PixelFormat::UInt;
  atlas.data.assign(size_t(atlas_w) * size_t(atlas_h) * 4, 0);  // transparent

  const ResizeFilter filter =
      srgb ? ResizeFilter::SRGB : ResizeFilter::Linear;

  size_t placed = 0;
  for (const auto &t : tiles) {
    Image decoded;
    std::string tile_err;
    if (!UDIMDecodeImageAsset(t.asset_path, assetResolver, &decoded, warn,
                              &tile_err)) {
      if (warn) {
        (*warn) += fmt::format("Skip UDIM tile {} (`{}`): {}", t.udim_id,
                               t.asset_path, tile_err);
      }
      continue;
    }

    Image rgba;
    if (!UDIMToRGBA8(decoded, &rgba)) {
      if (warn) {
        (*warn) += fmt::format("Skip UDIM tile {} (`{}`): unsupported channels\n",
                               t.udim_id, t.asset_path);
      }
      continue;
    }

    Image cell;
    if (int(per_tile) == rgba.width && int(per_tile) == rgba.height) {
      cell = std::move(rgba);
    } else {
      std::string resize_err;
      if (!ResizeImage(rgba, int(per_tile), int(per_tile), &cell, filter,
                       &resize_err)) {
        if (warn) {
          (*warn) += fmt::format("Skip UDIM tile {} (`{}`): resize failed: {}\n",
                                 t.udim_id, t.asset_path, resize_err);
        }
        continue;
      }
    }

    // Cell position. UV v increases upward; image row 0 is the top, so the
    // bottom-most UV row (v == min_v) is placed at the bottom of the atlas.
    const uint32_t cell_col = t.u - min_u;
    const uint32_t cell_row_from_bottom = t.v - min_v;
    const uint32_t dst_x0 = cell_col * per_tile;
    const uint32_t dst_y0 = (rows - 1 - cell_row_from_bottom) * per_tile;

    for (uint32_t y = 0; y < per_tile; y++) {
      const uint8_t *srow = cell.data.data() + size_t(y) * per_tile * 4;
      uint8_t *drow =
          atlas.data.data() + (size_t(dst_y0 + y) * atlas_w + dst_x0) * 4;
      std::memcpy(drow, srow, size_t(per_tile) * 4);
    }
    placed++;
  }

  if (placed == 0) {
    if (err) (*err) += "Failed to place any UDIM tile into the atlas\n";
    return false;
  }

  atlasOut->image = std::move(atlas);
  atlasOut->cols = cols;
  atlasOut->rows = rows;
  atlasOut->min_u = min_u;
  atlasOut->min_v = min_v;
  atlasOut->uv_scale = {1.0f / float(cols), 1.0f / float(rows)};
  atlasOut->uv_offset = {-float(min_u) / float(cols),
                         -float(min_v) / float(rows)};

  return true;
}


// Memory usage estimation implementations

size_t RenderMesh::estimate_memory_usage() const {
  size_t total = sizeof(RenderMesh);

  // String storage
  total += prim_name.capacity();
  total += abs_path.capacity();
  total += display_name.capacity();

  // Vertex data
  total += points.capacity() * sizeof(vec3);

  // Index data
  total += usdFaceVertexIndices.capacity() * sizeof(uint32_t);
  total += usdFaceVertexCounts.capacity() * sizeof(uint32_t);
  total += triangulatedFaceVertexIndices.capacity() * sizeof(uint32_t);
  total += triangulatedFaceVertexCounts.capacity() * sizeof(uint32_t);
  total += triangulatedToOrigFaceVertexIndexMap.capacity() * sizeof(uint32_t);
  total += triangulatedFaceCounts.capacity() * sizeof(uint32_t);

  // Vertex attributes helper
  auto estimate_vertex_attr = [](const VertexAttribute& attr) -> size_t {
    size_t size = sizeof(VertexAttribute);
    size += attr.name.capacity();
    size += attr.data.capacity();
    size += attr.indices.capacity() * sizeof(uint32_t);
    return size;
  };

  total += estimate_vertex_attr(normals);
  total += estimate_vertex_attr(tangents);
  total += estimate_vertex_attr(binormals);
  total += estimate_vertex_attr(vertex_colors);
  total += estimate_vertex_attr(vertex_opacities);

  // Texcoords map
  for (const auto& texcoord_pair : texcoords) {
    total += sizeof(uint32_t) + estimate_vertex_attr(texcoord_pair.second);
  }

  // StringAndIdMap for texcoords
  total += texcoordSlotIdMap.size() * (sizeof(uint64_t) + sizeof(std::string));
  for (auto it = texcoordSlotIdMap.s_begin(); it != texcoordSlotIdMap.s_end(); ++it) {
    total += it->first.capacity();
  }

  // Joint and weights
  total += sizeof(JointAndWeight);
  total += joint_and_weights.jointIndices.capacity() * sizeof(int);
  total += joint_and_weights.jointWeights.capacity() * sizeof(float);

  // Blend shapes
  for (const auto& blend_shape_pair : targets) {
    total += blend_shape_pair.first.capacity() + sizeof(ShapeTarget);
    const auto& st = blend_shape_pair.second;
    total += st.prim_name.capacity();
    total += st.abs_path.capacity();
    total += st.display_name.capacity();
    total += st.pointIndices.capacity() * sizeof(uint32_t);
    total += st.pointOffsets.capacity() * sizeof(vec3);
    total += st.normalOffsets.capacity() * sizeof(vec3);
    for (const auto& ib_pair : st.inbetweens) {
      total += sizeof(float) + sizeof(InbetweenShapeTarget);
      total += ib_pair.second.pointOffsets.capacity() * sizeof(vec3);
      total += ib_pair.second.normalOffsets.capacity() * sizeof(vec3);
    }
  }

  // Material subset map
  for (const auto& subset_pair : material_subsetMap) {
    total += subset_pair.first.capacity() + sizeof(MaterialSubset);
    const auto& ms = subset_pair.second;
    total += ms.prim_name.capacity();
    total += ms.abs_path.capacity();
    total += ms.display_name.capacity();
    total += ms.usdIndices.capacity() * sizeof(int);
    total += ms.triangulatedIndices.capacity() * sizeof(int);
  }

  return total;
}

// Helper to estimate Node tree memory iteratively.
static size_t EstimateNodeMemory(const Node& node) {
  size_t total = 0;
  std::vector<const Node*> stack;
  stack.push_back(&node);
  size_t iter = 0;
  constexpr size_t kMaxIter = 1024 * 1024;
  while (!stack.empty() && iter++ < kMaxIter) {
    const Node* n = stack.back();
    stack.pop_back();
    total += sizeof(Node);
    total += n->prim_name.capacity();
    total += n->abs_path.capacity();
    total += n->display_name.capacity();
    total += n->children.capacity() * sizeof(Node);
    for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
      stack.push_back(&(*it));
      total -= sizeof(Node); // avoid double-counting (same as recursive version)
    }
  }
  return total;
}

// Helper to estimate SkelNode tree memory iteratively.
static size_t EstimateSkelNodeMemory(const SkelNode& node) {
  size_t total = 0;
  std::vector<const SkelNode*> stack;
  stack.push_back(&node);
  size_t iter = 0;
  constexpr size_t kMaxIter = 1024 * 1024;
  while (!stack.empty() && iter++ < kMaxIter) {
    const SkelNode* n = stack.back();
    stack.pop_back();
    total += sizeof(SkelNode);
    total += n->joint_path.capacity();
    total += n->joint_name.capacity();
    total += n->children.capacity() * sizeof(SkelNode);
    for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
      stack.push_back(&(*it));
      total -= sizeof(SkelNode); // avoid double-counting (same as recursive version)
    }
  }
  return total;
}

size_t RenderScene::estimate_memory_usage() const {
  size_t total = sizeof(RenderScene);

  // Scene metadata and filename
  total += usd_filename.capacity();
  total += sizeof(SceneMetadata);

  // Nodes (recursive tree)
  total += nodes.capacity() * sizeof(Node);
  for (const auto& node : nodes) {
    total += EstimateNodeMemory(node) - sizeof(Node);
  }

  // Texture images
  total += images.capacity() * sizeof(TextureImage);
  for (const auto& img : images) {
    total += img.asset_identifier.capacity();
  }

  // Materials
  total += materials.capacity() * sizeof(RenderMaterial);
  for (const auto& mat : materials) {
    total += mat.name.capacity();
    total += mat.abs_path.capacity();
    total += mat.display_name.capacity();
    total += mat.displacement_shader_path.capacity();
    total += mat.volume_shader_path.capacity();
    // Spectral data vectors (if present)
    if (mat.surfaceShader.has_value()) {
      const auto& s = *mat.surfaceShader;
      if (s.spd_reflectance.has_value()) {
        total += s.spd_reflectance->samples.capacity() * sizeof(vec2);
      }
      if (s.spd_ior.has_value()) {
        total += s.spd_ior->samples.capacity() * sizeof(vec2);
      }
    }
  }

  total += cameras.capacity() * sizeof(RenderCamera);
  total += lights.capacity() * sizeof(RenderLight);

  total += textures.capacity() * sizeof(UVTexture);
  for (const auto& texture : textures) {
    total += texture.prim_name.capacity();
    total += texture.abs_path.capacity();
    total += texture.display_name.capacity();
  }

  total += udim_textures.capacity() * sizeof(UDIMTexture);
  for (const auto& udim : udim_textures) {
    total += udim.prim_name.capacity();
    total += udim.abs_path.capacity();
    total += udim.display_name.capacity();
    total += udim.asset_identifier.capacity();
    total += udim.imageTileIds.size() *
             (sizeof(uint32_t) + sizeof(int32_t));
  }

  // Meshes - use the detailed estimation
  total += meshes.capacity() * sizeof(RenderMesh);
  for (const auto& mesh : meshes) {
    total += mesh.estimate_memory_usage() - sizeof(RenderMesh);
  }

  // Animations
  total += animations.capacity() * sizeof(AnimationClip);
  for (const auto& clip : animations) {
    total += clip.name.capacity();
    total += clip.prim_name.capacity();
    total += clip.abs_path.capacity();
    total += clip.display_name.capacity();
    total += clip.samplers.capacity() * sizeof(KeyframeSampler);
    for (const auto& sampler : clip.samplers) {
      total += sampler.times.capacity() * sizeof(float);
      total += sampler.values.capacity() * sizeof(float);
    }
    total += clip.channels.capacity() * sizeof(AnimationChannel);
  }

  // Skeletons
  total += skeletons.capacity() * sizeof(SkelHierarchy);
  for (const auto& skel : skeletons) {
    total += skel.prim_name.capacity();
    total += skel.abs_path.capacity();
    total += skel.display_name.capacity();
    total += EstimateSkelNodeMemory(skel.root_node) - sizeof(SkelNode);
    total += skel.anim_ids.capacity() * sizeof(int);
    total += skel.parent_joint_indices.capacity() * sizeof(int);
    total += skel.bind_transforms.capacity() * sizeof(value::matrix4d);
    total += skel.rest_transforms.capacity() * sizeof(value::matrix4d);
  }

  total += buffers.capacity() * sizeof(BufferData);
  for (const auto& buffer : buffers) {
    total += buffer.data.capacity();
  }

  return total;
}

void RenderSceneConverter::SetProgressCallback(ProgressCallback callback, void *userptr) {
  _progress_callback = callback;
  _progress_userptr = userptr;
}

void RenderSceneConverter::SetDetailedProgressCallback(DetailedProgressCallback callback, void *userptr) {
  _detailed_progress_callback = callback;
  _detailed_progress_userptr = userptr;
}

bool RenderSceneConverter::CallProgressCallback(float progress) {
  if (_progress_callback) {
    return _progress_callback(progress, _progress_userptr);
  }
  return true; // Continue if no callback set
}

bool RenderSceneConverter::CallDetailedProgressCallback(const DetailedProgressInfo &info) {
  if (_detailed_progress_callback) {
    return _detailed_progress_callback(info, _detailed_progress_userptr);
  }
  return true; // Continue if no callback set
}

bool RenderSceneConverter::ReportMeshProgress(size_t meshes_processed, size_t meshes_total,
                                               const std::string& mesh_name, const std::string& message) {
  _progress_info.stage = DetailedProgressInfo::Stage::ConvertingMeshes;
  _progress_info.meshes_processed = meshes_processed;
  _progress_info.meshes_total = meshes_total;
  _progress_info.current_mesh_name = mesh_name;
  _progress_info.message = message;

  // Calculate progress: meshes are 20%-70% of total progress (50% range)
  float mesh_progress = 0.2f + (0.5f * float(meshes_processed) / float(std::max(size_t(1), meshes_total)));
  _progress_info.progress = mesh_progress;

  return CallDetailedProgressCallback(_progress_info);
}

// ---------------------------------------------------------------------------
// Streaming emit helpers (no-ops unless a sink is set via
// ConvertToRenderSceneStreaming). Each reads the just-appended element from the
// converter's member array by index and returns false to request cancellation.
// ---------------------------------------------------------------------------
bool RenderSceneConverter::EmitPhase(StreamPhase phase) {
  if (_sink && _sink->on_phase) {
    return _sink->on_phase(phase, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitImage(size_t index) {
  if (_sink && _sink->on_image) {
    return _sink->on_image(images[index], index, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitBuffer(size_t index) {
  if (_sink && _sink->on_buffer) {
    return _sink->on_buffer(buffers[index], index, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitTexture(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_texture) {
    return _sink->on_texture(textures[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitUdimTexture(size_t index) {
  if (_sink && _sink->on_udim_texture) {
    return _sink->on_udim_texture(udim_textures[index], index, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitMaterial(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_material) {
    return _sink->on_material(materials[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitMesh(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_mesh) {
    return _sink->on_mesh(meshes[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitLight(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_light) {
    return _sink->on_light(lights[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitCamera(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_camera) {
    return _sink->on_camera(cameras[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitRootNode(size_t index) {
  if (_sink && _sink->on_root_node) {
    return _sink->on_root_node(root_nodes[index], index, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitSkeleton(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_skeleton) {
    return _sink->on_skeleton(skeletons[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitAnimation(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_animation) {
    return _sink->on_animation(animations[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitInstance(size_t index, const std::string &abs_path) {
  if (_sink && _sink->on_instance) {
    return _sink->on_instance(instances[index], index, abs_path, _sink->userdata);
  }
  return true;
}
bool RenderSceneConverter::EmitComplete(const RenderScene &scene) {
  if (_sink && _sink->on_complete) {
    return _sink->on_complete(scene, _sink->userdata);
  }
  return true;
}

bool RenderSceneConverter::IsMeshMergeable(const RenderMesh &mesh) const {
  // Mesh cannot be merged if:
  // 1. Has skeletal animation
  if (mesh.skel_id >= 0) {
    return false;
  }

  // 2. Has blend shapes
  if (!mesh.targets.empty()) {
    return false;
  }

  // 3. Has per-face materials (GeomSubset)
  if (!mesh.material_subsetMap.empty()) {
    return false;
  }

  // 4. Is an area light (special rendering)
  if (mesh.is_area_light) {
    return false;
  }

  return true;
}

// Helper function to transform a vec3 point by a matrix4d
static vec3 TransformPoint(const value::matrix4d &m, const vec3 &p) {
  // Apply full 4x4 transform (position)
  double x = m.m[0][0] * double(p[0]) + m.m[1][0] * double(p[1]) + m.m[2][0] * double(p[2]) + m.m[3][0];
  double y = m.m[0][1] * double(p[0]) + m.m[1][1] * double(p[1]) + m.m[2][1] * double(p[2]) + m.m[3][1];
  double z = m.m[0][2] * double(p[0]) + m.m[1][2] * double(p[1]) + m.m[2][2] * double(p[2]) + m.m[3][2];
  double w = m.m[0][3] * double(p[0]) + m.m[1][3] * double(p[1]) + m.m[2][3] * double(p[2]) + m.m[3][3];

  if (std::abs(w) > 1e-10) {
    x /= w;
    y /= w;
    z /= w;
  }

  return vec3{float(x), float(y), float(z)};
}

// Helper function to transform a vec3 direction by a precomputed inverse
// matrix. The upper-left 3x3 of the inverse-transpose gives the correct normal
// transform under non-uniform scale.
static vec3 TransformNormalWithInverse(const value::matrix4d &inv,
                                       const vec3 &n) {
  double x = inv.m[0][0] * double(n[0]) + inv.m[0][1] * double(n[1]) + inv.m[0][2] * double(n[2]);
  double y = inv.m[1][0] * double(n[0]) + inv.m[1][1] * double(n[1]) + inv.m[1][2] * double(n[2]);
  double z = inv.m[2][0] * double(n[0]) + inv.m[2][1] * double(n[1]) + inv.m[2][2] * double(n[2]);

  // Normalize the result
  double len = std::sqrt(x*x + y*y + z*z);
  if (len > 1e-10) {
    x /= len;
    y /= len;
    z /= len;
  }

  return vec3{float(x), float(y), float(z)};
}

static bool CanBakeDirectionAttribute(const VertexAttribute &attr) {
  return attr.empty() ||
         (attr.format == VertexAttributeFormat::Vec3 &&
          attr.stride_bytes() == sizeof(vec3));
}

// Whether `src` can be appended onto `dst`. Two conditions:
//   - if both sides carry data, format and stride must match;
//   - once `dst` already holds vertices (`dst_has_vertices`), the attribute must
//     be present on both or neither. An attribute present on exactly one side
//     would leave the merged array shorter than (or misaligned with) the point
//     count, corrupting the mesh, so such a merge is refused.
static bool CompatibleVertexAttributeForAppend(const VertexAttribute &dst,
                                               const VertexAttribute &src,
                                               bool dst_has_vertices) {
  if (dst_has_vertices && (dst.empty() != src.empty())) {
    return false;
  }
  return dst.empty() || src.empty() ||
         (dst.format == src.format &&
          dst.stride_bytes() == src.stride_bytes());
}

static bool ValidateIndexedTopology(const std::vector<uint32_t> &counts,
                                    const std::vector<uint32_t> &indices,
                                    size_t point_count,
                                    const std::string &label,
                                    std::string *err) {
  if (counts.empty() && indices.empty()) {
    return true;
  }
  if (counts.empty() || indices.empty()) {
    if (err) {
      *err = "Cannot merge " + label +
             ": face counts and indices must both be present.";
    }
    return false;
  }

  size_t total = 0;
  for (uint32_t count : counts) {
    if (size_t(count) > indices.size() - total) {
      if (err) {
        *err = "Cannot merge " + label +
               ": face counts exceed index array length.";
      }
      return false;
    }
    total += size_t(count);
  }
  if (total != indices.size()) {
    if (err) {
      *err = "Cannot merge " + label +
             ": face counts do not match index array length.";
    }
    return false;
  }

  for (uint32_t idx : indices) {
    if (size_t(idx) >= point_count) {
      if (err) {
        *err = "Cannot merge " + label +
               ": face index is out of point range.";
      }
      return false;
    }
  }
  return true;
}

bool RenderSceneConverter::MergeMeshData(const RenderMesh &src,
                                         const value::matrix4d &src_transform,
                                         RenderMesh &dst,
                                         std::string *err) {
  auto set_merge_error = [&](const std::string &msg) {
    if (err) {
      *err = msg;
    }
  };

  // Check if transform is identity using tinyusdz::is_identity function
  bool transform_is_identity = tinyusdz::is_identity(src_transform);
  value::matrix4d src_transform_inverse = value::matrix4d::identity();
  const bool needs_direction_bake =
      !transform_is_identity &&
      (!src.normals.empty() || !src.tangents.empty() ||
       !src.binormals.empty());
  if (!transform_is_identity) {
    if (!CanBakeDirectionAttribute(src.normals) ||
        !CanBakeDirectionAttribute(src.tangents) ||
        !CanBakeDirectionAttribute(src.binormals)) {
      set_merge_error(
          "Cannot bake transform for packed or non-float3 direction attributes.");
      return false;
    }
    if (needs_direction_bake &&
        !tinyusdz::inverse(src_transform, src_transform_inverse)) {
      set_merge_error(
          "Cannot bake direction attributes with a non-invertible transform.");
      return false;
    }
  }

  // All attribute compatibility is validated up front, before `dst` is
  // mutated, so a refused merge leaves `dst` untouched and the caller can keep
  // the source as a standalone mesh.
  const bool dst_has_vertices = !dst.points.empty();
  if (!CompatibleVertexAttributeForAppend(dst.normals, src.normals,
                                          dst_has_vertices)) {
    set_merge_error("Cannot merge normals: incompatible format or presence.");
    return false;
  }
  if (!CompatibleVertexAttributeForAppend(dst.tangents, src.tangents,
                                          dst_has_vertices)) {
    set_merge_error("Cannot merge tangents: incompatible format or presence.");
    return false;
  }
  if (!CompatibleVertexAttributeForAppend(dst.binormals, src.binormals,
                                          dst_has_vertices)) {
    set_merge_error("Cannot merge binormals: incompatible format or presence.");
    return false;
  }
  if (!CompatibleVertexAttributeForAppend(dst.vertex_colors, src.vertex_colors,
                                          dst_has_vertices)) {
    set_merge_error(
        "Cannot merge vertex_colors: incompatible format or presence.");
    return false;
  }
  if (!CompatibleVertexAttributeForAppend(dst.vertex_opacities,
                                          src.vertex_opacities,
                                          dst_has_vertices)) {
    set_merge_error(
        "Cannot merge vertex_opacities: incompatible format or presence.");
    return false;
  }
  // Texcoords are keyed by slot; a slot present on exactly one side (once dst
  // has vertices) would leave a partially-filled UV set, so refuse it too.
  if (dst_has_vertices && dst.texcoords.size() != src.texcoords.size()) {
    set_merge_error("Cannot merge texcoords: mismatched UV slot sets.");
    return false;
  }
  for (const auto &src_tc : src.texcoords) {
    auto dst_tc_it = dst.texcoords.find(src_tc.first);
    if (dst_tc_it == dst.texcoords.end()) {
      if (dst_has_vertices) {
        set_merge_error("Cannot merge texcoords slot " +
                        std::to_string(src_tc.first) +
                        ": UV slot missing on merge target.");
        return false;
      }
      continue;
    }
    if (!CompatibleVertexAttributeForAppend(dst_tc_it->second, src_tc.second,
                                            dst_has_vertices)) {
      set_merge_error("Cannot merge texcoords slot " +
                      std::to_string(src_tc.first) +
                      ": incompatible format or presence.");
      return false;
    }
  }

  // Get the vertex offset for index adjustment.
#if SIZE_MAX > 0xFFFFFFFFu
  // Only meaningful where size_t is wider than uint32 (e.g. 64-bit). On a
  // 32-bit size_t (wasm32) points.size() can never exceed UINT32_MAX, so this
  // comparison is tautologically false and is compiled out.
  if (dst.points.size() >
      size_t((std::numeric_limits<uint32_t>::max)())) {
    set_merge_error("Cannot merge mesh: vertex offset exceeds uint32 range.");
    return false;
  }
#endif
  if (src.points.size() >
      size_t((std::numeric_limits<uint32_t>::max)()) - dst.points.size()) {
    set_merge_error("Cannot merge mesh: vertex count exceeds uint32 range.");
    return false;
  }
  if (!ValidateIndexedTopology(src.usdFaceVertexCounts,
                               src.usdFaceVertexIndices,
                               src.points.size(),
                               "face topology", err)) {
    return false;
  }
  if (!ValidateIndexedTopology(src.triangulatedFaceVertexCounts,
                               src.triangulatedFaceVertexIndices,
                               src.points.size(),
                               "triangulated topology", err)) {
    return false;
  }
  // With the vertex-count guard above, `vertex_offset + idx` cannot overflow
  // uint32 for any in-range index (idx < src.points.size()). The per-element
  // guards below therefore only catch malformed (out-of-range) source indices.
  uint32_t vertex_offset = static_cast<uint32_t>(dst.points.size());

  // Merge points (with transform if needed)
  if (transform_is_identity) {
    dst.points.insert(dst.points.end(), src.points.begin(), src.points.end());
  } else {
    for (const auto &p : src.points) {
      dst.points.push_back(TransformPoint(src_transform, p));
    }
  }

  // Merge face vertex indices (adjust by vertex offset)
  for (uint32_t idx : src.usdFaceVertexIndices) {
    if (idx > (std::numeric_limits<uint32_t>::max)() - vertex_offset) {
      set_merge_error("Cannot merge face indices: uint32 index overflow.");
      return false;
    }
    dst.usdFaceVertexIndices.push_back(idx + vertex_offset);
  }

  // Merge face vertex counts
  dst.usdFaceVertexCounts.insert(dst.usdFaceVertexCounts.end(),
                                  src.usdFaceVertexCounts.begin(),
                                  src.usdFaceVertexCounts.end());

  // Merge triangulated indices if present
  if (!src.triangulatedFaceVertexIndices.empty()) {
    for (uint32_t idx : src.triangulatedFaceVertexIndices) {
      if (idx > (std::numeric_limits<uint32_t>::max)() - vertex_offset) {
        set_merge_error(
            "Cannot merge triangulated indices: uint32 index overflow.");
        return false;
      }
      dst.triangulatedFaceVertexIndices.push_back(idx + vertex_offset);
    }
    dst.triangulatedFaceVertexCounts.insert(dst.triangulatedFaceVertexCounts.end(),
                                             src.triangulatedFaceVertexCounts.begin(),
                                             src.triangulatedFaceVertexCounts.end());
  }

  // Merge normals (transform direction if needed)
  if (!src.normals.empty()) {
    size_t src_normal_count = src.normals.vertex_count();

    // Ensure dst normals has same format
    if (dst.normals.empty()) {
      dst.normals = src.normals;
      if (!transform_is_identity) {
        // Transform the normals we just copied
        vec3 *normals_data = reinterpret_cast<vec3*>(dst.normals.data.data());
        for (size_t i = 0; i < src_normal_count; i++) {
          normals_data[i] =
              TransformNormalWithInverse(src_transform_inverse,
                                         normals_data[i]);
        }
      }
    } else {
      // Format/stride/presence already validated up front.
      // Append normals
      size_t old_size = dst.normals.data.size();
      size_t new_size;
      if (!safe::add(old_size, src.normals.data.size(), &new_size)) {
        return false;
      }
      dst.normals.data.resize(new_size);

      if (transform_is_identity) {
        memcpy(dst.normals.data.data() + old_size, src.normals.data.data(), src.normals.data.size());
      } else {
        const vec3 *src_normals = reinterpret_cast<const vec3*>(src.normals.data.data());
        vec3 *dst_normals = reinterpret_cast<vec3*>(dst.normals.data.data() + old_size);
        for (size_t i = 0; i < src_normal_count; i++) {
          dst_normals[i] =
              TransformNormalWithInverse(src_transform_inverse,
                                         src_normals[i]);
        }
      }
    }
  }

  // Merge texcoords (no transform needed)
  for (const auto &src_tc : src.texcoords) {
    uint32_t slot = src_tc.first;
    const auto &src_attr = src_tc.second;

    auto dst_tc_it = dst.texcoords.find(slot);
    if (dst_tc_it == dst.texcoords.end()) {
      dst.texcoords.emplace(slot, src_attr);
    } else {
      auto &dst_attr = dst_tc_it->second;
      // Format/stride/presence already validated up front.
      size_t old_size = dst_attr.data.size();
      size_t new_size;
      if (!safe::add(old_size, src_attr.data.size(), &new_size)) {
        return false;
      }
      dst_attr.data.resize(new_size);
      memcpy(dst_attr.data.data() + old_size, src_attr.data.data(), src_attr.data.size());
    }
  }

  // Merge tangents (transform direction if needed)
  if (!src.tangents.empty()) {
    if (dst.tangents.empty()) {
      dst.tangents = src.tangents;
      if (!transform_is_identity) {
        vec3 *tangents_data = reinterpret_cast<vec3*>(dst.tangents.data.data());
        size_t count = dst.tangents.vertex_count();
        for (size_t i = 0; i < count; i++) {
          tangents_data[i] =
              TransformNormalWithInverse(src_transform_inverse,
                                         tangents_data[i]);
        }
      }
    } else {
      // Format/stride/presence already validated up front.
      size_t old_size = dst.tangents.data.size();
      size_t src_count = src.tangents.vertex_count();
      size_t new_size;
      if (!safe::add(old_size, src.tangents.data.size(), &new_size)) {
        return false;
      }
      dst.tangents.data.resize(new_size);

      if (transform_is_identity) {
        memcpy(dst.tangents.data.data() + old_size, src.tangents.data.data(), src.tangents.data.size());
      } else {
        const vec3 *src_tangents = reinterpret_cast<const vec3*>(src.tangents.data.data());
        vec3 *dst_tangents = reinterpret_cast<vec3*>(dst.tangents.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_tangents[i] =
              TransformNormalWithInverse(src_transform_inverse,
                                         src_tangents[i]);
        }
      }
    }
  }

  // Merge binormals (transform direction if needed)
  if (!src.binormals.empty()) {
    if (dst.binormals.empty()) {
      dst.binormals = src.binormals;
      if (!transform_is_identity) {
        vec3 *binormals_data = reinterpret_cast<vec3*>(dst.binormals.data.data());
        size_t count = dst.binormals.vertex_count();
        for (size_t i = 0; i < count; i++) {
          binormals_data[i] =
              TransformNormalWithInverse(src_transform_inverse,
                                         binormals_data[i]);
        }
      }
    } else {
      // Format/stride/presence already validated up front.
      size_t old_size = dst.binormals.data.size();
      size_t src_count = src.binormals.vertex_count();
      size_t new_size;
      if (!safe::add(old_size, src.binormals.data.size(), &new_size)) {
        return false;
      }
      dst.binormals.data.resize(new_size);

      if (transform_is_identity) {
        memcpy(dst.binormals.data.data() + old_size, src.binormals.data.data(), src.binormals.data.size());
      } else {
        const vec3 *src_binormals = reinterpret_cast<const vec3*>(src.binormals.data.data());
        vec3 *dst_binormals = reinterpret_cast<vec3*>(dst.binormals.data.data() + old_size);
        for (size_t i = 0; i < src_count; i++) {
          dst_binormals[i] =
              TransformNormalWithInverse(src_transform_inverse,
                                         src_binormals[i]);
        }
      }
    }
  }

  // Merge vertex colors
  if (!src.vertex_colors.empty()) {
    if (dst.vertex_colors.empty()) {
      dst.vertex_colors = src.vertex_colors;
    } else {
      // Format/stride/presence already validated up front.
      size_t old_size = dst.vertex_colors.data.size();
      size_t new_size;
      if (!safe::add(old_size, src.vertex_colors.data.size(), &new_size)) {
        return false;
      }
      dst.vertex_colors.data.resize(new_size);
      memcpy(dst.vertex_colors.data.data() + old_size, src.vertex_colors.data.data(), src.vertex_colors.data.size());
    }
  }

  // Merge vertex opacities
  if (!src.vertex_opacities.empty()) {
    if (dst.vertex_opacities.empty()) {
      dst.vertex_opacities = src.vertex_opacities;
    } else {
      // Format/stride/presence already validated up front.
      size_t old_size = dst.vertex_opacities.data.size();
      size_t new_size;
      if (!safe::add(old_size, src.vertex_opacities.data.size(), &new_size)) {
        return false;
      }
      dst.vertex_opacities.data.resize(new_size);
      memcpy(dst.vertex_opacities.data.data() + old_size, src.vertex_opacities.data.data(), src.vertex_opacities.data.size());
    }
  }

  return true;
}

size_t RenderSceneConverter::DeduplicateMaterialsByTextureIdentityImpl() {
  if (materials.empty()) {
    return 0;
  }

  const size_t before = materials.size();
  std::unordered_map<std::string, int> signature_to_new_id;
  signature_to_new_id.reserve(before);
  std::vector<int> old_to_new(before, -1);
  std::vector<RenderMaterial> deduped;
  deduped.reserve(before);

  for (size_t i = 0; i < before; i++) {
    const RenderMaterial &mat = materials[i];
    const std::string signature = MaterialSignature(mat, textures);
    auto it = signature_to_new_id.find(signature);
    if (it != signature_to_new_id.end()) {
      old_to_new[i] = it->second;
      continue;
    }

    const int new_id = int(deduped.size());
    signature_to_new_id.emplace(signature, new_id);
    old_to_new[i] = new_id;
    deduped.push_back(mat);
  }

  if (deduped.size() == before) {
    return 0;
  }

  for (RenderMesh &mesh : meshes) {
    RemapMaterialId(mesh.material_id, old_to_new);
    RemapMaterialId(mesh.backface_material_id, old_to_new);
    for (auto &subset : mesh.material_subsetMap) {
      RemapMaterialSubsetIds(subset.second, old_to_new);
    }
  }

  for (RenderInstance &inst : instances) {
    RemapMaterialId(inst.material_id, old_to_new);
  }

  materials = std::move(deduped);
  // Note: materialMap (path -> material index) is left stale after dedup. It is
  // a converter-internal cache consulted only during material conversion (which
  // completes before this pass) and is not exported into RenderScene, so the
  // stale entries are never read. All live references (mesh/instance material
  // ids, subsets) are remapped above.
  return before - materials.size();
}

size_t RenderSceneConverter::DeduplicateTexturesByIdentityImpl() {
  if (textures.empty()) {
    return 0;
  }

  const size_t before = textures.size();
  std::unordered_map<std::string, int> signature_to_new_id;
  signature_to_new_id.reserve(before);
  std::vector<int> old_to_new(before, -1);
  std::vector<UVTexture> deduped;
  deduped.reserve(before);

  for (size_t i = 0; i < before; i++) {
    const UVTexture &texture = textures[i];
    const std::string signature = TextureSignature(texture);
    auto it = signature_to_new_id.find(signature);
    if (it != signature_to_new_id.end()) {
      old_to_new[i] = it->second;
      continue;
    }

    const int new_id = int(deduped.size());
    signature_to_new_id.emplace(signature, new_id);
    old_to_new[i] = new_id;
    deduped.push_back(texture);
  }

  if (deduped.size() == before) {
    return 0;
  }

  for (RenderMaterial &mat : materials) {
    RemapMaterialTextureIds(mat, old_to_new);
  }

  textures = std::move(deduped);
  return before - textures.size();
}

size_t RenderSceneConverter::FlattenOptimizedRenderTreeImpl() {
  std::vector<Node> flat_nodes;
  std::vector<int32_t> old_to_new_node_index;

  auto shouldKeep = [](const Node &node) {
    if (node.id < 0) {
      return false;
    }
    if (node.nodeType == NodeType::Mesh) {
      return true;
    }
    return node.category == NodeCategory::Camera ||
           node.category == NodeCategory::Light ||
           node.category == NodeCategory::Skeleton;
  };

  std::function<void(const Node &, int32_t)> collect =
      [&](const Node &node, int32_t depth) {
        if (size_t(depth) >= kMaxDefaultTraversalLimit) {
          return;
        }

        const size_t old_index = old_to_new_node_index.size();
        old_to_new_node_index.push_back(-1);
        if (shouldKeep(node)) {
          Node kept = node;
          kept.local_matrix = node.global_matrix;
          kept.children.clear();
          old_to_new_node_index[old_index] = int32_t(flat_nodes.size() + 1);
          flat_nodes.push_back(std::move(kept));
        }

        for (const Node &child : node.children) {
          collect(child, depth + 1);
        }
      };

  for (const Node &root : root_nodes) {
    collect(root, 0);
  }

  Node optimized_root;
  optimized_root.prim_name = "OptimizedRenderRoot";
  optimized_root.display_name = "Optimized Render Root";
  optimized_root.abs_path = "/OptimizedRenderRoot";
  optimized_root.category = NodeCategory::Group;
  optimized_root.nodeType = NodeType::Xform;
  optimized_root.id = -1;
  optimized_root.local_matrix = value::matrix4d::identity();
  optimized_root.global_matrix = value::matrix4d::identity();
  optimized_root.children = std::move(flat_nodes);

  const size_t kept_count = optimized_root.children.size();
  root_nodes.clear();
  root_nodes.push_back(std::move(optimized_root));
  root_nodeMap = StringAndIdMap{};
  root_nodeMap.add("/OptimizedRenderRoot", uint64_t(0));
  default_node = 0;

  for (AnimationClip &clip : animations) {
    for (AnimationChannel &channel : clip.channels) {
      if (channel.target_type != ChannelTargetType::SceneNode ||
          channel.target_node < 0) {
        continue;
      }
      const size_t old_index = size_t(channel.target_node);
      channel.target_node = old_index < old_to_new_node_index.size()
                                ? old_to_new_node_index[old_index]
                                : -1;
    }
  }

  return kept_count;
}

bool RenderSceneConverter::MergeMeshesImpl(const RenderSceneConverterEnv &env) {
  if (!env.scene_config.merge_meshes) {
    return true;  // Merging disabled, nothing to do
  }

  DCOUT("MergeMeshesImpl: Starting mesh merge...");

  // Build a map from mesh to its node and global transform
  // Structure: mesh_index -> (node_ptr, global_matrix)
  struct MeshNodeInfo {
    Node *node{nullptr};
    value::matrix4d global_matrix;
    size_t mesh_index{0};
  };

  std::vector<MeshNodeInfo> mesh_node_infos;
  mesh_node_infos.resize(meshes.size());
  std::vector<std::vector<Node *>> mesh_nodes_by_id(meshes.size());

  // Helper to traverse nodes and collect mesh info
  std::function<void(Node &, int32_t)> collectMeshNodes = [&](Node &node, int32_t depth) {
    if (size_t(depth) >= kMaxDefaultTraversalLimit) return;
    if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
        size_t(node.id) < meshes.size()) {
      mesh_node_infos[size_t(node.id)].node = &node;
      mesh_node_infos[size_t(node.id)].global_matrix = node.global_matrix;
      mesh_node_infos[size_t(node.id)].mesh_index = size_t(node.id);
      mesh_nodes_by_id[size_t(node.id)].push_back(&node);
    }
    for (auto &child : node.children) {
      collectMeshNodes(child, depth + 1);
    }
  };

  for (auto &root : root_nodes) {
    collectMeshNodes(root, 0);
  }

  // Group meshes by material_id
  // Only include meshes that are mergeable
  std::unordered_map<int, std::vector<size_t>> material_to_meshes;
  material_to_meshes.reserve(meshes.size());

  for (size_t i = 0; i < meshes.size(); i++) {
    const auto &mesh = meshes[i];
    if (!IsMeshMergeable(mesh)) {
      continue;
    }

    // Skip meshes that don't have a node (shouldn't happen but be safe)
    if (!mesh_node_infos[i].node) {
      continue;
    }

    material_to_meshes[mesh.material_id].push_back(i);
  }

  // For each material group with 2+ meshes, merge them
  std::vector<RenderMesh> merged_meshes;
  std::vector<std::pair<int32_t, std::vector<size_t>>> merged_groups;
  [[maybe_unused]] size_t merged_source_mesh_count{0};

  // Keep deterministic output order by processing material IDs in ascending order.
  std::vector<int> sorted_material_ids;
  sorted_material_ids.reserve(material_to_meshes.size());
  for (const auto &kv : material_to_meshes) {
    sorted_material_ids.push_back(kv.first);
  }
  std::sort(sorted_material_ids.begin(), sorted_material_ids.end());

  for (int material_id : sorted_material_ids) {
    auto group_it = material_to_meshes.find(material_id);
    if (group_it == material_to_meshes.end()) {
      continue;
    }
    auto &mesh_indices = group_it->second;

    if (mesh_indices.size() < 2) {
      // Only one mesh with this material, no merging needed
      continue;
    }

    DCOUT("Merging " << mesh_indices.size() << " meshes with material_id=" << material_id);

    // Check if all meshes have the same global transform (when bake_transform is false)
    bool can_merge = true;
    if (!env.scene_config.merge_meshes_bake_transform) {
      const auto &first_matrix = mesh_node_infos[mesh_indices[0]].global_matrix;
      for (size_t i = 1; i < mesh_indices.size(); i++) {
        const auto &matrix = mesh_node_infos[mesh_indices[i]].global_matrix;
        // Compare matrices (with epsilon)
        bool same_transform = true;
        for (int r = 0; r < 4 && same_transform; r++) {
          for (int c = 0; c < 4 && same_transform; c++) {
            if (std::abs(first_matrix.m[r][c] - matrix.m[r][c]) > 1e-6) {
              same_transform = false;
            }
          }
        }
        if (!same_transform) {
          can_merge = false;
          break;
        }
      }
    }

    if (!can_merge) {
      DCOUT("Cannot merge meshes with material_id=" << material_id << " - different transforms");
      continue;
    }

    // Create merged mesh
    RenderMesh merged;
    merged.prim_name = "merged_material_" + std::to_string(material_id);
    merged.abs_path = "/merged/" + merged.prim_name;
    merged.display_name = "Merged mesh (material " + std::to_string(material_id) + ")";
    merged.material_id = material_id;

    // Copy properties from first mesh
    const auto &first_mesh = meshes[mesh_indices[0]];
    merged.doubleSided = first_mesh.doubleSided;
    merged.displayColor = first_mesh.displayColor;
    merged.displayOpacity = first_mesh.displayOpacity;
    merged.is_rightHanded = first_mesh.is_rightHanded;

    // If baking transforms, we transform all vertices to world space
    // The merged mesh will have identity transform
    bool drop_normals = false;
    bool drop_tangents = false;
    bool drop_binormals = false;
    if (env.scene_config.merge_meshes_bake_transform) {
      for (size_t idx : mesh_indices) {
        const auto &src_mesh = meshes[idx];
        const auto &node_info = mesh_node_infos[idx];
        if (tinyusdz::is_identity(node_info.global_matrix)) {
          continue;
        }
        drop_normals = drop_normals ||
                       !CanBakeDirectionAttribute(src_mesh.normals);
        drop_tangents = drop_tangents ||
                        !CanBakeDirectionAttribute(src_mesh.tangents);
        drop_binormals = drop_binormals ||
                         !CanBakeDirectionAttribute(src_mesh.binormals);
      }
    }

    std::vector<size_t> merged_sources;
    merged_sources.reserve(mesh_indices.size());

    for (size_t idx : mesh_indices) {
      const auto &src_mesh = meshes[idx];
      const auto &node_info = mesh_node_infos[idx];

      value::matrix4d relative_transform;
      if (env.scene_config.merge_meshes_bake_transform) {
        // Use world space transform
        relative_transform = node_info.global_matrix;
      } else {
        // All transforms should be the same (checked above)
        relative_transform = value::matrix4d::identity();
      }

      std::string merge_err;
      RenderMesh scratch;
      const RenderMesh *merge_src = &src_mesh;
      if (drop_normals || drop_tangents || drop_binormals) {
        scratch = src_mesh;
        if (drop_normals) {
          scratch.normals = VertexAttribute{};
        }
        if (drop_tangents) {
          scratch.tangents = VertexAttribute{};
        }
        if (drop_binormals) {
          scratch.binormals = VertexAttribute{};
        }
        merge_src = &scratch;
      }

      if (!MergeMeshData(*merge_src, relative_transform, merged, &merge_err)) {
        PushInfo("Skipping mesh merge for " + src_mesh.abs_path +
                 (merge_err.empty() ? std::string()
                                    : std::string(": ") + merge_err));
        continue;
      }

      merged_sources.push_back(idx);
    }

    if (merged_sources.size() < 2) {
      // Nothing useful to merge for this material group.
      continue;
    }

    merged_source_mesh_count += merged_sources.size();

    // The merged mesh is either in world space (if bake_transform) or
    // shares the transform of the first mesh
    merged.is_single_indexable = first_mesh.is_single_indexable;

    // Add merged mesh
    size_t new_mesh_index = meshes.size() + merged_meshes.size();
    merged_meshes.push_back(std::move(merged));

    merged_groups.emplace_back(static_cast<int32_t>(new_mesh_index),
                               std::move(merged_sources));
  }

  if (merged_meshes.empty()) {
    DCOUT("No meshes were merged");
    return true;
  }

  DCOUT("Created " << merged_meshes.size() << " merged meshes from "
                   << merged_source_mesh_count << " source meshes");

  // Add merged meshes to the mesh array
  for (auto &mm : merged_meshes) {
    meshes.push_back(std::move(mm));
  }

  // Update node references for merged sources.
  //
  // Baked merges: the merged vertices are in WORLD space, so they must render
  // with a net-identity world transform. We attach the merged mesh to a fresh
  // ROOT-level node (identity transform, no ancestors) and turn every source
  // node into a plain group, leaving its local_matrix INTACT. Crucially we do
  // NOT neutralize a source node's transform in place: source mesh nodes can be
  // nested under one another (e.g. a window mesh under a wall mesh), and
  // rewriting an ancestor's local would corrupt the world transform of any
  // descendant mesh node that was neutralized assuming the original ancestor —
  // the cause of the "floating mesh" artifact. Keeping every source node's
  // local untouched means descendants stay correctly placed; the root-level
  // merged node is independent of all of them.
  //
  // Non-baked merges: the vertices share the (identical) source-group local
  // space, so we keep the first source node carrying that transform and
  // invalidate the rest.
  //
  // The new baked-merge nodes must live INSIDE the subtree that consumers
  // traverse from the default root (getDefaultRootNode), not as detached
  // top-level siblings (which would never be rendered). Attach them as children
  // of the default root node, with a local that cancels that root's own world
  // transform so the world-space merged vertices end up net-identity.
  const size_t default_root_index =
      (default_node >= 0 && size_t(default_node) < root_nodes.size())
          ? size_t(default_node)
          : 0;
  value::matrix4d default_root_local = value::matrix4d::identity();
  if (env.scene_config.merge_meshes_bake_transform && !root_nodes.empty()) {
    value::matrix4d inv_root;
    if (tinyusdz::inverse(root_nodes[default_root_index].global_matrix,
                          inv_root)) {
      default_root_local = inv_root;
    }
  }

  std::vector<Node> new_root_merged_nodes;
  for (const auto &group : merged_groups) {
    int32_t new_id = group.first;
    const auto &source_ids = group.second;

    if (env.scene_config.merge_meshes_bake_transform) {
      const RenderMesh &mm = meshes[size_t(new_id)];
      Node mnode;
      mnode.prim_name = mm.prim_name;
      mnode.display_name = mm.display_name;
      mnode.abs_path = mm.abs_path;
      mnode.category = NodeCategory::Geom;
      mnode.nodeType = NodeType::Mesh;
      mnode.id = new_id;
      mnode.local_matrix = default_root_local;
      mnode.global_matrix = value::matrix4d::identity();
      new_root_merged_nodes.push_back(std::move(mnode));

      for (size_t old_id : source_ids) {
        if (old_id >= mesh_nodes_by_id.size()) {
          continue;
        }
        for (Node *node_ptr : mesh_nodes_by_id[old_id]) {
          if (!node_ptr) {
            continue;
          }
          // Drop the mesh content; keep the transform for descendants.
          node_ptr->category = NodeCategory::Group;
          node_ptr->nodeType = NodeType::Xform;
          node_ptr->id = -1;
        }
      }
    } else {
      bool first_assigned = false;
      for (size_t old_id : source_ids) {
        if (old_id >= mesh_nodes_by_id.size()) {
          continue;
        }
        for (Node *node_ptr : mesh_nodes_by_id[old_id]) {
          if (!node_ptr) {
            continue;
          }
          if (!first_assigned) {
            node_ptr->id = new_id;
            first_assigned = true;
          } else {
            node_ptr->category = NodeCategory::Group;
            node_ptr->nodeType = NodeType::Xform;
            node_ptr->id = -1;
          }
        }
      }
    }
  }
  // Attach under the default root so they are reachable from
  // getDefaultRootNode(); falls back to top-level if there is no root node.
  if (!new_root_merged_nodes.empty()) {
    if (!root_nodes.empty()) {
      Node &host = root_nodes[default_root_index];
      for (Node &n : new_root_merged_nodes) {
        host.children.push_back(std::move(n));
      }
    } else {
      for (Node &n : new_root_merged_nodes) {
        root_nodes.push_back(std::move(n));
      }
    }
  }

  // Drop mesh records that are no longer referenced by the node tree.
  // This keeps the exported mesh IDs compact and makes numMeshes() reflect the
  // effective renderable mesh count after native aggregation.
  std::vector<uint8_t> mesh_used(meshes.size(), uint8_t{0});
  std::function<void(const Node &)> markNodeMeshes = [&](const Node &node) {
    if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
        size_t(node.id) < mesh_used.size()) {
      mesh_used[size_t(node.id)] = uint8_t{1};
    }
    for (const Node &child : node.children) {
      markNodeMeshes(child);
    }
  };
  for (const Node &root : root_nodes) {
    markNodeMeshes(root);
  }
  for (const RenderInstance &inst : instances) {
    if (inst.mesh_id >= 0 && size_t(inst.mesh_id) < mesh_used.size()) {
      mesh_used[size_t(inst.mesh_id)] = uint8_t{1};
    }
  }
  for (const RenderLight &light : lights) {
    if (light.geometry_mesh_id >= 0 &&
        size_t(light.geometry_mesh_id) < mesh_used.size()) {
      mesh_used[size_t(light.geometry_mesh_id)] = uint8_t{1};
    }
  }

  std::vector<int32_t> mesh_remap(meshes.size(), -1);
  std::vector<RenderMesh> compact_meshes;
  compact_meshes.reserve(meshes.size());
  for (size_t i = 0; i < meshes.size(); i++) {
    if (!mesh_used[i]) {
      continue;
    }
    mesh_remap[i] = int32_t(compact_meshes.size());
    compact_meshes.push_back(std::move(meshes[i]));
  }

  std::function<void(Node &)> remapNodeMeshes = [&](Node &node) {
    if (node.nodeType == NodeType::Mesh && node.id >= 0 &&
        size_t(node.id) < mesh_remap.size()) {
      node.id = mesh_remap[size_t(node.id)];
    }
    for (Node &child : node.children) {
      remapNodeMeshes(child);
    }
  };
  for (Node &root : root_nodes) {
    remapNodeMeshes(root);
  }
  for (RenderInstance &inst : instances) {
    if (inst.mesh_id >= 0 && size_t(inst.mesh_id) < mesh_remap.size()) {
      inst.mesh_id = mesh_remap[size_t(inst.mesh_id)];
    }
  }
  for (RenderLight &light : lights) {
    if (light.geometry_mesh_id >= 0 &&
        size_t(light.geometry_mesh_id) < mesh_remap.size()) {
      light.geometry_mesh_id = mesh_remap[size_t(light.geometry_mesh_id)];
    }
  }

  // Rebuild meshMap (abs_path -> mesh index) from the surviving node tree.
  // Note: meshes referenced only by instances or lights (not by any Mesh node)
  // are kept in `meshes` but intentionally omitted here. meshMap is a
  // converter-internal lookup used during conversion (e.g. PointInstancer
  // prototype resolution, which runs before this pass) and is not exported into
  // RenderScene, so node-only coverage is sufficient.
  StringAndIdMap remapped_mesh_map;
  std::function<void(const Node &)> remapMeshMapFromNodes =
      [&](const Node &node) {
    if (!node.abs_path.empty() && node.nodeType == NodeType::Mesh &&
        node.id >= 0 && size_t(node.id) < compact_meshes.size()) {
      remapped_mesh_map.add(node.abs_path, uint64_t(node.id));
    }
    for (const Node &child : node.children) {
      remapMeshMapFromNodes(child);
    }
  };
  for (const Node &root : root_nodes) {
    remapMeshMapFromNodes(root);
  }
  meshMap = std::move(remapped_mesh_map);

  const size_t before_compact = meshes.size();
  meshes = std::move(compact_meshes);
  PushInfo("Mesh merge compacted mesh records: " +
           std::to_string(before_compact) + " -> " +
           std::to_string(meshes.size()) + ".");

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
