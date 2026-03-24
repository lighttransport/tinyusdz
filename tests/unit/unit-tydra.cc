#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-tydra.h"

#include <algorithm>
#include <array>

#include "layer.hh"
#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "tydra/attribute-eval.hh"
#include "tydra/layer-to-renderscene.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "usdGeom.hh"
#include "usdMtlx.hh"

using namespace tinyusdz;

namespace {

std::vector<Path> MakeMultiTargetConnections() {
  return {Path("/ShaderA", "outputs:out"), Path("/ShaderB", "outputs:out")};
}

bool ContainsName(const std::vector<std::string> &names,
                  const std::string &name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

size_t CountName(const std::vector<std::string> &names,
                 const std::string &name) {
  return size_t(std::count(names.begin(), names.end(), name));
}

const std::array<value::texcoord2f, 3> kTexcoordsVec2 = {
    value::texcoord2f{0.0f, 0.0f}, value::texcoord2f{1.0f, 0.0f},
    value::texcoord2f{0.0f, 1.0f}};

const std::array<value::float3, 3> kTexcoordsVec3 = {
    value::float3{0.0f, 0.0f, 0.0f}, value::float3{1.0f, 0.0f, 0.0f},
    value::float3{0.0f, 1.0f, 0.0f}};

struct CancelOnMeshProgressState {
  size_t mesh_progress_calls{0};
};

bool FailingTextureImageLoader(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, tydra::TextureImage *imageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  (void)assetPath;
  (void)assetInfo;
  (void)assetResolver;
  (void)imageOut;
  (void)imageData;
  (void)userdata;
  (void)warn;

  if (err) {
    *err = "synthetic texture loader failure";
  }
  return false;
}

bool SingleChannelTextureImageLoader(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, tydra::TextureImage *imageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  (void)assetInfo;
  (void)assetResolver;
  (void)userdata;
  (void)warn;

  if (!imageOut || !imageData) {
    if (err) {
      *err = "output buffer is null";
    }
    return false;
  }

  imageOut->asset_identifier = assetPath.GetAssetPath();
  imageOut->width = 1;
  imageOut->height = 1;
  imageOut->channels = 1;
  imageOut->assetTexelComponentType = tydra::ComponentType::UInt8;

  imageData->assign(1, uint8_t(128));
  return true;
}

bool CancelOnFirstMeshProgress(const tydra::DetailedProgressInfo &info,
                               void *userptr) {
  auto *state = reinterpret_cast<CancelOnMeshProgressState *>(userptr);
  if (!state) {
    return false;
  }

  if (info.stage == tydra::DetailedProgressInfo::Stage::ConvertingMeshes) {
    state->mesh_progress_calls++;
    return false;
  }

  return true;
}

}  // namespace

void tydra_connection_validation_test(void) {
  std::string err;
  Path target_path;

  TEST_CHECK(!tydra::detail::ResolveSingleConnectionTargetPath(
      {}, "inputs:test", &target_path, &err));
  TEST_CHECK(err.find("empty") != std::string::npos);

  err.clear();
  const std::vector<Path> multi_targets = MakeMultiTargetConnections();
  TEST_CHECK(!tydra::detail::ResolveSingleConnectionTargetPath(
      multi_targets, "inputs:test", &target_path, &err));
  TEST_CHECK(err.find("Multiple targetPaths") != std::string::npos);

  Stage stage;
  tydra::TerminalAttributeValue resolved;

  {
    Attribute attr;
    attr.set_connections(multi_targets);

    err.clear();
    TEST_CHECK(
        !tydra::EvaluateAttribute(stage, attr, "inputs:test", &resolved, &err));
    TEST_CHECK(err.find("Multiple targetPaths") != std::string::npos);
  }

  {
    TypedAttribute<float> attr;
    attr.set_connections(multi_targets);

    float value = 0.0f;
    err.clear();
    TEST_CHECK(
        !tydra::EvaluateTypedAttribute(stage, attr, "inputs:test", &value, &err));
    TEST_CHECK(err.find("Multiple targetPaths") != std::string::npos);
  }

  {
    TypedAttribute<Animatable<float>> attr;
    attr.set_connections(multi_targets);

    float value = 0.0f;
    err.clear();
    TEST_CHECK(!tydra::EvaluateTypedAnimatableAttribute(
        stage, attr, "inputs:test", &value, &err, 1.0,
        value::TimeSampleInterpolationType::Held));
    TEST_CHECK(err.find("Multiple targetPaths") != std::string::npos);
  }

  {
    TypedAttributeWithFallback<float> attr{0.5f};
    attr.set_connections(multi_targets);
    attr.set_value_empty();

    float value = 0.0f;
    err.clear();
    TEST_CHECK(
        !tydra::EvaluateTypedAttribute(stage, attr, "inputs:test", &value, &err));
    TEST_CHECK(err.find("Multiple targetPaths") != std::string::npos);
  }

  {
    TypedAttributeWithFallback<Animatable<float>> attr{Animatable<float>(0.5f)};
    attr.set_connections(multi_targets);
    attr.set_value_empty();

    float value = 0.0f;
    err.clear();
    TEST_CHECK(!tydra::EvaluateTypedAnimatableAttribute(
        stage, attr, "inputs:test", &value, &err, 1.0,
        value::TimeSampleInterpolationType::Held));
    TEST_CHECK(err.find("Multiple targetPaths") != std::string::npos);
  }
}

void tydra_inplace_conversion_guard_test(void) {
  tydra::LayerToRenderSceneConverter converter;
  tydra::RenderScene render_scene;
  std::string warn;
  std::string err;

  {
    auto layer = std::make_unique<Layer>();
    PrimSpec xform_ps;
    xform_ps.specifier() = Specifier::Def;
    xform_ps.typeName() = "Xform";
    xform_ps.name() = "Root";
    TEST_CHECK(layer->add_primspec("Root", xform_ps));

    TEST_CHECK(!converter.ConvertLayerInPlace(std::move(layer), &render_scene,
                                              &warn, &err));
    TEST_CHECK(err.find("temporarily disabled") != std::string::npos);
    TEST_CHECK(render_scene.nodes.empty());
    TEST_CHECK(render_scene.meshes.empty());
    TEST_CHECK(render_scene.materials.empty());
  }

  {
    Layer layer;
    PrimSpec xform_ps;
    xform_ps.specifier() = Specifier::Def;
    xform_ps.typeName() = "Xform";
    xform_ps.name() = "Root";
    TEST_CHECK(layer.add_primspec("Root", xform_ps));

    tydra::RenderScene normal_scene;
    warn.clear();
    err.clear();
    TEST_CHECK(converter.ConvertLayer(&layer, &normal_scene, &warn, &err));
    TEST_CHECK(normal_scene.nodes.size() == 1);
  }

  {
    auto prim_spec = std::make_unique<PrimSpec>();
    prim_spec->specifier() = Specifier::Def;
    prim_spec->typeName() = "Mesh";
    prim_spec->name() = "MeshPrim";

    tydra::RenderMesh mesh;
    warn.clear();
    err.clear();
    TEST_CHECK(!converter.ConvertPrimSpecInPlace(std::move(prim_spec), &mesh,
                                                 &warn, &err));
    TEST_CHECK(err.find("temporarily disabled") != std::string::npos);
  }
}

void tydra_geommesh_property_accessor_test(void) {
  GeomMesh mesh;
  std::string err;
  Prim prim("MeshPrim", mesh);

  mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{4, 4});
  mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{0, 1, 2, 3, 3, 2, 1, 0});
  prim.set_primdata("MeshPrim", mesh);

  {
    Property prop;
    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "faceVertexCounts", &prop, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(prop.is_attribute());

    const Attribute *attr = prop.get_attribute_or_null();
    TEST_CHECK(attr != nullptr);
    auto value = attr->get_value<std::vector<int32_t>>();
    TEST_CHECK(value.has_value());
    TEST_CHECK(value.value() == std::vector<int32_t>({4, 4}));
  }

  {
    Property prop;
    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "faceVertexIndices", &prop, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(prop.is_attribute());

    const Attribute *attr = prop.get_attribute_or_null();
    TEST_CHECK(attr != nullptr);
    auto value = attr->get_value<std::vector<int32_t>>();
    TEST_CHECK(value.has_value());
    TEST_CHECK(value.value() ==
               std::vector<int32_t>({0, 1, 2, 3, 3, 2, 1, 0}));
    TEST_CHECK(value.value() != std::vector<int32_t>({4, 4}));
  }
}

