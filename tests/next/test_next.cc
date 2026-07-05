// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Unit tests

#include <iostream>
#include "test-check.hh"
#include <cstring>
#include <cmath>
#include <limits>
#include <string>

#include "next/types/type-id.hh"
#include "next/types/type-info.hh"
#include "next/types/value.hh"
#include "next/prim/path.hh"
#include "next/prim/attribute.hh"
#include "next/prim/prim.hh"
#include "next/parser/lexer.hh"
#include "next/parser/value-parser.hh"
#include "next/parser/ascii-parser.hh"
#include "next/stage/stage.hh"
#include "next/reader/usda-reader.hh"
#include "next/schema/physics-api.hh"
#include "next/schema/physics-joint.hh"

using namespace tinyusdz::next;

namespace {

std::string UsdaFixturePath(const std::string& filename) {
  const std::string file_path(__FILE__);
  const std::string marker = "/tests/next/";
  const size_t pos = file_path.rfind(marker);
  NEXT_CHECK(pos != std::string::npos);
  return file_path.substr(0, pos) + "/tests/usda/" + filename;
}

}  // namespace

// ============================================================
// Type system tests
// ============================================================

void test_type_id() {
  std::cout << "Testing TypeId..." << std::endl;

  // Test type name lookup
  NEXT_CHECK(GetTypeName(TypeId::Float) != nullptr);
  NEXT_CHECK(std::strcmp(GetTypeName(TypeId::Float), "float") == 0);
  NEXT_CHECK(std::strcmp(GetTypeName(TypeId::Float3), "float3") == 0);
  NEXT_CHECK(std::strcmp(GetTypeName(TypeId::Matrix4d), "matrix4d") == 0);

  // Test reverse lookup
  NEXT_CHECK(GetTypeIdFromName("float") == TypeId::Float);
  NEXT_CHECK(GetTypeIdFromName("float3") == TypeId::Float3);
  NEXT_CHECK(GetTypeIdFromName("unknown") == TypeId::Invalid);

  // Test type size
  NEXT_CHECK(GetTypeSize(TypeId::Float) == sizeof(float));
  NEXT_CHECK(GetTypeSize(TypeId::Double) == sizeof(double));
  NEXT_CHECK(GetTypeSize(TypeId::Float3) == 3 * sizeof(float));
  NEXT_CHECK(GetTypeSize(TypeId::Matrix4d) == 16 * sizeof(double));

  // Test scalar detection
  NEXT_CHECK(IsScalarType(TypeId::Float) == true);
  NEXT_CHECK(IsScalarType(TypeId::Float3) == false);
  NEXT_CHECK(IsScalarType(TypeId::Matrix4d) == false);

  // Test component type
  NEXT_CHECK(GetComponentType(TypeId::Float3) == TypeId::Float);
  NEXT_CHECK(GetComponentType(TypeId::Double3) == TypeId::Double);
  NEXT_CHECK(GetComponentType(TypeId::Matrix4d) == TypeId::Double);

  // Test component count
  NEXT_CHECK(GetComponentCount(TypeId::Float) == 1);
  NEXT_CHECK(GetComponentCount(TypeId::Float3) == 3);
  NEXT_CHECK(GetComponentCount(TypeId::Matrix4d) == 16);

  std::cout << "  TypeId tests passed!" << std::endl;
}

