// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
#include <emscripten/bind.h>
#include <emscripten/console.h>
#include <emscripten/em_js.h>
#include <emscripten/fetch.h>

#include <string>
#include <vector>

#include "external/fast_float/include/fast_float/bigint.h"
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"

void downloadSucceeded(emscripten_fetch_t *fetch) {
  printf("Finished downloading %llu bytes from URL %s.\n", fetch->numBytes, fetch->url);
  // The data is now available at fetch->data[0] through fetch->data[fetch->numBytes-1];
  emscripten_fetch_close(fetch); // Free data associated with the fetch.
}

void downloadFailed(emscripten_fetch_t *fetch) {
  printf("Downloading %s failed, HTTP failure status code: %d.\n", fetch->url, fetch->status);
  emscripten_fetch_close(fetch); // Also free data on failure.
}

void downloadProgress(emscripten_fetch_t *fetch) {
  if (fetch->totalBytes) {
    printf("Downloading %s.. %.2f%% complete.\n", fetch->url, fetch->dataOffset * 100.0 / fetch->totalBytes);
  } else {
    printf("Downloading %s.. %lld bytes complete.\n", fetch->url, fetch->dataOffset + fetch->numBytes);
  }
}

using namespace emscripten;

namespace detail {

std::array<double, 9> toArray(const tinyusdz::value::matrix3d &m) {
  std::array<double, 9> ret;

  ret[0] = m.m[0][0];
  ret[1] = m.m[0][1];
  ret[2] = m.m[0][2];

  ret[3] = m.m[1][0];
  ret[4] = m.m[1][1];
  ret[5] = m.m[1][2];

  ret[6] = m.m[2][0];
  ret[7] = m.m[2][1];
  ret[8] = m.m[2][2];

  return ret;
}

std::array<double, 16> toArray(const tinyusdz::value::matrix4d &m) {
  std::array<double, 16> ret;

  ret[0] = m.m[0][0];
  ret[1] = m.m[0][1];
  ret[2] = m.m[0][2];
  ret[3] = m.m[0][3];

  ret[4] = m.m[1][0];
  ret[5] = m.m[1][1];
  ret[6] = m.m[1][2];
  ret[7] = m.m[1][3];

  ret[8] = m.m[2][0];
  ret[9] = m.m[2][1];
  ret[10] = m.m[2][2];
  ret[11] = m.m[2][3];

  ret[12] = m.m[3][0];
  ret[13] = m.m[3][1];
  ret[14] = m.m[3][2];
  ret[15] = m.m[3][3];

  return ret;
}

// To RGBA
bool ToRGBA(const std::vector<uint8_t> &src, int channels,
            std::vector<uint8_t> &dst) {
  uint32_t npixels = src.size() / channels;
  dst.resize(npixels * 4);

  if (channels == 1) {  // grayscale
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[i];
      dst[4 * i + 1] = src[i];
      dst[4 * i + 2] = src[i];
      dst[4 * i + 3] = 1.0f;
    }
  } else if (channels == 2) {  // assume luminance + alpha
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[2 * i + 0];
      dst[4 * i + 1] = src[2 * i + 0];
      dst[4 * i + 2] = src[2 * i + 0];
      dst[4 * i + 3] = src[2 * i + 1];
    }
  } else if (channels == 3) {
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[3 * i + 0];
      dst[4 * i + 1] = src[3 * i + 1];
      dst[4 * i + 2] = src[3 * i + 2];
      dst[4 * i + 3] = 1.0f;
    }
  } else if (channels == 4) {
    dst = src;
  } else {
    return false;
  }

  return true;
}

}  // namespace detail

#if defined(TINYUSDZ_WASM_ASYNCIFY)
// forward decl.
class TinyUSDZLoaderNative;

// Async loading function using EM_ASYNC_JS
EM_ASYNC_JS(bool, loadAsyncImpl,
            (TinyUSDZLoaderNative * loader, const char *binary_data,
             int binary_size),
            {
              // Convert C string to JavaScript string
              const binary = UTF8ToString(binary_data, binary_size);

              // Call the synchronous loadFromBinary method
              const success =
                  Module.TinyUSDZLoaderNative.prototype.loadFromBinary.call(
                      loader, binary);

              if (!success) {
                const errorMsg =
                    Module.TinyUSDZLoaderNative.prototype.error.call(loader);
                throw new Error(errorMsg || 'Unknown error loading USD');
              }

              return success;
            });
#endif