void tydra_memory_tracking_test(void) {
  tydra::detail::MemoryUsageState state;
  std::vector<std::string> progress_messages;
  std::vector<size_t> freed_bytes;

  state.current_memory_usage = 128;
  state.peak_memory_usage = 128;

  tydra::detail::ApplyMemoryUsageDelta(
      0, 256, 1024, &state,
      [&progress_messages](const std::string &message) {
        progress_messages.push_back(message);
      },
      [&freed_bytes](size_t bytes_freed) { freed_bytes.push_back(bytes_freed); });

  TEST_CHECK(state.current_memory_usage == 0);
  TEST_CHECK(state.peak_memory_usage == 128);
  TEST_CHECK(freed_bytes.size() == 1);
  TEST_CHECK(freed_bytes[0] == 128);
  TEST_CHECK(progress_messages.empty());

  tydra::detail::ApplyMemoryUsageDelta(
      2 * 1024 * 1024, 0, 1, &state,
      [&progress_messages](const std::string &message) {
        progress_messages.push_back(message);
      },
      [&freed_bytes](size_t bytes_freed) { freed_bytes.push_back(bytes_freed); });

  TEST_CHECK(state.current_memory_usage == 2 * 1024 * 1024);
  TEST_CHECK(state.peak_memory_usage == 2 * 1024 * 1024);
  TEST_CHECK(freed_bytes.size() == 1);
  TEST_CHECK(progress_messages.size() == 1);
  TEST_CHECK(progress_messages[0].find("Memory limit exceeded") !=
             std::string::npos);
}

