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
  std::vector<FlatTri> tris;    // slim per-triangle: geometry + purpose + mat_id
  std::vector<TriMat> flat_mats;  // flat-path material table (one per mesh-job)
  std::vector<Texture> textures;  // diffuse textures referenced by flat_mats[].tex_id
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

// ---- tusdr_integrator.cc ----
bool PurposeVisible(uint32_t purpose_bit, uint32_t purpose_mask);
unsigned WorkerThreadCount(int requested);
int PurposeAnyHitFilter(void *user, uint32_t prim_id, float, float, float);

bool IntersectVisibleTriangles(lrt_tri_scene *scene,
                               const std::vector<FlatTri> &tris,
                               const lrt_ray &ray, uint32_t purpose_mask,
                               lrt_hit *hit);

bool Occluded(lrt_tri_scene *scene, const std::vector<FlatTri> &tris,
              const Vec3 &p, const Vec3 &n, const Vec3 &l, float max_t,
              const DirectScene *direct, uint32_t purpose_mask);

bool OccludedTLAS(const lrt_tlas *tlas, const Vec3 &p, const Vec3 &n,
                  const Vec3 &l, float max_t);

bool IntersectDirectScene(const DirectScene *direct, const Vec3 &ray_org,
                          const Vec3 &ray_dir, float tmin, float tmax,
                          DirectHit *best);

Vec3 PerturbNormalStorm(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                        const Vec3 &N, float u0, float v0, float u1, float v1,
                        float u2, float v2, const Vec3 &Nt);

Vec3 SampleTangentNormal(const Texture &nm, float u, float v, float lod);

float TextureLod(float dudx, float dvdx, float dudy, float dvdy, int w, int h);

bool ComputeUVFootprint(const Vec3 &org, const Vec3 &dir, const RayDiff &rd,
                        const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                        const Vec3 &N, float u0, float v0, float u1, float v1,
                        float u2, float v2, float *dudx, float *dvdx,
                        float *dudy, float *dvdy);

bool ResolveTLASHit(const lrt_tlas_hit &th, const std::vector<Blas> &blas,
                    const std::vector<InstanceRT> &instances,
                    const std::vector<Texture> *textures, const Vec3 &ray_org,
                    const Vec3 &ray_dir, const RayDiff &rd, TriInfo *out);

Vec3 Shade(lrt_tri_scene *scene, const DirectScene *direct,
           const std::vector<FlatTri> &tris, const std::vector<TriMat> &mats,
           const LightCache &lights, const IblCache *ibl,
           const CameraFrame &camera,
           const Options &opt, const Vec3 &ray_org, const Vec3 &ray_dir,
           const std::vector<Texture> *textures = nullptr,
           const std::vector<float> *tri_uvs = nullptr,
           const lrt_tlas *tlas = nullptr,
           const std::vector<Blas> *blas = nullptr,
           const std::vector<InstanceRT> *instances = nullptr,
           const RayDiff &rd = RayDiff{}, int depth = 0,
           const ByteVec *tri_colors = nullptr,
           const std::vector<float> *tri_normals = nullptr);

uint8_t ToSRGB8(float linear);

void MakeRay(const CameraFrame &camera, float aspect, float sx, float sy,
             Vec3 *org, Vec3 *dir);

std::vector<VolumeData> BuildVolumes(const RenderScene &scene);

static float SampleVolumeDensity(const VolumeData &vd, const Vec3 &p);

static bool RayAABBVol(const Vec3 &o, const Vec3 &d, const Vec3 &bmin,
                       const Vec3 &bmax, float *t0, float *t1);

Vec3 CompositeVolumes(const std::vector<VolumeData> &vols, const Vec3 &worg,
                      const Vec3 &wdir, Vec3 bg);

tinyusdz::Image RenderImage(lrt_tri_scene *scene, const DirectScene *direct,
                            const std::vector<FlatTri> &tris,
                            const std::vector<TriMat> &mats,
                            const LightCache &lights, const IblCache *ibl,
                            const CameraFrame &camera, const Options &opt,
                            int height,
                            const std::vector<Texture> *textures = nullptr,
                            const std::vector<float> *tri_uvs = nullptr,
                            const lrt_tlas *tlas = nullptr,
                            const std::vector<Blas> *blas = nullptr,
                            const std::vector<InstanceRT> *instances = nullptr,
                            const ByteVec *tri_colors = nullptr,
                            const std::vector<float> *tri_normals = nullptr,
                            const std::vector<VolumeData> *volumes = nullptr);

