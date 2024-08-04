
#include <atomic>
#include <cassert>
#include <thread>

// tinyusdz & Tydra
#include "io-util.hh"
#include "tydra/render-data.hh"

//
#include "simple-render.hh"

#include "nanort.h"
#include "nanosg.h"

// Loading image is the part of TinyUSDZ core
//#define STB_IMAGE_IMPLEMENTATION
//#include "external/stb_image.h"

// common
//#include "mapbox/earcut.hpp"  // For polygon triangulation
#include "matrix.h"
#include "trackball.h"

#define PAR_SHAPES_IMPLEMENTATION
#include "par_shapes.h"  // For meshing


const float kPI = 3.141592f;

typedef nanort::real3<float> float3;

namespace example {

struct DifferentialGeometry {

  float t;               // hit t
  float bary_u, bary_v;  // barycentric coordinate.
  uint32_t geom_id;      // geom id(Currently GeomMesh only)
  float tex_u, tex_v;    // texture u and v

  float3 position;
  float3 shading_normal;
  float3 geometric_normal;
};

struct PointLight {
  float3 position{1000.0f, 1000.0f, 1000.0f};
  float3 color{0.8f, 0.8f, 0.8f};
  float intensity{1.0f};
};

inline float3 Lerp3(float3 v0, float3 v1, float3 v2, float u, float v) {
  return (1.0f - u - v) * v0 + u * v1 + v * v2;
}

inline void CalcNormal(float3& N, float3 v0, float3 v1, float3 v2) {
  float3 v10 = v1 - v0;
  float3 v20 = v2 - v0;

  N = vcross(v10, v20);
  N = vnormalize(N);
}

#if 0
bool LoadTextureImage(const tinyusdz::UVTexture &tex, Image *out_image) {
  
  // Asssume asset name = file name
  std::string filename = tex.asset;

  // TODO: 16bit PNG image, EXR image
  int w, h, channels;
  stbi_uc *image = stbi_load(filename.c_str(), &w, &h, &channels, /* desired_channels */3);     
  if (!image) {
    return false;      
  }

  size_t n = w * h * channels;
  out_image->image.resize(n);
  memcpy(out_image->image.data(), image, n);

  out_image->width = w;
  out_image->height = h;
  out_image->channels = channels;

  return true;
 
}
#endif
    
#if 0
// TODO: Move to Tydra
bool ConvertToRenderMesh(const tinyusdz::GeomSphere& sphere,
                         DrawGeomMesh* dst) {
  // TODO: Write our own sphere -> polygon converter

  // TODO: Read subdivision parameter from somewhere.
  int slices = 16;
  int stacks = 8;

  // icohedron subdivision does not generate UV coordinate, so use
  // par_shapes_create_parametric_sphere for now
  par_shapes_mesh* par_mesh =
      par_shapes_create_parametric_sphere(slices, stacks);

  dst->vertices.resize(par_mesh->npoints * 3);

  // TODO: Animated radius
  float radius = 1.0;
  if (sphere.radius.IsTimeSampled()) {
    // TODO
  } else {
    // TODO
    //radius = sphere.radius.Get();
  }

  // scale by radius
  for (size_t i = 0; i < dst->vertices.size(); i++) {
    dst->vertices[i] = par_mesh->points[i] * radius;
  }

  std::vector<uint32_t> facevertex_indices;
  std::vector<float> facevarying_normals;
  std::vector<float> facevarying_texcoords;

  // Make uv and normal facevarying
  // ntriangles = slices * 2 + (stacks - 2) * slices * 2
  for (size_t i = 0; i < par_mesh->ntriangles; i++) {
    PAR_SHAPES_T vidx0 = par_mesh->triangles[3 * i + 0];
    PAR_SHAPES_T vidx1 = par_mesh->triangles[3 * i + 1];
    PAR_SHAPES_T vidx2 = par_mesh->triangles[3 * i + 2];

    facevertex_indices.push_back(vidx0);
    facevertex_indices.push_back(vidx1);
    facevertex_indices.push_back(vidx2);

    facevarying_normals.push_back(par_mesh->normals[3 * vidx0 + 0]);
    facevarying_normals.push_back(par_mesh->normals[3 * vidx0 + 1]);
    facevarying_normals.push_back(par_mesh->normals[3 * vidx0 + 2]);

    facevarying_normals.push_back(par_mesh->normals[3 * vidx1 + 0]);
    facevarying_normals.push_back(par_mesh->normals[3 * vidx1 + 1]);
    facevarying_normals.push_back(par_mesh->normals[3 * vidx1 + 2]);

    facevarying_normals.push_back(par_mesh->normals[3 * vidx2 + 0]);
    facevarying_normals.push_back(par_mesh->normals[3 * vidx2 + 1]);
    facevarying_normals.push_back(par_mesh->normals[3 * vidx2 + 2]);

    facevarying_texcoords.push_back(par_mesh->tcoords[2 * vidx0 + 0]);
    facevarying_texcoords.push_back(par_mesh->tcoords[2 * vidx0 + 1]);

    facevarying_texcoords.push_back(par_mesh->tcoords[2 * vidx1 + 0]);
    facevarying_texcoords.push_back(par_mesh->tcoords[2 * vidx1 + 1]);

    facevarying_texcoords.push_back(par_mesh->tcoords[2 * vidx2 + 0]);
    facevarying_texcoords.push_back(par_mesh->tcoords[2 * vidx2 + 1]);
  }

  par_shapes_free_mesh(par_mesh);

  dst->facevertex_indices = facevertex_indices;

  return true;
}
#endif

bool RTRenderScene::SetupFromUSDFile(const std::string &usd_filename,
  std::string &warn, std::string &err) {

  // When Xform, Mesh, Material, etc. have time-varying values,
  // values are evaluated at `timecode` time(except for animation values in
  // SkelAnimation)
  double timecode = tinyusdz::value::TimeCode::Default();

  tinyusdz::Stage stage;

  if (!tinyusdz::IsUSD(usd_filename)) {
    std::cerr << "File not found or not a USD format: " << usd_filename << "\n";
  }

  bool ret = tinyusdz::LoadUSDFromFile(usd_filename, &stage, &warn, &err);
  if (!warn.empty()) {
    std::cerr << "WARN : " << warn << "\n";
  }

  if (!ret) {
    return false;
  }

  bool is_usdz = tinyusdz::IsUSDZ(usd_filename);

  // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);

  // TODO: Do not triangulate & build indices for raytraced renderer.
  env.mesh_config.triangulate = true;
  env.mesh_config.build_vertex_indices = true;

  // Add base directory of .usd file to search path.
  std::string usd_basedir = tinyusdz::io::GetBaseDir(usd_filename);
  std::cout << "Add seach path: " << usd_basedir << "\n";

  tinyusdz::USDZAsset usdz_asset;
  if (is_usdz) {
    // Setup AssetResolutionResolver to read a asset(file) from memory.
    if (!tinyusdz::ReadUSDZAssetInfoFromFile(usd_filename, &usdz_asset, &warn,
                                             &err)) {
      std::cerr << "Failed to read USDZ assetInfo from file: " << err << "\n";
      exit(-1);
    }
    if (warn.size()) {
      std::cout << warn << "\n";
    }

    tinyusdz::AssetResolutionResolver arr;

    // NOTE: Pointer address of usdz_asset must be valid until the call of
    // RenderSceneConverter::ConvertToRenderScene.
    if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset)) {
      std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
      exit(-1);
    };

    env.asset_resolver = arr;

  } else {
    env.set_search_paths({usd_basedir});

    // TODO: Add example to set user-defined AssetResolutionResolver
    // AssetResolutionResolver arr;
    // ...
    // env.asset_resolver(arr);
  }

  if (!tinyusdz::value::TimeCode(timecode).is_default()) {
    std::cout << "Use timecode : " << timecode << "\n";
  }
  env.timecode = timecode;
  ret = converter.ConvertToRenderScene(env, &render_scene);
  if (!ret) {
    std::cerr << "Failed to convert USD Stage to RenderScene: \n"
              << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }

  if (converter.GetWarning().size()) {
    std::cout << "ConvertToRenderScene warn: " << converter.GetWarning()
              << "\n";
  }

  // TODO: Setup RTMesh

  return false;

}

bool Render(const RTRenderScene &scene, const Camera &cam, AOV *output) {
  // TODO
  return false;
}

}  // namespace example
