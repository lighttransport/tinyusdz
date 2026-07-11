// SPDX-License-Identifier: Apache-2.0

#include "next/composition/composition.hh"
#include "next/eval/attribute-eval.hh"
#include "next/reader/usda-reader.hh"
#include "next/writer/usda-writer.hh"
#include "next/writer/usdc-writer.hh"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

using namespace tinyusdz::next;

namespace {

LoadResult Parse(const std::string& body, bool strict = false) {
  LoadOptions options;
  options.parse_options.strict_aousd_conformance = strict;
  return LoadUSDAFromString("#usda 1.0\n" + body, options);
}

std::unique_ptr<Layer> TakeLayer(LoadResult* result) {
  assert(result && result->success);
  return result->stage.ReleaseRootLayer();
}

void TestUnicodeAndPaths() {
  LoadResult unicode = Parse("def Xform \"München\" {}\n", true);
  assert(unicode.success);
  assert(unicode.stage.GetPrimAtPath("/München").IsValid());

  const std::string invalid =
      "def Xform \"Root\" { rel r = </Root//Child> }\n";
  assert(Parse(invalid, false).success);
  LoadResult strict = Parse(invalid, true);
  assert(!strict.success);
  assert(strict.error_summary.find("Invalid AOUSD path") != std::string::npos);
}

void TestLosslessUnsupportedValues() {
  const std::string spline =
      "def Xform \"S\" {\n"
      "  double value.spline = { 0: 0; post linear, 10: 10, }\n"
      "}\n";
  LoadResult compat = Parse(spline, false);
  assert(compat.success);
  const std::string rewritten = WriteUSDAToString(compat.stage);
  assert(rewritten.find("value.spline") != std::string::npos);
  assert(rewritten.find("post linear") != std::string::npos);
  USDCWriteOptions strict_usdc;
  strict_usdc.crate_options.strict_aousd_conformance = true;
  std::vector<uint8_t> crate;
  USDCWriteResult crate_result =
      WriteUSDCToMemory(crate, compat.stage, strict_usdc);
  assert(!crate_result.success &&
         crate_result.error.find("cannot encode spline") != std::string::npos);
  assert(!Parse(spline, true).success);

  const std::string frame =
      "def Xform \"F\" { frame4d frame = identity }\n";
  LoadResult frame_compat = Parse(frame, false);
  assert(frame_compat.success);
  assert(WriteUSDAToString(frame_compat.stage).find(
             "frame4d frame = identity") != std::string::npos);
  assert(!Parse(frame, true).success);
}

void TestDictionaryAndRelationshipComposition() {
  LoadResult weak_result = Parse(
      "def \"Root\" (\n"
      "  customData = {\n"
      "    dictionary nested = { int a = 1 int b = 2 }\n"
      "    int weak = 3\n"
      "  }\n"
      ") { rel r = </A> }\n");
  LoadResult strong_result = Parse(
      "( subLayers = [@weak.usda@] )\n"
      "over \"Root\" (\n"
      "  customData = {\n"
      "    dictionary nested = { int a = 9 }\n"
      "    int strong = 4\n"
      "  }\n"
      ") { prepend rel r = </B> }\n");
  std::unique_ptr<Layer> weak = TakeLayer(&weak_result);
  std::unique_ptr<Layer> strong = TakeLayer(&strong_result);

  Compositor compositor;
  compositor.SetLayerLoader([&](const std::string& path, std::string*) {
    if (path.find("weak.usda") == std::string::npos)
      return std::unique_ptr<Layer>();
    return std::make_unique<Layer>(weak->Clone());
  });
  std::unique_ptr<Layer> composed = compositor.Compose(*strong, "root.usda");
  assert(composed);
  const PrimSpec* root = composed->prim_at_path("/Root");
  assert(root);
  const Dict* custom = root->meta().customData().as_dictionary();
  assert(custom && custom->find("weak") && custom->find("strong"));
  const Value* nested_value = custom->find("nested");
  const Dict* nested = nested_value ? nested_value->as_dictionary() : nullptr;
  assert(nested && nested->find("a") && nested->find("b"));
  assert(nested->find("a")->as_int() && *nested->find("a")->as_int() == 9);
  const std::vector<Path>* targets = root->relationship("r");
  assert(targets && targets->size() == 2);
  assert((*targets)[0].str() == "/B" && (*targets)[1].str() == "/A");

  // Three-site stack: weak explicit A, middle append C, strong prepend B.
  PrimSpec weak_rel("R"), middle_rel("R"), strong_rel("R"), merged("R");
  weak_rel.set_relationship_targets("r", {Path("/A")});
  middle_rel.set_relationship_targets("r", {Path("/C")});
  ArcEdit& middle_edit = middle_rel.ensure_relationship_edit("r");
  middle_edit.authored = true;
  middle_edit.is_explicit = false;
  middle_edit.appended = {"/C"};
  strong_rel.set_relationship_targets("r", {Path("/B")});
  ArcEdit& strong_edit = strong_rel.ensure_relationship_edit("r");
  strong_edit.authored = true;
  strong_edit.is_explicit = false;
  strong_edit.prepended = {"/B"};
  Compositor::CopyLocalOpinions(merged, strong_rel);
  Compositor::CopyLocalOpinions(merged, middle_rel);
  Compositor::CopyLocalOpinions(merged, weak_rel);
  const std::vector<Path>* stacked = merged.relationship("r");
  assert(stacked && stacked->size() == 3);
  assert((*stacked)[0].str() == "/B" && (*stacked)[1].str() == "/A" &&
         (*stacked)[2].str() == "/C");
}

void TestNamespaceOrdering() {
  LoadResult result = Parse(
      "reorder rootPrims = [\"B\", \"A\"]\n"
      "def \"A\" {}\n"
      "def \"B\" {\n"
      "  reorder properties = [\"y\", \"x\"]\n"
      "  reorder nameChildren = [\"D\", \"C\"]\n"
      "  int x = 1\n"
      "  int y = 2\n"
      "  def \"C\" {}\n"
      "  def \"D\" {}\n"
      "}\n", true);
  assert(result.success);
  const std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 2 && roots[0].GetName() == "B" &&
         roots[1].GetName() == "A");
  const UsdPrim b = result.stage.GetPrimAtPath("/B");
  const std::vector<UsdPrim> children = b.GetChildren();
  assert(children.size() == 2 && children[0].GetName() == "D" &&
         children[1].GetName() == "C");
  const std::vector<std::string> properties = b.GetPropertyNames();
  assert(properties.size() == 2 && properties[0] == "y" &&
         properties[1] == "x");
  const std::string rewritten = WriteUSDAToString(result.stage);
  assert(rewritten.find("reorder rootPrims") != std::string::npos);
  assert(rewritten.find("reorder properties") != std::string::npos);
  assert(rewritten.find("reorder nameChildren") != std::string::npos);
}

