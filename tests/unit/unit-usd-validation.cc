// SPDX-License-Identifier: Apache 2.0

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usd-validation.h"

#include <cstring>
#include <string>

#include "tinyusdz.hh"
#include "usd-validation.hh"

using namespace tinyusdz;

namespace {

static bool parse_layer(const char *usda, Layer *layer, std::string *warn,
                        std::string *err) {
  return LoadLayerFromMemory(reinterpret_cast<const uint8_t *>(usda),
                             std::strlen(usda), "validation-test.usda", layer,
                             warn, err);
}

static bool HasRule(const USDValidationResult &result,
                    const std::string &rule_id) {
  for (const auto &issue : result.issues) {
    if (issue.rule_id == rule_id) {
      return true;
    }
  }
  return false;
}

static ValidationOptions AllGroups() {
  ValidationOptions opts;
  opts.core = true;
  opts.geom = true;
  opts.shade = true;
  return opts;
}

}  // namespace

void usd_validation_valid_core_schema_test(void) {
  const char *usda = R"(#usda 1.0
(
    defaultPrim = "Root"
    colorConfiguration = @ocio://default@
)

def Xform "Root" (
    prepend apiSchemas = ["ColorSpaceAPI", "CollectionAPI:lookset"]
)
{
    uniform token colorSpace:name = "lin_rec709_scene"
    uniform token collection:lookset:expansionRule = "expandPrims"
    uniform bool collection:lookset:includeRoot = 1
    rel collection:lookset:includes = [</Root>]
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
  }
  TEST_CHECK(ok);
  if (!ok) {
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  if (!result.ok()) {
    TEST_MSG("%s", FormatValidationResult(result).c_str());
  }
  TEST_CHECK(result.ok());
}

void usd_validation_invalid_default_prim_test(void) {
  const char *usda = R"(#usda 1.0
(
    defaultPrim = "Missing"
)

def Scope "Root"
{
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!result.ok());
  TEST_CHECK(HasRule(result, "core.layer.defaultPrim"));
}

void usd_validation_invalid_collection_rule_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "Root" (
    prepend apiSchemas = ["CollectionAPI:lookset"]
)
{
    uniform token collection:lookset:expansionRule = "bogusRule"
    rel collection:lookset:includes = [</Root>]
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!result.ok());
  TEST_CHECK(HasRule(result, "core.schema.CollectionAPI.expansionRule"));
}

void usd_validation_invalid_color_space_test(void) {
  const char *usda = R"(#usda 1.0

def Scope "Root" (
    prepend apiSchemas = ["ColorSpaceAPI"]
)
{
    uniform token colorSpace:name = "not_a_real_color_space"
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!result.ok());
  TEST_CHECK(HasRule(result, "core.schema.ColorSpaceAPI.name"));
}

void usd_validation_layer_metadata_test(void) {
  const char *usda = R"(#usda 1.0
(
    metersPerUnit = 0
    startTimeCode = 10
    endTimeCode = 0
)

def Xform "Root"
{
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!result.ok());
  TEST_CHECK(HasRule(result, "core.layer.metersPerUnit"));
  TEST_CHECK(HasRule(result, "core.layer.timeCodeRange"));
}

void usd_validation_xformop_order_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "Root"
{
    float3 xformOp:scale = (1, 1, 1)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!result.ok());
  TEST_CHECK(HasRule(result, "core.xformOp.order"));
}

// A prim that carries a composition arc may inherit its xformOps; the rule must
// not fire (no false positive on uncomposed layers).
void usd_validation_xformop_order_gated_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "Root" (
    prepend references = @./base.usda@</Base>
)
{
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult result = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!HasRule(result, "core.xformOp.order"));
}

void usd_validation_nested_gprim_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "World"
{
    def Mesh "outer"
    {
        def Cube "inner"
        {
        }
    }
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  // Off by default (core only).
  const USDValidationResult core_only = ValidateLayerAgainstAOUSDCore(layer);
  TEST_CHECK(!HasRule(core_only, "geom.encapsulation.nestedGprim"));

  // Enabled with the geom group.
  const USDValidationResult all =
      ValidateLayerAgainstAOUSDCore(layer, AllGroups());
  TEST_CHECK(HasRule(all, "geom.encapsulation.nestedGprim"));
}

