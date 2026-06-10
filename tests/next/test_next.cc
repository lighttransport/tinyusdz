// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Unit tests

#include <iostream>
#include <cassert>
#include <cstring>
#include <cmath>

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

// ============================================================
// Type system tests
// ============================================================

void test_type_id() {
  std::cout << "Testing TypeId..." << std::endl;

  // Test type name lookup
  assert(GetTypeName(TypeId::Float) != nullptr);
  assert(std::strcmp(GetTypeName(TypeId::Float), "float") == 0);
  assert(std::strcmp(GetTypeName(TypeId::Float3), "float3") == 0);
  assert(std::strcmp(GetTypeName(TypeId::Matrix4d), "matrix4d") == 0);

  // Test reverse lookup
  assert(GetTypeIdFromName("float") == TypeId::Float);
  assert(GetTypeIdFromName("float3") == TypeId::Float3);
  assert(GetTypeIdFromName("unknown") == TypeId::Invalid);

  // Test type size
  assert(GetTypeSize(TypeId::Float) == sizeof(float));
  assert(GetTypeSize(TypeId::Double) == sizeof(double));
  assert(GetTypeSize(TypeId::Float3) == 3 * sizeof(float));
  assert(GetTypeSize(TypeId::Matrix4d) == 16 * sizeof(double));

  // Test scalar detection
  assert(IsScalarType(TypeId::Float) == true);
  assert(IsScalarType(TypeId::Float3) == false);
  assert(IsScalarType(TypeId::Matrix4d) == false);

  // Test component type
  assert(GetComponentType(TypeId::Float3) == TypeId::Float);
  assert(GetComponentType(TypeId::Double3) == TypeId::Double);
  assert(GetComponentType(TypeId::Matrix4d) == TypeId::Double);

  // Test component count
  assert(GetComponentCount(TypeId::Float) == 1);
  assert(GetComponentCount(TypeId::Float3) == 3);
  assert(GetComponentCount(TypeId::Matrix4d) == 16);

  std::cout << "  TypeId tests passed!" << std::endl;
}

void test_value() {
  std::cout << "Testing Value..." << std::endl;

  // Test scalar values
  {
    Value v(42);
    assert(v.type_id() == TypeId::Int);
    assert(!v.is_empty());
    assert(!v.is_array());
    assert(v.as_int() != nullptr);
    assert(*v.as_int() == 42);
    assert(v.as_float() == nullptr);  // Wrong type
  }

  // Test float value
  {
    Value v(3.14f);
    assert(v.type_id() == TypeId::Float);
    assert(v.as_float() != nullptr);
    assert(std::abs(*v.as_float() - 3.14f) < 0.001f);
  }

  // Test string value
  {
    Value v(std::string("hello"));
    assert(v.type_id() == TypeId::String);
    assert(v.as_string() != nullptr);
    assert(*v.as_string() == "hello");
  }

  // Test vector factory
  {
    Value v = Value::MakeFloat3(1.0f, 2.0f, 3.0f);
    assert(v.type_id() == TypeId::Float3);
    assert(v.as_float3() != nullptr);
    const float* data = v.as_float3();
    assert(data[0] == 1.0f);
    assert(data[1] == 2.0f);
    assert(data[2] == 3.0f);
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
    assert(v.type_id() == TypeId::Matrix4d);
    assert(v.as_matrix4d() != nullptr);
    const double* data = v.as_matrix4d();
    assert(data[0] == 1.0);
    assert(data[15] == 1.0);
  }

  // Test copy
  {
    Value v1(123);
    Value v2 = v1;
    assert(*v2.as_int() == 123);
    *v2.as_int() = 456;
    assert(*v1.as_int() == 123);  // v1 unchanged
    assert(*v2.as_int() == 456);
  }

  // Test move
  {
    Value v1(std::string("test"));
    Value v2 = std::move(v1);
    assert(v1.is_empty());
    assert(*v2.as_string() == "test");
  }

  // Test array
  {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    Value v = Value::MakeFloatArray(data);
    assert(v.is_array());
    assert(v.array_size() == 5);
    assert(v.as_float_array() != nullptr);
    assert(v.as_float_array()->size() == 5);
    assert((*v.as_float_array())[2] == 3.0f);
  }

  // Copy-on-write: a copy shares the buffer until one side mutates, then the
  // mutation must be private (the other copy is unaffected).
  {
    Value a = Value::MakeFloatArray(std::vector<float>{1, 2, 3});
    Value b = a;  // shares the buffer (refcount bump, no element copy)
    assert(*a.as_float_array() == *b.as_float_array());

    (*b.as_float_array())[0] = 99.0f;  // mutable access detaches b
    assert((*b.as_float_array())[0] == 99.0f);
    assert((*a.as_float_array())[0] == 1.0f && "CoW detach failed: a was mutated");

    // A token array (string elements) detaches correctly too.
    Value t = Value::MakeTokenArray(std::vector<std::string>{"x", "y"});
    Value t2 = t;
    assert(*t.as_token_array() == *t2.as_token_array());
  }

  // Array equality/hash/raw-bytes must cover every concrete array backing type,
  // not just float3/int. These are used by time-sample deduplication.
  {
    Value q1 = Value::MakeFloatCompArray(
        std::vector<float>{0, 0, 0, 1, 0, 1, 0, 0}, TypeId::Quatf, 4);
    Value q2 = Value::MakeFloatCompArray(
        std::vector<float>{0, 0, 0, 1, 0, 1, 0, 0}, TypeId::Quatf, 4);
    assert(q1 == q2);
    assert(q1.hash() == q2.hash());
    size_t n = 0;
    const uint8_t* raw = q1.raw_bytes(&n);
    assert(raw && n == 8 * sizeof(float));

    Value m1 = Value::MakeDoubleCompArray(std::vector<double>(32, 2.0),
                                          TypeId::Matrix4d, 16);
    Value m2 = m1;
    assert(m1 == m2);
    assert(m1.hash() == m2.hash());
    raw = m1.raw_bytes(&n);
    assert(raw && n == 32 * sizeof(double));

    Value tok1 = Value::MakeTokenArray(std::vector<std::string>{"a", "b"});
    Value tok2 = Value::MakeTokenArray(std::vector<std::string>{"a", "b"});
    assert(tok1 == tok2);
    assert(tok1.hash() == tok2.hash());
    assert(tok1.raw_bytes(&n) == nullptr && n == 0);

    (*q2.as_float_array())[0] = 42.0f;
    assert(q1 != q2);
  }

  std::cout << "  Value tests passed!" << std::endl;
}