void test_value() {
  std::cout << "Testing Value..." << std::endl;

  // Test scalar values
  {
    Value v(42);
    NEXT_CHECK(v.type_id() == TypeId::Int);
    NEXT_CHECK(!v.is_empty());
    NEXT_CHECK(!v.is_array());
    NEXT_CHECK(v.as_int() != nullptr);
    NEXT_CHECK(*v.as_int() == 42);
    NEXT_CHECK(v.as_float() == nullptr);  // Wrong type
  }

  // Test float value
  {
    Value v(3.14f);
    NEXT_CHECK(v.type_id() == TypeId::Float);
    NEXT_CHECK(v.as_float() != nullptr);
    NEXT_CHECK(std::abs(*v.as_float() - 3.14f) < 0.001f);
  }

  // Test string value
  {
    Value v(std::string("hello"));
    NEXT_CHECK(v.type_id() == TypeId::String);
    NEXT_CHECK(v.as_string() != nullptr);
    NEXT_CHECK(*v.as_string() == "hello");
  }

  // Test vector factory
  {
    Value v = Value::MakeFloat3(1.0f, 2.0f, 3.0f);
    NEXT_CHECK(v.type_id() == TypeId::Float3);
    NEXT_CHECK(v.as_float3() != nullptr);
    const float* data = v.as_float3();
    NEXT_CHECK(data[0] == 1.0f);
    NEXT_CHECK(data[1] == 2.0f);
    NEXT_CHECK(data[2] == 3.0f);
  }

  // Test matrix factory
  {
    double mat_data[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1
    };
    Value v = Value::MakeMatrix4d(mat_data);
    NEXT_CHECK(v.type_id() == TypeId::Matrix4d);
    NEXT_CHECK(v.as_matrix4d() != nullptr);
    const double* data = v.as_matrix4d();
    NEXT_CHECK(data[0] == 1.0);
    NEXT_CHECK(data[15] == 1.0);
  }

  // Test copy
  {
    Value v1(123);
    Value v2 = v1;
    NEXT_CHECK(*v2.as_int() == 123);
    *v2.as_int() = 456;
    NEXT_CHECK(*v1.as_int() == 123);  // v1 unchanged
    NEXT_CHECK(*v2.as_int() == 456);
  }

  // Test move
  {
    Value v1(std::string("test"));
    Value v2 = std::move(v1);
    NEXT_CHECK(v1.is_empty());
    NEXT_CHECK(*v2.as_string() == "test");
  }

  // Test array
  {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    Value v = Value::MakeFloatArray(data);
    NEXT_CHECK(v.is_array());
    NEXT_CHECK(v.array_size() == 5);
    NEXT_CHECK(v.as_float_array() != nullptr);
    NEXT_CHECK(v.as_float_array()->size() == 5);
    NEXT_CHECK((*v.as_float_array())[2] == 3.0f);
  }

  // Copy-on-write: a copy shares the buffer until one side mutates, then the
  // mutation must be private (the other copy is unaffected).
  {
    Value a = Value::MakeFloatArray(std::vector<float>{1, 2, 3});
    Value b = a;  // shares the buffer (refcount bump, no element copy)
    NEXT_CHECK(*a.as_float_array() == *b.as_float_array());

    (*b.as_float_array())[0] = 99.0f;  // mutable access detaches b
    NEXT_CHECK((*b.as_float_array())[0] == 99.0f);
    NEXT_CHECK((*a.as_float_array())[0] == 1.0f && "CoW detach failed: a was mutated");

    // A token array (string elements) detaches correctly too.
    Value t = Value::MakeTokenArray(std::vector<std::string>{"x", "y"});
    Value t2 = t;
    NEXT_CHECK(*t.as_token_array() == *t2.as_token_array());
  }

  // Array equality/hash/raw-bytes must cover every concrete array backing type,
  // not just float3/int. These are used by time-sample deduplication.
  {
    Value q1 = Value::MakeFloatCompArray(
        std::vector<float>{0, 0, 0, 1, 0, 1, 0, 0}, TypeId::Quatf, 4);
    Value q2 = Value::MakeFloatCompArray(
        std::vector<float>{0, 0, 0, 1, 0, 1, 0, 0}, TypeId::Quatf, 4);
    NEXT_CHECK(q1 == q2);
    NEXT_CHECK(q1.hash() == q2.hash());
    size_t n = 0;
    const uint8_t* raw = q1.raw_bytes(&n);
    NEXT_CHECK(raw && n == 8 * sizeof(float));

    Value m1 = Value::MakeDoubleCompArray(std::vector<double>(32, 2.0),
                                          TypeId::Matrix4d, 16);
    Value m2 = m1;
    NEXT_CHECK(m1 == m2);
    NEXT_CHECK(m1.hash() == m2.hash());
    raw = m1.raw_bytes(&n);
    NEXT_CHECK(raw && n == 32 * sizeof(double));

    Value tok1 = Value::MakeTokenArray(std::vector<std::string>{"a", "b"});
    Value tok2 = Value::MakeTokenArray(std::vector<std::string>{"a", "b"});
    NEXT_CHECK(tok1 == tok2);
    NEXT_CHECK(tok1.hash() == tok2.hash());
    NEXT_CHECK(tok1.raw_bytes(&n) == nullptr && n == 0);

    (*q2.as_float_array())[0] = 42.0f;
    NEXT_CHECK(q1 != q2);
  }

  std::cout << "  Value tests passed!" << std::endl;
}

