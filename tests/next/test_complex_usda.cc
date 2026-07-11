// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Complex USDA parsing test

#include <iostream>
#include <cassert>
#include <string>

#include "next/reader/usda-reader.hh"
#include "next/writer/usda-writer.hh"

using namespace tinyusdz::next;

void test_cube_with_material() {
  std::cout << "Testing complex cube with material..." << std::endl;

  const char* input = R"(#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
    metersPerUnit = 0.01
    timeCodesPerSecond = 24
    startTimeCode = 0
    endTimeCode = 100
)

def Xform "World" (
    kind = "group"
)
{
    def Mesh "Cube" (
        active = true
    )
    {
        uniform bool doubleSided = false
        float3[] extent = [(-0.5, -0.5, -0.5), (0.5, 0.5, 0.5)]
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4]
        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, 0, 0)]
        point3f[] points = [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5), (-0.5, -0.5, -0.5), (0.5, -0.5, -0.5)]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1), (0, 0), (1, 0), (1, 1), (0, 1)]

        rel material:binding = </World/Materials/Red>
    }

    def Scope "Materials" {
        def Material "Red" {
            token outputs:surface.connect = </World/Materials/Red/Surface.outputs:surface>

            def Shader "Surface" {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (1, 0, 0)
                float inputs:roughness = 0.5
                float inputs:metallic = 0
                token outputs:surface
            }
        }
    }
}
)";

  LoadResult result = LoadUSDAFromString(input);

  if (!result.success) {
    std::cerr << "Parse failed: " << result.error_summary << std::endl;
    for (const auto& err : result.errors) {
      std::cerr << "  Line " << err.line << ": " << err.message << std::endl;
    }
    assert(false);
  }

  // Verify stage metadata
  assert(result.stage.GetMeta().defaultPrim == "World");
  assert(result.stage.GetMeta().upAxis == "Y");
  assert(result.stage.GetMeta().metersPerUnit == 0.01);
  assert(result.stage.GetMeta().timeCodesPerSecond == 24);
  assert(result.stage.GetMeta().startTimeCode == 0);
  assert(result.stage.GetMeta().endTimeCode == 100);

  // Verify structure
  std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  const UsdPrim& world = roots[0];
  assert(world.GetName() == "World");
  assert(world.GetTypeName() == "Xform");
  assert(world.GetChildCount() == 2);  // Cube and Materials

  // Find Cube
  UsdPrim cube = world.GetChild("Cube");
  assert(cube);
  assert(cube.GetTypeName() == "Mesh");
  assert(cube.HasProperty("points"));
  assert(cube.HasProperty("faceVertexCounts"));
  assert(cube.HasProperty("faceVertexIndices"));
  assert(cube.HasProperty("normals"));

  // Check relationship
  const std::vector<Path>* binding = cube.GetRelationship("material:binding");
  assert(binding != nullptr);
  assert(binding->size() == 1);
  assert((*binding)[0].str() == "/World/Materials/Red");

  // Find Materials
  UsdPrim materials = world.GetChild("Materials");
  assert(materials);
  assert(materials.GetTypeName() == "Scope");

  // Find Red material
  UsdPrim red = materials.GetChild("Red");
  assert(red);
  assert(red.GetTypeName() == "Material");

  // Find Surface shader
  UsdPrim surface = red.GetChild("Surface");
  assert(surface);
  assert(surface.GetTypeName() == "Shader");
  assert(surface.HasProperty("inputs:diffuseColor"));
  assert(surface.HasProperty("inputs:roughness"));

  std::cout << "  Complex USDA test passed!" << std::endl;
}

void test_animation() {
  std::cout << "Testing animation with timeSamples..." << std::endl;

  const char* input = R"(#usda 1.0

def Xform "Cube" {
    double3 xformOp:translate.timeSamples = {
        0: (0, 0, 0),
        24: (10, 0, 0),
        48: (10, 10, 0),
        72: (0, 10, 0),
        96: (0, 0, 0),
    }
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)";

  LoadResult result = LoadUSDAFromString(input);

  if (!result.success) {
    std::cerr << "Parse failed: " << result.error_summary << std::endl;
    assert(false);
  }

  std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  const UsdPrim& cube = roots[0];
  assert(cube.GetName() == "Cube");

  assert(cube.HasTimeSamples("xformOp:translate"));

  std::vector<double> times = cube.GetTimeSampleTimes("xformOp:translate");
  assert(times.size() == 5);
  assert(times[0] == 0);
  assert(times[1] == 24);
  assert(times[4] == 96);

  std::cout << "  Animation test passed!" << std::endl;
}

