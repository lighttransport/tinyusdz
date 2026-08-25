// SPDX-License-Identifier: Apache-2.0
// Adapter from tusdrender's streamed flat geometry to the GPU RT core shared
// with tusdview. Material enrichment is performed before this boundary; this
// file deliberately owns only geometry/camera conversion and image output.
#if defined(HAVE_CUDA_RT) || defined(HAVE_HIP)

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef HAVE_CUDA_RT
#include "cuda/cuda_raytracer.hh"
#endif
#ifdef HAVE_HIP
#include "hip/hip_raytracer.hh"
#endif
#include "rt_scene_build.hh"
#include "lightrt_mtlx_bridge.hh"
#include "light3d/math.h"
#include "image-writer.hh"
#include "tusdr_context.hh"

namespace tusdr {
namespace {

void SetIdentity(float m[16]) {
  std::fill(m, m + 16, 0.0f);
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

std::vector<uint8_t> ToneMapLinear(const std::vector<float> &linear,
                                   int width, int height, float exposure) {
  std::vector<uint8_t> rgba(
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
  const float gain = std::exp2(exposure);
  const auto aces = [](float x) {
    return std::max(0.0f, std::min(1.0f,
        (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)));
  };
  const auto srgb = [](float x) {
    return x <= 0.0031308f ? 12.92f * x
                           : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
  };
  if (linear.size() != rgba.size()) return rgba;
  for (size_t i = 0; i < rgba.size(); i += 4u) {
    for (size_t channel = 0; channel < 3u; ++channel) {
      const float mapped = srgb(aces(std::max(0.0f, linear[i + channel]) * gain));
      rgba[i + channel] = static_cast<uint8_t>(std::lround(
          std::max(0.0f, std::min(1.0f, mapped)) * 255.0f));
    }
  }
  return rgba;
}

bool MakeDrawScene(const std::vector<Vec3> &base_colors,
                   const std::vector<RTPreviewStats::MeshGeometry> &geos,
                   const std::vector<ResolvedMat> &materials,
                   const std::vector<Texture> &textures,
                   const LightCache &lights,
                   const std::string &asset_path,
                   tusdview::DrawScene *scene) {
  if (!scene) return false;
  *scene = tusdview::DrawScene{};
  const auto append_light = [&](const PreviewLight &src) {
    tusdview::DrawLightCPU dst;
    dst.name = "tusdrender_light_" + std::to_string(scene->lights.size());
    switch (src.kind) {
      case PreviewLight::Kind::Point: dst.type = tusdview::DrawLightCPU::Type::Point; break;
      case PreviewLight::Kind::Sphere: dst.type = tusdview::DrawLightCPU::Type::Sphere; break;
      case PreviewLight::Kind::Rect: dst.type = tusdview::DrawLightCPU::Type::Rect; break;
      case PreviewLight::Kind::Disk: dst.type = tusdview::DrawLightCPU::Type::Disk; break;
      case PreviewLight::Kind::Cylinder: dst.type = tusdview::DrawLightCPU::Type::Cylinder; break;
      case PreviewLight::Kind::Distant: dst.type = tusdview::DrawLightCPU::Type::Distant; break;
      case PreviewLight::Kind::Dome: dst.type = tusdview::DrawLightCPU::Type::Dome; break;
      case PreviewLight::Kind::Mesh: dst.type = tusdview::DrawLightCPU::Type::Geometry; break;
    }
    const float position[3] = {src.position.x, src.position.y, src.position.z};
    const float direction[3] = {src.direction.x, src.direction.y, src.direction.z};
    const float color[3] = {src.radiance.x, src.radiance.y, src.radiance.z};
    for (size_t c = 0; c < 3u; ++c) {
      dst.position[c] = position[c];
      dst.direction[c] = direction[c];
      dst.color[c] = color[c];
      dst.effectiveColor[c] = color[c];
      dst.normalizedColor[c] = color[c];
    }
    dst.radius = src.radius;
    dst.width = src.width;
    dst.height = src.height;
    dst.length = src.length;
    dst.area = src.area;
    dst.invArea = src.area > 0.0f ? 1.0f / src.area : 0.0f;
    if (src.kind == PreviewLight::Kind::Mesh) {
      dst.geometryTriOffset = src.tri_id;
      dst.geometryTriCount = src.tri_id >= 0 ? 1 : 0;
    }
    dst.shadowEnable = src.shadow_enable;
    scene->lights.push_back(std::move(dst));
  };
  for (const PreviewLight &light : lights.finite) append_light(light);
  for (const PreviewLight &light : lights.mesh) append_light(light);
  if (lights.has_dome) append_light(lights.dome);
  float lo[3] = {std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max()};
  float hi[3] = {-std::numeric_limits<float>::max(),
                 -std::numeric_limits<float>::max(),
                 -std::numeric_limits<float>::max()};
  scene->textures.reserve(textures.size());
  for (const Texture &src : textures) {
    tusdview::DrawTextureCPU dst;
    dst.srgb = src.srgb;
    dst.wrapS = src.wrap_s == WrapMode::Clamp ? 0
                : src.wrap_s == WrapMode::Mirror ? 2
                : src.wrap_s == WrapMode::Black ? 3 : 1;
    dst.wrapT = src.wrap_t == WrapMode::Clamp ? 0
                : src.wrap_t == WrapMode::Mirror ? 2
                : src.wrap_t == WrapMode::Black ? 3 : 1;
    auto convert_image = [](int width, int height, int channels,
                            const std::vector<uint8_t> &pixels,
                            light3d::Image *image) {
      image->width = width;
      image->height = height;
      image->channels = 4;
      image->data.resize(size_t(width) * size_t(height) * 4u, 255u);
      for (size_t p = 0; p < size_t(width) * size_t(height); ++p) {
        for (int c = 0; c < 3; ++c) {
          const int source = channels == 1 ? 0 : std::min(c, channels - 1);
          image->data[p * 4u + size_t(c)] = pixels[p * size_t(channels) + size_t(source)];
        }
        if (channels > 3) image->data[p * 4u + 3u] = pixels[p * size_t(channels) + 3u];
      }
    };
    if (!src.is_udim) {
      convert_image(src.width, src.height, src.channels, src.pixels, &dst.image);
    } else {
      dst.isUdim = true;
      for (const Texture::UdimTile &tile : src.udim_tiles) {
        tusdview::DrawUdimTileCPU out_tile;
        out_tile.udim = static_cast<uint32_t>(tile.udim);
        out_tile.u = static_cast<uint32_t>((tile.udim - 1001) % 10);
        out_tile.v = static_cast<uint32_t>((tile.udim - 1001) / 10);
        convert_image(tile.width, tile.height, tile.channels, tile.pixels,
                      &out_tile.image);
        dst.udimTiles.push_back(std::move(out_tile));
      }
    }
    scene->textures.push_back(std::move(dst));
  }

  scene->materials.reserve(geos.size());
  scene->meshes.reserve(geos.size());
  for (size_t mi = 0; mi < geos.size(); ++mi) {
    const auto &src = geos[mi];
    if (src.positions.size() % 3u != 0u || src.indices.size() % 3u != 0u)
      return false;
    const size_t nv = src.positions.size() / 3u;
    const Vec3 color = mi < base_colors.size()
                           ? base_colors[mi]
                           : Vec3{0.5f, 0.5f, 0.5f};
    tusdview::DrawMaterialCPU mat;
    mat.name = "tusdrender_material_" + std::to_string(mi);
    // MaterialX image paths are relative to the containing USD layer. The
    // streamed resolver has already populated the semantic texture slots, but
    // arbitrary ND_image nodes in the retained graph still need this base path.
    mat.absPath = asset_path;
    mat.baseColor[0] = color.x;
    mat.baseColor[1] = color.y;
    mat.baseColor[2] = color.z;
    if (mi < materials.size()) {
      const ResolvedMat &src = materials[mi];
      mat.baseColor[0] = src.base_color.x;
      mat.baseColor[1] = src.base_color.y;
      mat.baseColor[2] = src.base_color.z;
      mat.baseColorTex = src.tex_id;
      mat.roughness = src.roughness;
      mat.metallic = src.metallic;
      mat.normalTex = src.normal_tex_id;
      mat.coatNormalTex = src.coat_normal_tex_id;
      mat.roughnessTex = src.rough_tex.id;
      mat.metallicTex = src.metal_tex.id;
      mat.emissive[0] = src.emission.x;
      mat.emissive[1] = src.emission.y;
      mat.emissive[2] = src.emission.z;
      mat.emissiveTex = src.emission_tex_id;
      mat.occlusion = src.occlusion;
      mat.occlusionTex = src.occ_tex.id;
      mat.opacityTex = src.opacity_tex.id;
      mat.alpha = src.opacity;
      mat.alphaCutoff = src.opacity_threshold;
      mat.alphaMode = src.opacity_threshold > 0.0f
                          ? static_cast<int>(tusdview::AlphaMode::Mask)
                          : src.opacity < 1.0f
                                ? static_cast<int>(tusdview::AlphaMode::Blend)
                                : static_cast<int>(tusdview::AlphaMode::Opaque);
      mat.coatWeight = src.clearcoat;
      mat.coatRoughness = src.clearcoat_roughness;
      mat.coatWeightTex = src.clearcoat_tex.id;
      mat.coatRoughnessTex = src.clearcoat_rough_tex.id;
      mat.displacementConst = src.displacement;
      mat.displacementTex = src.displacement_tex.id;
      mat.displacementTexScale = src.displacement_tex.scale;
      mat.displacementTexBias = src.displacement_tex.bias;
      mat.specularColor[0] = src.specular_color.x;
      mat.specularColor[1] = src.specular_color.y;
      mat.specularColor[2] = src.specular_color.z;
      mat.specularColorTex = src.specular_tex_id;
      mat.ior = src.ior;
      mat.useSpecularWorkflow = src.use_specular_workflow != 0;
      mat.hasOpenPBRSurface = src.has_openpbr;
      mat.hasLightRtOpenPBR = src.has_openpbr;
      mat.lightRtOpenPBR = src.openpbr;
      mat.materialXNodeGraphJson = src.materialx_graph_json;
      const auto set_sample = [&](int texture, int channel,
                                  const ScalarTex *scalar,
                                  tusdview::DrawTexSampleCPU *sample) {
        sample->tex = texture;
        sample->channel = channel;
        sample->uv.m00 = src.uv_xform.rc * src.uv_xform.sx;
        sample->uv.m01 = -src.uv_xform.rs * src.uv_xform.sy;
        sample->uv.m10 = src.uv_xform.rs * src.uv_xform.sx;
        sample->uv.m11 = src.uv_xform.rc * src.uv_xform.sy;
        sample->uv.tx = src.uv_xform.tx;
        sample->uv.ty = src.uv_xform.ty;
        if (scalar) {
          sample->scale[0] = scalar->scale;
          sample->bias[0] = scalar->bias;
        }
      };
      set_sample(src.tex_id, -1, nullptr, &mat.baseColorSample);
      set_sample(src.normal_tex_id, -1, nullptr, &mat.normalSample);
      set_sample(src.coat_normal_tex_id, -1, nullptr, &mat.coatNormalSample);
      set_sample(src.displacement_tex.id, src.displacement_tex.ch,
                 &src.displacement_tex, &mat.displacementSample);
      set_sample(src.rough_tex.id, src.rough_tex.ch, &src.rough_tex,
                 &mat.roughnessSample);
      set_sample(src.metal_tex.id, src.metal_tex.ch, &src.metal_tex,
                 &mat.metallicSample);
      set_sample(src.emission_tex_id, -1, nullptr, &mat.emissiveSample);
      set_sample(src.occ_tex.id, src.occ_tex.ch, &src.occ_tex,
                 &mat.occlusionSample);
      set_sample(src.opacity_tex.id, src.opacity_tex.ch, &src.opacity_tex,
                 &mat.opacitySample);
      set_sample(src.clearcoat_tex.id, src.clearcoat_tex.ch,
                 &src.clearcoat_tex, &mat.coatWeightSample);
      set_sample(src.clearcoat_rough_tex.id, src.clearcoat_rough_tex.ch,
                 &src.clearcoat_rough_tex, &mat.coatRoughnessSample);
      mat.roughnessChannel = src.rough_tex.ch;
      mat.metallicChannel = src.metal_tex.ch;
      mat.occlusionChannel = src.occ_tex.ch;
      mat.opacityChannel = src.opacity_tex.ch;
      mat.roughnessTexScale = src.rough_tex.scale;
      mat.roughnessTexBias = src.rough_tex.bias;
      mat.metallicTexScale = src.metal_tex.scale;
      mat.metallicTexBias = src.metal_tex.bias;
      mat.occlusionTexScale = src.occ_tex.scale;
      mat.occlusionTexBias = src.occ_tex.bias;
      mat.opacityTexScale = src.opacity_tex.scale;
      mat.opacityTexBias = src.opacity_tex.bias;
      if (!mat.materialXNodeGraphJson.empty()) {
        std::string graph_error;
        if (!tusdview::CompileMaterialXGraphRuntime(&mat, &graph_error)) {
          std::cerr << "MaterialX graph fallback for material " << mi << ": "
                    << graph_error << "\n";
        }
      }
    }
    scene->materials.push_back(std::move(mat));

    tusdview::DrawMeshCPU mesh;
    mesh.name = "tusdrender_mesh_" + std::to_string(mi);
    SetIdentity(mesh.world);
    // MeshGeometry currently does not retain the authored doubleSided bit.
    // Match the viewer's streamed/proxy scene builder and keep both windings
    // traceable until that bit becomes part of the streaming contract.
    mesh.doubleSided = true;
    mesh.vertices.resize(nv);
    for (size_t vi = 0; vi < nv; ++vi) {
      tusdview::DrawVertex &v = mesh.vertices[vi];
      v.px = src.positions[vi * 3u + 0u];
      v.py = src.positions[vi * 3u + 1u];
      v.pz = src.positions[vi * 3u + 2u];
      if (src.normals.size() == src.positions.size()) {
        v.nx = src.normals[vi * 3u + 0u];
        v.ny = src.normals[vi * 3u + 1u];
        v.nz = src.normals[vi * 3u + 2u];
      } else {
        v.nx = 0.0f;
        v.ny = 1.0f;
        v.nz = 0.0f;
        mesh.geometricNormal = true;
      }
      if (src.uvs.size() == nv * 2u) {
        v.u = src.uvs[vi * 2u + 0u];
        v.v = src.uvs[vi * 2u + 1u];
      } else {
        v.u = v.v = 0.0f;
      }
      lo[0] = std::min(lo[0], v.px);
      lo[1] = std::min(lo[1], v.py);
      lo[2] = std::min(lo[2], v.pz);
      hi[0] = std::max(hi[0], v.px);
      hi[1] = std::max(hi[1], v.py);
      hi[2] = std::max(hi[2], v.pz);
    }
    mesh.indices = src.indices;
    tusdview::DrawSubmesh sub;
    sub.indexCount = static_cast<uint32_t>(mesh.indices.size());
    sub.materialId = static_cast<int>(mi);
    mesh.submeshes.push_back(sub);
    const size_t first_tri = scene->triangleCount;
    if (mi < materials.size() && materials[mi].area_light) {
      constexpr size_t kMaxGeometryLights = 1024;
      for (size_t ti = 0; ti + 2u < src.indices.size() &&
                          scene->lights.size() < kMaxGeometryLights;
           ti += 3u) {
        const uint32_t ia = src.indices[ti], ib = src.indices[ti + 1u],
                       ic = src.indices[ti + 2u];
        if (size_t(std::max({ia, ib, ic})) >= mesh.vertices.size()) continue;
        const tusdview::DrawVertex &a = mesh.vertices[ia], &b = mesh.vertices[ib],
                                   &c = mesh.vertices[ic];
        const Vec3 pa{a.px, a.py, a.pz}, pb{b.px, b.py, b.pz},
                   pc{c.px, c.py, c.pz};
        const Vec3 cross_e = Cross(Sub(pb, pa), Sub(pc, pa));
        const float area = 0.5f * Length(cross_e);
        if (!(area > 1.0e-10f)) continue;
        const Vec3 n = Normalize(cross_e);
        tusdview::DrawLightCPU light;
        light.name = "tusdrender_mesh_triangle_" + std::to_string(first_tri + ti / 3u);
        light.type = tusdview::DrawLightCPU::Type::Geometry;
        light.position[0] = (pa.x + pb.x + pc.x) / 3.0f;
        light.position[1] = (pa.y + pb.y + pc.y) / 3.0f;
        light.position[2] = (pa.z + pb.z + pc.z) / 3.0f;
        light.direction[0] = -n.x; light.direction[1] = -n.y;
        light.direction[2] = -n.z;
        light.area = area; light.invArea = 1.0f / area;
        light.geometryTriOffset = static_cast<int>(first_tri + ti / 3u);
        light.geometryTriCount = 1;
        const Vec3 e = materials[mi].emission;
        for (int lane = 0; lane < 3; ++lane) {
          const float value = lane == 0 ? e.x : (lane == 1 ? e.y : e.z);
          light.color[lane] = value;
          light.effectiveColor[lane] = value;
          light.normalizedColor[lane] = value;
        }
        scene->lights.push_back(std::move(light));
      }
    }
    scene->triangleCount += mesh.indices.size() / 3u;
    scene->vertexCount += mesh.vertices.size();
    scene->meshes.push_back(std::move(mesh));
  }
  if (scene->meshes.empty()) return false;
  if (scene->lights.empty()) {
    PreviewLight preview;
    preview.kind = PreviewLight::Kind::Distant;
    preview.direction = Vec3{-0.5f, -0.8f, -0.6f};
    append_light(preview);
  }
  std::copy(lo, lo + 3, scene->aabbMin);
  std::copy(hi, hi + 3, scene->aabbMax);
  scene->hasBounds = true;
  for (tusdview::DrawMaterialCPU &material : scene->materials) {
    if (!material.materialXNodeGraphJson.empty()) {
      tusdview::BakeMaterialXGraphTextures(&material, scene);
    }
  }
  return true;
}

}  // namespace

bool BuildSharedDrawScene(
    const std::vector<Vec3> &base_colors,
    const std::vector<RTPreviewStats::MeshGeometry> &geos,
    const std::vector<ResolvedMat> &materials,
    const std::vector<Texture> &textures, const LightCache &lights,
    const std::string &asset_path, tusdview::DrawScene *scene) {
  return MakeDrawScene(base_colors, geos, materials, textures, lights,
                       asset_path, scene);
}

template <class Tracer>
bool RunSharedRT(const char *backend, const Options &opt,
                 const std::vector<Vec3> &base_colors,
                 std::vector<RTPreviewStats::MeshGeometry> &geos,
                 const std::vector<ResolvedMat> &materials,
                 const std::vector<Texture> &textures,
                 const LightCache &lights,
                 const CameraFrame &camera, int height) {
  tusdview::DrawScene scene;
  if (!BuildSharedDrawScene(base_colors, geos, materials, textures, lights,
                            opt.input, &scene)) {
    std::cerr << backend << " RT scene conversion failed.\n";
    return false;
  }
  Tracer tracer;
  std::string err;
  if (!tracer.init(&err)) {
    std::cerr << backend << " RT unavailable: " << err << "\n";
    return false;
  }
  if (!tracer.build(scene, scene.triangleCount, 0, &err,
                    opt.displace ? opt.displace_scale : 0.0f)) {
    std::cerr << backend << " RT scene build failed: " << err << "\n";
    return false;
  }

  const int w = opt.width > 0 ? opt.width : 960;
  const int h = height > 0 ? height : 540;
  const light3d::Vec3 eye{camera.origin.x, camera.origin.y, camera.origin.z};
  const light3d::Vec3 fwd{camera.forward.x, camera.forward.y, camera.forward.z};
  const light3d::Vec3 up{camera.up.x, camera.up.y, camera.up.z};
  const light3d::Mat4 view = light3d::lookAt(eye, eye + fwd, up);
  const float znear = std::max(camera.znear, 1.0e-5f);
  const float zfar = std::max(camera.zfar, znear + 1.0f);
  const light3d::Mat4 proj = light3d::perspectiveZeroOne(
      camera.yfov, static_cast<float>(w) / static_cast<float>(h), znear,
      zfar);
  const light3d::Mat4 vp = proj * view;
  const light3d::Mat4 inv = vp.inverse();
  const float cam[3] = {camera.origin.x, camera.origin.y, camera.origin.z};
  const float light[3] = {0.5f, 0.8f, 0.6f};
  const float clear[3] = {opt.bg.x, opt.bg.y, opt.bg.z};
  const float scene_min[3] = {scene.aabbMin[0], scene.aabbMin[1], scene.aabbMin[2]};
  const float scene_extent[3] = {
      std::max(scene.aabbMax[0] - scene.aabbMin[0], 1.0e-6f),
      std::max(scene.aabbMax[1] - scene.aabbMin[1], 1.0e-6f),
      std::max(scene.aabbMax[2] - scene.aabbMin[2], 1.0e-6f)};
  if (opt.stats) {
    size_t graph_count = 0;
    size_t graph_nodes = 0;
    for (const tusdview::DrawMaterialCPU &material : scene.materials) {
      if (!material.materialXGraph.valid) continue;
      ++graph_count;
      graph_nodes += material.materialXGraph.nodes.size();
    }
    std::cerr << backend << " RT shared scene: materials="
              << scene.materials.size() << " textures=" << scene.textures.size()
              << " lights=" << scene.lights.size() << " graphs=" << graph_count
              << " graph_nodes=" << graph_nodes << "\n";
    std::cerr << backend << " RT camera origin=" << cam[0] << "," << cam[1]
              << "," << cam[2] << " forward=" << camera.forward.x << ","
              << camera.forward.y << "," << camera.forward.z
              << " up=" << camera.up.x << "," << camera.up.y << ","
              << camera.up.z << " clip=" << znear << "," << zfar
              << " bounds=["
              << scene.aabbMin[0] << "," << scene.aabbMin[1] << ","
              << scene.aabbMin[2] << "]-[" << scene.aabbMax[0] << ","
              << scene.aabbMax[1] << "," << scene.aabbMax[2] << "]\n";
  }
  tusdview::PathTraceSettings path_trace =
      opt.path_trace_quality == Options::PathTraceQuality::Final
          ? tusdview::PathTraceSettings::Final()
          : tusdview::PathTraceSettings::Interactive();
  path_trace.enabled = opt.path_trace;
  if (opt.path_trace_samples > 0) {
    path_trace.targetSamples = opt.path_trace_samples;
  }
  path_trace.maxDepth = opt.path_trace_max_depth;
  path_trace.russianRouletteDepth = opt.path_trace_rr_depth;
  path_trace.seed = opt.path_trace_seed;
  path_trace.maxSubsurfaceEvents = opt.path_trace_max_subsurface_events;
  path_trace.maxVolumeEvents = opt.path_trace_max_volume_events;
  path_trace.varianceThreshold = opt.path_trace_variance;
  path_trace.sanitize();
  const int spp = path_trace.enabled && path_trace.targetSamples > 0
                      ? static_cast<int>(path_trace.targetSamples)
                      : std::max(1, opt.samples);
  std::vector<uint8_t> rgba;
  std::vector<float> linear_rgba;
  uint32_t rendered_samples = 0;
  if (!tracer.trace(inv.m, vp.m, cam, light, clear, 1.0f, 0, 1.0f,
                    scene_min, scene_extent, w, h, &rgba, &err,
                    spp, nullptr, path_trace.enabled ? &path_trace : nullptr,
                    path_trace.enabled ? &linear_rgba : nullptr,
                    path_trace.enabled ? &rendered_samples : nullptr)) {
    std::cerr << backend << " RT trace failed: " << err << "\n";
    return false;
  }
  if (path_trace.enabled) {
    rgba = ToneMapLinear(linear_rgba, w, h, 1.0f);
  }
  tinyusdz::Image image;
  image.width = w;
  image.height = h;
  image.channels = 4;
  image.bpp = 8;
  image.data = std::move(rgba);
  tinyusdz::image::WriteOption write_opt;
  write_opt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto written = tinyusdz::image::WriteImageToFile(opt.output, image, write_opt);
  if (!written) {
    std::cerr << "Failed to write " << backend << " RT image: "
              << written.error() << "\n";
    return false;
  }
  std::cerr << "backend: shared " << backend << " RT (" << tracer.deviceName()
            << ")";
  if (path_trace.enabled) std::cerr << ", path samples=" << rendered_samples;
  std::cerr << "\n";
  return true;
}

#ifdef HAVE_CUDA_RT
bool RunCudaSharedRT(const Options &opt,
                     const std::vector<Vec3> &base_colors,
                     std::vector<RTPreviewStats::MeshGeometry> &geos,
                     const std::vector<ResolvedMat> &materials,
                     const std::vector<Texture> &textures,
                     const LightCache &lights,
                     const CameraFrame &camera, int height) {
  return RunSharedRT<tusdview::CudaRayTracer>(
      "CUDA", opt, base_colors, geos, materials, textures, lights, camera,
      height);
}
#endif

#ifdef HAVE_HIP
bool RunHipSharedRT(const Options &opt,
                    const std::vector<Vec3> &base_colors,
                    std::vector<RTPreviewStats::MeshGeometry> &geos,
                    const std::vector<ResolvedMat> &materials,
                    const std::vector<Texture> &textures,
                    const LightCache &lights,
                    const CameraFrame &camera, int height) {
  return RunSharedRT<tusdview::HipRayTracer>(
      "HIP", opt, base_colors, geos, materials, textures, lights, camera,
      height);
}
#endif

}  // namespace tusdr
#endif  // HAVE_CUDA_RT || HAVE_HIP