void tydra_scene_access_helper_test(void) {
  GeomMesh mesh;
  mesh.points = Animatable<std::vector<value::point3f>>(
      std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                  {1.0f, 0.0f, 0.0f},
                                  {0.0f, 1.0f, 0.0f}});
  mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{3});
  mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{0, 1, 2});

  Relationship skeleton_rel;
  skeleton_rel.set(Path("/Skeleton", ""));
  mesh.skeleton = skeleton_rel;

  Prim prim("MeshPrim", mesh);
  std::string err;

  {
    std::vector<std::string> attr_names;
    TEST_CHECK(tydra::GetAttributeNames(prim, &attr_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(), "points") !=
               attr_names.end());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(),
                         "faceVertexCounts") != attr_names.end());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(),
                         "faceVertexIndices") != attr_names.end());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(), "skeleton") ==
               attr_names.end());
  }

  {
    std::vector<std::string> rel_names;
    err.clear();
    TEST_CHECK(tydra::GetRelationshipNames(prim, &rel_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(std::find(rel_names.begin(), rel_names.end(), "skeleton") !=
               rel_names.end());
    TEST_CHECK(std::find(rel_names.begin(), rel_names.end(),
                         "faceVertexCounts") == rel_names.end());
  }

  {
    Relationship rel;
    err.clear();
    TEST_CHECK(tydra::GetRelationship(prim, "skeleton", &rel, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(rel.is_path());
    TEST_CHECK(rel.targetPath.prim_part() == "/Skeleton");
  }
}

void tydra_shader_scene_access_test(void) {
  std::string err;

  {
    UsdPreviewSurface surface;
    surface.diffuseColor.set_value(
        Animatable<value::color3f>(value::color3f({0.8f, 0.2f, 0.1f})));
    surface.outputsSurface.set_authored(true);

    Shader shader;
    shader.info_id = kUsdPreviewSurface;
    shader.value = surface;

    Prim prim("PreviewSurface", shader);

    Property prop;
    TEST_CHECK(tydra::GetProperty(prim, "info:id", &prop, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(prop.is_attribute());
    {
      const Attribute *attr = prop.get_attribute_or_null();
      TEST_CHECK(attr != nullptr);
      auto info_id = attr->get_value<value::token>();
      TEST_CHECK(info_id.has_value());
      TEST_CHECK(info_id.value().str() == kUsdPreviewSurface);
    }

    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "inputs:diffuseColor", &prop, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(prop.is_attribute());
    {
      const Attribute *attr = prop.get_attribute_or_null();
      TEST_CHECK(attr != nullptr);
      auto diffuse = attr->get_value<value::color3f>();
      TEST_CHECK(diffuse.has_value());
      TEST_CHECK(diffuse.value()[0] == 0.8f);
      TEST_CHECK(diffuse.value()[1] == 0.2f);
      TEST_CHECK(diffuse.value()[2] == 0.1f);
    }

    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "outputs:surface", &prop, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(prop.is_attribute());

    std::vector<std::string> prop_names;
    TEST_CHECK(tydra::GetPropertyNames(prim, &prop_names, &err));
    TEST_CHECK(std::find(prop_names.begin(), prop_names.end(), "info:id") !=
               prop_names.end());
    TEST_CHECK(std::find(prop_names.begin(), prop_names.end(),
                         "inputs:diffuseColor") != prop_names.end());
    TEST_CHECK(std::find(prop_names.begin(), prop_names.end(),
                         "outputs:surface") != prop_names.end());

    std::vector<std::string> attr_names;
    err.clear();
    TEST_CHECK(tydra::GetAttributeNames(prim, &attr_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(), "info:id") !=
               attr_names.end());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(),
                         "inputs:diffuseColor") != attr_names.end());

    std::vector<std::string> rel_names;
    err.clear();
    TEST_CHECK(tydra::GetRelationshipNames(prim, &rel_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(rel_names.empty());
  }

  {
    UsdTransform2d tx;
    tx.rotation.set_value(Animatable<float>(45.0f));
    tx.result.set_authored(true);

    Shader shader;
    shader.info_id = kUsdTransform2d;
    shader.value = tx;

    Prim prim("Transform2d", shader);
    Property prop;

    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "inputs:rotation", &prop, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(prop.is_attribute());

    const Attribute *attr = prop.get_attribute_or_null();
    TEST_CHECK(attr != nullptr);
    auto rotation = attr->get_value<float>();
    TEST_CHECK(rotation.has_value());
    TEST_CHECK(rotation.value() == 45.0f);
  }

  {
    Material material;
    material.surface.set(Path("/PreviewSurface", "outputs:surface"));

    Prim prim("Material", material);
    std::vector<std::string> prop_names;
    err.clear();
    TEST_CHECK(tydra::GetPropertyNames(prim, &prop_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(std::find(prop_names.begin(), prop_names.end(),
                         "outputs:surface") != prop_names.end());

    std::vector<std::string> attr_names;
    err.clear();
    TEST_CHECK(tydra::GetAttributeNames(prim, &attr_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(std::find(attr_names.begin(), attr_names.end(),
                         "outputs:surface") != attr_names.end());
  }
}

void tydra_skel_scene_access_test(void) {
  std::string err;

  {
    SkelRoot skelroot;
    skelroot.purpose = Purpose::Render;
    skelroot.visibility = Animatable<Visibility>(Visibility::Invisible);

    Relationship proxy_rel;
    proxy_rel.set(Path("/Proxy", ""));
    skelroot.proxyPrim = proxy_rel;

    Relationship anim_rel;
    anim_rel.set(Path("/Anim", ""));
    skelroot.animationSource = anim_rel;

    Relationship skeleton_rel;
    skeleton_rel.set(Path("/Skeleton", ""));
    skelroot.skeleton = skeleton_rel;

    XformOp translate_op;
    translate_op.op_type = XformOp::OpType::Translate;
    translate_op.set_value(value::double3{1.0, 2.0, 3.0});
    skelroot.xformOps.push_back(translate_op);

    Prim prim("SkelRootPrim", skelroot);

    std::vector<std::string> prop_names;
    TEST_CHECK(tydra::GetPropertyNames(prim, &prop_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(prop_names, "purpose"));
    TEST_CHECK(ContainsName(prop_names, "visibility"));
    TEST_CHECK(ContainsName(prop_names, "proxyPrim"));
    TEST_CHECK(ContainsName(prop_names, "animationSource"));
    TEST_CHECK(ContainsName(prop_names, "skeleton"));
    TEST_CHECK(ContainsName(prop_names, "xformOp:translate"));
    TEST_CHECK(ContainsName(prop_names, "xformOpOrder"));

    std::vector<std::string> attr_names;
    err.clear();
    TEST_CHECK(tydra::GetAttributeNames(prim, &attr_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(attr_names, "purpose"));
    TEST_CHECK(ContainsName(attr_names, "visibility"));
    TEST_CHECK(ContainsName(attr_names, "xformOp:translate"));
    TEST_CHECK(ContainsName(attr_names, "xformOpOrder"));
    TEST_CHECK(!ContainsName(attr_names, "skeleton"));

    std::vector<std::string> rel_names;
    err.clear();
    TEST_CHECK(tydra::GetRelationshipNames(prim, &rel_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(rel_names, "proxyPrim"));
    TEST_CHECK(ContainsName(rel_names, "animationSource"));
    TEST_CHECK(ContainsName(rel_names, "skeleton"));
    TEST_CHECK(!ContainsName(rel_names, "purpose"));

    Property prop;
    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "purpose", &prop, &err));
    TEST_CHECK(err.empty());
    {
      const Attribute *attr = prop.get_attribute_or_null();
      TEST_CHECK(attr != nullptr);
      auto purpose = attr->get_value<value::token>();
      TEST_CHECK(purpose.has_value());
      TEST_CHECK(purpose.value().str() == "render");
    }

    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "xformOpOrder", &prop, &err));
    TEST_CHECK(err.empty());
    {
      const Attribute *attr = prop.get_attribute_or_null();
      TEST_CHECK(attr != nullptr);
      auto xform_op_order = attr->get_value<std::vector<value::token>>();
      TEST_CHECK(xform_op_order.has_value());
      TEST_CHECK(xform_op_order.value().size() == 1);
      TEST_CHECK(xform_op_order.value()[0].str() == "xformOp:translate");
    }

    Relationship rel;
    err.clear();
    TEST_CHECK(tydra::GetRelationship(prim, "skeleton", &rel, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(rel.is_path());
    TEST_CHECK(rel.targetPath.prim_part() == "/Skeleton");
  }

  {
    Skeleton skeleton;
    skeleton.bindTransforms = std::vector<value::matrix4d>{
        value::matrix4d::identity()};
    skeleton.jointNames = std::vector<value::token>{value::token("Root")};
    skeleton.joints = std::vector<value::token>{value::token("Root")};
    skeleton.restTransforms = std::vector<value::matrix4d>{
        value::matrix4d::identity()};
    skeleton.purpose = Purpose::Guide;

    Relationship proxy_rel;
    proxy_rel.set(Path("/Proxy", ""));
    skeleton.proxyPrim = proxy_rel;

    Relationship anim_rel;
    anim_rel.set(Path("/Anim", ""));
    skeleton.animationSource = anim_rel;

    XformOp scale_op;
    scale_op.op_type = XformOp::OpType::Scale;
    scale_op.set_value(value::double3{2.0, 2.0, 2.0});
    skeleton.xformOps.push_back(scale_op);

    Prim prim("SkeletonPrim", skeleton);

    std::vector<std::string> prop_names;
    err.clear();
    TEST_CHECK(tydra::GetPropertyNames(prim, &prop_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(prop_names, "bindTransforms"));
    TEST_CHECK(ContainsName(prop_names, "jointNames"));
    TEST_CHECK(ContainsName(prop_names, "joints"));
    TEST_CHECK(ContainsName(prop_names, "restTransforms"));
    TEST_CHECK(ContainsName(prop_names, "purpose"));
    TEST_CHECK(ContainsName(prop_names, "proxyPrim"));
    TEST_CHECK(ContainsName(prop_names, "animationSource"));
    TEST_CHECK(ContainsName(prop_names, "xformOp:scale"));
    TEST_CHECK(ContainsName(prop_names, "xformOpOrder"));

    std::vector<std::string> rel_names;
    err.clear();
    TEST_CHECK(tydra::GetRelationshipNames(prim, &rel_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(rel_names, "proxyPrim"));
    TEST_CHECK(ContainsName(rel_names, "animationSource"));
    TEST_CHECK(!ContainsName(rel_names, "bindTransforms"));

    Property prop;
    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "purpose", &prop, &err));
    TEST_CHECK(err.empty());
    {
      const Attribute *attr = prop.get_attribute_or_null();
      TEST_CHECK(attr != nullptr);
      auto purpose = attr->get_value<value::token>();
      TEST_CHECK(purpose.has_value());
      TEST_CHECK(purpose.value().str() == "guide");
    }
  }

  {
    BlendShape blendshape;
    blendshape.offsets = std::vector<value::vector3f>{
        value::vector3f{0.1f, 0.0f, 0.0f}};
    blendshape.normalOffsets = std::vector<value::vector3f>{
        value::vector3f{0.0f, 0.1f, 0.0f}};
    blendshape.pointIndices = std::vector<int>{0};

    Prim prim("BlendShapePrim", blendshape);

    std::vector<std::string> attr_names;
    err.clear();
    TEST_CHECK(tydra::GetAttributeNames(prim, &attr_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(attr_names, "offsets"));
    TEST_CHECK(ContainsName(attr_names, "normalOffsets"));
    TEST_CHECK(ContainsName(attr_names, "pointIndices"));

    std::vector<std::string> rel_names;
    err.clear();
    TEST_CHECK(tydra::GetRelationshipNames(prim, &rel_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(rel_names.empty());
  }

  {
    SkelAnimation anim;
    anim.blendShapes = std::vector<value::token>{value::token("shapeA")};
    anim.blendShapeWeights = Animatable<std::vector<float>>(
        std::vector<float>{0.5f});
    anim.joints = std::vector<value::token>{value::token("Root")};
    anim.translations = Animatable<std::vector<value::float3>>(
        std::vector<value::float3>{value::float3{1.0f, 2.0f, 3.0f}});

    Prim prim("SkelAnimPrim", anim);

    std::vector<std::string> prop_names;
    err.clear();
    TEST_CHECK(tydra::GetPropertyNames(prim, &prop_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(ContainsName(prop_names, "blendShapes"));
    TEST_CHECK(ContainsName(prop_names, "blendShapeWeights"));
    TEST_CHECK(ContainsName(prop_names, "joints"));
    TEST_CHECK(ContainsName(prop_names, "translations"));

    Property prop;
    err.clear();
    TEST_CHECK(tydra::GetProperty(prim, "blendShapeWeights", &prop, &err));
    TEST_CHECK(err.empty());
    {
      const Attribute *attr = prop.get_attribute_or_null();
      TEST_CHECK(attr != nullptr);
      auto weights = attr->get_value<std::vector<float>>();
      TEST_CHECK(weights.has_value());
      TEST_CHECK(weights.value().size() == 1);
      TEST_CHECK(weights.value()[0] == 0.5f);
    }
  }
}

void tydra_blendshape_resolution_test(void) {
  std::string err;

  {
    Xform xform;
    XformOp translate_op;
    translate_op.op_type = XformOp::OpType::Translate;
    translate_op.set_value(value::double3{1.0, 2.0, 3.0});
    xform.xformOps.push_back(translate_op);

    Prim prim("XformPrim", xform);
    std::vector<std::string> prop_names;
    TEST_CHECK(tydra::GetPropertyNames(prim, &prop_names, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(CountName(prop_names, "xformOp:translate") == 1);
    TEST_CHECK(CountName(prop_names, "xformOpOrder") == 1);
  }

  {
    Stage stage;

    BlendShape shape_a;
    shape_a.name = "ShapeA";
    Prim shape_a_prim("ShapeA", shape_a);

    BlendShape shape_b;
    shape_b.name = "ShapeB";
    Prim shape_b_prim("ShapeB", shape_b);

    GeomMesh mesh;
    mesh.points = Animatable<std::vector<value::point3f>>(
        std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f}});
    mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{3});
    mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{0, 1, 2});
    mesh.blendShapes = std::vector<value::token>{value::token("shapeA"),
                                                 value::token("shapeB")};

    Relationship targets_rel;
    targets_rel.set(std::vector<Path>{Path("/ShapeA", ""), Path("/ShapeB", "")});
    mesh.blendShapeTargets = targets_rel;

    Prim mesh_prim("MeshPrim", mesh);

    stage.add_root_prim(std::move(mesh_prim));
    stage.add_root_prim(std::move(shape_a_prim));
    stage.add_root_prim(std::move(shape_b_prim));

    auto mesh_result = stage.GetPrimAtPath(Path("/MeshPrim", ""));
    TEST_CHECK(mesh_result.has_value());
    if (mesh_result) {
      err.clear();
      auto blendshapes = tydra::GetBlendShapes(stage, *mesh_result.value(), &err);
      TEST_CHECK(err.empty());
      TEST_CHECK(blendshapes.size() == 2);
      if (blendshapes.size() == 2) {
        TEST_CHECK(blendshapes[0].first == "shapeA");
        TEST_CHECK(blendshapes[1].first == "shapeB");
        TEST_CHECK(blendshapes[0].second != nullptr);
        TEST_CHECK(blendshapes[1].second != nullptr);
      }
    }
  }

  {
    Stage stage;

    GeomMesh mesh;
    mesh.points = Animatable<std::vector<value::point3f>>(
        std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f}});
    mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{3});
    mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{0, 1, 2});
    mesh.blendShapes = std::vector<value::token>{value::token("shapeA")};

    Relationship invalid_targets_rel;
    invalid_targets_rel.set(Path("/MissingBlendShape", ""));
    mesh.blendShapeTargets = invalid_targets_rel;

    Prim mesh_prim("MeshPrim", mesh);
    stage.add_root_prim(std::move(mesh_prim));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("Failed to get BlendShapes prims") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MissingBlendShape") !=
               std::string::npos);
  }
}

void tydra_material_binding_validation_test(void) {
  auto make_mesh = []() {
    GeomMesh mesh;
    mesh.points = Animatable<std::vector<value::point3f>>(
        std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f}});
    mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{3});
    mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{0, 1, 2});
    return mesh;
  };

  auto make_mtlx_openpbr_stage = [&](const Path &nodegraph_output_target) {
    Stage stage;

    Material material;
    material.surface.set(Path("/MaterialPrim/OpenPBRShader",
                              "outputs:surface"));

    OpenPBRSurface openpbr_surface;
    openpbr_surface.surface.set_authored(true);
    openpbr_surface.base_color.set_connection(
        Path("/MaterialPrim/NodeGraphs", "outputs:out"));
    openpbr_surface.base_color.set_value_empty();

    Shader openpbr_shader;
    openpbr_shader.info_id = kOpenPBRSurface;
    openpbr_shader.value = openpbr_surface;

    NodeGraph nodegraph;
    nodegraph.props["outputs:out.connect"] = Property(
        nodegraph_output_target, value::TypeTraits<value::color3f>::type_name());

    Shader image_shader;
    image_shader.info_id = "ND_image_color3";
    image_shader.value = ShaderNode{};

    Prim nodegraph_prim("NodeGraphs", nodegraph);
    TEST_CHECK(nodegraph_prim.add_child(Prim("Image", image_shader)));

    Prim material_prim("MaterialPrim", material);
    TEST_CHECK(material_prim.add_child(Prim("OpenPBRShader", openpbr_shader)));
    TEST_CHECK(material_prim.add_child(std::move(nodegraph_prim)));

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(std::move(material_prim)));

    return stage;
  };

  {
    Stage stage;

    Xform not_material;
    Prim not_material_prim("NotMaterial", not_material);

    GeomMesh mesh = make_mesh();

    Relationship invalid_material_rel;
    invalid_material_rel.set(Path("/NotMaterial", ""));
    mesh.set_materialBinding(invalid_material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(std::move(not_material_prim)));

    tydra::RenderSceneConverter converter;
    Path material_path;
    const Material *material{nullptr};
    std::string err;

    TEST_CHECK(!converter.GetBoundMaterialCached(stage, Path("/MeshPrim", ""), "",
                                                 &material_path, &material,
                                                 &err));
    TEST_CHECK(err.find("/NotMaterial") != std::string::npos);
    TEST_CHECK(err.find("not a Material Prim") != std::string::npos);

    std::string cached_err;
    material_path = Path();
    material = nullptr;
    TEST_CHECK(!converter.GetBoundMaterialCached(stage, Path("/MeshPrim", ""), "",
                                                 &material_path, &material,
                                                 &cached_err));
    TEST_CHECK(cached_err.find("/NotMaterial") != std::string::npos);
    TEST_CHECK(cached_err.find("not a Material Prim") != std::string::npos);

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("/NotMaterial") != std::string::npos);
    TEST_CHECK(converter.GetError().find("not a Material Prim") !=
               std::string::npos);
  }

  {
    Stage stage;

    Material invalid_material;
    Prim material_prim("MaterialPrim", invalid_material);

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(std::move(material_prim)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("/MaterialPrim") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("outputs:surface isn't authored") !=
               std::string::npos);
  }

  {
    Stage stage;

    Material material;
    material.surface.set(Path("/PreviewSurface", "outputs:surface"));

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);
    preview_surface.diffuseColor.set_connection(Path("/Tex", "outputs:rgb"));
    preview_surface.diffuseColor.set_value_empty();

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);

    Shader tex_shader;
    tex_shader.info_id = kUsdUVTexture;
    tex_shader.value = uv_texture;

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(Prim("MaterialPrim", material)));
    TEST_CHECK(stage.add_root_prim(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(stage.add_root_prim(Prim("Tex", tex_shader)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("/Tex") != std::string::npos);
    TEST_CHECK(converter.GetError().find("`asset:file` is not authored") !=
               std::string::npos);
  }

  {
    Stage stage;

    Material material;
    material.surface.set(Path("/PreviewSurface", "outputs:surface"));

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);
    preview_surface.diffuseColor.set_connection(Path("/Tex", "outputs:rgb"));
    preview_surface.diffuseColor.set_value_empty();

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);
    uv_texture.file.set_value(
        Animatable<value::AssetPath>(value::AssetPath("dummy.png")));
    uv_texture.st.set_value(Animatable<value::texcoord2f>());

    Shader tex_shader;
    tex_shader.info_id = kUsdUVTexture;
    tex_shader.value = uv_texture;

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(Prim("MaterialPrim", material)));
    TEST_CHECK(stage.add_root_prim(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(stage.add_root_prim(Prim("Tex", tex_shader)));

    tydra::RenderSceneConverterEnv env(stage);
    env.scene_config.load_texture_assets = false;
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "Failed to get fallback `st` texcoord attribute") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/Tex") != std::string::npos);
  }

  {
    Stage stage = make_mtlx_openpbr_stage(
        Path("/MaterialPrim/NodeGraphs/Image", "outputs:out"));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("MaterialX image node") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MaterialPrim/NodeGraphs/Image") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("has no file input") !=
               std::string::npos);
  }

  {
    Stage stage = make_mtlx_openpbr_stage(
        Path("/MaterialPrim/NodeGraphs/Missing", "outputs:out"));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "Failed to find MaterialX texture for base_color") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MaterialPrim/NodeGraphs/Missing") !=
               std::string::npos);
  }

  {
    Stage stage;

    Material material;
    material.surface.set(Path("/MaterialPrim/PreviewSurface",
                              "outputs:surface"));
    material.materialXConfig = MaterialXConfigAPI{};

    Relationship mtlx_surface_rel;
    mtlx_surface_rel.set(Path("/MaterialPrim/MissingMtlx", ""));
    material.props["outputs:mtlx:surface.connect"] = Property(
        mtlx_surface_rel);

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    Prim material_prim("MaterialPrim", material);
    TEST_CHECK(material_prim.add_child(Prim("PreviewSurface", preview_shader)));

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(std::move(material_prim)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("MaterialX shader path") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MaterialPrim/MissingMtlx") !=
               std::string::npos);
  }

  {
    Stage stage;

    Material material;
    material.surface.set(Path("/MaterialPrim/PreviewSurface",
                              "outputs:surface"));
    material.materialXConfig = MaterialXConfigAPI{};

    Relationship mtlx_surface_rel;
    mtlx_surface_rel.set(Path("/MaterialPrim/BadMtlx", ""));
    material.props["outputs:mtlx:surface.connect"] = Property(
        mtlx_surface_rel);

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    Shader bad_mtlx_shader;
    bad_mtlx_shader.info_id = kUsdPreviewSurface;
    bad_mtlx_shader.value = preview_surface;

    Prim material_prim("MaterialPrim", material);
    TEST_CHECK(material_prim.add_child(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(material_prim.add_child(Prim("BadMtlx", bad_mtlx_shader)));

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(std::move(material_prim)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "ND_open_pbr_surface_surfaceshader") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MaterialPrim/BadMtlx") !=
               std::string::npos);
  }

  {
    Stage stage;

    Material material;
    material.surface.set(Path("/MaterialPrim/PreviewSurface",
                              "outputs:surface"));
    material.materialXConfig = MaterialXConfigAPI{};

    Relationship mtlx_surface_rel;
    mtlx_surface_rel.set(Path("/MaterialPrim/MtlxSurface", ""));
    material.props["outputs:mtlx:surface.connect"] = Property(
        mtlx_surface_rel);

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    MtlxOpenPBRSurface mtlx_surface;
    mtlx_surface.surface.set_authored(true);
    mtlx_surface.base_color.set_connection(
        Path("/MaterialPrim/NodeGraphs", "outputs:out"));
    mtlx_surface.base_color.set_value_empty();

    Shader mtlx_surface_shader;
    mtlx_surface_shader.info_id = kNdOpenPbrSurfaceSurfaceshader;
    mtlx_surface_shader.value = mtlx_surface;

    NodeGraph nodegraph;
    nodegraph.props["outputs:out.connect"] = Property(
        Path("/MaterialPrim/NodeGraphs/Image", "outputs:out"),
        value::TypeTraits<value::color3f>::type_name());

    Shader image_shader;
    image_shader.info_id = "ND_image_color3";
    image_shader.value = ShaderNode{};

    Prim nodegraph_prim("NodeGraphs", nodegraph);
    TEST_CHECK(nodegraph_prim.add_child(Prim("Image", image_shader)));

    Prim material_prim("MaterialPrim", material);
    TEST_CHECK(material_prim.add_child(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(
        material_prim.add_child(Prim("MtlxSurface", mtlx_surface_shader)));
    TEST_CHECK(material_prim.add_child(std::move(nodegraph_prim)));

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(std::move(material_prim)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "Failed to convert MtlxOpenPBRSurface") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MaterialPrim/MtlxSurface") !=
               std::string::npos);
  }
}

void tydra_progress_cancellation_test(void) {
  Stage stage;

  GeomMesh mesh;
  mesh.points = Animatable<std::vector<value::point3f>>(
      std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                  {1.0f, 0.0f, 0.0f},
                                  {0.0f, 1.0f, 0.0f}});
  mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{3});
  mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{0, 1, 2});

  Prim mesh_prim("MeshPrim", mesh);
  TEST_CHECK(stage.add_root_prim(std::move(mesh_prim)));

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;
  CancelOnMeshProgressState state;

  converter.SetDetailedProgressCallback(CancelOnFirstMeshProgress, &state);

  TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
  TEST_CHECK(state.mesh_progress_calls == 1);
  TEST_CHECK(converter.GetError().find("Conversion cancelled by user") !=
             std::string::npos);
}