bool LoadProgress(float progress, void *);

// ---- next-loader / driver (defined in tusdrender.cc for now) ----
bool BuildRenderContext(const Options &opt, RenderContext &ctx);
bool ExtractAndBuildBVH(RenderContext &ctx, double time);
void PrintRTStats(const RenderContext &ctx);
double RenderFrameTo(RenderContext &ctx, const std::string &path);
void ResolveCameraNext(RenderContext &ctx);

#ifdef TINYUSDZ_WITH_QJS
int RunJSScriptMode(const Options &opt, const std::string &script_path);
int RunMCPMode(const Options &opt);
#endif

#ifdef HAVE_VULKAN
bool RunVulkanLightRT(const Options &opt,
                              const std::vector<Vec3> &base_colors,
                              const std::vector<RTPreviewStats::MeshGeometry> &geos,
                              const CameraFrame &camera, int height);
#endif

#ifdef HAVE_D3D11
bool RunD3D11LightRT(const Options &opt,
                     const std::vector<Vec3> &base_colors,
                     const std::vector<RTPreviewStats::MeshGeometry> &geos,
                     const CameraFrame &camera, int height);
#endif

// ---- tusdr_next.cc (next loader + driver) ----
unsigned WorkerThreadCount(int requested);
bool PurposeVisible(uint32_t purpose_bit, uint32_t purpose_mask);
CameraFrame MakeCameraFrame(const RenderScene &scene, const Options &opt,
                            const Bounds &bounds, int height,
                            tinyusdz::Axis up_axis);
matrix4d Mat4FromArray(const double d[16]);
uint32_t PurposeBit(tinyusdz::Purpose purpose);

std::vector<float> ReadFloatArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                      const char *name, double time);

std::vector<int32_t> ReadIntArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                      const char *name, double time);

std::vector<int64_t> ReadInt64ArrayLazy(const tinyusdz::next::UsdPrim &prim,
                                        const char *name, double time);

std::string DirName(const std::string &path);

bool UsdzEntryMatches(const std::string &entry, const std::string &asset);


tinyusdz::next::UsdPrim ConnectedPrimNext(const tinyusdz::next::Stage &stage,
                                          const tinyusdz::next::UsdPrim &prim,
                                          const std::string &prop);

WrapMode ParseWrapMode(const std::string &s);

void ResolveScalarTextureNext(const tinyusdz::next::Stage &stage,
                              const tinyusdz::next::UsdPrim &surf,
                              const std::string &input, TextureCache &tc,
                              ScalarTex *out);

UvXform ResolveUvXform(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &uvtex);

void ResolveMeshMaterialNext(const tinyusdz::next::Stage &stage,
                             const tinyusdz::next::UsdPrim &mesh,
                             TextureCache &tc, Vec3 *base_color, int32_t *tex_id,
                             float *roughness, float *metallic,
                             int32_t *normal_tex_id, UvXform *uv_xform,
                             ScalarTex *rough_tex, ScalarTex *metal_tex,
                             Vec3 *emission, int32_t *emission_tex_id,
                             float *occlusion, ScalarTex *occ_tex,
                             float *opacity = nullptr,
                             ScalarTex *opacity_tex = nullptr,
                             float *opacity_threshold = nullptr,
                             float *clearcoat = nullptr,
                             float *clearcoat_roughness = nullptr,
                             ScalarTex *clearcoat_tex = nullptr,
                             ScalarTex *clearcoat_rough_tex = nullptr,
                             Vec3 *specular_color = nullptr,
                             int32_t *specular_tex_id = nullptr,
                             float *ior = nullptr,
                             uint8_t *use_specular_workflow = nullptr,
                             bool *vertex_color = nullptr,
                             float *displacement = nullptr,
                             ScalarTex *displacement_tex = nullptr);

void ResolveMeshMaterialCached(
    const tinyusdz::next::Stage &stage, const tinyusdz::next::UsdPrim &mesh,
    TextureCache &tc, std::unordered_map<std::string, ResolvedMat> &cache,
    MeshJobNext *job);

bool PathMatchesMask(const std::string &path,
                     const std::vector<std::string> &mask);

bool PrimHasAnimatedXform(const tinyusdz::next::UsdPrim &prim);

