// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
#include <emscripten/bind.h>

#include <vector>

#include "external/fast_float/include/fast_float/bigint.h"
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"

using namespace emscripten;

namespace detail {

// To RGBA 
bool ToRGBA(const std::vector<uint8_t> &src, int channels,
  std::vector<uint8_t> &dst) {
  
  uint32_t npixels = src.size() / channels;
  dst.resize(npixels * 4);

  if (channels == 1) { // grayscale
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[i];
      dst[4 * i + 1] = src[i];
      dst[4 * i + 2] = src[i];
      dst[4 * i + 3] = 1.0f;
    }
  } else if (channels == 2) { // assume luminance + alpha
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[2*i+0];
      dst[4 * i + 1] = src[2*i+0];
      dst[4 * i + 2] = src[2*i+0];
      dst[4 * i + 3] = src[2*i+1];
    }
  } else if (channels == 3) {
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[3*i+0];
      dst[4 * i + 1] = src[3*i+1];
      dst[4 * i + 2] = src[3*i+2];
      dst[4 * i + 3] = 1.0f;
    }
  } else if (channels == 4) {
    dst = src;
  } else {
    return false;
  }

  return true;
}

}

///
/// Simple C++ wrapper class for Emscripten
///
/// TODO: Provide Three.js GLTFLoader like interface.
///
class TinyUSDZLoader {
 public:
  ///
  /// `binary` is the buffer for TinyUSDZ binary(e.g. buffer read by
  /// fs.readFileSync) std::string can be used as UInt8Array in JS layer.
  ///
  TinyUSDZLoader(const std::string &binary) {
    tinyusdz::Stage stage;

    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        "dummy.usda", &stage, &warn_, &error_);

    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());

    tinyusdz::tydra::RenderSceneConverterEnv env(stage);

    env.material_config.preserve_texel_bitdepth = true;

    if (is_usdz) {
      // Setup AssetResolutionResolver to read a asset(file) from memory.
      bool asset_on_memory =
          false;  // duplicate asset data from USDZ(binary) to UDSZAsset struct.

      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
              reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
              asset_on_memory, &usdz_asset_, &warn_, &error_)) {
        std::cerr << "Failed to read USDZ assetInfo. \n";
        loaded_ = false;
      }

      tinyusdz::AssetResolutionResolver arr;

      // NOTE: Pointer address of usdz_asset must be valid until the call of
      // RenderSceneConverter::ConvertToRenderScene.
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset_)) {
        std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
        loaded_ = false;
      }

      env.asset_resolver = arr;
    }

    // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
    tinyusdz::tydra::RenderSceneConverter converter;

    // env.timecode = timecode; // TODO
    loaded_ = converter.ConvertToRenderScene(env, &render_scene_);
    if (!loaded_) {
      std::cerr << "Failed to convert USD Stage to RenderScene: \n"
                << converter.GetError() << "\n";
    }
  }
  ~TinyUSDZLoader() {}

  emscripten::val loadAsync(const std::string &binary) {
    tinyusdz::Layer layer;

    // TODO: 
    return emscripten::val::null();
  }


  emscripten::val loadAsLayer(const std::string &binary) {
    tinyusdz::Layer layer;

    // TODO: 
    return emscripten::val::null();
  }

  int numMeshes() const {
    return render_scene_.meshes.size();
  }

  emscripten::val getMaterial(int mat_id) const {

    emscripten::val mat = emscripten::val::object();

    if (!loaded_) {
      return mat;
    }

    if (mat_id >= render_scene_.materials.size()) {
      return mat;
    }

    const auto &m = render_scene_.materials[mat_id];

    // UsdPreviewSurface like shader param
    // [x] diffuseColor : color3f or texture
    // [x] emissiveColor : color3f or texture
    // [x] useSpecularWorkflow : bool
    // * SpecularWorkflow
    //   [x] specularColor : color3f or texture
    // * MetalnessWorkflow
    //   [x] metallic : float or texture
    // [x] roughness : float or texture
    // [x] clearcoat : float or texture
    // [x] clearcoatRoughness : float or texture
    // [x] opacity : float or texture
    // [ ] opacityMode(from 2.6) : transparent or presence
    // [x] opacityThreshold : float or texture
    // [x] ior : float or texture
    // [x] normal : normal3f or texture
    // [x] displacement : float or texture
    // [x] occlusion : float or texture

    mat.set("diffuseColor", m.surfaceShader.diffuseColor.value);
    if (m.surfaceShader.diffuseColor.is_texture()) {
      mat.set("diffuseColorTextureId", m.surfaceShader.diffuseColor.texture_id);
    }

    mat.set("emissiveColor", m.surfaceShader.emissiveColor.value);
    if (m.surfaceShader.emissiveColor.is_texture()) {
      mat.set("emissiveColorTextureId", m.surfaceShader.emissiveColor.texture_id);
    }

    mat.set("useSpecularWorkflow", m.surfaceShader.useSpecularWorkflow);
    if (m.surfaceShader.useSpecularWorkflow) {
      mat.set("specularColor", m.surfaceShader.specularColor.value);
      if (m.surfaceShader.specularColor.is_texture()) {
        mat.set("specularColorTextureId", m.surfaceShader.specularColor.texture_id);
      }

    } else {
      mat.set("metallic", m.surfaceShader.metallic.value);
      if (m.surfaceShader.metallic.is_texture()) {
        mat.set("metallicTextureId", m.surfaceShader.metallic.texture_id);
      }
    }

    mat.set("roughness", m.surfaceShader.roughness.value);
    if (m.surfaceShader.roughness.is_texture()) {
      mat.set("roughnessTextureId", m.surfaceShader.roughness.texture_id);
    }

    mat.set("cleacoat", m.surfaceShader.clearcoat.value);
    if (m.surfaceShader.clearcoat.is_texture()) {
      mat.set("cleacoatTextureId", m.surfaceShader.clearcoat.texture_id);
    }

    mat.set("clearcoatRoughness", m.surfaceShader.clearcoatRoughness.value);
    if (m.surfaceShader.clearcoatRoughness.is_texture()) {
      mat.set("clearcoatRoughnessTextureId", m.surfaceShader.clearcoatRoughness.texture_id);
    }

    mat.set("opacity", m.surfaceShader.opacity.value);
    if (m.surfaceShader.opacity.is_texture()) {
      mat.set("opacityTextureId", m.surfaceShader.opacity.texture_id);
    }

    // TODO
    //mat.set("opacityMode", m.surfaceShader.opacityMode);

    mat.set("opacityThreshold", m.surfaceShader.opacityThreshold.value);
    if (m.surfaceShader.opacityThreshold.is_texture()) {
      mat.set("opacityThresholdTextureId", m.surfaceShader.opacityThreshold.texture_id);
    }

    mat.set("ior", m.surfaceShader.ior.value);
      if (m.surfaceShader.ior.is_texture()) {
        mat.set("iorTextureId", m.surfaceShader.ior.texture_id);
      }

    mat.set("normal", m.surfaceShader.normal.value);
    if (m.surfaceShader.normal.is_texture()) {
      mat.set("normalTextureId", m.surfaceShader.normal.texture_id);
    }

    mat.set("displacement", m.surfaceShader.displacement.value);
    if (m.surfaceShader.displacement.is_texture()) {
      mat.set("displacementTextureId", m.surfaceShader.displacement.texture_id);
    }

    mat.set("occlusion", m.surfaceShader.occlusion.value);
    if (m.surfaceShader.occlusion.is_texture()) {
      mat.set("occlusionTextureId", m.surfaceShader.occlusion.texture_id);
    }

    return mat;
  }

  emscripten::val getTexture(int tex_id) const {

    emscripten::val tex = emscripten::val::object();

    if (!loaded_) {
      return tex;
    }

    if (tex_id >= render_scene_.textures.size()) {
      return tex;
    }

    const auto &t = render_scene_.textures[tex_id];

    tex.set("textureImageId", int(t.texture_image_id));
    //tex.set("wrapS", to_string(t.wrapS));
    //tex.set("wrapT", to_string(t.wrapT));
    // TOOD: bias, scale, rot/scale/trans

    return tex;
  }

  emscripten::val getImage(int img_id) const {

    emscripten::val img = emscripten::val::object();

    if (!loaded_) {
      return img;
    }

    if (img_id >= render_scene_.images.size()) {
      return img;
    }

    const auto &i = render_scene_.images[img_id];

    if ((i.buffer_id >= 0) && (i.buffer_id < render_scene_.buffers.size())) {
      const auto &b = render_scene_.buffers[i.buffer_id];

      // TODO: Support HDR
      
      img.set("data", emscripten::typed_memory_view(b.data.size(), b.data.data()));
      img.set("width", int(i.width));
      img.set("height", int(i.height));
      img.set("channels", int(i.channels));
    }


    return img;
  }

  emscripten::val getMesh(int mesh_id) const {
    emscripten::val mesh = emscripten::val::object();

    if (!loaded_) {
      return mesh;
    }

    if (mesh_id >= render_scene_.meshes.size()) {
      return mesh;
    }

    const tinyusdz::tydra::RenderMesh &rmesh = render_scene_.meshes[size_t(mesh_id)];

    // TODO: Use three.js scene description format?
    mesh.set("prim_name", rmesh.prim_name);
    mesh.set("display_name", rmesh.prim_name);
    mesh.set("abs_path", rmesh.abs_path);
    const uint32_t *indices_ptr = rmesh.faceVertexIndices().data();
    mesh.set("faceVertexIndices", emscripten::typed_memory_view(rmesh.faceVertexIndices().size(), indices_ptr));
    const uint32_t *counts_ptr = rmesh.faceVertexCounts().data();
    mesh.set("faceVertexCounts", emscripten::typed_memory_view(rmesh.faceVertexCounts().size(), counts_ptr));
    const float *points_ptr = reinterpret_cast<const float *>(rmesh.points.data());
    // vec3
    mesh.set("points", emscripten::typed_memory_view(rmesh.points.size() * 3, points_ptr)); 

    {
      // slot 0 hardcoded.
      uint32_t uvSlotId = 0;
      if (rmesh.texcoords.count(uvSlotId)) {
        const float *uvs_ptr = reinterpret_cast<const float *>(rmesh.texcoords.at(uvSlotId).data.data());

        // assume vec2
        mesh.set("texcoords", emscripten::typed_memory_view(rmesh.texcoords.at(uvSlotId).vertex_count() * 2, uvs_ptr)); 
      }
    }

    mesh.set("materialId", rmesh.material_id);


    return mesh;
  }

  bool ok() const { return loaded_; }

  const std::string error() const { return error_; }

 private:
  bool loaded_{false};
  std::string warn_;
  std::string error_;

  tinyusdz::tydra::RenderScene render_scene_;
  tinyusdz::USDZAsset usdz_asset_;
};

///
/// USD composition 
///
class TinyUSDZComposer
{
  // TODO

};

// Register STL
EMSCRIPTEN_BINDINGS(stl_wrappters) {
  register_vector<float>("VectorFloat");
  register_vector<int>("VectorInt");
  register_vector<uint32_t>("VectorUInt");
}

EMSCRIPTEN_BINDINGS(tinyusdz_module) {
  class_<TinyUSDZLoader>("TinyUSDZLoader")
      .constructor<const std::string &>()
      .function("getMesh", &TinyUSDZLoader::getMesh)
      .function("numMeshes", &TinyUSDZLoader::numMeshes)
      .function("getMaterial", &TinyUSDZLoader::getMaterial)
      .function("getTexture", &TinyUSDZLoader::getTexture)
      .function("getImage", &TinyUSDZLoader::getImage)
      .function("ok", &TinyUSDZLoader::ok)
      .function("error", &TinyUSDZLoader::error);
}
