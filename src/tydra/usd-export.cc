// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
#include <sstream>
#include <numeric>
#include <unordered_set>

#include "usd-export.hh"
#include "common-macros.inc"
#include "tiny-format.hh"

namespace tinyusdz {
namespace tydra {

#define PushError(msg) { \
  if (err) { \
    (*err) += msg + "\n"; \
  } \
}

namespace detail {

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
static bool ExportSkelAnimation(const Animation &anim, const std::vector<std::string> &jointNames, SkelAnimation *dst, std::string *err) {
  (void)err;
  dst->name = anim.prim_name;
  if (anim.display_name.size()) {
    dst->metas().displayName = anim.display_name;
  }

  {
    std::vector<value::token> joints(jointNames.size());
    for (size_t i = 0; i < jointNames.size(); i++) {
      joints[i] = value::token(jointNames[i]);
    }
    dst->joints = joints;
  }

  for (size_t i = 0; i < anim.channels.size(); i++) {
    const AnimationChannel &channel = anim.channels[i];

    if (channel.rotations.samples.size()) {
      Animatable<std::vector<value::quatf>> rots;
      for (const auto &sample : channel.rotations.samples) {
        std::vector<value::quatf> ts(sample.value.size());
        for (size_t k = 0; k < sample.value.size(); k++) {
          ts[k][0] = sample.value[k][0];
          ts[k][1] = sample.value[k][1];
          ts[k][2] = sample.value[k][2];
          ts[k][3] = sample.value[k][3];
        }
        rots.add_sample(double(sample.t), ts);
      }
      dst->rotations.set_value(rots);
    } else if (channel.translations.samples.size()) {
      Animatable<std::vector<value::float3>> txs;
      for (const auto &sample : channel.translations.samples) {
        txs.add_sample(double(sample.t), sample.value);
      }
      dst->translations.set_value(txs);
    } else if (channel.scales.samples.size()) {
      Animatable<std::vector<value::half3>> scales;
      for (const auto &sample : channel.scales.samples) {
        std::vector<value::half3> ts(sample.value.size());
        for (size_t k = 0; k < sample.value.size(); k++) {
          ts[k][0] = tinyusdz::value::float_to_half_full(sample.value[k][0]);
          ts[k][1] = tinyusdz::value::float_to_half_full(sample.value[k][1]);
          ts[k][2] = tinyusdz::value::float_to_half_full(sample.value[k][2]);
        }
        scales.add_sample(double(sample.t), ts);
      }
      dst->scales.set_value(scales);
    }
  }
  return false;
}

static bool ToGeomMesh(const RenderMesh &rmesh, GeomMesh *dst, std::string *err) {
  std::vector<int> fvCounts(rmesh.faceVertexCounts().size());
  for (size_t i = 0; i < rmesh.faceVertexCounts().size(); i++) {
    fvCounts[i] = int(rmesh.faceVertexCounts()[i]);
  }
  dst->faceVertexCounts = fvCounts;

  std::vector<int> fvIndices(rmesh.faceVertexIndices().size());
  for (size_t i = 0; i < rmesh.faceVertexIndices().size(); i++) {
    fvCounts[i] = int(rmesh.faceVertexIndices()[i]);
  }
  dst->faceVertexIndices = fvIndices;

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

  // TODO: GeomSubset, Material assignment, skel binding, ...
 
  return true;
}

} // namespace

bool export_to_usda(const RenderScene &scene,
  std::string &usda_str, std::string *warn, std::string *err) {

  (void)warn;

  Stage stage;

  stage.metas().comment = "Exported from TinyUSDZ Tydra.";
  if (scene.upAxis == "X") {
    stage.metas().upAxis = Axis::X;
  } else if (scene.upAxis == "Y") {
    stage.metas().upAxis = Axis::Y;
  } else if (scene.upAxis == "Z") {
    stage.metas().upAxis = Axis::Z;
  } 

  // TODO: Construct Node hierarchy

  for (size_t i = 0; i < scene.meshes.size(); i++) {
    GeomMesh mesh;
    if (!detail::ToGeomMesh(scene.meshes[i], &mesh, err)) {
      return false;
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
    Prim prim(mesh);

    if (bss.size()) {
      for (size_t t = 0; t < bss.size(); t++) {
        Prim bsPrim(bss[t]);
        prim.add_child(std::move(bsPrim));
      }
    }

    stage.add_root_prim(std::move(prim));
  }

  for (size_t i = 0; i < scene.animations.size(); i++) {
    SkelAnimation skelAnim;
    std::vector<std::string> jointNames; // TODO: Read jointNames from SkelHierarchy.
    if (!detail::ExportSkelAnimation(scene.animations[i], jointNames, &skelAnim, err)) {
      return false;
    }

    // TODO: Put SkelAnimation under SkelRoot
    Prim prim(skelAnim);
    stage.add_root_prim(std::move(prim));
  }

  usda_str =stage.ExportToString();

  return true;
}


} // namespace tydra
} // namespace tinyusdz

