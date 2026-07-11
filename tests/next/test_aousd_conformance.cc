// SPDX-License-Identifier: Apache-2.0

#include "next/composition/composition.hh"
#include "next/eval/attribute-eval.hh"
#include "next/reader/usda-reader.hh"
#include "next/reader/usdc-reader.hh"
#include "next/types/spline.hh"
#include "next/writer/usda-writer.hh"
#include "next/writer/usdc-writer.hh"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include "next/schema/schema-registry.hh"

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
  // Typed splines are now first-class: they parse (even in strict mode),
  // evaluate, and encode to USDC (Crate type 59).
  const std::string spline =
      "def Xform \"S\" {\n"
      "  double value.spline = { 0: 0; post linear, 10: 10, }\n"
      "}\n";
  LoadResult compat = Parse(spline, false);
  assert(compat.success);
  const std::string rewritten = WriteUSDAToString(compat.stage);
  assert(rewritten.find("value.spline") != std::string::npos);
  assert(rewritten.find("post linear") != std::string::npos);
  // Strict AOUSD parse now accepts the (grammatically valid) spline.
  assert(Parse(spline, true).success);
  // A grammatically malformed spline is a hard error in strict mode.
  assert(!Parse(
              "def Xform \"B\" {\n"
              "  double value.spline = { 0: 0; post bogus, }\n"
              "}\n",
              true)
              .success);

  // frame4d is now a first-class matrix4d role type (pxr itself rejects
  // the old `= identity` shorthand): the matrix form parses, round-trips,
  // and keeps the frame4d name.
  const std::string frame =
      "def Xform \"F\" {\n"
      "    frame4d frame = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,2,1))\n"
      "}\n";
  LoadResult frame_compat = Parse(frame, false);
  assert(frame_compat.success);
  {
    UsdPrim f = frame_compat.stage.GetPrimAtPath("/F");
    const Value* v = f.GetPropertyValue("frame");
    assert(v && v->type_id() == TypeId::Frame4d);
    const double* m = v->as_matrix4d();
    assert(m && m[14] == 2.0);
    const std::string rew = WriteUSDAToString(frame_compat.stage);
    assert(rew.find("frame4d frame") != std::string::npos);
  }
  // pathExpression is first-class too.
  const std::string pe =
      "def Xform \"P\" {\n"
      "    pathExpression expr = \"/World//Mesh*\"\n"
      "}\n";
  LoadResult pe_res = Parse(pe, false);
  assert(pe_res.success);
  {
    UsdPrim pprim = pe_res.stage.GetPrimAtPath("/P");
    const Value* v = pprim.GetPropertyValue("expr");
    assert(v && v->type_id() == TypeId::PathExpression);
    assert(v->as_string() && *v->as_string() == "/World//Mesh*");
    const std::string rew = WriteUSDAToString(pe_res.stage);
    assert(rew.find("pathExpression expr = \"/World//Mesh*\"") !=
           std::string::npos);
  }
}