bool MeshHasAnimatedGeom(const tinyusdz::next::UsdPrim &prim);

bool SubtreeGeometryAnimated(const tinyusdz::next::UsdPrim &prim,
                             const std::vector<std::string> &mask,
                             bool ancestor_xform_animated);

bool SceneGeometryAnimated(const tinyusdz::next::Stage &stage,
                           const std::vector<std::string> &mask);

void CollectRTPreviewMeshesNext(const tinyusdz::next::Stage &stage,
                                const tinyusdz::next::UsdPrim &prim,
                                const matrix4d &parent_world,
                                tinyusdz::Purpose inherited_purpose, double time,
                                const std::vector<std::string> &mask,
                                std::vector<MeshJobNext> *jobs);

void CollectVolumesNext(const tinyusdz::next::Stage &stage,
                        const tinyusdz::next::UsdPrim &prim,
                        const matrix4d &parent_world, double time,
                        const std::string &baseDir,
                        std::vector<VolumeData> *out);

float ReadCamFloatNext(const tinyusdz::next::UsdPrim &prim, const char *name,
                       float fallback);

bool FindNextCameraFrameRecursive(const tinyusdz::next::Stage &stage,
                                  const tinyusdz::next::UsdPrim &prim,
                                  const matrix4d &parent_world,
                                  const std::string &query, double time,
                                  CameraFrame *frame, float *aspect);

bool FindNextCameraFrame(const tinyusdz::next::Stage &stage,
                         const std::string &query, double time,
                         CameraFrame *frame, float *aspect);

CameraFrame MakeUsdRecordCamera(const Bounds &bounds, tinyusdz::Axis up_axis,
                                int width, int *out_height);

bool IsCurvePrimNext(const tinyusdz::next::UsdPrim &prim);

std::vector<tinyusdz::value::point3f> ReadCurvePointsNext(
    const tinyusdz::next::UsdPrim &prim, double time);

bool BuildNextCurves(RenderContext &ctx, const std::vector<CurveJobNext> &jobs,
                     double time);

matrix4d InstanceTRS(const float *pos, const float *quat_xyzw,
                     const float *scale3);

void CollectCurvesNextRec(const tinyusdz::next::UsdPrim &prim,
                          const matrix4d &parent_world,
                          tinyusdz::Purpose inherited_purpose, double time,
                          std::vector<CurveJobNext> *out);

void CollectProtoCurves(const tinyusdz::next::Stage &stage,
                        const std::string &proto_path,
                        tinyusdz::Purpose start_purpose, double time,
                        std::vector<CurveJobNext> *out);

int32_t ReserveCurveProto(const tinyusdz::next::Stage &stage,
                          const std::string &proto_path,
                          tinyusdz::Purpose purpose, double time,
                          CurveProtoCollect *curve_inst);

void CollectPointInstancer(const tinyusdz::next::Stage &stage,
                           const tinyusdz::next::UsdPrim &instancer,
                           const matrix4d &instancer_world,
                           tinyusdz::Purpose purpose, double time,
                           const std::vector<std::string> &mask,
                           std::vector<InstanceRT> *instances,
                           std::unordered_map<std::string, uint32_t> *proto_ids,
                           std::vector<ProtoBuildReq> *protos,
                           CurveProtoCollect *curve_inst, RTPreviewStats *stats,
                           // When set, curve placements go here (in `instancer_world`
                           // space) instead of curve_inst->instances -- used to
                           // capture a NESTED instancer's curve placements per
                           // prototype for later flattening. Curve prototypes are
                           // still deduped into curve_inst.
                           std::vector<CurveInstanceRT> *curve_out = nullptr);

void CollectSceneSplit(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &prim,
                       const matrix4d &parent_world,
                       tinyusdz::Purpose inherited_purpose, double time,
                       const std::vector<std::string> &mask,
                       std::vector<MeshJobNext> *base_jobs,
                       std::vector<InstanceRT> *instances,
                       std::unordered_map<std::string, uint32_t> *proto_ids,
                       std::vector<ProtoBuildReq> *protos,
                       std::vector<CurveJobNext> *curve_jobs,
                       CurveProtoCollect *curve_inst, RTPreviewStats *stats,
                       const std::unordered_set<std::string> *proto_holders);