void tydra_texture_loader_policy_test(void) {
  auto make_mesh = []() {
    GeomMesh mesh;
    mesh.points = Animatable<std::vector<value::point3f>>(
        std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f}});
    mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{3});
    mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{0, 1, 2});
    return mesh;
  };

  auto make_stage_with_texture = [&](const UsdUVTexture &uv_texture) {
    Stage stage;

    Material material;
    material.surface.set(Path("/PreviewSurface", "outputs:surface"));

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);
    preview_surface.diffuseColor.set_connection(Path("/Tex", "outputs:rgb"));
    preview_surface.diffuseColor.set_value_empty();

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    Shader tex_shader;
    tex_shader.info_id = kUsdUVTexture;
    tex_shader.value = uv_texture;

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(Prim("MaterialPrim", material)));
    TEST_CHECK(stage.add_root_prim(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(stage.add_root_prim(Prim("Tex", tex_shader)));

    return stage;
  };

  {
    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);
    uv_texture.file.set_value(
        Animatable<value::AssetPath>(value::AssetPath("missing.png")));

    Stage stage = make_stage_with_texture(uv_texture);

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        FailingTextureImageLoader;
    env.material_config.allow_texture_load_failure = true;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(scene.images.size() == 1);
    if (scene.images.size() == 1) {
      TEST_CHECK(scene.images[0].asset_identifier == "missing.png");
      TEST_CHECK(!scene.images[0].decoded);
    }
    TEST_CHECK(converter.GetWarning().find("missing.png") !=
               std::string::npos);
    TEST_CHECK(converter.GetWarning().find("synthetic texture loader failure") !=
               std::string::npos);
  }

  {
    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);
    uv_texture.file.set_value(
        Animatable<value::AssetPath>(value::AssetPath("missing.png")));

    Stage stage = make_stage_with_texture(uv_texture);

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        FailingTextureImageLoader;
    env.material_config.allow_texture_load_failure = false;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("missing.png") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("synthetic texture loader failure") !=
               std::string::npos);
  }

  {
    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);
    uv_texture.file.set_value(
        Animatable<value::AssetPath>(value::AssetPath("mask.png")));
    uv_texture.sourceColorSpace.set_value(
        Animatable<UsdUVTexture::SourceColorSpace>(
            UsdUVTexture::SourceColorSpace::Auto));

    Stage stage = make_stage_with_texture(uv_texture);

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        SingleChannelTextureImageLoader;
    env.material_config.allow_texture_load_failure = false;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(scene.images.size() == 1);
    if (scene.images.size() == 1) {
      TEST_CHECK(scene.images[0].decoded);
      TEST_CHECK(scene.images[0].usdColorSpace == tydra::ColorSpace::Raw);
      TEST_CHECK(scene.images[0].asset_identifier == "mask.png");
    }
    TEST_CHECK(converter.GetWarning().find("Infer colorSpace failed") ==
               std::string::npos);
  }

  {
    Stage stage;

    Material material;
    material.surface.set(Path("/PreviewSurface", "outputs:surface"));

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);
    preview_surface.diffuseColor.set_connection(Path("/Tex", "outputs:rgb"));
    preview_surface.diffuseColor.set_value_empty();

    Attribute source_color_space_attr;
    source_color_space_attr.set_type_name(value::kToken);
    source_color_space_attr.variability() = Variability::Uniform;
    primvar::PrimVar source_color_space_pvar;
    source_color_space_pvar.set_value(value::token("raw"));
    source_color_space_attr.set_var(std::move(source_color_space_pvar));
    preview_surface.props["inputs:sourceColorSpace"] =
        Property(std::move(source_color_space_attr), false);

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);
    uv_texture.file.set_value(
        Animatable<value::AssetPath>(value::AssetPath("mask.png")));
    uv_texture.sourceColorSpace.set_connection(
        Path("/PreviewSurface", "inputs:sourceColorSpace"));
    uv_texture.sourceColorSpace.set_value_empty();

    Shader tex_shader;
    tex_shader.info_id = kUsdUVTexture;
    tex_shader.value = uv_texture;

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(Prim("MaterialPrim", material)));
    TEST_CHECK(stage.add_root_prim(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(stage.add_root_prim(Prim("Tex", tex_shader)));

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        SingleChannelTextureImageLoader;
    env.material_config.allow_texture_load_failure = false;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(scene.images.size() == 1);
    if (scene.images.size() == 1) {
      TEST_CHECK(scene.images[0].decoded);
      TEST_CHECK(scene.images[0].usdColorSpace == tydra::ColorSpace::Raw);
      TEST_CHECK(scene.images[0].asset_identifier == "mask.png");
    }
    TEST_CHECK(converter.GetWarning().find("inputs:sourceColorSpace") ==
               std::string::npos);
  }

  {
    Stage stage;

    auto make_uniform_property = [](const value::Value &val,
                                    const std::string &type_name) {
      Attribute attr;
      attr.set_type_name(type_name);
      attr.variability() = Variability::Uniform;
      primvar::PrimVar pvar;
      pvar.set_value(val);
      attr.set_var(std::move(pvar));
      return Property(std::move(attr), false);
    };

    Material material;
    material.surface.set(Path("/PreviewSurface", "outputs:surface"));

    UsdPreviewSurface preview_surface;
    preview_surface.outputsSurface.set_authored(true);
    preview_surface.diffuseColor.set_connection(Path("/Tex", "outputs:rgb"));
    preview_surface.diffuseColor.set_value_empty();
    preview_surface.useSpecularWorkflow.set_connection(
        Path("/PreviewSurface", "inputs:useSpecularWorkflowDriver"));
    preview_surface.useSpecularWorkflow.set_value_empty();
    preview_surface.props["inputs:sourceColorSpace"] = make_uniform_property(
        value::Value(value::token("raw")), value::kToken);
    preview_surface.props["inputs:fileDriver"] = make_uniform_property(
        value::Value(value::AssetPath("mask.png")),
        value::TypeTraits<value::AssetPath>::type_name());
    preview_surface.props["inputs:wrapSDriver"] = make_uniform_property(
        value::Value(value::token("mirror")), value::kToken);
    preview_surface.props["inputs:wrapTDriver"] = make_uniform_property(
        value::Value(value::token("repeat")), value::kToken);
    preview_surface.props["inputs:useSpecularWorkflowDriver"] =
        make_uniform_property(value::Value(int32_t(1)),
                              value::TypeTraits<int>::type_name());

    Shader preview_shader;
    preview_shader.info_id = kUsdPreviewSurface;
    preview_shader.value = preview_surface;

    UsdUVTexture uv_texture;
    uv_texture.outputsRGB.set_authored(true);
    uv_texture.file.set_connection(Path("/PreviewSurface", "inputs:fileDriver"));
    uv_texture.sourceColorSpace.set_connection(
        Path("/PreviewSurface", "inputs:sourceColorSpace"));
    uv_texture.sourceColorSpace.set_value_empty();
    uv_texture.wrapS.set_connection(Path("/PreviewSurface", "inputs:wrapSDriver"));
    uv_texture.wrapS.set_value_empty();
    uv_texture.wrapT.set_connection(Path("/PreviewSurface", "inputs:wrapTDriver"));
    uv_texture.wrapT.set_value_empty();

    Shader tex_shader;
    tex_shader.info_id = kUsdUVTexture;
    tex_shader.value = uv_texture;

    GeomMesh mesh = make_mesh();
    Relationship material_rel;
    material_rel.set(Path("/MaterialPrim", ""));
    mesh.set_materialBinding(material_rel);

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));
    TEST_CHECK(stage.add_root_prim(Prim("MaterialPrim", material)));
    TEST_CHECK(stage.add_root_prim(Prim("PreviewSurface", preview_shader)));
    TEST_CHECK(stage.add_root_prim(Prim("Tex", tex_shader)));

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        SingleChannelTextureImageLoader;
    env.material_config.allow_texture_load_failure = false;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(scene.materials.size() == 1);
    TEST_CHECK(scene.textures.size() == 1);
    TEST_CHECK(scene.images.size() == 1);
    if (scene.materials.size() == 1) {
      TEST_CHECK(scene.materials[0].surfaceShader.has_value());
      if (scene.materials[0].surfaceShader.has_value()) {
        TEST_CHECK(scene.materials[0].surfaceShader->useSpecularWorkflow);
      }
    }
    if (scene.textures.size() == 1) {
      TEST_CHECK(scene.textures[0].wrapS == tydra::UVTexture::WrapMode::MIRROR);
      TEST_CHECK(scene.textures[0].wrapT == tydra::UVTexture::WrapMode::REPEAT);
    }
    if (scene.images.size() == 1) {
      TEST_CHECK(scene.images[0].asset_identifier == "mask.png");
      TEST_CHECK(scene.images[0].usdColorSpace == tydra::ColorSpace::Raw);
    }
    TEST_CHECK(converter.GetWarning().find("Failed to resolve") ==
               std::string::npos);
  }
}