// ============================================================
// Prim tests
// ============================================================

void test_path() {
  std::cout << "Testing Path..." << std::endl;

  Path p1("/World/Cube");
  assert(p1.is_absolute());
  assert(!p1.is_root());
  assert(p1.name() == "Cube");
  assert(p1.parent().str() == "/World");

  Path p2 = p1.append_child("child");
  assert(p2.str() == "/World/Cube/child");

  Path p3("/Cube.xformOp:translate");
  assert(p3.has_property());
  assert(p3.property_name() == "xformOp:translate");
  assert(p3.prim_path().str() == "/Cube");

  Path root = Path::root();
  assert(root.is_root());
  assert(root.str() == "/");

  std::cout << "  Path tests passed!" << std::endl;
}

void test_prim() {
  std::cout << "Testing Prim..." << std::endl;

  Prim prim("Cube", "Mesh");
  assert(prim.name() == "Cube");
  assert(prim.type_name() == "Mesh");
  assert(prim.specifier() == Specifier::Def);

  // Test attributes
  Attribute attr("points", TypeId::Float3);
  attr.set_default(Value::MakeFloat3(0, 0, 0));
  prim.set_attribute(std::move(attr));

  assert(prim.has_attribute("points"));
  const Attribute* a = prim.get_attribute("points");
  assert(a != nullptr);
  assert(a->type_id() == TypeId::Float3);

  // Test children
  Prim child("SubMesh", "Mesh");
  prim.add_child(std::move(child));
  assert(prim.child_count() == 1);
  assert(prim.find_child("SubMesh") != nullptr);

  // Test metadata
  prim.set_metadata("purpose", Value::MakeToken("render"));
  assert(prim.has_metadata("purpose"));

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
  assert(tok.type == TokenType::Def);

  tok = lexer.next();
  assert(tok.type == TokenType::Identifier);
  assert(tok.value == "Mesh");

  tok = lexer.next();
  assert(tok.type == TokenType::String);
  assert(tok.value == "Cube");

  tok = lexer.next();
  assert(tok.type == TokenType::OpenBrace);

  // Skip to string value test
  while (tok.type != TokenType::Eof) {
    tok = lexer.next();
    if (tok.type == TokenType::String && tok.value == "hello world") {
      break;
    }
  }
  assert(tok.value == "hello world");

  std::cout << "  Lexer tests passed!" << std::endl;
}