void usd_validation_shader_encapsulation_test(void) {
  const char *usda = R"(#usda 1.0

def Xform "World"
{
    def Shader "shader"
    {
        uniform token info:id = "UsdPreviewSurface"
    }
}
)";

  Layer layer;
  std::string warn;
  std::string err;
  const bool ok = parse_layer(usda, &layer, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("parse failed: %s", err.c_str());
    return;
  }

  const USDValidationResult all =
      ValidateLayerAgainstAOUSDCore(layer, AllGroups());
  TEST_CHECK(HasRule(all, "shade.encapsulation.shaderParent"));
}

void usd_validation_material_binding_test(void) {
  // material:binding authored as an attribute -> error.
  {
    const char *usda = R"(#usda 1.0

def Xform "World"
{
    def Mesh "geom"
    {
        custom token material:binding = "/World/mat"
    }
}
)";
    Layer layer;
    std::string warn;
    std::string err;
    const bool ok = parse_layer(usda, &layer, &warn, &err);
    TEST_CHECK(ok);
    if (ok) {
      const USDValidationResult all =
          ValidateLayerAgainstAOUSDCore(layer, AllGroups());
      TEST_CHECK(HasRule(all, "shade.material.binding"));
    }
  }

  // material:binding relationship without MaterialBindingAPI -> warning.
  {
    const char *usda = R"(#usda 1.0

def Xform "World"
{
    def Mesh "geom"
    {
        rel material:binding = </World/mat>
    }
}
)";
    Layer layer;
    std::string warn;
    std::string err;
    const bool ok = parse_layer(usda, &layer, &warn, &err);
    TEST_CHECK(ok);
    if (ok) {
      const USDValidationResult all =
          ValidateLayerAgainstAOUSDCore(layer, AllGroups());
      TEST_CHECK(HasRule(all, "shade.material.bindingAPI"));
      // A well-formed relationship must not trip the "must be a relationship"
      // error.
      TEST_CHECK(!HasRule(all, "shade.material.binding"));
    }
  }

  // material:binding relationship WITH MaterialBindingAPI -> no warning.
  // (Verifies known API schemas surface through CollectAppliedSchemas.)
  {
    const char *usda = R"(#usda 1.0

def Xform "World"
{
    def Mesh "geom" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/mat>
    }
}
)";
    Layer layer;
    std::string warn;
    std::string err;
    const bool ok = parse_layer(usda, &layer, &warn, &err);
    TEST_CHECK(ok);
    if (ok) {
      const USDValidationResult all =
          ValidateLayerAgainstAOUSDCore(layer, AllGroups());
      TEST_CHECK(!HasRule(all, "shade.material.bindingAPI"));
      TEST_CHECK(!HasRule(all, "shade.material.binding"));
    }
  }
}

void usd_validation_preview_surface_test(void) {
  // Off-schema UsdPreviewSurface inputs: wrong type + unknown name.
  {
    const char *usda = R"(#usda 1.0

def Material "mat"
{
    def Shader "surface"
    {
        uniform token info:id = "UsdPreviewSurface"
        float inputs:diffuseColor = 0.5
        float inputs:rooughness = 0.5
        token outputs:surface
    }
}
)";
    Layer layer;
    std::string warn;
    std::string err;
    const bool ok = parse_layer(usda, &layer, &warn, &err);
    TEST_CHECK(ok);
    if (ok) {
      const USDValidationResult all =
          ValidateLayerAgainstAOUSDCore(layer, AllGroups());
      TEST_CHECK(HasRule(all, "shade.preview.inputType"));
      TEST_CHECK(HasRule(all, "shade.preview.unknownInput"));
    }
  }

  // Schema-conformant UsdPreviewSurface: no preview warnings. Also verifies
  // role-type agreement (color3f accepted where color3f is expected, and a
  // connected input keeps its declared type).
  {
    const char *usda = R"(#usda 1.0

def Material "mat"
{
    def Shader "surface"
    {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.1, 0.2, 0.3)
        float inputs:roughness = 0.4
        float inputs:metallic = 0.0
        normal3f inputs:normal = (0, 0, 1)
        int inputs:useSpecularWorkflow = 0
        token outputs:surface
    }
}
)";
    Layer layer;
    std::string warn;
    std::string err;
    const bool ok = parse_layer(usda, &layer, &warn, &err);
    TEST_CHECK(ok);
    if (ok) {
      const USDValidationResult all =
          ValidateLayerAgainstAOUSDCore(layer, AllGroups());
      TEST_CHECK(!HasRule(all, "shade.preview.inputType"));
      TEST_CHECK(!HasRule(all, "shade.preview.unknownInput"));
    }
  }
}