void test_large_scene_ascii_grammar_regressions() {
  std::cout << "Testing large-scene ASCII grammar regressions..." << std::endl;

  const char* input = R"USD(#usda 1.0

def Xform "World" {
    def Mesh "Mesh" (
        customData = {
            bool AutoCreated = 1
        }
    )
    {
        point3f[] points = [(0, 0, 0), (1, 0, 0)] (
            interpolation = "vertex"
        )
        int[] primvars:height:indices = None
        texCoord3f[] primvars:uv = [(12.5, -8.0, 960.5)] (
            interpolation = "constant"
        )
        quath[] orientations = [(0.64209, -0.0053215, -0.766602, 0.000286341)]
        custom bool shadow:enable = 0
        bool[] shadow:flags = [true, false, 1, 0]
        token[] primvars:tags = [leaf, "branch"]
        string[] sourceSnippets = ['''line one
line two
''']
        half scalarHalf = 0.5
        point3h halfPoint = (1, 2, 3)
        quath halfOrientation = (0, 0, 0, 1)
        matrix2f uvMatrix = ((1, 0), (0, 1))
        matrix4f localMatrix = ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
        matrix2d uvMatrixD = ((1, 0), (0, 1))
        uint3 objectIds = (1, 2, 3)
        float3[] extent.timeSamples = {
            1: [(-1, -1, -1), (1, 1, 1)],
        }
    }

    def Shader "Shader"
    {
        uniform string info:sourceCode = '''#ifndef VOP_SHADING
#define VOP_SHADING
#endif
'''
    }
}
)USD";

  LoadResult result = LoadUSDAFromString(input);
  if (!result.success) {
    std::cerr << "Parse failed: " << result.error_summary << std::endl;
    for (const auto& err : result.errors) {
      std::cerr << "  Line " << err.line << ": " << err.message << std::endl;
    }
    assert(false);
  }

  std::vector<UsdPrim> roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  const UsdPrim& world = roots[0];
  UsdPrim mesh = world.GetChild("Mesh");
  assert(mesh);
  assert(mesh.HasProperty("points"));
  assert(mesh.HasProperty("primvars:height:indices"));
  assert(mesh.HasProperty("primvars:uv"));
  assert(mesh.HasProperty("orientations"));
  assert(mesh.HasProperty("shadow:enable"));
  assert(mesh.HasProperty("shadow:flags"));
  assert(mesh.HasProperty("primvars:tags"));
  assert(mesh.HasProperty("sourceSnippets"));
  assert(mesh.HasProperty("scalarHalf"));
  assert(mesh.HasProperty("halfPoint"));
  assert(mesh.HasProperty("halfOrientation"));
  assert(mesh.HasProperty("uvMatrix"));
  assert(mesh.HasProperty("localMatrix"));
  assert(mesh.HasProperty("uvMatrixD"));
  assert(mesh.HasProperty("objectIds"));

  assert(mesh.HasTimeSamples("extent"));
  assert(mesh.GetTimeSampleTimes("extent").size() == 1);

  UsdPrim shader = world.GetChild("Shader");
  assert(shader);
  assert(shader.HasProperty("info:sourceCode"));

  std::cout << "  Large-scene ASCII grammar regression test passed!" << std::endl;
}