void TestSchemaRegistryBreadth() {
  // Fallbacks expanded from pxr's generated schemas: Imageable purpose /
  // visibility, camera clippingRange, common light inputs, curve basis,
  // GeomSubset elementType, ShadowAPI defaults.
  const std::string usda =
      "def Mesh \"M\" { }\n"
      "def Camera \"C\" { }\n"
      "def SphereLight \"L\" { }\n"
      "def BasisCurves \"B\" { }\n";
  LoadResult r = Parse("#usda 1.0\n" + usda, false);
  assert(r.success);
  auto expect_tok = [&](const char* prim, const char* attr,
                        const char* want) {
    UsdPrim p = r.stage.GetPrimAtPath(prim);
    assert(p.IsValid());
    const SchemaPropertyDefinition* def =
        GetSchemaRegistry().FindProperty(*p.GetPrimSpec(), attr);
    assert(def && def->has_fallback);
    const std::string* t = def->fallback.as_token();
    assert(t && *t == want);
  };
  expect_tok("/M", "purpose", "default");
  expect_tok("/M", "visibility", "inherited");
  expect_tok("/B", "basis", "bezier");
  {
    UsdPrim c = r.stage.GetPrimAtPath("/C");
    const SchemaPropertyDefinition* def =
        GetSchemaRegistry().FindProperty(*c.GetPrimSpec(), "clippingRange");
    assert(def && def->has_fallback);
  }
  {
    UsdPrim l = r.stage.GetPrimAtPath("/L");
    const SchemaPropertyDefinition* def = GetSchemaRegistry().FindProperty(
        *l.GetPrimSpec(), "inputs:intensity");
    assert(def && def->fallback.as_float() && *def->fallback.as_float() == 1.0f);
    def = GetSchemaRegistry().FindProperty(*l.GetPrimSpec(), "inputs:radius");
    assert(def && def->fallback.as_float() && *def->fallback.as_float() == 0.5f);
  }

  std::cout << "  Schema registry breadth: PASSED\n";
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

  // Manifest gating + interpolateMissingClipValues: clipB has no opinion
  // for x, so the value comes from the neighboring clipA; y is NOT
  // declared in the manifest so it must not resolve through clips.
  LoadResult manifest_result = Parse(
      "def Xform \"Root\" (\n"
      "  clips = {\n"
      "    dictionary default = {\n"
      "      double2[] active = [(0, 0), (10, 1)]\n"
      "      asset[] assetPaths = [@clipA.usda@, @clipB.usda@]\n"
      "      string primPath = \"/Root\"\n"
      "      asset manifestAssetPath = @manifest.usda@\n"
      "      bool interpolateMissingClipValues = true\n"
      "      double2[] times = [(0, 0), (20, 20)]\n"
      "    }\n"
      "  }\n"
      ") { float x\n  float y\n }\n");
  assert(manifest_result.success);
  AttributeEval m_eval(&manifest_result.stage);
  EvalOptions m_opts;
  m_opts.time = 12.0;  // clipB active
  m_opts.clip_stage_loader =
      [](const std::string& asset, Stage* out, std::string*, std::string*) {
        const char* text = nullptr;
        if (asset == "clipA.usda") {
          text = "def Xform \"Root\" {\n"
                 "  float x.timeSamples = { 0: 7, 20: 7 }\n"
                 "  float y.timeSamples = { 0: 9, 20: 9 }\n"
                 "}\n";
        } else if (asset == "clipB.usda") {
          text = "def Xform \"Root\" { }\n";  // no opinion for x
        } else if (asset == "manifest.usda") {
          text = "def Xform \"Root\" { float x }\n";  // declares x only
        } else {
          return false;
        }
        LoadResult clip = Parse(text);
        if (!clip.success || !out) return false;
        *out = std::move(clip.stage);
        return true;
      };
  EvalResult from_neighbor = m_eval.EvalWith(
      manifest_result.stage.GetPrimAtPath("/Root"), "x", m_opts);
  assert(from_neighbor.success && from_neighbor.source_asset == "clipA.usda");
  assert(from_neighbor.value.as_float() &&
         *from_neighbor.value.as_float() == 7.0f);
  // y is not in the manifest: no clip resolution (falls back to no value).
  EvalResult gated = m_eval.EvalWith(
      manifest_result.stage.GetPrimAtPath("/Root"), "y", m_opts);
  assert(!(gated.success && gated.source_asset == "clipA.usda") &&
         "manifest must gate undeclared properties out of clip resolution");

  // interpolateMissingClipValues must INTERPOLATE (not hold) between the
  // nearest earlier and later clips that carry a value. Three clips, empty
  // middle: A holds 0 at t=0, C holds 20 at t=20; querying t=12 (empty B
  // active) must yield 12.0 (pxr valueClips.md worked example), not 0.0.
  LoadResult interp_result = Parse(
      "def Xform \"Root\" (\n"
      "  clips = {\n"
      "    dictionary default = {\n"
      "      double2[] active = [(0, 0), (10, 1), (20, 2)]\n"
      "      asset[] assetPaths = [@a.usda@, @b.usda@, @c.usda@]\n"
      "      string primPath = \"/Root\"\n"
      "      bool interpolateMissingClipValues = true\n"
      "      double2[] times = [(0, 0), (30, 30)]\n"
      "    }\n"
      "  }\n"
      ") { double v }\n");
  assert(interp_result.success);
  AttributeEval i_eval(&interp_result.stage);
  EvalOptions i_opts;
  i_opts.time = 12.0;  // clip b (empty) active
  i_opts.clip_stage_loader =
      [](const std::string& asset, Stage* out, std::string*, std::string*) {
        const char* text = nullptr;
        if (asset == "a.usda")
          text = "def Xform \"Root\" { double v.timeSamples = { 0: 0 } }\n";
        else if (asset == "b.usda")
          text = "def Xform \"Root\" { }\n";  // empty
        else if (asset == "c.usda")
          text = "def Xform \"Root\" { double v.timeSamples = { 20: 20 } }\n";
        else
          return false;
        LoadResult clip = Parse(text);
        if (!clip.success || !out) return false;
        *out = std::move(clip.stage);
        return true;
      };
  EvalResult interp = i_eval.EvalWith(
      interp_result.stage.GetPrimAtPath("/Root"), "v", i_opts);
  assert(interp.success && interp.value.as_double());
  assert(std::fabs(*interp.value.as_double() - 12.0) < 1e-9 &&
         "missing clip value must interpolate to 12.0, not hold 0.0");
}