void TestSchemaFallbackAndValueClips() {
  LoadResult mesh_result = Parse("def Mesh \"M\" {}\n", true);
  assert(mesh_result.success);
  const UsdPrim mesh = mesh_result.stage.GetPrimAtPath("/M");
  assert(mesh.HasProperty("doubleSided"));
  const Value* double_sided = mesh.GetPropertyValue("doubleSided");
  assert(double_sided && double_sided->as_bool() &&
         !*double_sided->as_bool());
  AttributeEval mesh_eval(&mesh_result.stage);
  EvalOptions default_time;
  default_time.default_time = true;
  default_time.strict_aousd_conformance = true;
  EvalResult fallback = mesh_eval.EvalWith(mesh, "orientation", default_time);
  assert(fallback.success && fallback.from_schema_fallback);
  assert(fallback.value.as_token() &&
         *fallback.value.as_token() == "rightHanded");

  LoadResult root_result = Parse(
      "def Xform \"Root\" (\n"
      "  clips = {\n"
      "    dictionary default = {\n"
      "      double2[] active = [(0, 0)]\n"
      "      asset[] assetPaths = [@clip.usda@]\n"
      "      string primPath = \"/Root\"\n"
      "      double2[] times = [(0, 0), (1, 1)]\n"
      "    }\n"
      "  }\n"
      ") { float x }\n");
  assert(root_result.success);
  AttributeEval clip_eval(&root_result.stage);
  EvalOptions clip_options;
  clip_options.time = 0.5;
  clip_options.strict_aousd_conformance = true;
  clip_options.clip_stage_loader =
      [](const std::string& asset, Stage* out, std::string*, std::string*) {
        if (asset != "clip.usda" || !out) return false;
        LoadResult clip = Parse(
            "def Xform \"Root\" {\n"
            "  float x.timeSamples = { 0: 2, 1: 4 }\n"
            "}\n");
        if (!clip.success) return false;
        *out = std::move(clip.stage);
        return true;
      };
  EvalResult clipped = clip_eval.EvalWith(
      root_result.stage.GetPrimAtPath("/Root"), "x", clip_options);
  assert(clipped.success && clipped.source_asset == "clip.usda");
  assert(clipped.value.as_float() &&
         std::fabs(*clipped.value.as_float() - 3.0f) < 1e-6f);

  clip_options.clip_stage_loader = {};
  EvalResult strict_missing_loader = clip_eval.EvalWith(
      root_result.stage.GetPrimAtPath("/Root"), "x", clip_options);
  assert(!strict_missing_loader.success &&
         strict_missing_loader.error.find("clip_stage_loader") !=
             std::string::npos);
}

}  // namespace

int main() {
  TestUnicodeAndPaths();
  TestLosslessUnsupportedValues();
  TestDictionaryAndRelationshipComposition();
  TestNamespaceOrdering();
  TestSchemaFallbackAndValueClips();
  std::cout << "AOUSD conformance regressions: PASSED\n";
  return 0;
}