// Parser+writer parity: the new metadata / dictionary / property-metadata / value
// features must parse, survive a USDA write, and re-parse to the same facts.
void test_metadata_dict_propmeta_roundtrip() {
  std::cout << "Testing metadata/dict/property-metadata round-trip..." << std::endl;

  const char* input = R"USD(#usda 1.0
(
    defaultPrim = "W"
    framesPerSecond = 30
    customLayerData = {
        string author = "syoyo"
        dictionary nested = {
            int version = 2
        }
    }
)

def Xform "W" (
    kind = "component"
    displayName = "My World"
    instanceable = true
    customData = {
        string note = "hello"
        bool flag = true
    }
)
{
    token[] tags = ["alpha", "beta", "gamma"]
    string[] names = ["x", "y"]
    float farPlane = inf
    float nearPlane = -inf
    color3f[] primvars:displayColor = [(1, 0, 0)] (
        interpolation = "constant"
        elementSize = 1
    )
}
)USD";

  auto check = [](const Stage& stage) {
    std::vector<UsdPrim> roots = stage.GetRootPrims();
    assert(roots.size() == 1);
    const UsdPrim& w = roots[0];
    // Prim metadata
    assert(w.GetMeta().kind() == "component");
    assert(w.GetMeta().displayName() == "My World");
    assert(w.GetMeta().instanceable == true);
    // customData dictionary
    assert(w.GetMeta().customData().is_dictionary());
    const Dict* cd = w.GetMeta().customData().as_dictionary();
    assert(cd && cd->find("note") && cd->find("note")->as_string());
    assert(*cd->find("note")->as_string() == "hello");
    // String/token arrays carry real values (regression for the data-loss bug)
    const Value* tags = w.GetPropertyValue("tags");
    assert(tags && tags->as_token_array());
    assert(tags->as_token_array()->size() == 3);
    assert((*tags->as_token_array())[1] == "beta");
    const Value* names = w.GetPropertyValue("names");
    assert(names && names->as_token_array() &&
           names->as_token_array()->size() == 2);
  };

  LoadResult r1 = LoadUSDAFromString(input);
  if (!r1.success) {
    std::cerr << "Parse failed: " << r1.error_summary << std::endl;
    assert(false);
  }
  check(r1.stage);

  // Write it back out and confirm the new opinions are emitted.
  std::string out = WriteUSDAToString(r1.stage);
  assert(out.find("kind = \"component\"") != std::string::npos);
  assert(out.find("displayName = \"My World\"") != std::string::npos);
  assert(out.find("instanceable = true") != std::string::npos);
  assert(out.find("framesPerSecond = 30") != std::string::npos);
  assert(out.find("customData = {") != std::string::npos);
  assert(out.find("customLayerData = {") != std::string::npos);
  assert(out.find("\"alpha\", \"beta\", \"gamma\"") != std::string::npos);
  assert(out.find("interpolation = \"constant\"") != std::string::npos);
  assert(out.find("inf") != std::string::npos);
  assert(out.find("-inf") != std::string::npos);

  // Re-parse the written USDA and confirm the same facts survive a round-trip.
  LoadResult r2 = LoadUSDAFromString(out.c_str());
  if (!r2.success) {
    std::cerr << "Re-parse failed: " << r2.error_summary << std::endl;
    std::cerr << out << std::endl;
    assert(false);
  }
  check(r2.stage);

  std::cout << "  Metadata/dict/property-metadata round-trip passed!" << std::endl;
}

void test_relationship_body_listops() {
  std::cout << "Testing relationship body list-ops..." << std::endl;

  const char* input = R"usda(#usda 1.0

def Xform "World"
{
    rel targets = </A>
    append rel targets = [</B>, </C>]
    prepend rel targets = </Root>
    add rel targets = [</C>, </D>]
    delete rel targets = </B>
}
)usda";

  LoadResult r = LoadUSDAFromString(input);
  if (!r.success) {
    std::cerr << "Parse failed: " << r.error_summary << std::endl;
    assert(false);
  }

  UsdPrim world = r.stage.GetPrimAtPath("/World");
  assert(world.IsValid());
  const std::vector<Path>* targets = world.GetRelationship("targets");
  assert(targets && targets->size() == 4);
  assert((*targets)[0].str() == "/Root");
  assert((*targets)[1].str() == "/A");
  assert((*targets)[2].str() == "/C");
  assert((*targets)[3].str() == "/D");

  std::string out = WriteUSDAToString(r.stage);
  LoadResult r2 = LoadUSDAFromString(out.c_str());
  assert(r2.success);
  UsdPrim world2 = r2.stage.GetPrimAtPath("/World");
  const std::vector<Path>* targets2 = world2.GetRelationship("targets");
  assert(targets2 && targets2->size() == 4);
  assert((*targets2)[0].str() == "/Root");
  assert((*targets2)[1].str() == "/A");
  assert((*targets2)[2].str() == "/C");
  assert((*targets2)[3].str() == "/D");

  std::cout << "  Relationship body list-ops passed!" << std::endl;
}