void CollectProtoMeshNestingRec(
    const tinyusdz::next::Stage &stage, const tinyusdz::next::UsdPrim &prim,
    const matrix4d &parent_world, tinyusdz::Purpose inherited_purpose, double time,
    const std::vector<std::string> &mask, std::vector<InstanceRT> *nested,
    std::vector<CurveInstanceRT> *nested_curves,
    std::unordered_map<std::string, uint32_t> *proto_ids,
    std::vector<ProtoBuildReq> *protos, CurveProtoCollect *curve_inst,
    RTPreviewStats *stats,
    const std::unordered_set<std::string> *proto_holders);

void CollectProtoMeshNesting(
    const tinyusdz::next::Stage &stage, const std::string &proto_path,
    tinyusdz::Purpose purpose, double time, const std::vector<std::string> &mask,
    std::vector<InstanceRT> *nested, std::vector<CurveInstanceRT> *nested_curves,
    std::unordered_map<std::string, uint32_t> *proto_ids,
    std::vector<ProtoBuildReq> *protos, CurveProtoCollect *curve_inst,
    RTPreviewStats *stats,
    const std::unordered_set<std::string> *proto_holders);

void CollectPrototypePaths(const tinyusdz::next::UsdPrim &prim,
                           std::unordered_set<std::string> *out);

void CollectProtoJobs(const tinyusdz::next::Stage &stage,
                      const std::string &proto_path,
                      tinyusdz::Purpose start_purpose, double time,
                      std::vector<MeshJobNext> *jobs);

inline size_t EstimateTrisForJob(const tinyusdz::next::UsdPrim &prim,
                                 double time);

void ExpandBoundsByTransformedO2W(Bounds *g, const Bounds &local,
                                  const float o2w[12]);

void CollectLightsNext(const tinyusdz::next::Stage &stage,
                       const tinyusdz::next::UsdPrim &prim,
                       const matrix4d &parent_world, double time,
                       LightCache *cache);

tinyusdz::next::UsdPrim FindDomeLightRec(const tinyusdz::next::UsdPrim &prim);

bool BuildNextIbl(const tinyusdz::next::Stage &stage, const Options &opt,
                  const std::string &base_dir, double time, IblCache *ibl);

bool ParseFrameSpec(const std::string &spec, std::vector<double> *times);

std::string SubstituteFrame(const std::string &path, long frame);

int RunRTPreviewNext(const Options &opt);

// ---- shared helpers (defined in tusdrender.cc) ----
void AppendLinearCurveStrands(const std::vector<tinyusdz::value::point3f> &points,
                              const std::vector<int> &counts,
                              const std::vector<float> &widths,
                              const matrix4d &world,
                              std::vector<float> *curve_points,
                              std::vector<float> *curve_radii,
                              std::vector<uint32_t> *first,
                              std::vector<uint32_t> *count,
                              std::vector<TriInfo> *info,
                              Bounds *bounds);
void MergeStats(RTPreviewStats *dst, const RTPreviewStats &src);
void MergeBounds(Bounds *dst, const Bounds &src);
inline const Vec3 kCurveColor{0.62f, 0.50f, 0.34f};

// ---- tusdr_legacy.cc (legacy loader + shared utils) ----
std::vector<int> FaceMaterialIds(const RenderMesh &mesh);

void AddMeshTriangles(const RenderScene &scene, const RenderMesh &mesh,
                      const matrix4d &world, std::vector<float> *vertices,
                      std::vector<TriInfo> *tris, Bounds *bounds,
                      LightCache *lights = nullptr);

void CollectGeometry(const RenderScene &scene, const Node &node,
                     std::vector<float> *vertices, std::vector<TriInfo> *tris,
                     Bounds *bounds,
                     const std::unordered_set<std::string> *skip_paths,
                     LightCache *lights);

void CollectAllGeometry(const RenderScene &scene, std::vector<float> *vertices,
                        std::vector<TriInfo> *tris, Bounds *bounds,
                        const std::unordered_set<std::string> *skip_paths,
                        LightCache *lights);

matrix4d LocalMatrixOrIdentity(const tinyusdz::Xformable *xformable, double time,
                               bool *reset);

tinyusdz::Purpose ResolvePurpose(const tinyusdz::Prim &prim,
                                 tinyusdz::Purpose inherited);