// ============================================================
// Prim tests
// ============================================================

void test_path() {
  std::cout << "Testing Path..." << std::endl;

  Path p1("/World/Cube");
  NEXT_CHECK(p1.is_absolute());
  NEXT_CHECK(!p1.is_root());
  NEXT_CHECK(p1.name() == "Cube");
  NEXT_CHECK(p1.parent().str() == "/World");

  Path p2 = p1.append_child("child");
  NEXT_CHECK(p2.str() == "/World/Cube/child");

  Path p3("/Cube.xformOp:translate");
  NEXT_CHECK(p3.has_property());
  NEXT_CHECK(p3.property_name() == "xformOp:translate");
  NEXT_CHECK(p3.prim_path().str() == "/Cube");

  Path root = Path::root();
  NEXT_CHECK(root.is_root());
  NEXT_CHECK(root.str() == "/");

  std::cout << "  Path tests passed!" << std::endl;
}

void test_prim() {
  std::cout << "Testing Prim..." << std::endl;

  Prim prim("Cube", "Mesh");
  NEXT_CHECK(prim.name() == "Cube");
  NEXT_CHECK(prim.type_name() == "Mesh");
  NEXT_CHECK(prim.specifier() == Specifier::Def);

  // Test attributes
  Attribute attr("points", TypeId::Float3);
  attr.set_default(Value::MakeFloat3(0, 0, 0));
  prim.set_attribute(std::move(attr));

  NEXT_CHECK(prim.has_attribute("points"));
  const Attribute* a = prim.get_attribute("points");
  NEXT_CHECK(a != nullptr);
  NEXT_CHECK(a->type_id() == TypeId::Float3);

  // Test children
  Prim child("SubMesh", "Mesh");
  prim.add_child(std::move(child));
  NEXT_CHECK(prim.child_count() == 1);
  NEXT_CHECK(prim.find_child("SubMesh") != nullptr);

  // Test metadata
  prim.set_metadata("purpose", Value::MakeToken("render"));
  NEXT_CHECK(prim.has_metadata("purpose"));

  std::cout << "  Prim tests passed!" << std::endl;
}

// ============================================================
// Parser tests
// ============================================================

void test_lexer() {
  std::cout << "Testing Lexer..." << std::endl;

  const char* input = R"(
    def Mesh "Cube" {
      float3 points = (1.0, 2.0, 3.0)
      int count = 42
      string name = "hello world"
    }
  )";

  Lexer lexer(input, std::strlen(input));

  Token tok = lexer.next();
  NEXT_CHECK(tok.type == TokenType::Def);

  tok = lexer.next();
  NEXT_CHECK(tok.type == TokenType::Identifier);
  NEXT_CHECK(tok.text == "Mesh");

  tok = lexer.next();
  NEXT_CHECK(tok.type == TokenType::String);
  NEXT_CHECK(tok.value == "Cube");

  tok = lexer.next();
  NEXT_CHECK(tok.type == TokenType::OpenBrace);

  // Skip to string value test
  while (tok.type != TokenType::Eof) {
    tok = lexer.next();
    if (tok.type == TokenType::String && tok.value == "hello world") {
      break;
    }
  }
  NEXT_CHECK(tok.value == "hello world");

  std::cout << "  Lexer tests passed!" << std::endl;
}

