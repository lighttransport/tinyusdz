// SPDX-License-Identifier: Apache-2.0
// tusdrender — the RenderContext aggregator (persistent render state shared by
// the next-loader pipeline) plus the cross-TU function prototype surface (added
// as the monolith is split into per-module .cc files).
#pragma once

#include "tusdr_types.hh"

namespace tusdr {

struct RenderContext {
  tinyusdz::next::Stage stage;  // keeps the lazy point/index arrays alive
  // Flat (no-instance) path buffers: default allocator so the shared
  // Shade/RenderImage signatures stay std::vector. The big, OOM-prone instanced
  // geometry lives in the budget-tracked Blas buffers below.
  std::vector<float> vertices;  // packed triangle positions (flat-path BVH input)
  std::vector<TriInfo> tris;
  std::vector<Texture> textures;  // diffuse textures referenced by tris[].tex_id
  std::vector<float> tri_uvs;  // 6 floats/tri (parallel to tris); empty if none
  ByteVec tri_colors;  // 12 bytes/tri (per-corner RGBA8); empty if none
  std::vector<float> tri_normals;  // 9 floats/tri (per-corner normals); empty if none
  std::vector<VolumeData> volumes;  // UsdVol volumes (OpenVDB) for raymarching
  Bounds bounds;
  RTPreviewStats stats;
  lrt_tri_scene *scene{nullptr};  // owned flat BVH (no-instance path)
  // Two-level (instanced) BVH path: built when the composed scene has native
  // instances. blas[0] is the base (non-instanced) geometry; blas[1..] are the
  // unique prototypes. instances[] place them; tlas is the top-level BVH.
  std::vector<Blas> blas;
  std::vector<InstanceRT> instances;
  lrt_tlas *tlas{nullptr};
  bool use_tlas{false};
  DirectScene direct;             // empty for the next path
  LightCache lights;              // empty -> camera-headlight fallback
  IblCache ibl;                   // image-based lighting (--env / DomeLight)
  tinyusdz::Axis up_axis{tinyusdz::Axis::Y};
  CameraFrame camera;
  Options opt;  // mutable render parameters (width/height/ambient/bg/...)
  int width{960};
  int height{540};
  // Time at which geometry + transforms are evaluated (NaN = default value).
  double frame_time{std::numeric_limits<double>::quiet_NaN()};
  double load_seconds{0.0}, stream_seconds{0.0}, bvh_seconds{0.0};

  // Free the TLAS before the BLAS scenes it references (blas[] destructs after
  // this body runs).
  ~RenderContext() {
    if (tlas) lrt_tlas_free(tlas);
    if (scene) lrt_tri_scene_free(scene);
  }
  RenderContext() = default;
  RenderContext(const RenderContext &) = delete;
  RenderContext &operator=(const RenderContext &) = delete;
};

// ---- tusdr_args.cc / tusdr_material.cc ----
bool ParseIntStrict(const std::string &s, int *out);

bool ParseFloatStrict(const std::string &s, float *out);

bool ParseDoubleStrict(const std::string &s, double *out);

bool ParseColor(const std::string &s, Vec3 *out);

void PrintUsage(const char *prog);

bool ParseArgs(int argc, char **argv, Options *opt);

void SetupNullAssetResolution(tinyusdz::AssetResolutionResolver *resolver);

Vec3 MaterialColor(const RenderScene &scene, const RenderMesh &mesh,
                   int material_id);

Vec3 MaterialEmission(const RenderScene &scene, int material_id);

float MaterialRoughness(const RenderScene &scene, int material_id);

float MaterialMetallic(const RenderScene &scene, int material_id);

Vec3 MeshLightEmission(const RenderScene &scene, const RenderMesh &mesh,
                       int material_id, float total_area);

// ---- tusdr_lighting.cc ----
void AppendPowerCdf(std::vector<PreviewLight> *lights, std::vector<float> *cdf);

Vec3 DirectionFromLatlong(float u, float v);

void LatlongUV(const Vec3 &dir, float *u, float *v);

Vec3 SampleEnvNearest(const EnvImage &img, float u, float v);

Vec3 SampleEnv(const EnvImage &img, const Vec3 &dir);

bool DecodeTextureToEnvImage(const RenderScene &scene, int texture_id,
                             EnvImage *out);

EnvImage ConvolveDiffuseEnv(const EnvImage &env, int width, int height);

EnvImage PrefilterEnvMip(const EnvImage &env, int width, int height,
                         float roughness);

float RadicalInverseVdc(uint32_t bits);

Vec3 ImportanceSampleGGX(float xi0, float xi1, float roughness, const Vec3 &n);

float GeometrySchlickGGX(float ndotv, float roughness);

float GeometrySmith(float ndotv, float ndotl, float roughness);

void BuildBrdfLut(int size, IblCache *ibl);

bool BuildIblFromEnv(EnvImage &&env, IblCache *ibl);

bool BuildIblCache(const RenderScene &scene, const LightCache &lights,
                   IblCache *ibl);

bool LoadEnvImageFromFile(const std::string &path, const Vec3 &scale,
                          EnvImage *out);

Vec3 SampleIblMip(const std::vector<EnvImage> &mips, const Vec3 &dir,
                  float roughness);

void SampleBrdfLut(const IblCache &ibl, float ndotv, float roughness, float *a,
                   float *b);

float RectArea(const RenderLight &light);

float DiskArea(const RenderLight &light);

float SphereArea(const RenderLight &light);

float CylinderArea(const RenderLight &light);

Vec3 LightColor(const RenderLight &light);

void AddFiniteLight(const RenderLight &light, PreviewLight::Kind kind,
                    LightCache *cache);

void CollectLights(const RenderScene &scene, LightCache *cache);

// ---- tusdr_geom.cc ----
void ExpandBoundsByVolume(const std::vector<VolumeData> &vols, Bounds *b);
tinyusdz::Axis GetUpAxis(const std::string &up);
bool BuildNodeMatrixMap(const Node &node,
                        std::unordered_map<std::string, matrix4d> *map);

std::unordered_map<std::string, matrix4d> BuildNodeMatrixMap(
    const RenderScene &scene);

matrix4d MatrixForPath(const std::unordered_map<std::string, matrix4d> &map,
                       const std::string &path);

Vec3 TransformNormal(const matrix4d &inv_world, const Vec3 &n);

int AxisIndex(tinyusdz::Axis axis);

Vec3 AxisVec(tinyusdz::Axis axis);

float Coord(const Vec3 &v, int axis);

Vec3 WithCoord(Vec3 v, int axis, float c);

Vec3 RadialPart(Vec3 v, int axis);

bool SolveQuadratic(float a, float b, float c, float *t0, float *t1);

bool IntersectTriangleMT(const Vec3 &o, const Vec3 &d, const Vec3 &a,
                         const Vec3 &b, const Vec3 &c, float tmin,
                         float tmax, float *t);

bool IntersectTetPrim(const TetPrim &tet, const Vec3 &o, const Vec3 &d,
                      float tmin, float tmax, float *best_t, Vec3 *normal);

int TetUserIntersect(const lrt_ray *ray, uint32_t prim_id, void *user,
                     float *t, float *u, float *v);

int TetUserOccluded(const lrt_ray *ray, uint32_t prim_id, void *user);

bool AcceptT(float t, float tmin, float tmax, float *best);

bool IntersectDirectShape(const DirectShape &shape, const Vec3 &ray_org,
                          const Vec3 &ray_dir, float tmin, float tmax,
                          DirectHit *hit);

float TriangleArea(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2);

}  // namespace tusdr
