// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
#include <sstream>
#include <numeric>
#include <unordered_set>

#include "usd-export.hh"
#include "common-macros.inc"
#include "tiny-format.hh"
#include "math-util.inc"

namespace tinyusdz {
namespace tydra {

#define PushError(msg) { \
  if (err) { \
    (*err) += msg + "\n"; \
  } \
}

namespace detail {

static void CountNodes(const SkelNode &node, size_t &count) {
  count++;

  for (const auto &child : node.children) {
    CountNodes(child, count);
  } 
}

static bool FlattenSkelNode(const SkelNode &node,
  std::vector<value::token> &joints,
  std::vector<value::token> &jointNames,
  std::vector<value::matrix4d> &bindTransforms,
  std::vector<value::matrix4d> &restTransforms, std::string *err) {

  size_t idx = size_t(node.joint_id);
  if (idx >= joints.size()) {
    if (err) {
      (*err) += "joint_id out-of-bounds.";
    }
    return false;
  }

  joints[idx] = value::token(node.joint_path);
  jointNames[idx] = value::token(node.joint_name);
  bindTransforms[idx] = node.bind_transform;
  restTransforms[idx] = node.rest_transform;

  for (const auto &child : node.children) {
    if (!FlattenSkelNode(child, joints, jointNames, bindTransforms, restTransforms, err)) {
      return false;
    }
  }

  return true;
}


static bool ExportSkeleton(const SkelHierarchy &skel, const std::string &animSourcePath, Skeleton *dst, std::string *err) {

  size_t num_joints{0};
  CountNodes(skel.root_node, num_joints);

  std::vector<value::token> joints(num_joints);
  std::vector<value::token> jointNames(num_joints);
  std::vector<value::matrix4d> bindTransforms(num_joints);
  std::vector<value::matrix4d> restTransforms(num_joints);

  // Flatten hierachy.
  if (!FlattenSkelNode(skel.root_node, joints, jointNames, bindTransforms, restTransforms, err)) {
    return false;
  } 

  dst->joints.set_value(joints);

  // TODO: Do not author jointNames when for all i: joints[i] == jointNames[i];
  bool name_is_same = true;
  for (size_t i = 0; i < num_joints; i++) {
    if (joints[i].str() != jointNames[i].str()) {
      name_is_same = false;
      break;
    }
  }

  if (!name_is_same) {
    dst->jointNames.set_value(jointNames);
  }

  dst->bindTransforms.set_value(bindTransforms);
  dst->restTransforms.set_value(restTransforms);

  if (animSourcePath.size()) {
    Path animSourceTarget(animSourcePath, ""); 
    Relationship animSourceRel;
    animSourceRel.set(animSourceTarget);
    // TODO: add `prepend` qualifier?
    dst->animationSource = animSourceRel;
  }

  dst->name = skel.prim_name;

  return true;
}

static bool ExportBlendShape(const ShapeTarget &target, BlendShape *dst, std::string *err) {
  (void)err;

  dst->name = target.prim_name;
  if (target.display_name.size()) {
    dst->metas().displayName = target.display_name;
  }

  if (target.pointIndices.size()) {
    std::vector<int> indices(target.pointIndices.size());
    for (size_t i = 0; i < target.pointOffsets.size(); i++) {
      indices[i] = int(target.pointIndices[i]);
    }
    dst->pointIndices = indices;
  }

  if (target.pointOffsets.size()) {
    std::vector<value::vector3f> offsets(target.pointOffsets.size());
    for (size_t i = 0; i < target.pointOffsets.size(); i++) {
      offsets[i][0] = target.pointOffsets[i][0];
      offsets[i][1] = target.pointOffsets[i][1];
      offsets[i][2] = target.pointOffsets[i][2];
    }
    dst->offsets = offsets;
  }

  if (target.normalOffsets.size()) {
    std::vector<value::vector3f> normalOffsets(target.normalOffsets.size());
    for (size_t i = 0; i < target.normalOffsets.size(); i++) {
      normalOffsets[i][0] = target.normalOffsets[i][0];
      normalOffsets[i][1] = target.normalOffsets[i][1];
      normalOffsets[i][2] = target.normalOffsets[i][2];
    }
    dst->normalOffsets = normalOffsets;
  }

  return true;
}

// TODO: Support BlendShapes target
static bool ExportSkelAnimation(const Animation &anim, SkelAnimation *dst, std::string *err) {
  (void)err;
  dst->name = anim.prim_name;
  if (anim.display_name.size()) {
    dst->metas().displayName = anim.display_name;
  }

  if (anim.channels_map.empty()) {
    // TODO: Warn message
    return true;
  }

  StringAndIdMap joint_idMap;
  for (const auto &channels : anim.channels_map)
  {
    uint64_t joint_id = uint64_t(joint_idMap.size());
    joint_idMap.add(channels.first, uint64_t(joint_id));
  }

  std::vector<value::token> joints(joint_idMap.size());
  for (const auto &channels : anim.channels_map) {
    joints[joint_idMap.at(channels.first)] = value::token(channels.first);
  }
  dst->joints = joints;

  bool no_tx_channel{true};
  bool no_rot_channel{true};
  bool no_scale_channel{true};

  bool all_joints_has_tx_channel{true};
  bool all_joints_has_rot_channel{true};
  bool all_joints_has_scale_channel{true};

  for (const auto &channels : anim.channels_map) {

    bool has_tx_channel;
    bool has_rot_channel;
    bool has_scale_channel;

    has_tx_channel = channels.second.count(AnimationChannel::ChannelType::Translation);
    has_rot_channel = channels.second.count(AnimationChannel::ChannelType::Rotation);
    has_scale_channel = channels.second.count(AnimationChannel::ChannelType::Scale);

    if (has_tx_channel) {
      no_tx_channel = false;
    } else {
      all_joints_has_tx_channel = false;
    }

    if (has_rot_channel) {
      no_rot_channel = false;
    } else {
      all_joints_has_rot_channel = false;
    }

    if (has_scale_channel) {
      no_scale_channel = false;
    } else {
      all_joints_has_scale_channel = false;
    }
  }

  if (!no_tx_channel && !all_joints_has_tx_channel) {
    PUSH_ERROR_AND_RETURN("translation channel partially exists among joints. No joints have animation channel or all joints have animation channels.");
  }

  if (!no_rot_channel && !all_joints_has_rot_channel) {
    PUSH_ERROR_AND_RETURN("rotation channel partially exists among joints. No joints have animation channel or all joints have animation channels.");
  }

  if (!no_scale_channel && !all_joints_has_scale_channel) {
    PUSH_ERROR_AND_RETURN("scale channel partially exists among joints. No joints have animation channel or all joints have animation channels.");
  }

  if (no_tx_channel) {
    // Author static(default) value.
    std::vector<value::float3> translations;
    translations.assign(joints.size(), {1.0f, 1.0f, 1.0f});
    
    dst->translations.set_value(translations);
  } else {

    // All joints should have same timeCode.
    // First collect timeCodes
    std::unordered_set<float> timeCodes;

    for (const auto &channels : anim.channels_map) {

      const auto &tx_it = channels.second.find(AnimationChannel::ChannelType::Translation);
      if (tx_it != channels.second.end()) {
        for (size_t t = 0; t < tx_it->second.translations.samples.size(); t++) {
          timeCodes.insert(tx_it->second.translations.samples[t].t);
        }
      }

    }

    // key: timeCode. value: values for each joints.
    std::map<double, std::vector<value::float3>> ts_txs;
    for (const auto &tc : timeCodes) {
      ts_txs[double(tc)].resize(joints.size()); 
    }

    // Pack channel values
    for (const auto &channels : anim.channels_map) {

      const auto &tx_it = channels.second.find(AnimationChannel::ChannelType::Translation);
      if (tx_it != channels.second.end()) {

        for (size_t t = 0; t < tx_it->second.translations.samples.size(); t++) {
          float tc = tx_it->second.translations.samples[t].t;
          if (!timeCodes.count(tc)) {
            PUSH_ERROR_AND_RETURN(fmt::format("All animation channels have same timeCodes. timeCode {} is only seen in `translation` animation channel {}", tc, channels.first));
          }
          uint64_t joint_id = joint_idMap.at(channels.first);

          std::vector<value::float3> &txs = ts_txs.at(double(tc));
          // just in case
          if (joint_id > txs.size()) {
            PUSH_ERROR_AND_RETURN(fmt::format("Internal error. joint_id {} exceeds # of joints {}", joint_id, txs.size()));
          }
          txs[size_t(joint_id)] = tx_it->second.translations.samples[t].value;
        }
      }
    }

    Animatable<std::vector<value::float3>> ts;
    for (const auto &s : ts_txs) {
      ts.add_sample(s.first, s.second);
    } 

    dst->translations.set_value(ts);
  }

  if (no_rot_channel) {
    // Author static(default) value.
    std::vector<value::quatf> rots;
    value::quatf q;
    q.imag = {0.0f, 0.0f, 0.0f};
    q.real = 1.0f;
    rots.assign(joints.size(), q);

    dst->rotations.set_value(rots);
    
  } else {

    std::unordered_set<float> timeCodes;

    for (const auto &channels : anim.channels_map) {

      const auto &rot_it = channels.second.find(AnimationChannel::ChannelType::Rotation);
      if (rot_it != channels.second.end()) {
        for (size_t t = 0; t < rot_it->second.rotations.samples.size(); t++) {
          timeCodes.insert(rot_it->second.rotations.samples[t].t);
        }
      }

    }

    std::map<double, std::vector<value::quatf>> ts_rots;
    for (const auto &tc : timeCodes) {
      ts_rots[double(tc)].resize(joints.size()); 
    }

    for (const auto &channels : anim.channels_map) {

      const auto &rot_it = channels.second.find(AnimationChannel::ChannelType::Rotation);
      if (rot_it != channels.second.end()) {

        for (size_t t = 0; t < rot_it->second.rotations.samples.size(); t++) {
          float tc = rot_it->second.rotations.samples[t].t;
          if (!timeCodes.count(tc)) {
            PUSH_ERROR_AND_RETURN(fmt::format("All animation channels have same timeCodes. timeCode {} is only seen in `rotation` animation channel {}", tc, channels.first));
          }
          uint64_t joint_id = joint_idMap.at(channels.first);

          std::vector<value::quatf> &rots = ts_rots.at(double(tc));
          value::quatf v;
          v[0] = rot_it->second.rotations.samples[t].value[0];
          v[1] = rot_it->second.rotations.samples[t].value[1];
          v[2] = rot_it->second.rotations.samples[t].value[2];
          v[3] = rot_it->second.rotations.samples[t].value[3];
          rots[size_t(joint_id)] = v;
        }
      }
    }

    Animatable<std::vector<value::quatf>> ts;
    for (const auto &s : ts_rots) {
      ts.add_sample(s.first, s.second);
    } 

    dst->rotations.set_value(ts);
  }

  if (no_scale_channel) {
    // Author static(default) value.
    std::vector<value::half3> scales;
    scales.assign(joints.size(), {value::float_to_half_full(1.0f), value::float_to_half_full(1.0f), value::float_to_half_full(1.0f)});

    dst->scales.set_value(scales);
    
  } else {
    std::unordered_set<float> timeCodes;

    for (const auto &channels : anim.channels_map) {

      const auto &scale_it = channels.second.find(AnimationChannel::ChannelType::Scale);
      if (scale_it != channels.second.end()) {
        for (size_t t = 0; t < scale_it->second.scales.samples.size(); t++) {
          timeCodes.insert(scale_it->second.scales.samples[t].t);
        }
      }

    }

    std::map<double, std::vector<value::half3>> ts_scales;
    for (const auto &tc : timeCodes) {
      ts_scales[double(tc)].resize(joints.size()); 
    }

    for (const auto &channels : anim.channels_map) {

      const auto &scale_it = channels.second.find(AnimationChannel::ChannelType::Scale);
      if (scale_it != channels.second.end()) {

        for (size_t t = 0; t < scale_it->second.scales.samples.size(); t++) {
          float tc = scale_it->second.scales.samples[t].t;
          if (!timeCodes.count(tc)) {
            PUSH_ERROR_AND_RETURN(fmt::format("All animation channels have same timeCodes. timeCode {} is only seen in `scale` animation channel {}", tc, channels.first));
          }
          uint64_t joint_id = joint_idMap.at(channels.first);

          std::vector<value::half3> &scales = ts_scales.at(double(tc));
          value::half3 v;
          v[0] = value::float_to_half_full(scale_it->second.scales.samples[t].value[0]);
          v[1] = value::float_to_half_full(scale_it->second.scales.samples[t].value[1]);
          v[2] = value::float_to_half_full(scale_it->second.scales.samples[t].value[2]);
          scales[size_t(joint_id)] = v;
        }
      }
    }

    Animatable<std::vector<value::half3>> ts;
    for (const auto &s : ts_scales) {
      ts.add_sample(s.first, s.second);
    } 

    dst->scales.set_value(ts);
  }

  dst->name = anim.prim_name;
  if (anim.display_name.size()) {
    dst->metas().displayName = anim.display_name;
  }
  return true;
}

static bool ToGeomMesh(const RenderMesh &rmesh, GeomMesh *dst, std::string *err) {

  std::vector<int> fvCounts(rmesh.faceVertexCounts().size());
  for (size_t i = 0; i < rmesh.faceVertexCounts().size(); i++) {
    fvCounts[i] = int(rmesh.faceVertexCounts()[i]);
  }
  dst->faceVertexCounts.set_value(fvCounts);

  std::vector<int> fvIndices(rmesh.faceVertexIndices().size());
  for (size_t i = 0; i < rmesh.faceVertexIndices().size(); i++) {
    fvIndices[i] = int(rmesh.faceVertexIndices()[i]);
  }
  dst->faceVertexIndices.set_value(fvIndices);

  std::vector<value::point3f> points(rmesh.points.size());
  for (size_t i = 0; i < rmesh.points.size(); i++) {
    points[i][0] = rmesh.points[i][0];
    points[i][1] = rmesh.points[i][1];
    points[i][2] = rmesh.points[i][2];
  }

  dst->points = points;

  dst->name = rmesh.prim_name;
  if (rmesh.display_name.size()) {
    dst->meta.displayName = rmesh.display_name;
  }

  if (!rmesh.normals.empty()) {
    // export as primvars:normals

    const auto &vattr = rmesh.normals;
    std::vector<value::normal3f> normals(vattr.vertex_count());
    const float *psrc = reinterpret_cast<const float *>(vattr.buffer());
    for (size_t i = 0; i < vattr.vertex_count(); i++) {
      normals[i][0] = psrc[3 * i + 0];
      normals[i][1] = psrc[3 * i + 1];
      normals[i][2] = psrc[3 * i + 2];
    }

    GeomPrimvar normalPvar;
    normalPvar.set_name("normals");
    normalPvar.set_value(normals);
    if (vattr.is_facevarying()) {
      normalPvar.set_interpolation(Interpolation::FaceVarying);
    } else if (vattr.is_vertex()) {
      normalPvar.set_interpolation(Interpolation::Vertex);
    } else if (vattr.is_uniform()) {
      normalPvar.set_interpolation(Interpolation::Uniform);
    } else if (vattr.is_constant()) {
      normalPvar.set_interpolation(Interpolation::Constant);
    } else {
      PUSH_ERROR_AND_RETURN("Invalid variability in RenderMesh.normals");
    }

    // primvar name is extracted from Primvar::name
    dst->set_primvar(normalPvar);
  }

  // Primary texcoord only.
  // TODO: Multi-texcoords support
  if (rmesh.texcoords.count(0)) {
    const auto &vattr = rmesh.texcoords.at(0);
    std::vector<value::texcoord2f> texcoords(vattr.vertex_count());
    const float *psrc = reinterpret_cast<const float *>(vattr.buffer());
    for (size_t i = 0; i < vattr.vertex_count(); i++) {
      texcoords[i][0] = psrc[2 * i + 0];
      texcoords[i][1] = psrc[2 * i + 1];
    }

    GeomPrimvar uvPvar;
    uvPvar.set_name(vattr.name);
    uvPvar.set_value(texcoords);
    if (vattr.is_facevarying()) {
      uvPvar.set_interpolation(Interpolation::FaceVarying);
    } else if (vattr.is_vertex()) {
      uvPvar.set_interpolation(Interpolation::Vertex);
    } else if (vattr.is_uniform()) {
      uvPvar.set_interpolation(Interpolation::Uniform);
    } else if (vattr.is_constant()) {
      uvPvar.set_interpolation(Interpolation::Constant);
    } else {
      PUSH_ERROR_AND_RETURN("Invalid variability in RenderMesh.texcoord0");
    }

    dst->set_primvar(uvPvar);
  }

  if (rmesh.joint_and_weights.jointIndices.size() && rmesh.joint_and_weights.jointWeights.size() && (rmesh.joint_and_weights.elementSize > 0)) {
    GeomPrimvar weightpvar;
    weightpvar.set_name("skel:jointWeights");
    weightpvar.set_value(rmesh.joint_and_weights.jointWeights);
    weightpvar.set_elementSize(uint32_t(rmesh.joint_and_weights.elementSize));
    weightpvar.set_interpolation(Interpolation::Vertex);
    dst->set_primvar(weightpvar);

    GeomPrimvar idxpvar;
    idxpvar.set_name("skel:jointIndices");
    idxpvar.set_value(rmesh.joint_and_weights.jointIndices);
    idxpvar.set_elementSize(uint32_t(rmesh.joint_and_weights.elementSize));
    idxpvar.set_interpolation(Interpolation::Vertex);
    dst->set_primvar(idxpvar);

    // matrix4d primvars:skel:geomBindTransform
    GeomPrimvar geomBindTransform;
    geomBindTransform.set_name("skel:geomBindTransform");
    geomBindTransform.set_value(rmesh.joint_and_weights.geomBindTransform);
    dst->set_primvar(geomBindTransform);
  }

  // TODO: GeomSubset, Material assignment, ...

  return true;
}

///
/// Convert Material and Shader graph as Material Prim(it encloses Shaders).
///
static bool ToMaterialPrim(const RenderScene &scene, const std::string &abs_path, size_t material_id, Prim *dst, std::string *err) {

  const RenderMaterial &rmat = scene.materials[material_id];

  // TODO: create two UsdUVTextures for RGBA imagge(rgb and alpha)
  auto ConstructUVTexture = [&](const UVTexture &tex, const std::string &param_name, const std::string &abs_mat_path, /* inout */std::vector<Shader> &shader_nodes) -> bool {


    std::string preaderPrimPath = abs_mat_path + "/uvmap_" + param_name + ".outputs:result";
    Path preaderPath(preaderPrimPath, "");

    if ((tex.texture_image_id < 0) || (size_t(tex.texture_image_id) >= scene.images.size())) {
      PUSH_ERROR_AND_RETURN(fmt::format("Invalid texture_image_id for `{}`", param_name));
    }

    const TextureImage &src_teximg = scene.images[size_t(tex.texture_image_id)];

    UsdUVTexture image_tex;
    image_tex.name = "Image_Texture_" + param_name;

    DCOUT("asset_identifier: " << src_teximg.asset_identifier);
    if (src_teximg.asset_identifier.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format("file asset name is empty for texture image `{}`", param_name));
    }
    value::AssetPath fileAssetPath(src_teximg.asset_identifier);
    image_tex.file = fileAssetPath;

    // TODO: Set colorSpace in attribute meta.
    if (src_teximg.colorSpace == ColorSpace::sRGB) {
      image_tex.sourceColorSpace = UsdUVTexture::SourceColorSpace::SRGB;
    } else if (src_teximg.colorSpace == ColorSpace::Linear) {
      image_tex.sourceColorSpace = UsdUVTexture::SourceColorSpace::Raw;
    } else {
      image_tex.sourceColorSpace = UsdUVTexture::SourceColorSpace::Auto;
    }
    
    image_tex.st.set_connection(preaderPath);

    if (tex.wrapS == UVTexture::WrapMode::CLAMP_TO_EDGE) {
      image_tex.wrapS = UsdUVTexture::Wrap::Clamp;
    } else if (tex.wrapS == UVTexture::WrapMode::REPEAT) {
      image_tex.wrapS = UsdUVTexture::Wrap::Repeat;
    } else if (tex.wrapS == UVTexture::WrapMode::MIRROR) {
      image_tex.wrapS = UsdUVTexture::Wrap::Mirror;
    } else if (tex.wrapS == UVTexture::WrapMode::CLAMP_TO_BORDER) {
      image_tex.wrapS = UsdUVTexture::Wrap::Black;
    } else {
      image_tex.wrapS = UsdUVTexture::Wrap::Repeat;
    }

    if (tex.wrapT == UVTexture::WrapMode::CLAMP_TO_EDGE) {
      image_tex.wrapT = UsdUVTexture::Wrap::Clamp;
    } else if (tex.wrapT == UVTexture::WrapMode::REPEAT) {
      image_tex.wrapT = UsdUVTexture::Wrap::Repeat;
    } else if (tex.wrapT == UVTexture::WrapMode::MIRROR) {
      image_tex.wrapT = UsdUVTexture::Wrap::Mirror;
    } else if (tex.wrapT == UVTexture::WrapMode::CLAMP_TO_BORDER) {
      image_tex.wrapT = UsdUVTexture::Wrap::Black;
    } else {
      image_tex.wrapT = UsdUVTexture::Wrap::Repeat;
    }

    if (tex.outputChannel == UVTexture::Channel::R) {
      image_tex.outputsR.set_authored(true);
    } else if (tex.outputChannel == UVTexture::Channel::G) {
      image_tex.outputsG.set_authored(true);
    } else if (tex.outputChannel == UVTexture::Channel::B) {
      image_tex.outputsB.set_authored(true);
    } else if (tex.outputChannel == UVTexture::Channel::RGB) {
      image_tex.outputsRGB.set_authored(true);
    } else if (tex.outputChannel == UVTexture::Channel::RGBA) {
      PUSH_ERROR_AND_RETURN("rgba texture is not supported yet.");
    } 

    UsdTransform2d uv_xform;
    uv_xform.name = "place2d_" + param_name;
    if (tex.has_transform2d) {
      // TODO: Deompose tex.transform and use it?
      uv_xform.translation = tex.tx_translation;
      uv_xform.rotation = tex.tx_rotation;
      uv_xform.scale = tex.tx_scale;

      Shader uv_xformShader;
      uv_xformShader.name = "place2d_" + param_name;
      uv_xformShader.info_id = tinyusdz::kUsdTransform2d;
      uv_xformShader.value = uv_xform;
      shader_nodes.emplace_back(std::move(uv_xformShader));
    }

    UsdPrimvarReader_float2 preader;
    preader.name = "primvar_reader_" + param_name;

    value::float2 fallback_value;
    fallback_value[0] = tex.fallback_uv[0];
    fallback_value[1] = tex.fallback_uv[1];
    if (!math::is_close(fallback_value, {0.0f, 0.0f})) {
      preader.fallback = fallback_value;
    }

    Animatable<std::string> varname;
    // TODO: Read varname from RenderMesh.
    varname = std::string("UVMap");
    preader.varname.set_value(varname);
    preader.result.set_authored(true);


    Shader preaderShader;
    preaderShader.name = "uvmap_" + param_name;
    preaderShader.info_id = tinyusdz::kUsdPrimvarReader_float2;
    preaderShader.value = preader;

    shader_nodes.emplace_back(std::move(preaderShader));

    Shader imageTexShader;
    imageTexShader.name = "Image_Texture_" + param_name;
    imageTexShader.info_id = tinyusdz::kUsdUVTexture;
    imageTexShader.value = image_tex;

    shader_nodes.emplace_back(std::move(imageTexShader));
    
    return true;
  };