void test_value_parser() {
  std::cout << "Testing ValueParser..." << std::endl;

  // Test parsing float3
  {
    const char* input = "(1.0, 2.0, 3.0)";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseValue(lexer, TypeId::Float3);
    NEXT_CHECK(result.success);
    NEXT_CHECK(result.value.type_id() == TypeId::Float3);
    const float* data = result.value.as_float3();
    NEXT_CHECK(data != nullptr);
    NEXT_CHECK(data[0] == 1.0f);
    NEXT_CHECK(data[1] == 2.0f);
    NEXT_CHECK(data[2] == 3.0f);
  }

  // Test parsing matrix4d
  {
    const char* input = "((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseValue(lexer, TypeId::Matrix4d);
    NEXT_CHECK(result.success);
    NEXT_CHECK(result.value.type_id() == TypeId::Matrix4d);
    const double* data = result.value.as_matrix4d();
    NEXT_CHECK(data != nullptr);
    NEXT_CHECK(data[0] == 1.0);
    NEXT_CHECK(data[15] == 1.0);
  }

  // Test parsing array
  {
    const char* input = "[1.0, 2.0, 3.0]";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseArrayValue(lexer, TypeId::Float);
    NEXT_CHECK(result.success);
    NEXT_CHECK(result.value.is_array());
    const std::vector<float>* arr = result.value.as_float_array();
    NEXT_CHECK(arr != nullptr);
    NEXT_CHECK(arr->size() == 3);
    NEXT_CHECK((*arr)[0] == 1.0f);
  }

  // Numeric arrays use fast_float and accept USD special float literals.
  {
    const char* input = "[+1.25, inf, -inf, nan]";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseArrayValue(lexer, TypeId::Float);
    NEXT_CHECK(result.success);
    const std::vector<float>* arr = result.value.as_float_array();
    NEXT_CHECK(arr != nullptr);
    NEXT_CHECK(arr->size() == 4);
    NEXT_CHECK((*arr)[0] == 1.25f);
    NEXT_CHECK(std::isinf((*arr)[1]) && (*arr)[1] > 0.0f);
    NEXT_CHECK(std::isinf((*arr)[2]) && (*arr)[2] < 0.0f);
    NEXT_CHECK(std::isnan((*arr)[3]));
  }

  // Prefix-only numeric parses must be rejected (`1foo` is not `1`).
  {
    const char* input = "[1foo]";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseArrayValue(lexer, TypeId::Float);
    NEXT_CHECK(!result.success);
  }

  std::cout << "  ValueParser tests passed!" << std::endl;
}

void test_ascii_parser() {
  std::cout << "Testing AsciiParser..." << std::endl;

  const char* input = R"(#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
    metersPerUnit = 0.01
)