// Regressions for the 2026-07 parser audit cluster (H1/H3/H4/M5/M6/M7/
// M10/M13/M14). Each case failed (silently mis-parsed or derailed) before
// the corresponding fix.
void test_parser_audit_2026_07() {
  std::cout << "Testing parser audit regressions..." << std::endl;

  // H1: a list-edit qualifier before an ATTRIBUTE must be a hard error, not
  // a silent drop (a dropped `delete X.connect` becomes an ADD).
  {
    const char* input = R"(#usda 1.0
def Xform "W"
{
    delete float inputs:x.connect = </W/S.outputs:o>
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(!r.success);
    bool found = false;
    for (const auto& err : r.errors) {
      if (err.message.find("List-editing qualifier") != std::string::npos) {
        found = true;
      }
    }
    assert(found);
  }

  // H3: scalar int overflow must error (previously saturated silently).
  {
    const char* input = R"(#usda 1.0
def Xform "W"
{
    int v = 99999999999
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(!r.success);
  }
  {
    // Boundary values still parse.
    const char* input = R"(#usda 1.0
def Xform "W"
{
    int lo = -2147483648
    int hi = 2147483647
    uint uhi = 4294967295
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
  }

  // H4: `\@@@` escape inside a triple-@ asset path (literal "@@@") must not
  // desync the lexer; the following attribute still parses.
  {
    const char* input = "#usda 1.0\n"
        "def Xform \"W\"\n"
        "{\n"
        "    asset a = @@@weird\\@@@name.usd@@@\n"
        "    int after = 7\n"
        "}\n";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim w = r.stage.GetPrimAtPath("/W");
    assert(w.IsValid());
    assert(w.HasProperty("a"));
    assert(w.HasProperty("after"));
  }

  // M5: trailing commas in arrays (scalar and tuple) are valid USDA.
  {
    const char* input = R"(#usda 1.0
def Mesh "W"
{
    int[] idx = [1, 2, 3,]
    point3f[] pts = [(0, 0, 0), (1, 1, 1),]
    int after = 1
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim w = r.stage.GetPrimAtPath("/W");
    assert(w.HasProperty("idx"));
    assert(w.HasProperty("pts"));
    assert(w.HasProperty("after"));
  }

  // M6: relative relationship / connection targets resolve against the
  // owning prim's path.
  {
    const char* input = R"(#usda 1.0
def Xform "World"
{
    def Xform "A"
    {
        rel r = <../Sib>
        float inputs:x.connect = <../Sib.outputs:o>
    }
    def Xform "Sib"
    {
    }
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim a = r.stage.GetPrimAtPath("/World/A");
    const std::vector<Path>* targets = a.GetRelationship("r");
    assert(targets && targets->size() == 1);
    assert((*targets)[0].str() == "/World/Sib");
    std::string out = WriteUSDAToString(r.stage);
    assert(out.find("</World/Sib.outputs:o>") != std::string::npos);
  }

  // M6 edge forms: ".prop" anchors to the owning prim, bare ".."/"." resolve
  // to prim paths, bracketed connect lists resolve like the single form, and
  // relative targets inside variant content keep the AUTHORED form (resolving
  // there would leak the internal /__self__ sentinel into written layers).
  {
    const char* input = R"(#usda 1.0
def Xform "World"
{
    def Xform "A"
    {
        rel up = <..>
        rel self = <.>
        float inputs:x.connect = <.outputs:o>
        float inputs:y.connect = [<../Sib.outputs:o>]
        token outputs:o
    }
    def Xform "Sib"
    {
        token outputs:o
    }
    def Xform "V" (
        variants = { string s = "a" }
        prepend variantSets = "s"
    )
    {
        variantSet "s" = {
            "a" {
                def Xform "Child" {
                    rel r = <../Sib>
                }
                def Xform "Sib" {}
            }
        }
    }
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim a = r.stage.GetPrimAtPath("/World/A");
    const std::vector<Path>* up = a.GetRelationship("up");
    assert(up && up->size() == 1 && (*up)[0].str() == "/World");
    const std::vector<Path>* self_rel = a.GetRelationship("self");
    assert(self_rel && self_rel->size() == 1 &&
           (*self_rel)[0].str() == "/World/A");
    std::string out = WriteUSDAToString(r.stage);
    assert(out.find("</World/A.outputs:o>") != std::string::npos);
    assert(out.find("</World/Sib.outputs:o>") != std::string::npos);
    assert(out.find("__self__") == std::string::npos);
    assert(out.find("<../Sib>") != std::string::npos);
  }

  // M13 whitespace tolerance: multiple spaces between "#usda" and the
  // version are valid (the legacy parser accepts arbitrary spacing).
  {
    const char* input =
        "#usda  1.0\ndef Xform \"W\"\n{\n}\n";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
  }

  // M7: per-arc customData on a reference must be skipped structurally, not
  // derail the metadata paren scan.
  {
    const char* input = R"(#usda 1.0
def Xform "W" (
    references = @./missing.usda@ (
        customData = {
            int priority = 1
        }
    )
    kind = "component"
)
{
    int after = 3
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim w = r.stage.GetPrimAtPath("/W");
    assert(w.HasProperty("after"));
  }

  // M10: single non-bracketed apiSchemas value must land in apiSchemas, not
  // be misparsed into the prim's doc string.
  {
    const char* input = R"(#usda 1.0
def Xform "W" (
    prepend apiSchemas = "PhysicsRigidBodyAPI"
)
{
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim w = r.stage.GetPrimAtPath("/W");
    const std::vector<std::string>& schemas = w.GetMeta().apiSchemas();
    assert(schemas.size() == 1);
    assert(schemas[0] == "PhysicsRigidBodyAPI");
    std::string out = WriteUSDAToString(r.stage);
    assert(out.find("doc = \"PhysicsRigidBodyAPI\"") == std::string::npos);
  }

  // M13: text without a `#usda 1.x` header must be rejected (the old
  // token-level check was dead code — the lexer eats `#` as comments).
  {
    const char* input = R"(def Xform "W"
{
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(!r.success);
  }
  {
    // Leading whitespace before the header is fine.
    const char* input = "\n  #usda 1.0\ndef Xform \"W\"\n{\n}\n";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
  }

  // Property metadata inside a variantSet body must survive (previously
  // silently dropped; VariantProperty cannot carry PropMeta, so it is
  // routed through the variant content "__self__" prim).
  {
    const char* input = R"(#usda 1.0
def Xform "W" (
    variants = {
        string shape = "a"
    }
    prepend variantSets = "shape"
)
{
    variantSet "shape" = {
        "a" {
            float3[] extent = [(0, 0, 0), (1, 1, 1)] (
                interpolation = "constant"
            )
            int v = 1
        }
        "b" {
            int v = 2
        }
    }
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    std::string out = WriteUSDAToString(r.stage);
    assert(out.find("interpolation = \"constant\"") != std::string::npos);
    // Value must not be duplicated (inline VariantProperty + __self__).
    size_t first = out.find("float3[] extent");
    assert(first != std::string::npos);
    assert(out.find("float3[] extent", first + 1) == std::string::npos);
    LoadResult r2 = LoadUSDAFromString(out.c_str());
    assert(r2.success);
    std::string out2 = WriteUSDAToString(r2.stage);
    assert(out2.find("interpolation = \"constant\"") != std::string::npos);
  }

  // M14: a `.spline` statement is skipped structurally; the prim parser
  // keeps going.
  {
    const char* input = R"(#usda 1.0
def Xform "W"
{
    double x.spline = {
        bezier,
        1: 5,
        10: 20,
    }
    int after = 9
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim w = r.stage.GetPrimAtPath("/W");
    assert(w.HasProperty("after"));
  }

  // Scalar timecode timeSamples interpolate (typed TimeCode result) instead
  // of snapping to the held sample.
  {
    const char* input = R"(#usda 1.0
def Xform "W"
{
    timecode t.timeSamples = {
        1: 10,
        3: 30,
    }
}
)";
    LoadResult r = LoadUSDAFromString(input);
    assert(r.success);
    UsdPrim w = r.stage.GetPrimAtPath("/W");
    Value mid = w.GetInterpolatedValue("t", 2.0);
    assert(mid.type_id() == TypeId::TimeCode);
    const double* d = mid.as_double();
    assert(d && *d == 20.0);
  }

  std::cout << "  Parser audit regressions passed!" << std::endl;
}

int main() {
  std::cout << "=== TinyUSDZ Next Complex USDA Tests ===" << std::endl;
  std::cout << std::endl;

  try {
    test_cube_with_material();
    test_animation();
    test_large_scene_ascii_grammar_regressions();
    test_metadata_dict_propmeta_roundtrip();
    test_relationship_body_listops();
    test_parser_audit_2026_07();

    std::cout << std::endl;
    std::cout << "All complex USDA tests passed!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