void tydra_envmap_loader_policy_test(void) {
  auto make_stage_with_dome = []() {
    Stage stage;
    DomeLight light;
    light.name = "DomeLightPrim";
    light.file.set_value(
        Animatable<value::AssetPath>(value::AssetPath("missing.exr")));
    TEST_CHECK(stage.add_root_prim(Prim("DomeLightPrim", light)));
    return stage;
  };

  {
    Stage stage = make_stage_with_dome();

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        FailingTextureImageLoader;
    env.material_config.allow_texture_load_failure = true;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(scene.lights.size() == 1);
    if (scene.lights.size() == 1) {
      TEST_CHECK(scene.lights[0].type == tydra::RenderLight::Type::Dome);
      TEST_CHECK(scene.lights[0].envmap_texture_id == -1);
      TEST_CHECK(scene.lights[0].textureFile == "missing.exr");
    }
    TEST_CHECK(converter.GetWarning().find("missing.exr") !=
               std::string::npos);
    TEST_CHECK(converter.GetWarning().find(
                   "synthetic texture loader failure") !=
               std::string::npos);
  }

  {
    Stage stage = make_stage_with_dome();

    tydra::RenderSceneConverterEnv env(stage);
    env.material_config.texture_image_loader_function =
        FailingTextureImageLoader;
    env.material_config.allow_texture_load_failure = false;

    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("missing.exr") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find(
                   "Failed to load envmap texture") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find(
                   "synthetic texture loader failure") !=
               std::string::npos);
  }
}