bool AddRTPreviewMesh(const tinyusdz::Stage &stage, const std::string &prim_path,
                      const tinyusdz::GeomMesh &mesh, const matrix4d &world,
                      double time, tinyusdz::Purpose purpose,
                      uint32_t purpose_mask,
                      std::vector<float> *vertices, std::vector<TriInfo> *tris,
                      Bounds *bounds, RTPreviewStats *stats);

void CollectRTPreviewMeshes(const tinyusdz::Prim &prim,
                            const matrix4d &parent_world, double time,
                            tinyusdz::Purpose inherited_purpose,
                            std::vector<MeshJob> *jobs);

bool BuildRTPreviewScene(const tinyusdz::Stage &stage, const Options &opt,
                         std::vector<float> *vertices,
                         std::vector<TriInfo> *tris, Bounds *bounds,
                         RTPreviewStats *stats, std::string *err);

bool MatchPrimNameOrPath(const tinyusdz::Prim &prim, const std::string &query);

bool CameraFrameFromGeomCamera(const tinyusdz::Stage &stage,
                               const tinyusdz::GeomCamera &cam,
                               const matrix4d &world, double time,
                               CameraFrame *frame);

bool FindStageCameraFrameRecursive(const tinyusdz::Stage &stage,
                                   const tinyusdz::Prim &prim,
                                   const std::string &query,
                                   const matrix4d &parent_world, double time,
                                   CameraFrame *frame);

bool FindStageCameraFrame(const tinyusdz::Stage &stage, const std::string &query,
                          double time, CameraFrame *frame);

bool EvalAxis(const tinyusdz::TypedAttributeWithFallback<tinyusdz::Axis> &attr,
              tinyusdz::Axis *out);

std::string PrimPathString(const tinyusdz::Prim &prim);

float ApproxScale(const matrix4d &m);

void AddNurbsTriangle(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                      std::vector<float> *vertices, std::vector<TriInfo> *tris,
                      Bounds *bounds);

void AddDirectTriangle(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2,
                       const Vec3 &color, std::vector<float> *vertices,
                       std::vector<TriInfo> *tris, Bounds *bounds);

void AddDirectCube(double size, const matrix4d &world, std::vector<float> *vertices,
                   std::vector<TriInfo> *tris, Bounds *bounds);

void AddDirectPlane(double width, double length, tinyusdz::Axis axis,
                    const matrix4d &world, std::vector<float> *vertices,
                    std::vector<TriInfo> *tris, Bounds *bounds);

double BSplineBasis(int i, int degree, double u, const std::vector<double> &knots);

Vec3 EvalNurbsPatchPoint(const std::vector<tinyusdz::value::point3f> &points,
                         const std::vector<double> &weights, int u_count,
                         int v_count, int u_order, int v_order,
                         const std::vector<double> &u_knots,
                         const std::vector<double> &v_knots, double u, double v);

void AddNurbsPatchTriangles(const tinyusdz::Stage &stage,
                            const tinyusdz::GeomNurbsPatch &patch,
                            const matrix4d &world, double time,
                            std::vector<float> *vertices,
                            std::vector<TriInfo> *tris, Bounds *bounds);

void TraverseDirectPrims(const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
                         const std::unordered_map<std::string, matrix4d> &matrices,
                         double time, DirectScene *direct,
                         std::vector<float> *vertices, std::vector<TriInfo> *tris,
                         Bounds *bounds, std::vector<float> *sphere_data,
                         std::vector<float> *round_points,
                         std::vector<float> *round_radii,
                         std::vector<uint32_t> *round_first,
                         std::vector<uint32_t> *round_count,
                         std::vector<float> *flat_points,
                         std::vector<float> *flat_radii,
                         std::vector<uint32_t> *flat_first,
                         std::vector<uint32_t> *flat_count,
                         std::vector<float> *point_centers,
                         std::vector<float> *point_radii,
                         std::vector<float> *bez_cps,
                         std::vector<float> *tet_aabbs);

bool BuildDirectScene(const tinyusdz::Stage &stage, const RenderScene &render_scene,
                      const Options &opt, std::vector<float> *vertices,
                      std::vector<TriInfo> *tris, Bounds *bounds,
                      DirectScene *direct, std::string *err);

bool FindCameraNode(const RenderScene &scene, const Node &node,
                    const std::string &query, const Node **node_out);
const Node *FindCameraNode(const RenderScene &scene, const std::string &query);

}  // namespace tusdr