  // Layout
  //
  // - Material
  //   - Shader(UsdPreviewSurface)
  //     - UsdUVTexture(Shader)
  //     - TexTransform2d(Shader)
  //     - PrimvarReader
  //

  Material mat;

  mat.name = rmat.name;

  Shader shader;  // root Shader 
  shader.name = "defaultPBR";

  std::string abs_mat_path = abs_path + "/" + mat.name;
  std::string abs_shader_path = abs_mat_path + "/" + shader.name;

  std::vector<Shader> shader_nodes;
  
  {
    UsdPreviewSurface surfaceShader;  // Concrete Shader node object

    //
    // Asssign actual shader object to Shader::value.
    // Also do not forget set its shader node type name through Shader::info_id
    //
    shader.info_id = tinyusdz::kUsdPreviewSurface;  // "UsdPreviewSurface" token

    //
    // Currently no shader network/connection API.
    // Manually construct it.
    //
    surfaceShader.outputsSurface.set_authored(
        true);  // Author `token outputs:surface`

    surfaceShader.useSpecularWorkflow = rmat.surfaceShader.useSpecularWorkFlow ? 1 : 0;

    if (rmat.surfaceShader.diffuseColor.is_texture()) {

      if (size_t(rmat.surfaceShader.diffuseColor.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'diffuseColor' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.diffuseColor.textureId)], "diffuseColor", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'diffuseColor' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "diffuseColor", "outputs:rgb");
      surfaceShader.diffuseColor.set_connection(connPath);
    } else {
      value::color3f diffuseCol;
      diffuseCol.r = rmat.surfaceShader.diffuseColor.value[0];
      diffuseCol.g = rmat.surfaceShader.diffuseColor.value[1];
      diffuseCol.b = rmat.surfaceShader.diffuseColor.value[2];

      surfaceShader.diffuseColor.set_value(diffuseCol);
    }

