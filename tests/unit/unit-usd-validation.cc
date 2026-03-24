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
