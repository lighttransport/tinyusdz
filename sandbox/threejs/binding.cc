#include <emscripten/bind.h>

#include <vector>

#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"

using namespace emscripten;

///
/// Simple C++ wrapper class for Emscripten
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

    // TODO
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

    tex.set("textureImageId", t.texture_image_id);
    tex.set("wrapS", to_string(t.wrapS));
    tex.set("wrapT", to_string(t.wrapS));
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

    if ((i.buffer_id > 0) && (i.buffer_id < render_scene_.buffers.size())) {
      const auto &b = render_scene_.buffers[i.buffer_id];

      // TODO: RGBA
      
      img.set("data", emscripten::typed_memory_view(b.data.size(), b.data.data()));
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
      .function("ok", &TinyUSDZLoader::ok)
      .function("error", &TinyUSDZLoader::error);
}