void TestTypedSplines() {
  // A linear spline: value ramps 0->10 over t in [0,10], held outside.
  const std::string body =
      "def Xform \"S\" {\n"
      "  double v.spline = {\n"
      "    bezier,\n"
      "    0: 0; post linear,\n"
      "    10: 10,\n"
      "  }\n"
      "}\n";
  LoadResult res = Parse(body, true);
  assert(res.success);

  AttributeEval eval(&res.stage);
  const UsdPrim s = res.stage.GetPrimAtPath("/S");

  auto sample = [&](double t) -> double {
    EvalOptions o;
    o.time = t;
    EvalResult r = eval.EvalWith(s, "v", o);
    assert(r.success && r.value.as_double());
    return *r.value.as_double();
  };
  assert(std::fabs(sample(0.0) - 0.0) < 1e-9);
  assert(std::fabs(sample(5.0) - 5.0) < 1e-9);
  assert(std::fabs(sample(10.0) - 10.0) < 1e-9);
  assert(std::fabs(sample(-3.0) - 0.0) < 1e-9);   // pre-extrapolation: held
  assert(std::fabs(sample(20.0) - 10.0) < 1e-9);  // post-extrapolation: held

  // Round-trip through USDC (Crate type 59) and re-evaluate.
  USDCWriteOptions usdc_opts;
  usdc_opts.crate_options.strict_aousd_conformance = true;
  std::vector<uint8_t> crate;
  USDCWriteResult wr = WriteUSDCToMemory(crate, res.stage, usdc_opts);
  assert(wr.success && !crate.empty());

  USDCLoadOptions lopts;
  USDCLoadResult back = LoadUSDCFromMemory(crate.data(), crate.size(), lopts);
  assert(back.success);
  const UsdPrim s2 = back.stage.GetPrimAtPath("/S");
  assert(s2.IsValid());
  AttributeEval eval2(&back.stage);
  EvalOptions o5;
  o5.time = 5.0;
  EvalResult r5 = eval2.EvalWith(s2, "v", o5);
  assert(r5.success && r5.value.as_double() &&
         std::fabs(*r5.value.as_double() - 5.0) < 1e-9);
  // The decoded USDC also re-emits the spline as USDA text.
  const std::string usda_back = WriteUSDAToString(back.stage);
  assert(usda_back.find("v.spline") != std::string::npos);

  // A float-typed spline keeps its declared scalar type through evaluation.
  LoadResult fr = Parse(
      "def Xform \"F\" {\n"
      "  float w.spline = { 0: 1, 4: 5; post linear, }\n"
      "}\n",
      true);
  assert(fr.success);
  AttributeEval feval(&fr.stage);
  EvalOptions fo;
  fo.time = 2.0;
  EvalResult fres =
      feval.EvalWith(fr.stage.GetPrimAtPath("/F"), "w", fo);
  assert(fres.success && fres.value.as_float());

  // Review regressions:
  // (a) half-typed spline values round to the nearest half (round-to-even),
  // matching pxr — a truncating encoder would give 0.299805 for 0.3.
  {
    SplineData sd;
    sd.value_desc = 3;  // half
    SplineKnot k0;
    k0.time = 0;
    k0.value = 0.3;
    k0.interp = 1;
    sd.knots.push_back(k0);
    std::vector<uint8_t> blob;
    std::string err;
    assert(EncodeSplineBinary(sd, &blob, &err));
    SplineData rt;
    assert(DecodeSplineBinary(blob.data(), blob.size(), &rt, &err));
    assert(std::fabs(rt.knots[0].value - 0.300049) < 1e-5 &&
           "half spline value must round to nearest, not truncate");
  }
  // (b) autoEase tangents are recomputed for evaluation (slope 0 at a local
  // max), so the curve peaks exactly at the knot instead of overshooting with
  // the authored placeholder tangents.
  {
    LoadResult ae = Parse(
        "def Xform \"E\" {\n"
        "  double v.spline = {\n"
        "    bezier,\n"
        "    0: 0; post curve (2, 3),\n"
        "    5: 10; pre (2, 9, autoEase); post curve (2, 9, autoEase),\n"
        "    10: 0; pre (2, 3),\n"
        "  }\n"
        "}\n",
        true);
    assert(ae.success);
    AttributeEval aeval(&ae.stage);
    EvalOptions aopts;
    aopts.time = 5.0;
    EvalResult apk = aeval.EvalWith(ae.stage.GetPrimAtPath("/E"), "v", aopts);
    assert(apk.success && apk.value.as_double() &&
           std::fabs(*apk.value.as_double() - 10.0) < 1e-6 &&
           "autoEase knot at a local max must evaluate to its value, not "
           "overshoot");
  }
  // (c) knot customData containing a brace inside a string must not desync the
  // dictionary skip (strict parse must accept it).
  {
    LoadResult cd = Parse(
        "def Xform \"C\" {\n"
        "  double v.spline = { 0: 1; { string s = \"}\" }, 10: 2, }\n"
        "}\n",
        true);
    assert(cd.success &&
           "spline knot customData with a brace in a string must parse");
  }
}

