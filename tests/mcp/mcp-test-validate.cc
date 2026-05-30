#define TEST_NO_MAIN
#include "acutest.h"

#include <cstring>
#include <string>

#include "external/jsonhpp/nlohmann/json.hpp"

#include "tydra/mcp-context.hh"
#include "tydra/mcp-tools-scene.hh"
#include "tydra/mcp-tools-validate.hh"
#include "tinyusdz.hh"
#include "str-util.hh"
#include "tydra/js-script.hh"

using namespace tinyusdz::tydra::mcp;
using json = nlohmann::json;

namespace {

std::string B64(const std::string &s) {
  return tinyusdz::base64_encode(
      reinterpret_cast<const unsigned char *>(s.data()),
      static_cast<unsigned int>(s.size()));
}

bool HasRuleId(const json &result, const std::string &rule_id) {
  if (!result.contains("issues") || !result["issues"].is_array()) {
    return false;
  }
  for (const auto &issue : result["issues"]) {
    if (issue.contains("rule_id") && issue["rule_id"] == rule_id) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// usd_validate: base64 `data`, core rules detect a bad metersPerUnit
// ---------------------------------------------------------------------------
void mcp_validate_data_test(void) {
  Context ctx;
  json args, result;
  std::string err;

  const char *usda = R"(#usda 1.0
(
    metersPerUnit = 0
)

def Xform "World"
{
}
)";
  args["data"] = B64(usda);

  TEST_CHECK(UsdValidate(ctx, args, result, err));
  TEST_CHECK(result.contains("ok"));
  TEST_CHECK(result["ok"] == false);
  TEST_CHECK(result["error_count"] >= 1);
  TEST_CHECK(result["source"] == "data");
  TEST_CHECK(HasRuleId(result, "core.layer.metersPerUnit"));

  // Default run is core-only; report which groups were checked.
  TEST_CHECK(result.contains("checked_groups"));
  TEST_CHECK(result["checked_groups"].size() == 1);
  TEST_CHECK(result["checked_groups"][0] == "core");
}

// ---------------------------------------------------------------------------
// usd_validate: `groups` selection toggles geom rules
// ---------------------------------------------------------------------------
void mcp_validate_groups_test(void) {
  Context ctx;
  std::string err;

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

  // Default (core) must NOT flag nested gprims.
  {
    json args, result;
    args["data"] = B64(usda);
    TEST_CHECK(UsdValidate(ctx, args, result, err));
    TEST_CHECK(!HasRuleId(result, "geom.encapsulation.nestedGprim"));
  }

  // groups: ["all"] enables the geom group.
  {
    json args, result;
    args["data"] = B64(usda);
    args["groups"] = json::array({"all"});
    TEST_CHECK(UsdValidate(ctx, args, result, err));
    TEST_CHECK(HasRuleId(result, "geom.encapsulation.nestedGprim"));
    TEST_CHECK(result["checked_groups"].size() == 3);
  }
}

// ---------------------------------------------------------------------------
// usd_validate: no explicit input validates the current session stage
// ---------------------------------------------------------------------------
void mcp_validate_session_stage_test(void) {
  Context ctx;
  json args, result;
  std::string err;

  TEST_CHECK(StageNew(ctx, json::object(), result, err));

  args["path"] = "/World";
  args["type_name"] = "Xform";
  result = json::object();
  TEST_CHECK(PrimCreate(ctx, args, result, err));

  result = json::object();
  TEST_CHECK(UsdValidate(ctx, json::object(), result, err));
  TEST_CHECK(result["source"] == "stage");
  TEST_CHECK(result["ok"] == true);
}

// ---------------------------------------------------------------------------
// usd_validate: no input and no stage is an error
// ---------------------------------------------------------------------------
void mcp_validate_no_input_test(void) {
  Context ctx;
  json result;
  std::string err;

  TEST_CHECK(!UsdValidate(ctx, json::object(), result, err));
  TEST_CHECK(!err.empty());
}