// Use Emscripten's FETCH API
struct FetchAssetResolutionResolver {
  static int Resolve(const char *asset_name,
                     const std::vector<std::string> &search_paths,
                     std::string *resolved_asset_name, std::string *err,
                     void *userdata) {
    (void)err;
    (void)userdata;
    (void)search_paths;

    if (!asset_name) {
      return -2;  // err
    }

    if (!resolved_asset_name) {
      return -2;  // err
    }

#if 0  // TODO
    if (g_map.count(asset_name)) {
      (*resolved_asset_name) = asset_name;
      return 0;  // OK
    }
#endif

    return -1;  // failed to resolve.
  }

  // AssetResoltion handlers
  static int Size(const char *asset_name, uint64_t *nbytes, std::string *err,
                  void *userdata) {
    (void)userdata;

    if (!asset_name) {
      if (err) {
        (*err) += "asset_name arg is nullptr.\n";
      }
      return -1;
    }

    if (!nbytes) {
      if (err) {
        (*err) += "nbytes arg is nullptr.\n";
      }
      return -1;
    }

#if 0  // TODO
    if (g_map.count(asset_name)) {
      // Use strlen()(length until the first appearance of '\0' character), since
      // std::string::size() reports buffer size, not bytes until the first
      // appearance of '\0' character.
      (*nbytes) = strlen(g_map[asset_name].c_str());
      return 0;  // OK
    }
#endif

    return -1;
  }

  static int Read(const char *asset_name, uint64_t req_nbytes, uint8_t *out_buf,
                  uint64_t *nbytes, std::string *err, void *userdata) {
    if (!asset_name) {
      if (err) {
        (*err) += "asset_name arg is nullptr.\n";
      }
      return -3;
    }

    if (!nbytes) {
      if (err) {
        (*err) += "nbytes arg is nullptr.\n";
      }
      return -3;
    }

    if (req_nbytes < 9) {  // at least 9 bytes(strlen("#usda 1.0")) or more
      return -2;
    }

#if 0  // TODO
    if (g_map.count(asset_name)) {
      size_t sz = strlen(g_map[asset_name].c_str());
      if (sz > req_nbytes) {
        if (err) {
          (*err) += "Insufficient dst buffer size.\n";
        }
        return -4;
      }

      std::cout << "read asset: " << asset_name << "\n";

      memcpy(out_buf, &g_map[asset_name][0], sz);

      (*nbytes) = sz;
      return 0;
    }
#endif

    //
    return -1;
  }
};

bool SetupFetchAssetResolution(
    tinyusdz::AssetResolutionResolver &resolver,
    /* must be the persistent pointer address until usd load finishes */
    const FetchAssetResolutionResolver *p) {
  if (!p) {
    return false;
  }

  tinyusdz::AssetResolutionHandler handler;
  handler.resolve_fun = FetchAssetResolutionResolver::Resolve;
  handler.size_fun = FetchAssetResolutionResolver::Size;
  handler.read_fun = FetchAssetResolutionResolver::Read;
  handler.write_fun = nullptr;
  handler.userdata =
      reinterpret_cast<void *>(const_cast<FetchAssetResolutionResolver *>(p));

  resolver.register_wildcard_asset_resolution_handler(handler);

  return true;
}

///
/// Simple C++ wrapper class for Emscripten
///
/// TODO: Provide Three.js GLTFLoader like interface.
///
class TinyUSDZLoaderNative {
 public:
  struct CompositionFeatures {
    bool subLayers{true};
    bool inherits{true};
    bool variantSets{true};
    bool references{true};
    bool payload{true};  // Not 'payloads'
    bool specializes{true};
  };

  // Default constructor for async loading
  TinyUSDZLoaderNative() : loaded_(false) {}

#if 0
  ///
  /// `binary` is the buffer for TinyUSDZ binary(e.g. buffer read by
  /// fs.readFileSync) std::string can be used as UInt8Array in JS layer.
  ///
  TinyUSDZLoaderNative(const std::string &binary) {
    loadFromBinary(binary);
  }
#endif

  bool loadFromBinary(const std::string &binary, const std::string &filename) {
    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());