def Xform "World" {
    def Mesh "Cube" {
        float3[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0)]
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
    }
}
)";

  AsciiParser parser;
  bool success = parser.Parse(input, std::strlen(input));

  if (!success) {
    std::cout << "Parse errors:" << std::endl;
    for (const auto& err : parser.GetErrors()) {
      std::cout << "  Line " << err.line << ": " << err.message << std::endl;
    }
  }
  NEXT_CHECK(success);

  Stage stage = parser.TakeStage();
  NEXT_CHECK(stage.GetMeta().defaultPrim == "World");
  NEXT_CHECK(stage.GetUpAxis() == "Y");
  NEXT_CHECK(std::abs(stage.GetMetersPerUnit() - 0.01) < 0.001);

  auto roots = stage.GetRootPrims();
  NEXT_CHECK(roots.size() == 1);
  NEXT_CHECK(roots[0].GetName() == "World");
  NEXT_CHECK(roots[0].GetTypeName() == "Xform");
  NEXT_CHECK(roots[0].GetChildCount() == 1);

  UsdPrim cube = stage.GetPrimAtPath("/World/Cube");
  NEXT_CHECK(cube.IsValid());
  NEXT_CHECK(cube.GetTypeName() == "Mesh");
  NEXT_CHECK(cube.HasProperty("points"));

  // Exercise the batched async array parse (attribute defaults + timeSamples)
  // when thread support is enabled. Non-threaded builds keep the same option
  // as a no-op.
  {
    std::string points;
    points.reserve(32000);
    points += "[";
    for (int i = 0; i < 2048; i++) {
      if (i) points += ", ";
      points += "(" + std::to_string(i) + ", " + std::to_string(i + 1) +
                ", " + std::to_string(i + 2) + ")";
    }
    points += "]";
    std::string indices = "[";
    for (int i = 0; i < 2048; i++) {
      if (i) indices += ", ";
      indices += std::to_string(i);
    }
    indices += "]";
    std::string async_input =
        "#usda 1.0\n"
        "def Mesh \"AnimMesh\" {\n"
        "  float3[] points = " + points + "\n"
        "  int[] faceVertexIndices = " + indices + "\n"
        "  float3[] points.timeSamples = {\n"
        "    0: " + points + ",\n"
        "    1: " + points + "\n"
        "  }\n"
        "}\n";
    ParseOptions async_opts;
    async_opts.num_threads = 2;
    async_opts.async_arrays = true;
    AsciiParser async_parser(async_opts);
    bool async_success = async_parser.Parse(async_input.data(),
                                            async_input.size());
    if (!async_success) {
      std::cout << "Async array parse errors:" << std::endl;
      for (const auto& err : async_parser.GetErrors()) {
        std::cout << "  Line " << err.line << ": " << err.message << std::endl;
      }
    }
    NEXT_CHECK(async_success);
    Stage async_stage = async_parser.TakeStage();
    UsdPrim anim_mesh = async_stage.GetPrimAtPath("/AnimMesh");
    NEXT_CHECK(anim_mesh.IsValid());
    NEXT_CHECK(anim_mesh.HasProperty("points"));
    NEXT_CHECK(anim_mesh.HasProperty("faceVertexIndices"));
    // Deferred payloads must be fully filled after Parse() returns.
    {
      const PrimSpec* spec = anim_mesh.GetPrimSpec();
      NEXT_CHECK(spec);
      const PropNameId pid = GetPropNameTable().intern("points");
      const PropSlot* slot = spec->property(pid);
      NEXT_CHECK(slot);
      const Value* pv = spec->property_value(pid);
      NEXT_CHECK(pv);
      NEXT_CHECK(pv->is_array());
      NEXT_CHECK(pv->array_size() == 2048);
      const std::vector<float>* fa = pv->as_float_array();
      NEXT_CHECK(fa);
      NEXT_CHECK(fa->size() == 2048 * 3);
      NEXT_CHECK((*fa)[0] == 0.0f && (*fa)[1] == 1.0f && (*fa)[2] == 2.0f);
      NEXT_CHECK((*fa)[3 * 2047 + 0] == 2047.0f);
      const PropNameId iid = GetPropNameTable().intern("faceVertexIndices");
      const Value* iv = spec->property_value(iid);
      NEXT_CHECK(iv);
      const std::vector<int32_t>* ia = iv->as_int_array();
      NEXT_CHECK(ia && ia->size() == 2048);
      NEXT_CHECK((*ia)[0] == 0 && (*ia)[2047] == 2047);
    }

    // A malformed array inside a deferred batch must fail the load.
    std::string bad_input =
        "#usda 1.0\n"
        "def Mesh \"Bad\" {\n"
        "  int[] faceVertexIndices = " + indices.substr(0, indices.size() - 1) +
        ",]\n"
        "}\n";
    AsciiParser bad_parser(async_opts);
    bool bad_success = bad_parser.Parse(bad_input.data(), bad_input.size());
    NEXT_CHECK(!bad_success);
  }

  std::cout << "  AsciiParser tests passed!" << std::endl;
}