    if (rmat.surfaceShader.specularColor.is_texture()) {

      if (size_t(rmat.surfaceShader.specularColor.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'specularColor' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.specularColor.textureId)], "specularColor", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'specularColor' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "specularColor", "outputs:rgb");
      surfaceShader.specularColor.set_connection(connPath);
    } else {
      value::color3f col;
      col.r = rmat.surfaceShader.specularColor.value[0];
      col.g = rmat.surfaceShader.specularColor.value[1];
      col.b = rmat.surfaceShader.specularColor.value[2];
      surfaceShader.specularColor = col;
    }

    if (rmat.surfaceShader.emissiveColor.is_texture()) {

      if (size_t(rmat.surfaceShader.emissiveColor.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'emissiveColor' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.emissiveColor.textureId)], "emissiveColor", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'emissiveColor' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "emissiveColor", "outputs:rgb");
      surfaceShader.emissiveColor.set_connection(connPath);
    } else {
      value::color3f col;
      col.r = rmat.surfaceShader.emissiveColor.value[0];
      col.g = rmat.surfaceShader.emissiveColor.value[1];
      col.b = rmat.surfaceShader.emissiveColor.value[2];
      surfaceShader.emissiveColor = col;
    }

    if (rmat.surfaceShader.metallic.is_texture()) {

      if (size_t(rmat.surfaceShader.metallic.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'metallic' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.metallic.textureId)], "metallic", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'metallic' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "metallic", "outputs:r");
      surfaceShader.metallic.set_connection(connPath);
    } else {
      surfaceShader.metallic = rmat.surfaceShader.metallic.value;
    }

    if (rmat.surfaceShader.roughness.is_texture()) {

      if (size_t(rmat.surfaceShader.roughness.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'roughness' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.roughness.textureId)], "roughness", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'roughness' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "roughness", "outputs:r");
      surfaceShader.roughness.set_connection(connPath);
    } else {
      surfaceShader.roughness = rmat.surfaceShader.roughness.value;
    }

    if (rmat.surfaceShader.clearcoat.is_texture()) {

      if (size_t(rmat.surfaceShader.clearcoat.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'clearcoat' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.clearcoat.textureId)], "clearcoat", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'clearcoat' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "clearcoat", "outputs:r");
      surfaceShader.clearcoat.set_connection(connPath);
    } else {
      surfaceShader.clearcoat = rmat.surfaceShader.clearcoat.value;
    }

    if (rmat.surfaceShader.clearcoatRoughness.is_texture()) {

      if (size_t(rmat.surfaceShader.clearcoatRoughness.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'clearcoatRoughness' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.clearcoatRoughness.textureId)], "clearcoatRoughness", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'clearcoatRoughness' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "clearcoatRoughness", "outputs:r");
      surfaceShader.clearcoatRoughness.set_connection(connPath);
    } else {
      surfaceShader.clearcoatRoughness = rmat.surfaceShader.clearcoatRoughness.value;
    }

    if (rmat.surfaceShader.opacity.is_texture()) {

      if (size_t(rmat.surfaceShader.opacity.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'opacity' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.opacity.textureId)], "opacity", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'opacity' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "opacity", "outputs:r");
      surfaceShader.opacity.set_connection(connPath);
    } else {
      surfaceShader.opacity = rmat.surfaceShader.opacity.value;
    }

    if (rmat.surfaceShader.opacityThreshold.is_texture()) {

      if (size_t(rmat.surfaceShader.opacityThreshold.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'opacityThreshold' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.opacityThreshold.textureId)], "opacityThreshold", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'opacityThreshold' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "opacityThreshold", "outputs:r");
      surfaceShader.opacityThreshold.set_connection(connPath);
    } else {
      surfaceShader.opacityThreshold = rmat.surfaceShader.opacityThreshold.value;
    }

    if (rmat.surfaceShader.ior.is_texture()) {

      if (size_t(rmat.surfaceShader.ior.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'ior' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.ior.textureId)], "ior", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'ior' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "ior", "outputs:r");
      surfaceShader.ior.set_connection(connPath);
    } else {
      surfaceShader.ior = rmat.surfaceShader.ior.value;
    }

    if (rmat.surfaceShader.occlusion.is_texture()) {

      if (size_t(rmat.surfaceShader.occlusion.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'occlusion' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.occlusion.textureId)], "occlusion", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'occlusion' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "occlusion", "outputs:r");
      surfaceShader.occlusion.set_connection(connPath);
    } else {
      surfaceShader.occlusion = rmat.surfaceShader.occlusion.value;
    }

    if (rmat.surfaceShader.normal.is_texture()) {

      if (size_t(rmat.surfaceShader.normal.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'normal' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.normal.textureId)], "normal", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'normal' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "normal", "outputs:rgb");
      surfaceShader.normal.set_connection(connPath);
    } else {
      value::normal3f n;
      n[0] = rmat.surfaceShader.normal.value[0];
      n[1] = rmat.surfaceShader.normal.value[1];
      n[2] = rmat.surfaceShader.normal.value[2];
      surfaceShader.normal = n;
    }

    if (rmat.surfaceShader.displacement.is_texture()) {

      if (size_t(rmat.surfaceShader.displacement.textureId) > scene.textures.size()) {
        PUSH_ERROR_AND_RETURN("Invalid texture_id for 'displacement' texture.");
      }
    
      if (!ConstructUVTexture(scene.textures[size_t(rmat.surfaceShader.displacement.textureId)], "displacement", abs_mat_path, shader_nodes)) {
        PUSH_ERROR_AND_RETURN("Failed to convert 'displacement' texture.");
      }

      Path connPath(abs_mat_path + "/Image_Texture_" + "displacement", "outputs:rgb");
      surfaceShader.displacement.set_connection(connPath);
    } else {
      surfaceShader.displacement = rmat.surfaceShader.displacement.value;
    }

    // Connect to UsdPreviewSurface's outputs:surface by setting targetPath.
    //
    // token outputs:surface = </path/to/mat/defaultPBR.outputs:surface>
    mat.surface.set(tinyusdz::Path(/* prim path */ abs_shader_path,
                                   /* prop path */ "outputs:surface"));

    //
    // Shaer::value is `value::Value` type, so can use '=' to assign Shader
    // object.
    //
    shader.value = std::move(surfaceShader);
  }

  Prim shaderPrim(shader);

  Prim matPrim(mat);

  matPrim.add_child(std::move(shaderPrim));

  for (const auto &node : shader_nodes) {
    Prim shaderNodePrim(node);
    
    if (!matPrim.add_child(std::move(shaderNodePrim), /* rename_primname */false, err)) {
      return false;
    } 
  }
  
  (*dst) = std::move(matPrim);
  return true;
}

} // namespace detail