    tinyusdz::Stage stage;
    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &stage, &warn_, &error_);

    if (!loaded_) {
      return false;
    }


    tinyusdz::tydra::RenderSceneConverterEnv env(stage);

    //
    // false = Load Texture in JS Layer
    //

    env.scene_config.load_texture_assets = loadTextureOnLoad_;

    env.material_config.preserve_texel_bitdepth = true;

    if (is_usdz) {
      // TODO: Support USDZ + Composition
      // Setup AssetResolutionResolver to read a asset(file) from memory.
      bool asset_on_memory =
          false;  // duplicate asset data from USDZ(binary) to UDSZAsset struct.

      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
              reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
              asset_on_memory, &usdz_asset_, &warn_, &error_)) {
        std::cerr << "Failed to read USDZ assetInfo. \n";
        loaded_ = false;
        return false;
      }

      tinyusdz::AssetResolutionResolver arr;

      // NOTE: Pointer address of usdz_asset must be valid until the call of
      // RenderSceneConverter::ConvertToRenderScene.
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset_)) {
        std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
        loaded_ = false;
        return false;
      }

      env.asset_resolver = arr;
    } else {
      tinyusdz::AssetResolutionResolver arr;
      if (!SetupFetchAssetResolution(arr, &http_resolver_)) {
        std::cerr << "Failed to setup FetchAssetResolution\n";
        loaded_ = false;
        return false;
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
      error_ = converter.GetError();
      return false;
    }

    filename_ = filename;

    return true;
  }
  ~TinyUSDZLoaderNative() {}

#if defined(TINYUSDZ_WASM_ASYNCIFY)
  emscripten::val loadAsync(const std::string &binary) {
    return emscripten::val::global("Promise").new_(
        emscripten::val::module_property("asyncifyWrapper"),
        emscripten::val([this, binary](emscripten::val resolve,
                                       emscripten::val reject) {
          try {
            bool success = loadAsyncImpl(this, binary.c_str(), binary.size());
            if (success) {
              resolve(emscripten::val(this));
            } else {
              reject(emscripten::val(this->error()));
            }
          } catch (const std::exception &e) {
            reject(emscripten::val(std::string("Exception: ") + e.what()));
          } catch (...) {
            reject(emscripten::val("Unknown exception during loading"));
          }
        }));
  }
