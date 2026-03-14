#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-tydra.h"

#include "layer.hh"
#include "prim-types.hh"
#include "tydra/attribute-eval.hh"
#include "tydra/layer-to-renderscene.hh"
#include "tydra/scene-access.hh"
#include "usdGeom.hh"

using namespace tinyusdz;

namespace {

std::vector<Path> MakeMultiTargetConnections() {
  return {Path("/ShaderA", "outputs:out"), Path("/ShaderB", "outputs:out")};
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