// AOUSD-TYPE-001 coverage: every supported foundational type must survive a
// USDA -> USDC -> USDA round trip with no value/type loss and no spurious
// structural additions (e.g. a `reorder properties` synthesized from the
// crate `properties` field).
void TestFoundationalTypeMatrix() {
  const std::string body =
      "def Scope \"T\" {\n"
      "    bool b = true\n"
      "    uchar uc = 200\n"
      "    int i = -5\n"
      "    uint ui = 7\n"
      "    int64 i64 = -100000000000\n"
      "    uint64 u64 = 100000000000\n"
      "    half h = 1.5\n"
      "    float f = 2.5\n"
      "    double d = 3.5\n"
      "    string s = \"hi\"\n"
      "    token tok = \"abc\"\n"
      "    asset a = @./tex.png@\n"
      "    int2 i2 = (1, 2)\n"
      "    int3 i3 = (1, 2, 3)\n"
      "    int4 i4 = (1, 2, 3, 4)\n"
      "    half2 h2 = (1.5, 2.5)\n"
      "    half3 h3 = (1.5, 2.5, 3.5)\n"
      "    half4 h4 = (1.5, 2.5, 3.5, 4.5)\n"
      "    float2 f2 = (1.5, 2.5)\n"
      "    float3 f3 = (1.5, 2.5, 3.5)\n"
      "    float4 f4 = (1.5, 2.5, 3.5, 4.5)\n"
      "    double2 d2 = (1.5, 2.5)\n"
      "    double3 d3 = (1.5, 2.5, 3.5)\n"
      "    double4 d4 = (1.5, 2.5, 3.5, 4.5)\n"
      "    point3f p3f = (1, 2, 3)\n"
      "    normal3f n3f = (0, 1, 0)\n"
      "    vector3f v3f = (1, 0, 0)\n"
      "    color3f c3f = (0.1, 0.2, 0.3)\n"
      "    color4f c4f = (0.1, 0.2, 0.3, 1)\n"
      "    texCoord2f uv = (0.5, 0.5)\n"
      "    quatf qf = (1, 0, 0, 0)\n"
      "    quatd qd = (1, 0, 0, 0)\n"
      "    quath qh = (1, 0, 0, 0)\n"
      "    matrix2d m2 = ((1, 0), (0, 1))\n"
      "    matrix3d m3 = ((1, 0, 0), (0, 1, 0), (0, 0, 1))\n"
      "    matrix4d m4 = ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))\n"
      "    frame4d fr = ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 2, 1))\n"
      "    int[] ia = [1, 2, 3]\n"
      "    float[] fa = [1.5, 2.5]\n"
      "    double[] da = [1.5, 2.5]\n"
      "    half[] ha = [1.5, 2.5]\n"
      "    uchar[] uca = [1, 2, 200]\n"
      "    token[] toka = [\"a\", \"b\"]\n"
      "    string[] sa = [\"x\", \"y\"]\n"
      "    asset[] aa = [@./p.png@, @./q.png@]\n"
      "    float3[] f3a = [(1, 2, 3), (4, 5, 6)]\n"
      "    point3f[] p3a = [(1, 2, 3), (4, 5, 6)]\n"
      "    color3f[] c3a = [(0.1, 0.2, 0.3)]\n"
      "    matrix4d[] m4a = [((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1))]\n"
      "    bool[] ba = [true, false, true]\n"
      "}\n";
  LoadResult direct = Parse(body, true);
  assert(direct.success);
  const std::string a1 = WriteUSDAToString(direct.stage);

  USDCWriteOptions usdc_opts;
  std::vector<uint8_t> crate;
  USDCWriteResult wr = WriteUSDCToMemory(crate, direct.stage, usdc_opts);
  assert(wr.success);
  USDCLoadOptions lopts;
  USDCLoadResult back = LoadUSDCFromMemory(crate.data(), crate.size(), lopts);
  assert(back.success);
  const std::string a2 = WriteUSDAToString(back.stage);
  assert(a1 == a2 &&
         "foundational type matrix must survive USDA->USDC->USDA unchanged");

  // An attribute that carries BOTH a default value and a connection must keep
  // the connection through USDC (the connection flag was formerly set only
  // when the connect statement created the slot, so `v = x` + `v.connect = <t>`
  // dropped the connection in the crate).
  LoadResult conn = Parse(
      "def Scope \"C\" {\n"
      "    float v = 100.5\n"
      "    float v.connect = </C.other>\n"
      "    float other = 1\n"
      "}\n",
      true);
  assert(conn.success);
  std::vector<uint8_t> ccrate;
  assert(WriteUSDCToMemory(ccrate, conn.stage, usdc_opts).success);
  USDCLoadResult cback = LoadUSDCFromMemory(ccrate.data(), ccrate.size(), lopts);
  assert(cback.success);
  assert(WriteUSDAToString(cback.stage).find("v.connect = </C.other>") !=
             std::string::npos &&
         "value + connection must both survive USDC");
}

}  // namespace

int main() {
  TestUnicodeAndPaths();
  TestLosslessUnsupportedValues();
  TestTypedSplines();
  TestFoundationalTypeMatrix();
  TestDictionaryAndRelationshipComposition();
  TestNamespaceOrdering();
  TestSchemaFallbackAndValueClips();
  std::cout << "AOUSD conformance regressions: PASSED\n";
  TestSchemaRegistryBreadth();
  return 0;
}