#endif

  emscripten::val loadAsLayer(const std::string &binary) {
    tinyusdz::Layer layer;

    // TODO:
    return emscripten::val::null();
  }

  int numMeshes() const { return render_scene_.meshes.size(); }

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
      mat.set("emissiveColorTextureId",
              m.surfaceShader.emissiveColor.texture_id);
    }
    mat.set("useSpecularWorkflow", m.surfaceShader.useSpecularWorkflow);
    if (m.surfaceShader.useSpecularWorkflow) {
      mat.set("specularColor", m.surfaceShader.specularColor.value);
      if (m.surfaceShader.specularColor.is_texture()) {
        mat.set("specularColorTextureId",
                m.surfaceShader.specularColor.texture_id);
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
      mat.set("clearcoatRoughnessTextureId",
              m.surfaceShader.clearcoatRoughness.texture_id);
    }

    mat.set("opacity", m.surfaceShader.opacity.value);
    if (m.surfaceShader.opacity.is_texture()) {
      mat.set("opacityTextureId", m.surfaceShader.opacity.texture_id);
    }

    // TODO
    // mat.set("opacityMode", m.surfaceShader.opacityMode);

    mat.set("opacityThreshold", m.surfaceShader.opacityThreshold.value);
    if (m.surfaceShader.opacityThreshold.is_texture()) {
      mat.set("opacityThresholdTextureId",
              m.surfaceShader.opacityThreshold.texture_id);
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
    tex.set("wrapS", to_string(t.wrapS));
    tex.set("wrapT", to_string(t.wrapT));
    //  TOOD: bias, scale, rot/scale/trans, etc

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

    img.set("width", int(i.width));
    img.set("height", int(i.height));
    img.set("channels", int(i.channels));
    img.set("uri", i.asset_identifier);
    img.set("decoded", bool(i.decoded));
    img.set("colorSpace", to_string(i.colorSpace));
    img.set("usdColorSpace", to_string(i.usdColorSpace));
    img.set("bufferId", int(i.buffer_id));

    if ((i.buffer_id >= 0) && (i.buffer_id < render_scene_.buffers.size())) {
      const auto &b = render_scene_.buffers[i.buffer_id];

      // TODO: Support HDR

      img.set("data",
              emscripten::typed_memory_view(b.data.size(), b.data.data()));
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

    const tinyusdz::tydra::RenderMesh &rmesh =
        render_scene_.meshes[size_t(mesh_id)];

    // TODO: Use three.js scene description format?
    mesh.set("primName", rmesh.prim_name);
    mesh.set("displayName", rmesh.display_name);
    mesh.set("absPath", rmesh.abs_path);
    const uint32_t *indices_ptr = rmesh.faceVertexIndices().data();
    mesh.set("faceVertexIndices",
             emscripten::typed_memory_view(rmesh.faceVertexIndices().size(),
                                           indices_ptr));
    const uint32_t *counts_ptr = rmesh.faceVertexCounts().data();
    mesh.set("faceVertexCounts",
             emscripten::typed_memory_view(rmesh.faceVertexCounts().size(),
                                           counts_ptr));
    const float *points_ptr =
        reinterpret_cast<const float *>(rmesh.points.data());
    // vec3
    mesh.set("points", emscripten::typed_memory_view(rmesh.points.size() * 3,
                                                     points_ptr));

    if (!rmesh.normals.empty()) {
      const float *normals_ptr =
          reinterpret_cast<const float *>(rmesh.normals.data.data());

      mesh.set("normals", emscripten::typed_memory_view(
                              rmesh.normals.vertex_count() * 3, normals_ptr));
    }

    {
      // slot 0 hardcoded.
      uint32_t uvSlotId = 0;
      if (rmesh.texcoords.count(uvSlotId)) {
        const float *uvs_ptr = reinterpret_cast<const float *>(
            rmesh.texcoords.at(uvSlotId).data.data());

        // assume vec2
        mesh.set("texcoords",
                 emscripten::typed_memory_view(
                     rmesh.texcoords.at(uvSlotId).vertex_count() * 2, uvs_ptr));
      }
    }

    mesh.set("materialId", rmesh.material_id);

    return mesh;
  }

  int getDefaultRootNodeId() { return render_scene_.default_root_node; }

  emscripten::val getDefaultRootNode() {
    return getRootNode(getDefaultRootNodeId());
  }

  emscripten::val getRootNode(int idx) {
    emscripten::val val = emscripten::val::object();

    if ((idx < 0) || (idx >= render_scene_.nodes.size())) {
      return val;
    }

    val = buildNodeRec(render_scene_.nodes[size_t(idx)]);
    return val;
  }

  int numRootNodes() { return render_scene_.nodes.size(); }

  void setEnableComposition(bool enabled) { enableComposition_ = enabled; }
  void setLoadTextureOnLoad(bool onoff) {
    loadTextureOnLoad_ = onoff;
  }

  // Return filename passed to loadFromBinary.
  std::string getURI() const {
    return filename_;
  }

  // TODO: Deprecate
  bool ok() const { return loaded_; }

  const std::string &error() const { return error_; }

 private:
  // Simple glTF-like Node
  emscripten::val buildNodeRec(const tinyusdz::tydra::Node &rnode) {
    emscripten::val node = emscripten::val::object();

    node.set("primName", rnode.prim_name);
    node.set("displayName", rnode.display_name);
    node.set("absPath", rnode.abs_path);

    std::string nodeTypeStr = to_string(rnode.nodeType);
    node.set("nodeType", nodeTypeStr);

    node.set("contentId",
             rnode.id);  // e.g. index to Mesh if nodeType == 'mesh'

    std::array<double, 16> localMatrix = detail::toArray(rnode.local_matrix);
    std::array<double, 16> globalMatrix = detail::toArray(rnode.global_matrix);

    node.set("localMatrix", localMatrix);
    node.set("globalMatrix", globalMatrix);
    node.set("hasResetXform", rnode.has_resetXform);

    emscripten::val children = emscripten::val::array();

    for (const tinyusdz::tydra::Node &child : rnode.children) {
      emscripten::val child_val = buildNodeRec(child);

      children.call<void>("push", child_val);
    }

    node.set("children", children);

    return node;
  }

  bool loaded_{false};
  bool enableComposition_{false};
  bool loadTextureOnLoad_{false};

  std::string filename_;
  std::string warn_;
  std::string error_;

  tinyusdz::tydra::RenderScene render_scene_;
  tinyusdz::USDZAsset usdz_asset_;
  FetchAssetResolutionResolver http_resolver_;
};

///
/// USD composition
///
class TinyUSDZComposerNative {
 public:
  // Default constructor for async loading
  TinyUSDZComposerNative() : loaded_(false) {}

  bool loaded() const { return loaded_; }
  const std::string &error() const { return error_; }

 private:
  bool loaded_{false};
  std::string warn_;
  std::string error_;

  tinyusdz::Layer root_layer_;
};

#if 0
// Helper to register std::array
namespace emscripten {
    namespace internal {
        template<typename T, size_t N>
        struct TypeID<std::array<T, N>> {
            static constexpr TYPEID get() {
                return TypeID<val>::get();
            }
        };
    }
}

// Convert std::array<float, 3> to/from JavaScript array
namespace emscripten {
    namespace internal {
        template<>
        struct BindingType<std::array<float, 3>> {
            typedef std::array<float, 3> WireType;
            static WireType toWireType(const std::array<float, 3>& arr) {
                return arr;
            }
            static std::array<float, 3> fromWireType(const WireType& arr) {
                return arr;
            }
        };
    }
}
#endif

// TODO: quaternion type.

// Register STL
EMSCRIPTEN_BINDINGS(stl_wrappters) {
  register_vector<float>("VectorFloat");
  register_vector<int16_t>("VectorInt16");
  register_vector<uint16_t>("VectorUInt16");
  register_vector<uint32_t>("VectorUInt");
  register_vector<int>("VectorInt");
  register_vector<std::string>("VectorStrin");
}

// Register the array type
EMSCRIPTEN_BINDINGS(array_bindings) {
  value_array<std::array<int16_t, 2>>("Short2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<int16_t, 3>>("Short3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<int16_t, 4>>("Short4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<uint16_t, 2>>("UShort2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<uint16_t, 3>>("UShort3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<uint16_t, 4>>("UShort4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<int, 2>>("Int2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<int, 3>>("Int3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<int, 4>>("Int4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<uint32_t, 2>>("UInt2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<uint32_t, 3>>("UInt3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<uint32_t, 4>>("UInt4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<float, 2>>("Float2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<float, 3>>("Float3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<float, 4>>("Float4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  //  for mat33
  value_array<std::array<float, 9>>("Mat33")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>());

  value_array<std::array<double, 9>>("DMat33")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>());

  //  for mat44
  value_array<std::array<float, 16>>("Mat44")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>())
      .element(emscripten::index<9>())
      .element(emscripten::index<10>())
      .element(emscripten::index<11>())
      .element(emscripten::index<12>())
      .element(emscripten::index<13>())
      .element(emscripten::index<14>())
      .element(emscripten::index<15>());

  value_array<std::array<double, 16>>("DMat44")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>())
      .element(emscripten::index<9>())
      .element(emscripten::index<10>())
      .element(emscripten::index<11>())
      .element(emscripten::index<12>())
      .element(emscripten::index<13>())
      .element(emscripten::index<14>())
      .element(emscripten::index<15>());
}

EMSCRIPTEN_BINDINGS(tinyusdz_module) {
  class_<TinyUSDZLoaderNative>("TinyUSDZLoaderNative")
      .constructor<>()  // Default constructor for async loading
  //.constructor<const std::string &>()  // Keep original for compatibility
#if defined(TINYUSDZ_WASM_ASYNCIFY)
      .function("loadAsync", &TinyUSDZLoaderNative::loadAsync)
#endif
      .function("loadFromBinary", &TinyUSDZLoaderNative::loadFromBinary)
      .function("getURI", &TinyUSDZLoaderNative::getURI)
      .function("getMesh", &TinyUSDZLoaderNative::getMesh)
      .function("numMeshes", &TinyUSDZLoaderNative::numMeshes)
      .function("getMaterial", &TinyUSDZLoaderNative::getMaterial)
      .function("getTexture", &TinyUSDZLoaderNative::getTexture)
      .function("getImage", &TinyUSDZLoaderNative::getImage)
      .function("getDefaultRootNodeId",
                &TinyUSDZLoaderNative::getDefaultRootNodeId)
      .function("getRootNode", &TinyUSDZLoaderNative::getRootNode)
      .function("getDefaultRootNode", &TinyUSDZLoaderNative::getDefaultRootNode)
      .function("numRootNodes", &TinyUSDZLoaderNative::numRootNodes)
      .function("setEnableComposition",
                &TinyUSDZLoaderNative::setEnableComposition)
      .function("setLoadTextureOnLoad",
                &TinyUSDZLoaderNative::setLoadTextureOnLoad)
      .function("ok", &TinyUSDZLoaderNative::ok)
      .function("error", &TinyUSDZLoaderNative::error);

  class_<TinyUSDZComposerNative>("TinyUSDZComposerNative")
      .constructor<>()  // Default constructor for async loading
      .function("ok", &TinyUSDZComposerNative::loaded)
      .function("error", &TinyUSDZComposerNative::error);
}