void tydra_geometry_light_validation_test(void) {
  auto make_mesh = []() {
    GeomMesh mesh;
    mesh.points = Animatable<std::vector<value::point3f>>(
        std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f}});
    mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{3});
    mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{0, 1, 2});
    return mesh;
  };

  {
    Stage stage;
    GeometryLight light;
    light.name = "GeomLight";

    TEST_CHECK(stage.add_root_prim(Prim("GeomLight", light)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("missing geometry relationship") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/GeomLight") != std::string::npos);
  }

  {
    Stage stage;
    GeometryLight light;
    light.name = "GeomLight";

    Relationship empty_geometry_rel;
    empty_geometry_rel.set(std::vector<Path>{});
    light.geometry = empty_geometry_rel;

    TEST_CHECK(stage.add_root_prim(Prim("GeomLight", light)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "must have exactly one geometry target") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/GeomLight") != std::string::npos);
  }

  {
    Stage stage;
    GeometryLight light;
    light.name = "GeomLight";

    Relationship multi_geometry_rel;
    multi_geometry_rel.set(
        std::vector<Path>{Path("/MeshA", ""), Path("/MeshB", "")});
    light.geometry = multi_geometry_rel;

    TEST_CHECK(stage.add_root_prim(Prim("GeomLight", light)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "must have exactly one geometry target") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/GeomLight") != std::string::npos);
  }

  {
    Stage stage;
    GeometryLight light;
    light.name = "GeomLight";

    Relationship missing_geometry_rel;
    missing_geometry_rel.set(Path("/MissingMesh", ""));
    light.geometry = missing_geometry_rel;

    TEST_CHECK(stage.add_root_prim(Prim("GeomLight", light)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("references missing geometry target") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MissingMesh") !=
               std::string::npos);
  }

  {
    Stage stage;
    GeometryLight light;
    light.name = "GeomLight";

    Relationship geometry_rel;
    geometry_rel.set(Path("/MeshPrim", ""));
    light.geometry = geometry_rel;

    TEST_CHECK(stage.add_root_prim(Prim("GeomLight", light)));
    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", make_mesh())));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(scene.lights.size() == 1);
    if (scene.lights.size() == 1) {
      TEST_CHECK(scene.lights[0].type == tydra::RenderLight::Type::Geometry);
      TEST_CHECK(scene.lights[0].geometry_mesh_id == -1);
    }
  }
}

void tydra_mesh_fallback_policy_test(void) {
  {
    tydra::RenderSceneConverter converter;

    tydra::RenderMesh dst;
    dst.abs_path = "/MeshA";
    dst.points = std::vector<tydra::vec3>{{0.0f, 0.0f, 0.0f}};
    dst.usdFaceVertexIndices = std::vector<uint32_t>{0};
    dst.usdFaceVertexCounts = std::vector<uint32_t>{1};

    tydra::VertexAttribute dst_tc;
    dst_tc.format = tydra::VertexAttributeFormat::Vec2;
    dst_tc.data.resize(sizeof(float) * 2);
    dst.texcoords.emplace(0, dst_tc);

    tydra::RenderMesh src;
    src.abs_path = "/MeshB";
    src.points = std::vector<tydra::vec3>{{1.0f, 0.0f, 0.0f}};
    src.usdFaceVertexIndices = std::vector<uint32_t>{0};
    src.usdFaceVertexCounts = std::vector<uint32_t>{1};

    tydra::VertexAttribute src_tc;
    src_tc.format = tydra::VertexAttributeFormat::Vec3;
    src_tc.data.resize(sizeof(float) * 3);
    src.texcoords.emplace(0, src_tc);

    std::string merge_err;
    TEST_CHECK(!converter.MergeMeshData(
        src, value::matrix4d::identity(), dst, &merge_err));
    TEST_CHECK(merge_err.find("Cannot merge texcoords slot 0") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().empty());
  }

  {
    Stage stage;
    tydra::RenderSceneConverterEnv env(stage);
    env.scene_config.merge_meshes = true;
    env.scene_config.merge_meshes_bake_transform = true;

    tydra::RenderSceneConverter converter;

    tydra::RenderMesh mesh_a;
    mesh_a.prim_name = "MeshA";
    mesh_a.abs_path = "/MeshA";
    mesh_a.material_id = 7;
    mesh_a.points = {{0.0f, 0.0f, 0.0f},
                     {1.0f, 0.0f, 0.0f},
                     {0.0f, 1.0f, 0.0f}};
    mesh_a.usdFaceVertexIndices = {0, 1, 2};
    mesh_a.usdFaceVertexCounts = {3};
    mesh_a.texcoords[0].format = tydra::VertexAttributeFormat::Vec2;
    mesh_a.texcoords[0].set_buffer(
        reinterpret_cast<const uint8_t *>(kTexcoordsVec2.data()),
        kTexcoordsVec2.size() * sizeof(value::texcoord2f));
    mesh_a.texcoords[0].variability = tydra::VertexVariability::Vertex;

    tydra::RenderMesh mesh_b = mesh_a;
    mesh_b.prim_name = "MeshB";
    mesh_b.abs_path = "/MeshB";
    mesh_b.texcoords[0].format = tydra::VertexAttributeFormat::Vec3;
    mesh_b.texcoords[0].set_buffer(
        reinterpret_cast<const uint8_t *>(kTexcoordsVec3.data()),
        kTexcoordsVec3.size() * sizeof(value::float3));
    mesh_b.texcoords[0].variability = tydra::VertexVariability::Vertex;

    converter.meshes.push_back(mesh_a);
    converter.meshes.push_back(mesh_b);

    tydra::Node node_a;
    node_a.prim_name = "MeshA";
    node_a.abs_path = "/MeshA";
    node_a.category = tydra::NodeCategory::Geom;
    node_a.nodeType = tydra::NodeType::Mesh;
    node_a.id = 0;
    node_a.local_matrix = value::matrix4d::identity();
    node_a.global_matrix = value::matrix4d::identity();

    tydra::Node node_b = node_a;
    node_b.prim_name = "MeshB";
    node_b.abs_path = "/MeshB";
    node_b.id = 1;

    converter.root_nodes.push_back(node_a);
    converter.root_nodes.push_back(node_b);

    TEST_CHECK(converter.MergeMeshesImpl(env));
    TEST_CHECK(converter.GetError().empty());
    TEST_CHECK(converter.GetInfo().find("Skipping mesh merge for /MeshB") !=
               std::string::npos);
    TEST_CHECK(converter.GetInfo().find("Cannot merge texcoords slot 0") !=
               std::string::npos);
  }
}

void tydra_skel_animation_validation_test(void) {
  {
    SkelAnimation anim;
    anim.name = "Anim";
    anim.joints = std::vector<value::token>{value::token("Root")};
    anim.translations = Animatable<std::vector<value::float3>>(
        std::vector<value::float3>{value::float3{1.0f, 2.0f, 3.0f}});
    // Invalid per USD skel rules: translation is authored, rotation/scale are not.

    Stage stage;
    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderSceneConverter converter;
    tydra::SkelHierarchy hierarchy;
    hierarchy.root_node.joint_name = "Root";
    hierarchy.root_node.joint_path = "Root";
    hierarchy.root_node.joint_id = 0;
    converter.skeletons.push_back(hierarchy);
    tydra::AnimationClip clip;

    TEST_CHECK(!converter.ConvertSkelAnimation(env, Path("/Anim", ""), anim, 0,
                                               &clip));
    TEST_CHECK(converter.GetError().find("must be all authored") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/Anim") != std::string::npos);
  }

  {
    Stage stage;

    Skeleton invalid_skeleton;
    invalid_skeleton.name = "BrokenSkeleton";
    invalid_skeleton.joints = std::vector<value::token>{value::token("Root")};
    invalid_skeleton.jointNames =
        std::vector<value::token>{value::token("Root")};
    invalid_skeleton.bindTransforms =
        std::vector<value::matrix4d>{};  // Authored but invalid size.
    invalid_skeleton.restTransforms = std::vector<value::matrix4d>{
        value::matrix4d::identity()};

    TEST_CHECK(stage.add_root_prim(Prim("BrokenSkeleton", invalid_skeleton)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find("Failed to convert standalone skeleton") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("bindTransforms.size") !=
               std::string::npos);
  }
}

void tydra_skin_binding_validation_test(void) {
  auto make_skinned_mesh = []() {
    GeomMesh mesh;
    mesh.points = Animatable<std::vector<value::point3f>>(
        std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f}});
    mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{3});
    mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
        std::vector<int32_t>{0, 1, 2});

    GeomPrimvar joint_indices;
    joint_indices.set_name("skel:jointIndices");
    joint_indices.set_value(std::vector<int>{0, 0, 0});
    joint_indices.set_interpolation(Interpolation::Vertex);
    joint_indices.set_elementSize(1);

    GeomPrimvar joint_weights;
    joint_weights.set_name("skel:jointWeights");
    joint_weights.set_value(std::vector<float>{1.0f, 1.0f, 1.0f});
    joint_weights.set_interpolation(Interpolation::Vertex);
    joint_weights.set_elementSize(1);

    std::string err;
    TEST_CHECK(mesh.set_primvar(joint_indices, &err));
    TEST_CHECK(err.empty());
    TEST_CHECK(mesh.set_primvar(joint_weights, &err));
    TEST_CHECK(err.empty());

    return mesh;
  };

  {
    Stage stage;
    GeomMesh mesh = make_skinned_mesh();

    Relationship invalid_skeleton_rel;
    invalid_skeleton_rel.set(std::vector<Path>{});
    mesh.skeleton = invalid_skeleton_rel;

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "`skel:skeleton` must have exactly one target") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MeshPrim") != std::string::npos);
  }

  {
    Stage stage;
    GeomMesh mesh = make_skinned_mesh();

    Relationship invalid_skeleton_rel;
    invalid_skeleton_rel.set(
        std::vector<Path>{Path("/SkeletonA", ""), Path("/SkeletonB", "")});
    mesh.skeleton = invalid_skeleton_rel;

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "`skel:skeleton` must have exactly one target") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MeshPrim") != std::string::npos);
  }

  {
    Stage stage;
    GeomMesh mesh = make_skinned_mesh();

    TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));

    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderScene scene;
    tydra::RenderSceneConverter converter;

    TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
    TEST_CHECK(converter.GetError().find(
                   "Mesh has skinning data but no skeleton found") !=
               std::string::npos);
    TEST_CHECK(converter.GetError().find("/MeshPrim") != std::string::npos);
  }
}