void test_usda_reader() {
  std::cout << "Testing USDAReader..." << std::endl;

  const char* input = R"(#usda 1.0
def Sphere "MySphere" {
    double radius = 1.5
    float3 center = (0, 0, 0)
}
)";

  LoadResult result = LoadUSDAFromString(input, std::strlen(input));
  NEXT_CHECK(result.success);

  auto roots = result.stage.GetRootPrims();
  NEXT_CHECK(roots.size() == 1);
  NEXT_CHECK(roots[0].GetName() == "MySphere");
  NEXT_CHECK(roots[0].GetTypeName() == "Sphere");

  // Check property access
  UsdPrim sphere = result.stage.GetPrimAtPath("/MySphere");
  NEXT_CHECK(sphere.IsValid());
  NEXT_CHECK(sphere.HasProperty("radius"));
  NEXT_CHECK(sphere.HasProperty("center"));

  const Value* radius = sphere.GetPropertyValue("radius");
  NEXT_CHECK(radius != nullptr);
  NEXT_CHECK(radius->as_double() != nullptr);
  NEXT_CHECK(std::abs(*radius->as_double() - 1.5) < 0.001);

  std::cout << "  USDAReader tests passed!" << std::endl;
}

// ============================================================
// Arc list-op qualifiers (prepend / append / delete)
// ============================================================

void test_arc_listops() {
  std::cout << "Testing arc list-op qualifiers..." << std::endl;

  // prepend inserts at the front; append at the back; delete removes. The
  // composed `references` list must reflect those ops in authoring order.
  const char* input = R"(#usda 1.0
def "A" (
    references = [@base.usd@</X>]
    prepend references = [@front.usd@</X>]
    append references = [@back.usd@</X>]
    delete references = [@base.usd@</X>]
)
{
}
)";
  LoadResult result = LoadUSDAFromString(input, std::strlen(input));
  NEXT_CHECK(result.success);
  UsdPrim a = result.stage.GetPrimAtPath("/A");
  NEXT_CHECK(a.IsValid());
  const std::vector<std::string>& refs = a.GetMeta().references;
  // explicit [base] -> prepend front -> [front, base] -> append back ->
  // [front, base, back] -> delete base -> [front, back].
  NEXT_CHECK(refs.size() == 2 && "list-op qualifiers not applied");
  NEXT_CHECK(refs[0].find("front.usd") != std::string::npos && "prepend not at front");
  NEXT_CHECK(refs[1].find("back.usd") != std::string::npos && "append not at back");
  for (const auto& r : refs) {
    NEXT_CHECK(r.find("base.usd") == std::string::npos && "delete did not remove");
  }

  std::cout << "  Arc list-op tests passed!" << std::endl;
}

// A per-reference layer offset `(offset = N; scale = M)` must be captured into
// the canonical ref string as `?layerOffset=N:M`.
void test_arc_layer_offset_parse() {
  std::cout << "Testing arc layer-offset parse..." << std::endl;
  const char* input = R"(#usda 1.0
def "R" (
    references = @asset.usd@</A> (offset = 12; scale = 2)
)
{
}
)";
  LoadResult result = LoadUSDAFromString(input, std::strlen(input));
  NEXT_CHECK(result.success);
  UsdPrim r = result.stage.GetPrimAtPath("/R");
  NEXT_CHECK(r.IsValid());
  const std::vector<std::string>& refs = r.GetMeta().references;
  NEXT_CHECK(refs.size() == 1);
  NEXT_CHECK(refs[0].find("layerOffset=") != std::string::npos &&
         "reference layer offset not captured");
  NEXT_CHECK(refs[0].find("12") != std::string::npos &&
         refs[0].find(":2") != std::string::npos && "offset/scale wrong");
  std::cout << "  Arc layer-offset parse passed!" << std::endl;
}

