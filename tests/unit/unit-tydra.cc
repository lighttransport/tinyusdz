#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-tydra.h"

#include <algorithm>

#include "layer.hh"
#include "prim-types.hh"
#include "tydra/attribute-eval.hh"
#include "tydra/layer-to-renderscene.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "usdGeom.hh"

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

struct CancelOnMeshProgressState {
  size_t mesh_progress_calls{0};
};

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
  Stage stage;

  Xform not_material;
  Prim not_material_prim("NotMaterial", not_material);

  GeomMesh mesh;
  mesh.points = Animatable<std::vector<value::point3f>>(
      std::vector<value::point3f>{{0.0f, 0.0f, 0.0f},
                                  {1.0f, 0.0f, 0.0f},
                                  {0.0f, 1.0f, 0.0f}});
  mesh.faceVertexCounts = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{3});
  mesh.faceVertexIndices = Animatable<std::vector<int32_t>>(
      std::vector<int32_t>{0, 1, 2});

  Relationship invalid_material_rel;
  invalid_material_rel.set(Path("/NotMaterial", ""));
  mesh.set_materialBinding(invalid_material_rel);

  Prim mesh_prim("MeshPrim", mesh);

  TEST_CHECK(stage.add_root_prim(std::move(mesh_prim)));
  TEST_CHECK(stage.add_root_prim(std::move(not_material_prim)));

  tydra::RenderSceneConverter converter;
  Path material_path;
  const Material *material{nullptr};
  std::string err;

  TEST_CHECK(!converter.GetBoundMaterialCached(stage, Path("/MeshPrim", ""), "",
                                               &material_path, &material, &err));
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

  Relationship invalid_skeleton_rel;
  invalid_skeleton_rel.set(std::vector<Path>{});
  mesh.skeleton = invalid_skeleton_rel;

  TEST_CHECK(stage.add_root_prim(Prim("MeshPrim", mesh)));

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  TEST_CHECK(!converter.ConvertToRenderScene(env, &scene));
  TEST_CHECK(converter.GetError().find("`skel:skeleton` has invalid definition") !=
             std::string::npos);
  TEST_CHECK(converter.GetError().find("/MeshPrim") != std::string::npos);
}