bool export_to_usda(const RenderScene &scene,
  std::string &usda_str, std::string *warn, std::string *err) {

  (void)warn;

  Stage stage;

  stage.metas().comment = "Exported from TinyUSDZ Tydra.";
  if (scene.meta.upAxis == "X") {
    stage.metas().upAxis = Axis::X;
  } else if (scene.meta.upAxis == "Y") {
    stage.metas().upAxis = Axis::Y;
  } else if (scene.meta.upAxis == "Z") {
    stage.metas().upAxis = Axis::Z;
  }

  // TODO: Construct Node hierarchy

  for (size_t i = 0; i < scene.meshes.size(); i++) {
    GeomMesh mesh;
    if (!detail::ToGeomMesh(scene.meshes[i], &mesh, err)) {
      return false;
    }

    Skeleton skel;
    bool has_skel{false};

    if ((scene.meshes[i].skel_id > -1) && (size_t(scene.meshes[i].skel_id) < scene.skeletons.size())) {
      DCOUT("Export Skeleton");

      const SkelHierarchy &src_skel = scene.skeletons[size_t(scene.meshes[i].skel_id)];

      std::string src_animsource; // empty = no animationSource
      if (src_skel.anim_id > -1) {
        src_animsource = "/animations/" + scene.animations[size_t(src_skel.anim_id)].prim_name;
      }

      if (!detail::ExportSkeleton(src_skel, src_animsource, &skel, err)) {
        return false;
      }

      // Add some skel settings to GeomMesh.
      
      // rel skel:skeleton
      Relationship skelRel;
      // FIXME: Set abs_path 
      Path skelTargetPath("/skelRoot" + std::to_string(i) + "/" + skel.name, "");
      skelRel.set(skelTargetPath);
      mesh.skeleton = skelRel;

    
      // Add apiSchemas both for GeomMesh and Skeleton
      //
      // prepend apiSchemas = ["SkelBindingAPI"]
      //
      APISchemas skelAPISchema;
      skelAPISchema.listOpQual = ListEditQual::Prepend;
      skelAPISchema.names.push_back({APISchemas::APIName::SkelBindingAPI, ""});
      mesh.metas().apiSchemas = skelAPISchema;

      skel.metas().apiSchemas = skelAPISchema;

      has_skel = true;
    }

    std::vector<BlendShape> bss;
    if (scene.meshes[i].targets.size()) {

      std::vector<value::token> bsNames;
      Relationship bsTargets;

      for (const auto &target : scene.meshes[i].targets) {
        BlendShape bs;
        if (!detail::ExportBlendShape(target.second, &bs, err)) {
          return false;
        }

        bss.emplace_back(bs);
        bsNames.push_back(value::token(target.first));
        // TODO: Set abs_path
        Path targetPath = Path(mesh.name, "").AppendPrim(target.first);
        bsTargets.targetPathVector.push_back(targetPath);
      }

      mesh.blendShapeTargets = bsTargets;
      mesh.blendShapes = bsNames;

    }

    // Add BlendShape prim under GeomMesh prim.
    Prim meshPrim(mesh);

    if (bss.size()) {
      for (size_t t = 0; t < bss.size(); t++) {
        Prim bsPrim(bss[t]);
        meshPrim.add_child(std::move(bsPrim));
      }
    }

    if (has_skel) {

      SkelRoot skelRoot;
      skelRoot.set_name("skelRoot" + std::to_string(i)); 

      Prim skelPrim(skel);

      Prim skelRootPrim(skelRoot);
      skelRootPrim.add_child(std::move(meshPrim), /* rename_primname_if_require */true);
      skelRootPrim.add_child(std::move(skelPrim), /* rename_primname_if_require */true);

      stage.add_root_prim(std::move(skelRootPrim));

    } else {
      // Put Prims under Scope Prim
      Scope scope;
      scope.name = "scope" + std::to_string(i);

      Prim scopePrim(scope);
      scopePrim.add_child(std::move(meshPrim));

      stage.add_root_prim(std::move(scopePrim));
    }

  }

  {
    Scope animGroup;
    animGroup.name = "animations";

    Prim animGroupPrim(animGroup);

    for (size_t i = 0; i < scene.animations.size(); i++) {
      SkelAnimation skelAnim;
      if (!detail::ExportSkelAnimation(scene.animations[i], &skelAnim, err)) {
        return false;
      }

      // TODO: Put SkelAnimation under SkelRoot
      Prim prim(skelAnim);
      animGroupPrim.add_child(std::move(prim));
    }
    stage.add_root_prim(std::move(animGroupPrim));
  }

  {
    Scope matGroup;
    matGroup.name = "materials";

    Prim matGroupPrim(matGroup);

    DCOUT("export " << scene.materials.size() << " materials");

    for (size_t i = 0; i < scene.materials.size(); i++) {
      // init with dummy object(Model Prim)
      Model dummy;
      Prim matPrim(std::move(dummy));  
      if (!detail::ToMaterialPrim(scene, "/materials", i, &matPrim, err)) {
        return false;
      }
      if (!matGroupPrim.add_child(std::move(matPrim), /* rename_primname_if_required */false, err)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to add child Prim: {}", err));
      }
      
    }
    stage.add_root_prim(std::move(matGroupPrim));
  }

  usda_str =stage.ExportToString();

  return true;
}


} // namespace tydra
} // namespace tinyusdz