void test_value_parser() {
  std::cout << "Testing ValueParser..." << std::endl;

  // Test parsing float3
  {
    const char* input = "(1.0, 2.0, 3.0)";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseValue(lexer, TypeId::Float3);
    assert(result.success);
    assert(result.value.type_id() == TypeId::Float3);
    const float* data = result.value.as_float3();
    assert(data != nullptr);
    assert(data[0] == 1.0f);
    assert(data[1] == 2.0f);
    assert(data[2] == 3.0f);
  }

  // Test parsing matrix4d
  {
    const char* input = "((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseValue(lexer, TypeId::Matrix4d);
    assert(result.success);
    assert(result.value.type_id() == TypeId::Matrix4d);
    const double* data = result.value.as_matrix4d();
    assert(data != nullptr);
    assert(data[0] == 1.0);
    assert(data[15] == 1.0);
  }

  // Test parsing array
  {
    const char* input = "[1.0, 2.0, 3.0]";
    Lexer lexer(input, std::strlen(input));
    ParseResult result = ParseArrayValue(lexer, TypeId::Float);
    assert(result.success);
    assert(result.value.is_array());
    const std::vector<float>* arr = result.value.as_float_array();
    assert(arr != nullptr);
    assert(arr->size() == 3);
    assert((*arr)[0] == 1.0f);
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
  assert(success);

  Stage stage = parser.TakeStage();
  assert(stage.GetMeta().defaultPrim == "World");
  assert(stage.GetUpAxis() == "Y");
  assert(std::abs(stage.GetMetersPerUnit() - 0.01) < 0.001);

  auto roots = stage.GetRootPrims();
  assert(roots.size() == 1);
  assert(roots[0].GetName() == "World");
  assert(roots[0].GetTypeName() == "Xform");
  assert(roots[0].GetChildCount() == 1);

  UsdPrim cube = stage.GetPrimAtPath("/World/Cube");
  assert(cube.IsValid());
  assert(cube.GetTypeName() == "Mesh");
  assert(cube.HasProperty("points"));

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
  assert(result.success);

  auto roots = result.stage.GetRootPrims();
  assert(roots.size() == 1);
  assert(roots[0].GetName() == "MySphere");
  assert(roots[0].GetTypeName() == "Sphere");

  // Check property access
  UsdPrim sphere = result.stage.GetPrimAtPath("/MySphere");
  assert(sphere.IsValid());
  assert(sphere.HasProperty("radius"));
  assert(sphere.HasProperty("center"));

  const Value* radius = sphere.GetPropertyValue("radius");
  assert(radius != nullptr);
  assert(radius->as_double() != nullptr);
  assert(std::abs(*radius->as_double() - 1.5) < 0.001);

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
  assert(result.success);
  UsdPrim a = result.stage.GetPrimAtPath("/A");
  assert(a.IsValid());
  const std::vector<std::string>& refs = a.GetMeta().references;
  // explicit [base] -> prepend front -> [front, base] -> append back ->
  // [front, base, back] -> delete base -> [front, back].
  assert(refs.size() == 2 && "list-op qualifiers not applied");
  assert(refs[0].find("front.usd") != std::string::npos && "prepend not at front");
  assert(refs[1].find("back.usd") != std::string::npos && "append not at back");
  for (const auto& r : refs) {
    assert(r.find("base.usd") == std::string::npos && "delete did not remove");
  }

  std::cout << "  Arc list-op tests passed!" << std::endl;
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
    vector3f physics:velocity = (1, 2, 3)
    vector3f physics:angularVelocity = (4, 5, 6)
    point3f physics:centerOfMass = (0.5, 0.5, 0.5)
    float3 physics:diagonalInertia = (2, 3, 4)
}

def PhysicsRevoluteJoint "Joint"
{
    point3f physics:localPos0 = (1, 0, 0)
    point3f physics:localPos1 = (0, 1, 0)
}
)";

  LoadResult result = LoadUSDAFromString(input, std::strlen(input));
  assert(result.success);

  UsdPrim body = result.stage.GetPrimAtPath("/Body");
  assert(body.IsValid());

  // Rigid body velocity / angularVelocity (single vector3f attrs).
  PhysicsRigidBodyData rb;
  assert(GetPhysicsRigidBodyData(result.stage, body, &rb, 0.0));
  assert(std::abs(rb.velocity[0] - 1.0f) < 0.001f);
  assert(std::abs(rb.velocity[1] - 2.0f) < 0.001f);
  assert(std::abs(rb.velocity[2] - 3.0f) < 0.001f);
  assert(std::abs(rb.angularVelocity[0] - 4.0f) < 0.001f);
  assert(std::abs(rb.angularVelocity[1] - 5.0f) < 0.001f);
  assert(std::abs(rb.angularVelocity[2] - 6.0f) < 0.001f);

  // Mass: centerOfMass / diagonalInertia (vector3f).
  PhysicsMassData mass;
  assert(GetPhysicsMassData(result.stage, body, &mass));
  assert(std::abs(mass.centerOfMass[0] - 0.5f) < 0.001f);
  assert(std::abs(mass.centerOfMass[2] - 0.5f) < 0.001f);
  assert(std::abs(mass.diagonalInertia[0] - 2.0f) < 0.001f);
  assert(std::abs(mass.diagonalInertia[1] - 3.0f) < 0.001f);
  assert(std::abs(mass.diagonalInertia[2] - 4.0f) < 0.001f);

  // Joint local frame positions (vector3f).
  UsdPrim joint = result.stage.GetPrimAtPath("/Joint");
  assert(joint.IsValid());
  PhysicsJointData jd;
  assert(GetPhysicsJointData(result.stage, joint, &jd, 0.0));
  assert(jd.hasLocalPos0);
  assert(std::abs(jd.localPos0[0] - 1.0f) < 0.001f);
  assert(jd.hasLocalPos1);
  assert(std::abs(jd.localPos1[1] - 1.0f) < 0.001f);

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
    test_physics_schema();

    std::cout << std::endl;
    std::cout << "All tests passed!" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