// ============================================================
// Physics schema readers (regression: vector3f / quatf properties were dropped)
// ============================================================

void test_physics_schema() {
  std::cout << "Testing physics schema readers..." << std::endl;

  const char* input = R"(#usda 1.0
def Xform "Body" (
    prepend apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsMassAPI"]
)
{
    bool physics:kinematicEnabled = 1
    rel physics:simulationOwner = </Scene>
    vector3f physics:velocity = (1, 2, 3)
    vector3f physics:angularVelocity = (4, 5, 6)
    point3f physics:centerOfMass = (0.5, 0.5, 0.5)
    float3 physics:diagonalInertia = (2, 3, 4)
}

def Xform "BodyDefaults" (
    prepend apiSchemas = ["PhysicsMassAPI"]
)
{
}

def PhysicsScene "Scene"
{
}

def PhysicsRevoluteJoint "Joint"
{
    point3f physics:localPos0 = (1, 0, 0)
    point3f physics:localPos1 = (0, 1, 0)
}

def PhysicsSphericalJoint "Ball"
{
    float physics:coneAngle0Limit = 30
    float physics:coneAngle1Limit = 45
}
)";

  LoadResult result = LoadUSDAFromString(input, std::strlen(input));
  NEXT_CHECK(result.success);

  UsdPrim body = result.stage.GetPrimAtPath("/Body");
  NEXT_CHECK(body.IsValid());

  // Rigid body velocity / angularVelocity (single vector3f attrs).
  PhysicsRigidBodyData rb;
  NEXT_CHECK(GetPhysicsRigidBodyData(result.stage, body, &rb, 0.0));
  NEXT_CHECK(rb.kinematicEnabled);
  NEXT_CHECK(rb.simulationOwner == "/Scene");
  NEXT_CHECK(std::abs(rb.velocity[0] - 1.0f) < 0.001f);
  NEXT_CHECK(std::abs(rb.velocity[1] - 2.0f) < 0.001f);
  NEXT_CHECK(std::abs(rb.velocity[2] - 3.0f) < 0.001f);
  NEXT_CHECK(std::abs(rb.angularVelocity[0] - 4.0f) < 0.001f);
  NEXT_CHECK(std::abs(rb.angularVelocity[1] - 5.0f) < 0.001f);
  NEXT_CHECK(std::abs(rb.angularVelocity[2] - 6.0f) < 0.001f);

  // Mass: centerOfMass / diagonalInertia (vector3f).
  PhysicsMassData mass;
  NEXT_CHECK(GetPhysicsMassData(result.stage, body, &mass));
  NEXT_CHECK(std::abs(mass.centerOfMass[0] - 0.5f) < 0.001f);
  NEXT_CHECK(std::abs(mass.centerOfMass[2] - 0.5f) < 0.001f);
  NEXT_CHECK(std::abs(mass.diagonalInertia[0] - 2.0f) < 0.001f);
  NEXT_CHECK(std::abs(mass.diagonalInertia[1] - 3.0f) < 0.001f);
  NEXT_CHECK(std::abs(mass.diagonalInertia[2] - 4.0f) < 0.001f);

  UsdPrim body_defaults = result.stage.GetPrimAtPath("/BodyDefaults");
  NEXT_CHECK(body_defaults.IsValid());
  PhysicsMassData mass_defaults;
  NEXT_CHECK(GetPhysicsMassData(result.stage, body_defaults, &mass_defaults));
  NEXT_CHECK(std::isinf(mass_defaults.centerOfMass[0]) &&
         mass_defaults.centerOfMass[0] < 0.0f);
  NEXT_CHECK(mass_defaults.principalAxes[0] == 0.0f);
  NEXT_CHECK(mass_defaults.principalAxes[3] == 0.0f);

  // Joint local frame positions (vector3f).
  UsdPrim joint = result.stage.GetPrimAtPath("/Joint");
  NEXT_CHECK(joint.IsValid());
  PhysicsJointData jd;
  NEXT_CHECK(GetPhysicsJointData(result.stage, joint, &jd, 0.0));
  NEXT_CHECK(jd.hasLocalPos0);
  NEXT_CHECK(std::abs(jd.localPos0[0] - 1.0f) < 0.001f);
  NEXT_CHECK(jd.hasLocalPos1);
  NEXT_CHECK(std::abs(jd.localPos1[1] - 1.0f) < 0.001f);
  NEXT_CHECK(std::isinf(jd.breakForce));
  NEXT_CHECK(!jd.collisionEnabled);

  UsdPrim scene = result.stage.GetPrimAtPath("/Scene");
  NEXT_CHECK(scene.IsValid());
  NEXT_CHECK(!IsPhysicsJoint(scene));

  UsdPrim ball = result.stage.GetPrimAtPath("/Ball");
  NEXT_CHECK(ball.IsValid());
  PhysicsSphericalJointData sj;
  NEXT_CHECK(GetPhysicsSphericalJointData(result.stage, ball, &sj, 0.0));
  NEXT_CHECK(std::abs(sj.coneAngle0Limit - 30.0f) < 0.001f);
  NEXT_CHECK(std::abs(sj.coneAngle1Limit - 45.0f) < 0.001f);

  {
    LoadResult fixture =
        LoadUSDAFromFile(UsdaFixturePath("physics-schema-defaults-001.usda"));
    NEXT_CHECK(fixture.success);
    UsdPrim fixture_body = fixture.stage.GetPrimAtPath("/World/Body");
    NEXT_CHECK(fixture_body.IsValid());
    PhysicsRigidBodyData fixture_rb;
    NEXT_CHECK(GetPhysicsRigidBodyData(fixture.stage, fixture_body, &fixture_rb));
    NEXT_CHECK(fixture_rb.kinematicEnabled);
    NEXT_CHECK(fixture_rb.simulationOwner == "/World/Scene");

    PhysicsMassData fixture_mass;
    NEXT_CHECK(GetPhysicsMassData(fixture.stage, fixture_body, &fixture_mass));
    NEXT_CHECK(std::isinf(fixture_mass.centerOfMass[0]) &&
           fixture_mass.centerOfMass[0] < 0.0f);
    NEXT_CHECK(fixture_mass.principalAxes[0] == 0.0f);
    NEXT_CHECK(fixture_mass.principalAxes[3] == 0.0f);

    UsdPrim fixture_scene = fixture.stage.GetPrimAtPath("/World/Scene");
    NEXT_CHECK(fixture_scene.IsValid());
    NEXT_CHECK(!IsPhysicsJoint(fixture_scene));
  }

  {
    LoadResult fixture = LoadUSDAFromFile(
        UsdaFixturePath("physics-spherical-schema-names-001.usda"));
    NEXT_CHECK(fixture.success);
    UsdPrim fixture_ball = fixture.stage.GetPrimAtPath("/World/Ball");
    NEXT_CHECK(fixture_ball.IsValid());
    PhysicsSphericalJointData fixture_sj;
    NEXT_CHECK(GetPhysicsSphericalJointData(fixture.stage, fixture_ball,
                                        &fixture_sj, 0.0));
    NEXT_CHECK(std::abs(fixture_sj.coneAngle0Limit - 30.0f) < 0.001f);
    NEXT_CHECK(std::abs(fixture_sj.coneAngle1Limit - 45.0f) < 0.001f);
  }

  std::cout << "  physics schema tests passed!" << std::endl;
}

// ============================================================
// Main
// ============================================================

int main() {
  std::cout << "=== TinyUSDZ Next Unit Tests ===" << std::endl;
  std::cout << std::endl;

  try {
    test_type_id();
    test_value();
    test_path();
    test_prim();
    test_lexer();
    test_value_parser();
    test_ascii_parser();
    test_usda_reader();
    test_arc_listops();
    test_arc_layer_offset_parse();
    test_physics_schema();

    std::cout << std::endl;
    std::cout << "All tests passed!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
