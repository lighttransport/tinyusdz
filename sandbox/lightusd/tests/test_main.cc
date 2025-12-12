// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Basic tests

#include "lightusd/lightusd.hh"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Simple test framework
static int g_test_count = 0;
static int g_pass_count = 0;
static int g_fail_count = 0;

#define TEST(name) \
    void test_##name(); \
    struct TestRegister_##name { \
        TestRegister_##name() { \
            printf("Running test: %s\n", #name); \
            g_test_count++; \
            test_##name(); \
        } \
    } g_register_##name; \
    void test_##name()

#define EXPECT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
            g_fail_count++; \
        } else { \
            g_pass_count++; \
        } \
    } while(0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            printf("  FAIL: %s == %s (line %d)\n", #a, #b, __LINE__); \
            g_fail_count++; \
        } else { \
            g_pass_count++; \
        } \
    } while(0)

#define EXPECT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            printf("  FAIL: %s != %s (line %d)\n", #a, #b, __LINE__); \
            g_fail_count++; \
        } else { \
            g_pass_count++; \
        } \
    } while(0)

using namespace lightusd;

// ============================================================================
// Type Tests
// ============================================================================

TEST(TypeDescriptor) {
    const TypeDescriptor* desc = get_type_descriptor(TypeId::Float);
    EXPECT_TRUE(desc != nullptr);
    EXPECT_EQ(desc->id, TypeId::Float);
    EXPECT_EQ(strcmp(desc->name, "float"), 0);
    EXPECT_EQ(desc->size, sizeof(float));
    EXPECT_TRUE(desc->is_numeric);

    desc = get_type_descriptor(TypeId::Float3);
    EXPECT_TRUE(desc != nullptr);
    EXPECT_EQ(desc->num_components, 3);

    // Role types
    desc = get_type_descriptor(TypeId::Color3f);
    EXPECT_TRUE(desc != nullptr);
    EXPECT_TRUE(desc->is_role_type);
    EXPECT_EQ(desc->underlying_id, TypeId::Float3);
}

// ============================================================================
// Token Tests
// ============================================================================

TEST(Token) {
    Token t1("hello");
    Token t2("hello");
    Token t3("world");

    EXPECT_TRUE(t1 == t2);
    EXPECT_TRUE(t1 != t3);
    EXPECT_EQ(strcmp(t1.c_str(), "hello"), 0);
    EXPECT_FALSE(t1.empty());

    Token empty;
    EXPECT_TRUE(empty.empty());
}

// ============================================================================
// Path Tests
// ============================================================================

TEST(Path) {
    Path p("/World/Mesh");
    EXPECT_TRUE(p.is_valid());
    EXPECT_TRUE(p.is_absolute());
    EXPECT_FALSE(p.is_root());
    EXPECT_TRUE(p.is_prim_path());
    EXPECT_FALSE(p.is_property_path());

    EXPECT_EQ(p.element_name(), "Mesh");
    EXPECT_EQ(p.prim_part(), "/World/Mesh");
    EXPECT_TRUE(p.prop_part().empty());

    Path parent = p.parent();
    EXPECT_EQ(parent.prim_part(), "/World");

    Path root = Path::root();
    EXPECT_TRUE(root.is_root());

    // Property path
    Path prop_path("/World/Mesh.points");
    EXPECT_TRUE(prop_path.is_property_path());
    EXPECT_EQ(prop_path.prim_part(), "/World/Mesh");
    EXPECT_EQ(prop_path.prop_part(), "points");
}

// ============================================================================
// Value Tests
// ============================================================================

TEST(Value_Scalars) {
    Value v_bool = Value::from_bool(true);
    EXPECT_EQ(v_bool.type_id(), TypeId::Bool);
    EXPECT_TRUE(v_bool.as_bool() != nullptr);
    EXPECT_EQ(*v_bool.as_bool(), true);

    Value v_int = Value::from_int32(42);
    EXPECT_EQ(v_int.type_id(), TypeId::Int32);
    EXPECT_EQ(*v_int.as_int32(), 42);

    Value v_float = Value::from_float(3.14f);
    EXPECT_EQ(v_float.type_id(), TypeId::Float);
    EXPECT_TRUE(*v_float.as_float() > 3.13f && *v_float.as_float() < 3.15f);
}

TEST(Value_Vectors) {
    Value v3 = Value::from_float3(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(v3.type_id(), TypeId::Float3);
    const float* data = v3.as_float3();
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(data[0], 1.0f);
    EXPECT_EQ(data[1], 2.0f);
    EXPECT_EQ(data[2], 3.0f);
}

TEST(Value_String) {
    Value v_str = Value::from_string("Hello, LightUSD!");
    EXPECT_EQ(v_str.type_id(), TypeId::String);
    const std::string* str = v_str.as_string();
    EXPECT_TRUE(str != nullptr);
    EXPECT_EQ(*str, "Hello, LightUSD!");
}

TEST(Value_Copy) {
    Value v1 = Value::from_float3(1.0f, 2.0f, 3.0f);
    Value v2 = v1;  // Copy

    EXPECT_EQ(v2.type_id(), TypeId::Float3);
    const float* data = v2.as_float3();
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(data[0], 1.0f);
}

TEST(Value_Size) {
    EXPECT_EQ(sizeof(Value), 32u);
}

// ============================================================================
// TimeSamples Tests
// ============================================================================

TEST(TimeSamples) {
    TimeSamples ts;
    EXPECT_TRUE(ts.empty());

    ts.add_sample(0.0, Value::from_float3(0.0f, 0.0f, 0.0f));
    ts.add_sample(1.0, Value::from_float3(1.0f, 1.0f, 1.0f));
    ts.add_sample(2.0, Value::from_float3(2.0f, 2.0f, 2.0f));

    EXPECT_FALSE(ts.empty());
    EXPECT_EQ(ts.size(), 3u);
    EXPECT_EQ(ts.value_type_id(), TypeId::Float3);

    auto result = ts.get_at_time(0.5);
    EXPECT_TRUE(result.ok());
    const float* data = result.value().as_float3();
    EXPECT_TRUE(data != nullptr);
    // Should return sample at time 0.0 (held value)
    EXPECT_EQ(data[0], 0.0f);
}

// ============================================================================
// Attribute Tests
// ============================================================================

TEST(Attribute) {
    Attribute attr(TypeId::Float3);
    EXPECT_EQ(attr.type_id(), TypeId::Float3);
    EXPECT_FALSE(attr.is_authored());

    attr.set_default(Value::from_float3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(attr.is_authored());
    EXPECT_TRUE(attr.has_default());

    auto result = attr.get();
    EXPECT_TRUE(result.ok());
    const float* data = result.value().as_float3();
    EXPECT_TRUE(data != nullptr);
    EXPECT_EQ(data[0], 1.0f);
}

// ============================================================================
// Prim Tests
// ============================================================================

TEST(Prim) {
    Prim prim("Cube", "Mesh");
    EXPECT_EQ(prim.name(), "Cube");
    EXPECT_EQ(prim.type_name(), "Mesh");
    EXPECT_EQ(prim.specifier(), Specifier::Def);
    EXPECT_TRUE(prim.is_active());

    // Add attribute
    Attribute points_attr(TypeId::Float3);
    points_attr.set_default(Value::from_float3(1.0f, 0.0f, 0.0f));
    prim.set_attribute("center", std::move(points_attr));

    EXPECT_TRUE(prim.has_property("center"));
    const Attribute* attr = prim.get_attribute("center");
    EXPECT_TRUE(attr != nullptr);

    // Add child
    Prim child("ChildPrim", "Xform");
    EXPECT_TRUE(prim.add_child(std::move(child)));
    EXPECT_EQ(prim.child_count(), 1u);
}

// ============================================================================
// Stage Tests
// ============================================================================

TEST(Stage) {
    Stage stage = Stage::create();
    EXPECT_EQ(stage.root_prim_count(), 0u);

    // Create scene
    Prim world("World", "Xform");

    Prim mesh("Cube", "Mesh");
    Attribute points(TypeId::Float3);
    points.set_default(Value::from_float3(1.0f, 2.0f, 3.0f));
    mesh.set_attribute("extent", std::move(points));

    world.add_child(std::move(mesh));
    stage.add_root_prim(std::move(world));

    EXPECT_EQ(stage.root_prim_count(), 1u);

    // Path lookup
    auto result = stage.get_prim_at_path(Path("/World"));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value()->name(), "World");

    result = stage.get_prim_at_path(Path("/World/Cube"));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value()->name(), "Cube");

    // Set default prim
    stage.set_default_prim("World");
    EXPECT_EQ(stage.default_prim(), "World");
}

TEST(Stage_USDA_Export) {
    Stage stage = Stage::create();
    stage.set_up_axis("Y");
    stage.set_meters_per_unit(0.01);

    Prim xform("MyXform", "Xform");
    stage.add_root_prim(std::move(xform));
    stage.set_default_prim("MyXform");

    std::string usda = stage.to_usda();
    EXPECT_FALSE(usda.empty());

    // Check key elements
    EXPECT_TRUE(usda.find("#usda 1.0") != std::string::npos);
    EXPECT_TRUE(usda.find("defaultPrim = \"MyXform\"") != std::string::npos);
    EXPECT_TRUE(usda.find("def Xform \"MyXform\"") != std::string::npos);

    printf("USDA output:\n%s\n", usda.c_str());
}

// ============================================================================
// USDA Reader Tests
// ============================================================================

TEST(USDA_Reader_Simple) {
    const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 0.01
    upAxis = "Y"
)

def Xform "World"
{
    def Mesh "Cube"
    {
        float3 extent = (1, 1, 1)
        int faceCount = 6
    }
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Stage& stage = result.stage;
        EXPECT_EQ(stage.default_prim(), "World");
        EXPECT_EQ(stage.up_axis(), "Y");
        EXPECT_EQ(stage.root_prim_count(), 1u);

        const Prim* world = stage.root_prim("World");
        EXPECT_TRUE(world != nullptr);
        if (world) {
            EXPECT_EQ(world->type_name(), "Xform");
            EXPECT_EQ(world->child_count(), 1u);

            const Prim* cube = world->child("Cube");
            EXPECT_TRUE(cube != nullptr);
            if (cube) {
                EXPECT_EQ(cube->type_name(), "Mesh");
                const Attribute* extent = cube->get_attribute("extent");
                EXPECT_TRUE(extent != nullptr);
                if (extent) {
                    auto val = extent->get();
                    EXPECT_TRUE(val.ok());
                }
            }
        }
    } else {
        printf("Parse error: %s\n", result.format_errors().c_str());
    }
}

TEST(USDA_Reader_TimeSamples) {
    const char* usda = R"(#usda 1.0

def Xform "Animated"
{
    double3 xformOp:translate.timeSamples = {
        0: (0, 0, 0),
        24: (10, 0, 0),
        48: (10, 10, 0),
    }
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Prim* animated = result.stage.root_prim("Animated");
        EXPECT_TRUE(animated != nullptr);
        if (animated) {
            const Attribute* translate = animated->get_attribute("xformOp:translate");
            EXPECT_TRUE(translate != nullptr);
            if (translate) {
                EXPECT_TRUE(translate->has_timesamples());
                const TimeSamples* ts = translate->timesamples();
                EXPECT_TRUE(ts != nullptr);
                if (ts) {
                    EXPECT_EQ(ts->size(), 3u);
                }
            }
        }
    } else {
        printf("Parse error: %s\n", result.format_errors().c_str());
    }
}

TEST(USDA_Reader_Arrays) {
    const char* usda = R"(#usda 1.0

def Mesh "TestMesh"
{
    int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]
    point3f[] points = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Prim* mesh = result.stage.root_prim("TestMesh");
        EXPECT_TRUE(mesh != nullptr);
        if (mesh) {
            const Attribute* fvc = mesh->get_attribute("faceVertexCounts");
            EXPECT_TRUE(fvc != nullptr);

            const Attribute* points = mesh->get_attribute("points");
            EXPECT_TRUE(points != nullptr);
        }
    } else {
        printf("Parse error: %s\n", result.format_errors().c_str());
    }
}

TEST(USDA_Reader_Relationship) {
    const char* usda = R"(#usda 1.0

def Xform "World"
{
    rel material:binding = </Materials/DefaultMaterial>
}

def Scope "Materials"
{
    def Material "DefaultMaterial"
    {
    }
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Prim* world = result.stage.root_prim("World");
        EXPECT_TRUE(world != nullptr);
        if (world) {
            const Relationship* binding = world->get_relationship("material:binding");
            EXPECT_TRUE(binding != nullptr);
            if (binding) {
                EXPECT_TRUE(binding->has_targets());
                EXPECT_EQ(binding->target_count(), 1u);
            }
        }
    } else {
        printf("Parse error: %s\n", result.format_errors().c_str());
    }
}

TEST(USDA_Reader_ErrorReporting) {
    const char* bad_usda = R"(#usda 1.0

def Xform "Test"
{
    float3 badValue = (1, 2)  # Missing component
}
)";

    auto result = read_usda_string(bad_usda);
    // This might succeed with a partial parse or fail - either is ok
    // The key is that we get proper diagnostics
    printf("Error diagnostics test: %s\n",
           result.ok() ? "parsed (warning)" : result.format_errors().c_str());
}

TEST(USDA_Reader_IsUSDA) {
    EXPECT_TRUE(is_usda_string("#usda 1.0\n"));
    EXPECT_TRUE(is_usda_string("  #usda 1.0\n"));
    EXPECT_FALSE(is_usda_string("not usda"));
    EXPECT_FALSE(is_usda_string(""));
}

// ============================================================================
// USDA Writer Tests
// ============================================================================

TEST(USDA_Writer_Value_ToString) {
    // Test scalar values
    Value v_bool = Value::from_bool(true);
    EXPECT_EQ(to_string(v_bool), "true");

    Value v_int = Value::from_int32(42);
    EXPECT_EQ(to_string(v_int), "42");

    Value v_float = Value::from_float(3.5f);
    EXPECT_TRUE(to_string(v_float).find("3.5") != std::string::npos);

    // Test vector values
    Value v_float3 = Value::from_float3(1.0f, 2.0f, 3.0f);
    std::string f3_str = to_string(v_float3);
    EXPECT_TRUE(f3_str.find("(") != std::string::npos);
    EXPECT_TRUE(f3_str.find("1") != std::string::npos);
    EXPECT_TRUE(f3_str.find("2") != std::string::npos);
    EXPECT_TRUE(f3_str.find("3") != std::string::npos);

    // Test string value
    Value v_str = Value::from_string("hello");
    EXPECT_EQ(to_string(v_str), "\"hello\"");

    // Test None
    Value v_none = Value::make_none();
    EXPECT_EQ(to_string(v_none), "None");
}

TEST(USDA_Writer_Path_ToString) {
    Path p("/World/Mesh.points");
    std::string p_str = to_string(p);
    EXPECT_TRUE(p_str.find("<") != std::string::npos);
    EXPECT_TRUE(p_str.find("/World/Mesh.points") != std::string::npos);
    EXPECT_TRUE(p_str.find(">") != std::string::npos);
}

TEST(USDA_Writer_Attribute_ToString) {
    Attribute attr(TypeId::Float3);
    attr.set_default(Value::from_float3(1.0f, 2.0f, 3.0f));

    std::string attr_str = to_string(attr, "myAttr");
    EXPECT_TRUE(attr_str.find("float3") != std::string::npos);
    EXPECT_TRUE(attr_str.find("myAttr") != std::string::npos);
    EXPECT_TRUE(attr_str.find("=") != std::string::npos);
}

TEST(USDA_Writer_Prim_ToString) {
    Prim prim("TestPrim", "Mesh");
    Attribute attr(TypeId::Float);
    attr.set_default(Value::from_float(1.5f));
    prim.set_attribute("size", std::move(attr));

    std::string prim_str = to_string(prim);
    EXPECT_TRUE(prim_str.find("def") != std::string::npos);
    EXPECT_TRUE(prim_str.find("Mesh") != std::string::npos);
    EXPECT_TRUE(prim_str.find("TestPrim") != std::string::npos);
    EXPECT_TRUE(prim_str.find("size") != std::string::npos);
}

TEST(USDA_Writer_CustomIndent) {
    Stage stage = Stage::create();
    stage.set_default_prim("Test");

    Prim prim("Test", "Xform");
    Attribute attr(TypeId::Float);
    attr.set_default(Value::from_float(1.0f));
    prim.set_attribute("value", std::move(attr));
    stage.add_root_prim(std::move(prim));

    // Test with 2-space indent - check indented content inside prim
    std::string usda_2space = stage.to_usda(2, false);
    EXPECT_TRUE(usda_2space.find("  default") != std::string::npos);  // metadata indent
    EXPECT_TRUE(usda_2space.find("  float value") != std::string::npos);  // property indent

    // Test with 4-space indent (default)
    std::string usda_4space = stage.to_usda(4, false);
    EXPECT_TRUE(usda_4space.find("    default") != std::string::npos);  // metadata indent
    EXPECT_TRUE(usda_4space.find("    float value") != std::string::npos);  // property indent

    // Test with tab indent
    std::string usda_tabs = stage.to_usda(1, true);
    EXPECT_TRUE(usda_tabs.find("\tdefault") != std::string::npos);  // metadata indent
    EXPECT_TRUE(usda_tabs.find("\tfloat value") != std::string::npos);  // property indent
}

TEST(USDA_Writer_FormatOptions) {
    UsdaFormatOptions opts = UsdaFormatOptions::compact();
    EXPECT_EQ(opts.indent_size, 2);

    opts = UsdaFormatOptions::pretty();
    EXPECT_EQ(opts.indent_size, 4);
    EXPECT_TRUE(opts.sort_properties);
}

TEST(USDA_Writer_UsdaWriter) {
    // Test UsdaWriter class directly
    UsdaFormatOptions opts;
    opts.indent_size = 3;
    opts.build_indent_string();
    UsdaWriter writer(opts);

    Value v = Value::from_double3(1.0, 2.0, 3.0);
    std::string v_str = writer.format(v);
    EXPECT_TRUE(v_str.find("(") != std::string::npos);
    EXPECT_TRUE(v_str.find("1") != std::string::npos);

    // Test full stage formatting
    Stage stage = Stage::create();
    stage.set_default_prim("Root");
    Prim root("Root", "Xform");
    stage.add_root_prim(std::move(root));

    std::string stage_str = writer.format(stage);
    EXPECT_TRUE(stage_str.find("#usda 1.0") != std::string::npos);
    EXPECT_TRUE(stage_str.find("defaultPrim") != std::string::npos);
    EXPECT_TRUE(stage_str.find("Root") != std::string::npos);
}

TEST(USDA_Writer_TimeSamples) {
    TimeSamples ts;
    ts.add_sample(0.0, Value::from_float3(0.0f, 0.0f, 0.0f));
    ts.add_sample(1.0, Value::from_float3(1.0f, 1.0f, 1.0f));

    std::string ts_str = to_string(ts);
    EXPECT_TRUE(ts_str.find("{") != std::string::npos);
    EXPECT_TRUE(ts_str.find("0:") != std::string::npos || ts_str.find("0 :") != std::string::npos);
    EXPECT_TRUE(ts_str.find("1:") != std::string::npos || ts_str.find("1 :") != std::string::npos);
    EXPECT_TRUE(ts_str.find("}") != std::string::npos);
}

TEST(USDA_Writer_Relationship) {
    Relationship rel;
    rel.set_target(Path("/World/Material"));

    std::string rel_str = to_string(rel, "material:binding");
    EXPECT_TRUE(rel_str.find("rel") != std::string::npos);
    EXPECT_TRUE(rel_str.find("material:binding") != std::string::npos);
    EXPECT_TRUE(rel_str.find("/World/Material") != std::string::npos);
}

TEST(USDA_Writer_Roundtrip) {
    // Create a stage, export to USDA, parse it back, verify
    Stage original = Stage::create();
    original.set_default_prim("World");
    original.set_up_axis("Y");

    Prim world("World", "Xform");
    Attribute translate(TypeId::Double3);
    translate.set_default(Value::from_double3(1.0, 2.0, 3.0));
    world.set_attribute("xformOp:translate", std::move(translate));

    Prim child("Cube", "Mesh");
    Attribute extent(TypeId::Float3);
    extent.set_default(Value::from_float3(1.0f, 1.0f, 1.0f));
    child.set_attribute("extent", std::move(extent));
    world.add_child(std::move(child));

    original.add_root_prim(std::move(world));

    // Export to USDA
    std::string usda = original.to_usda();
    printf("\nRoundtrip USDA:\n%s\n", usda.c_str());

    // Parse back
    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Stage& parsed = result.stage;
        EXPECT_EQ(parsed.default_prim(), "World");
        EXPECT_EQ(parsed.up_axis(), "Y");

        const Prim* world_prim = parsed.root_prim("World");
        EXPECT_TRUE(world_prim != nullptr);
        if (world_prim) {
            const Prim* cube = world_prim->child("Cube");
            EXPECT_TRUE(cube != nullptr);
        }
    }
}

// ============================================================================
// Prim Metadata Tests
// ============================================================================

TEST(Prim_Metadata_Basic) {
    Prim prim("TestPrim", "Xform");

    // Test kind
    prim.set_kind("component");
    EXPECT_EQ(prim.kind(), "component");

    // Test purpose
    prim.set_purpose("render");
    EXPECT_EQ(prim.purpose(), "render");

    // Test hidden
    prim.set_hidden(true);
    EXPECT_TRUE(prim.is_hidden());
    prim.set_hidden(false);
    EXPECT_FALSE(prim.is_hidden());

    // Test documentation
    prim.set_documentation("This is a test prim");
    EXPECT_EQ(prim.documentation(), "This is a test prim");

    // Test generic metadata
    prim.set_metadata("customKey", Value::from_string("customValue"));
    EXPECT_TRUE(prim.has_metadata("customKey"));
    const Value* v = prim.get_metadata("customKey");
    EXPECT_TRUE(v != nullptr);
    if (v) {
        EXPECT_TRUE(v->as_string() != nullptr);
    }

    // Test metadata count
    EXPECT_TRUE(prim.metadata_count() >= 4);  // kind, purpose, documentation, customKey
}

TEST(Prim_CustomData) {
    Prim prim("TestPrim", "Mesh");

    // Test custom data
    prim.set_custom_data("myFloat", Value::from_float(3.14f));
    prim.set_custom_data("myInt", Value::from_int32(42));
    prim.set_custom_data("myString", Value::from_string("hello"));

    EXPECT_TRUE(prim.has_custom_data("myFloat"));
    EXPECT_TRUE(prim.has_custom_data("myInt"));
    EXPECT_TRUE(prim.has_custom_data("myString"));

    const Value* f = prim.get_custom_data("myFloat");
    EXPECT_TRUE(f != nullptr);
    if (f && f->as_float()) {
        EXPECT_TRUE(*f->as_float() > 3.0f && *f->as_float() < 3.2f);
    }
}

TEST(Prim_Metadata_Parser) {
    const char* usda = R"(#usda 1.0

def Xform "World" (
    kind = "component"
    purpose = "render"
    hidden = false
    documentation = "Main world transform"
)
{
    def Mesh "Cube" (
        kind = "subcomponent"
    )
    {
    }
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Prim* world = result.stage.root_prim("World");
        EXPECT_TRUE(world != nullptr);
        if (world) {
            EXPECT_EQ(world->kind(), "component");
            EXPECT_EQ(world->purpose(), "render");
            EXPECT_FALSE(world->is_hidden());
            EXPECT_EQ(world->documentation(), "Main world transform");

            const Prim* cube = world->child("Cube");
            EXPECT_TRUE(cube != nullptr);
            if (cube) {
                EXPECT_EQ(cube->kind(), "subcomponent");
            }
        }
    } else {
        printf("Parse error: %s\n", result.format_errors().c_str());
    }
}

TEST(Prim_Metadata_Writer) {
    Stage stage = Stage::create();

    Prim prim("Model", "Xform");
    prim.set_kind("assembly");
    prim.set_purpose("default");
    prim.set_documentation("Test model");
    stage.add_root_prim(std::move(prim));

    std::string usda = stage.to_usda();
    printf("\nMetadata USDA:\n%s\n", usda.c_str());

    // Verify output contains metadata
    EXPECT_TRUE(usda.find("kind") != std::string::npos);
    EXPECT_TRUE(usda.find("assembly") != std::string::npos);
    EXPECT_TRUE(usda.find("purpose") != std::string::npos);
    EXPECT_TRUE(usda.find("documentation") != std::string::npos);
}

TEST(Prim_Metadata_Roundtrip) {
    // Create stage with metadata
    Stage original = Stage::create();
    Prim prim("TestPrim", "Scope");
    prim.set_kind("group");
    prim.set_purpose("proxy");
    prim.set_hidden(true);
    prim.set_documentation("Roundtrip test");
    original.add_root_prim(std::move(prim));

    // Export
    std::string usda = original.to_usda();

    // Parse back
    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        const Prim* parsed = result.stage.root_prim("TestPrim");
        EXPECT_TRUE(parsed != nullptr);
        if (parsed) {
            EXPECT_EQ(parsed->kind(), "group");
            EXPECT_EQ(parsed->purpose(), "proxy");
            EXPECT_TRUE(parsed->is_hidden());
            EXPECT_EQ(parsed->documentation(), "Roundtrip test");
        }
    }
}

// ============================================================================
// Stage Metadata Tests
// ============================================================================

TEST(Stage_Metadata_Basic) {
    Stage stage = Stage::create();

    // Test generic metadata
    EXPECT_FALSE(stage.has_metadata("myKey"));
    EXPECT_EQ(stage.metadata_count(), 0u);

    stage.set_metadata("myKey", Value::from_string("myValue"));
    EXPECT_TRUE(stage.has_metadata("myKey"));
    EXPECT_EQ(stage.metadata_count(), 1u);

    const Value* val = stage.get_metadata("myKey");
    EXPECT_TRUE(val != nullptr);
    if (val) {
        const std::string* s = val->as_string();
        EXPECT_TRUE(s != nullptr);
        if (s) EXPECT_EQ(*s, "myValue");
    }

    // Test removal
    EXPECT_TRUE(stage.remove_metadata("myKey"));
    EXPECT_FALSE(stage.has_metadata("myKey"));
    EXPECT_EQ(stage.metadata_count(), 0u);

    // Test convenience accessors
    stage.set_documentation("Test layer documentation");
    EXPECT_EQ(stage.documentation(), "Test layer documentation");

    stage.set_comment("A comment about this layer");
    EXPECT_EQ(stage.comment(), "A comment about this layer");

    stage.set_owner("test_user");
    EXPECT_EQ(stage.owner(), "test_user");
}

TEST(Stage_CustomLayerData) {
    Stage stage = Stage::create();

    // Test custom layer data
    EXPECT_FALSE(stage.has_custom_layer_data("creator"));
    EXPECT_EQ(stage.custom_layer_data_count(), 0u);

    stage.set_custom_layer_data("creator", Value::from_string("LightUSD"));
    stage.set_custom_layer_data("version", Value::from_int64(100));
    stage.set_custom_layer_data("enabled", Value::from_bool(true));

    EXPECT_TRUE(stage.has_custom_layer_data("creator"));
    EXPECT_TRUE(stage.has_custom_layer_data("version"));
    EXPECT_TRUE(stage.has_custom_layer_data("enabled"));
    EXPECT_EQ(stage.custom_layer_data_count(), 3u);

    const Value* creator = stage.get_custom_layer_data("creator");
    EXPECT_TRUE(creator != nullptr);
    if (creator) {
        const std::string* s = creator->as_string();
        EXPECT_TRUE(s != nullptr);
        if (s) EXPECT_EQ(*s, "LightUSD");
    }

    const Value* version = stage.get_custom_layer_data("version");
    EXPECT_TRUE(version != nullptr);
    if (version) {
        const int64_t* v = version->as_int64();
        EXPECT_TRUE(v != nullptr);
        if (v) EXPECT_EQ(*v, 100);
    }

    // Test removal
    EXPECT_TRUE(stage.remove_custom_layer_data("version"));
    EXPECT_FALSE(stage.has_custom_layer_data("version"));
    EXPECT_EQ(stage.custom_layer_data_count(), 2u);

    // Test keys
    std::vector<std::string> keys = stage.custom_layer_data_keys();
    EXPECT_EQ(keys.size(), 2u);
}

TEST(Stage_Metadata_Parser) {
    // Parse USDA with stage metadata
    const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
    documentation = "Test layer"
    comment = "Test comment"
    owner = "test_user"
    customLayerData = {
        string creator = "LightUSD"
        int64 version = 100
    }
)

def Xform "World"
{
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        EXPECT_EQ(result.stage.default_prim(), "World");
        EXPECT_EQ(result.stage.documentation(), "Test layer");
        EXPECT_EQ(result.stage.comment(), "Test comment");
        EXPECT_EQ(result.stage.owner(), "test_user");

        // Check custom layer data
        EXPECT_TRUE(result.stage.has_custom_layer_data("creator"));
        EXPECT_TRUE(result.stage.has_custom_layer_data("version"));

        const Value* creator = result.stage.get_custom_layer_data("creator");
        EXPECT_TRUE(creator != nullptr);
        if (creator) {
            const std::string* s = creator->as_string();
            EXPECT_TRUE(s != nullptr);
            if (s) EXPECT_EQ(*s, "LightUSD");
        }
    }
}

TEST(Stage_Metadata_Writer) {
    // Create stage with metadata
    Stage stage = Stage::create();
    stage.set_default_prim("Root");
    stage.set_documentation("Layer documentation");
    stage.set_comment("A layer comment");
    stage.set_owner("owner_name");
    stage.set_custom_layer_data("appName", Value::from_string("TestApp"));
    stage.set_custom_layer_data("buildNumber", Value::from_int64(42));

    Prim root("Root", "Xform");
    stage.add_root_prim(std::move(root));

    // Export
    std::string usda = stage.to_usda();
    printf("\nStage Metadata USDA:\n%s\n", usda.c_str());

    // Verify output contains metadata
    EXPECT_TRUE(usda.find("documentation = \"Layer documentation\"") != std::string::npos);
    EXPECT_TRUE(usda.find("comment = \"A layer comment\"") != std::string::npos);
    EXPECT_TRUE(usda.find("owner = \"owner_name\"") != std::string::npos);
    EXPECT_TRUE(usda.find("customLayerData") != std::string::npos);
    EXPECT_TRUE(usda.find("appName") != std::string::npos);
    EXPECT_TRUE(usda.find("buildNumber") != std::string::npos);
}

TEST(Stage_Metadata_Roundtrip) {
    // Create stage with full metadata
    Stage original = Stage::create();
    original.set_default_prim("TestPrim");
    original.set_documentation("Roundtrip documentation");
    original.set_comment("Roundtrip comment");
    original.set_owner("roundtrip_user");
    original.set_start_time_code(1.0);
    original.set_end_time_code(100.0);
    original.set_frames_per_second(30.0);
    original.set_meters_per_unit(1.0);
    original.set_up_axis("Z");
    original.set_custom_layer_data("testKey", Value::from_string("testValue"));
    original.set_custom_layer_data("testInt", Value::from_int64(999));

    Prim prim("TestPrim", "Xform");
    original.add_root_prim(std::move(prim));

    // Export
    std::string usda = original.to_usda();

    // Parse back
    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        // Verify all metadata preserved
        EXPECT_EQ(result.stage.default_prim(), "TestPrim");
        EXPECT_EQ(result.stage.documentation(), "Roundtrip documentation");
        EXPECT_EQ(result.stage.comment(), "Roundtrip comment");
        EXPECT_EQ(result.stage.owner(), "roundtrip_user");
        EXPECT_EQ(result.stage.start_time_code(), 1.0);
        EXPECT_EQ(result.stage.end_time_code(), 100.0);
        EXPECT_EQ(result.stage.frames_per_second(), 30.0);
        EXPECT_EQ(result.stage.meters_per_unit(), 1.0);
        EXPECT_EQ(result.stage.up_axis(), "Z");

        // Check custom layer data
        const Value* testKey = result.stage.get_custom_layer_data("testKey");
        EXPECT_TRUE(testKey != nullptr);
        if (testKey) {
            const std::string* s = testKey->as_string();
            EXPECT_TRUE(s != nullptr);
            if (s) EXPECT_EQ(*s, "testValue");
        }

        const Value* testInt = result.stage.get_custom_layer_data("testInt");
        EXPECT_TRUE(testInt != nullptr);
        if (testInt) {
            const int64_t* v = testInt->as_int64();
            EXPECT_TRUE(v != nullptr);
            if (v) EXPECT_EQ(*v, 999);
        }
    }
}

// ============================================================================
// Composition Tests
// ============================================================================

TEST(Composition_Reference) {
    // Test Reference struct
    Reference ref1("./model.usd", Path("/World"));
    EXPECT_EQ(ref1.asset_path, "./model.usd");
    EXPECT_EQ(ref1.prim_path.full_path(), "/World");
    EXPECT_FALSE(ref1.is_internal());

    Reference ref2(Path("/LocalClass"));
    EXPECT_TRUE(ref2.is_internal());
    EXPECT_TRUE(ref2.asset_path.empty());

    // Test ReferenceList
    ReferenceList refs;
    EXPECT_TRUE(refs.empty());

    refs.prepend(Reference("./first.usd"));
    refs.append(Reference("./last.usd"));
    EXPECT_FALSE(refs.empty());
    EXPECT_EQ(refs.prepended_items().size(), 1u);
    EXPECT_EQ(refs.appended_items().size(), 1u);
}

TEST(Composition_Payload) {
    Payload p("./heavy_asset.usd", Path("/HeavyModel"));
    EXPECT_EQ(p.asset_path, "./heavy_asset.usd");
    EXPECT_EQ(p.prim_path.full_path(), "/HeavyModel");

    PayloadList payloads;
    payloads.prepend(Payload("./payload1.usd"));
    EXPECT_EQ(payloads.prepended_items().size(), 1u);
}

TEST(Composition_PathList) {
    PathList paths;
    EXPECT_TRUE(paths.empty());

    paths.prepend(Path("/_class_Model"));
    paths.append(Path("/Shared/Base"));
    EXPECT_FALSE(paths.empty());
    EXPECT_EQ(paths.prepended_items().size(), 1u);
    EXPECT_EQ(paths.appended_items().size(), 1u);
}

TEST(Composition_LayerOffset) {
    LayerOffset offset1;
    EXPECT_TRUE(offset1.is_identity());
    EXPECT_EQ(offset1.offset, 0.0);
    EXPECT_EQ(offset1.scale, 1.0);

    LayerOffset offset2(10.0, 2.0);
    EXPECT_FALSE(offset2.is_identity());
    EXPECT_EQ(offset2.offset, 10.0);
    EXPECT_EQ(offset2.scale, 2.0);
}

TEST(Composition_Variant) {
    Variant var;
    var.set_name("LOD0");
    EXPECT_EQ(var.name(), "LOD0");
    EXPECT_FALSE(var.has_content());

    Prim content("content");
    Attribute attr(TypeId::Float);
    attr.set_default(Value::from_float(1.0f));
    content.set_attribute("value", std::move(attr));
    var.set_content(std::move(content));

    EXPECT_TRUE(var.has_content());
    const Prim* c = var.content();
    EXPECT_TRUE(c != nullptr);
    if (c) {
        EXPECT_TRUE(c->has_property("value"));
    }
}

TEST(Composition_VariantSet) {
    VariantSet vs;
    vs.set_name("lodVariant");
    EXPECT_EQ(vs.name(), "lodVariant");
    EXPECT_EQ(vs.variant_count(), 0u);

    Variant lod0;
    lod0.set_name("LOD0");
    vs.add_variant(std::move(lod0));

    Variant lod1;
    lod1.set_name("LOD1");
    vs.add_variant(std::move(lod1));

    EXPECT_EQ(vs.variant_count(), 2u);
    EXPECT_TRUE(vs.has_variant("LOD0"));
    EXPECT_TRUE(vs.has_variant("LOD1"));
    EXPECT_FALSE(vs.has_variant("LOD2"));

    const Variant* v = vs.get_variant("LOD0");
    EXPECT_TRUE(v != nullptr);
    if (v) {
        EXPECT_EQ(v->name(), "LOD0");
    }
}

TEST(Composition_PrimIndex) {
    PrimIndex index;
    EXPECT_FALSE(index.is_valid());

    index.set_path(Path("/World/Model"));
    EXPECT_TRUE(index.is_valid());
    EXPECT_EQ(index.path().full_path(), "/World/Model");

    // Root node should exist
    const CompositionNode& root = index.root_node();
    EXPECT_EQ(root.arc_type, CompositionArcType::None);
}

TEST(Composition_PrimIndexCache) {
    PrimIndexCache cache;
    EXPECT_EQ(cache.size(), 0u);

    PrimIndex index1;
    index1.set_path(Path("/World"));
    cache.insert(Path("/World"), std::move(index1));

    EXPECT_EQ(cache.size(), 1u);
    EXPECT_TRUE(cache.contains(Path("/World")));
    EXPECT_FALSE(cache.contains(Path("/Other")));

    const PrimIndex* found = cache.get(Path("/World"));
    EXPECT_TRUE(found != nullptr);
    if (found) {
        EXPECT_EQ(found->path().full_path(), "/World");
    }

    cache.remove(Path("/World"));
    EXPECT_EQ(cache.size(), 0u);
}

TEST(Prim_Composition_Instanceable) {
    Prim prim("Model", "Mesh");
    EXPECT_FALSE(prim.is_instanceable());

    prim.set_instanceable(true);
    EXPECT_TRUE(prim.is_instanceable());
}

TEST(Prim_Composition_References) {
    Prim prim("Model", "Xform");
    EXPECT_FALSE(prim.has_references());

    prim.references_mutable().prepend(Reference("./model.usd", Path("/Model")));
    EXPECT_TRUE(prim.has_references());
    EXPECT_EQ(prim.references().prepended_items().size(), 1u);
}

TEST(Prim_Composition_Inherits) {
    Prim prim("Model", "Xform");
    EXPECT_FALSE(prim.has_inherits());

    prim.inherits_mutable().prepend(Path("/_class_Model"));
    EXPECT_TRUE(prim.has_inherits());
    EXPECT_EQ(prim.inherits().prepended_items().size(), 1u);
}

TEST(Prim_Composition_VariantSets) {
    Prim prim("Model", "Xform");
    EXPECT_EQ(prim.variant_set_count(), 0u);

    VariantSet vs;
    vs.set_name("lodVariant");

    Variant lod0;
    lod0.set_name("high");
    vs.add_variant(std::move(lod0));

    Variant lod1;
    lod1.set_name("low");
    vs.add_variant(std::move(lod1));

    prim.add_variant_set(std::move(vs));
    EXPECT_EQ(prim.variant_set_count(), 1u);
    EXPECT_TRUE(prim.has_variant_set("lodVariant"));

    prim.set_variant_selection("lodVariant", "high");
    EXPECT_EQ(prim.get_variant_selection("lodVariant"), "high");
}

TEST(Composition_Writer_References) {
    Stage stage = Stage::create();

    Prim prim("Model", "Xform");
    prim.references_mutable().prepend(Reference("./base_model.usd", Path("/Model")));
    stage.add_root_prim(std::move(prim));

    std::string usda = stage.to_usda();
    EXPECT_TRUE(usda.find("prepend references") != std::string::npos);
    EXPECT_TRUE(usda.find("@./base_model.usd@") != std::string::npos);
    EXPECT_TRUE(usda.find("</Model>") != std::string::npos);
}

TEST(Composition_Writer_Inherits) {
    Stage stage = Stage::create();

    Prim prim("Model", "Xform");
    prim.inherits_mutable().prepend(Path("/_class_BaseModel"));
    stage.add_root_prim(std::move(prim));

    std::string usda = stage.to_usda();
    EXPECT_TRUE(usda.find("prepend inherits") != std::string::npos);
    EXPECT_TRUE(usda.find("</_class_BaseModel>") != std::string::npos);
}

TEST(Composition_Writer_VariantSet) {
    Stage stage = Stage::create();

    Prim prim("Model", "Xform");

    // Add variant set
    VariantSet vs;
    vs.set_name("displayColor");

    Variant red;
    red.set_name("red");
    Prim red_content("red");
    Attribute red_attr(TypeId::Float3);
    red_attr.set_default(Value::from_float3(1.0f, 0.0f, 0.0f));
    red_content.set_attribute("primvars:displayColor", std::move(red_attr));
    red.set_content(std::move(red_content));
    vs.add_variant(std::move(red));

    Variant blue;
    blue.set_name("blue");
    Prim blue_content("blue");
    Attribute blue_attr(TypeId::Float3);
    blue_attr.set_default(Value::from_float3(0.0f, 0.0f, 1.0f));
    blue_content.set_attribute("primvars:displayColor", std::move(blue_attr));
    blue.set_content(std::move(blue_content));
    vs.add_variant(std::move(blue));

    prim.add_variant_set(std::move(vs));
    prim.set_variant_selection("displayColor", "red");

    stage.add_root_prim(std::move(prim));

    std::string usda = stage.to_usda();
    printf("\nVariantSet USDA:\n%s\n", usda.c_str());

    EXPECT_TRUE(usda.find("variantSet \"displayColor\"") != std::string::npos);
    EXPECT_TRUE(usda.find("\"red\"") != std::string::npos);
    EXPECT_TRUE(usda.find("\"blue\"") != std::string::npos);
    EXPECT_TRUE(usda.find("variants = {") != std::string::npos);
    EXPECT_TRUE(usda.find("displayColor = \"red\"") != std::string::npos);
}

TEST(Composition_Parser_References) {
    const char* usda = R"(#usda 1.0

def Xform "Model" (
    prepend references = [
        @./base_model.usd@</Model>
    ]
)
{
}
)";

    auto result = read_usda_string(usda);
    if (!result.ok()) {
        printf("Parse error: %s\n", result.error_summary.c_str());
    }
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        auto prim_result = result.stage.get_prim_at_path(Path("/Model"));
        EXPECT_TRUE(prim_result.ok());
        if (prim_result.ok()) {
            const Prim* prim = prim_result.value();
            EXPECT_TRUE(prim->has_references());
            const auto& refs = prim->references().prepended_items();
            EXPECT_EQ(refs.size(), 1u);
            if (!refs.empty()) {
                EXPECT_EQ(refs[0].asset_path, "./base_model.usd");
                EXPECT_EQ(refs[0].prim_path.full_path(), "/Model");
            }
        }
    }
}

TEST(Composition_Parser_Inherits) {
    const char* usda = R"(#usda 1.0

def Xform "Model" (
    prepend inherits = [</_class_BaseModel>]
)
{
}
)";

    auto result = read_usda_string(usda);
    if (!result.ok()) {
        printf("Parse error: %s\n", result.error_summary.c_str());
    }
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        auto prim_result = result.stage.get_prim_at_path(Path("/Model"));
        EXPECT_TRUE(prim_result.ok());
        if (prim_result.ok()) {
            const Prim* prim = prim_result.value();
            EXPECT_TRUE(prim->has_inherits());
            const auto& paths = prim->inherits().prepended_items();
            EXPECT_EQ(paths.size(), 1u);
            if (!paths.empty()) {
                EXPECT_EQ(paths[0].full_path(), "/_class_BaseModel");
            }
        }
    }
}

TEST(Composition_Parser_Instanceable) {
    const char* usda = R"(#usda 1.0

def Xform "Instance" (
    instanceable = true
)
{
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        auto prim_result = result.stage.get_prim_at_path(Path("/Instance"));
        EXPECT_TRUE(prim_result.ok());
        if (prim_result.ok()) {
            EXPECT_TRUE(prim_result.value()->is_instanceable());
        }
    }
}

TEST(Prim_AssetInfo) {
    Prim prim("Model", "Mesh");

    // Initially empty
    EXPECT_EQ(prim.asset_info_count(), 0u);
    EXPECT_TRUE(prim.asset_identifier().empty());
    EXPECT_TRUE(prim.asset_name().empty());
    EXPECT_TRUE(prim.asset_version().empty());

    // Set asset info
    prim.set_asset_identifier("@./model.usd@");
    prim.set_asset_name("MyModel");
    prim.set_asset_version("1.0.0");
    prim.set_asset_info("author", Value::from_string("Artist Name"));

    EXPECT_EQ(prim.asset_info_count(), 4u);
    EXPECT_EQ(prim.asset_identifier(), "@./model.usd@");
    EXPECT_EQ(prim.asset_name(), "MyModel");
    EXPECT_EQ(prim.asset_version(), "1.0.0");

    const Value* author = prim.get_asset_info("author");
    EXPECT_TRUE(author != nullptr);
    if (author) {
        const std::string* s = author->as_string();
        EXPECT_TRUE(s != nullptr);
        if (s) EXPECT_EQ(*s, "Artist Name");
    }

    // Get all keys
    std::vector<std::string> keys = prim.asset_info_keys();
    EXPECT_EQ(keys.size(), 4u);
}

TEST(Prim_AssetInfo_Writer) {
    Stage stage = Stage::create();

    Prim prim("Model", "Mesh");
    prim.set_asset_identifier("@./source_model.usd@");
    prim.set_asset_name("TestModel");
    prim.set_asset_version("2.0");

    stage.add_root_prim(std::move(prim));

    std::string usda = stage.to_usda();
    printf("\nAssetInfo USDA:\n%s\n", usda.c_str());

    EXPECT_TRUE(usda.find("assetInfo = {") != std::string::npos);
    EXPECT_TRUE(usda.find("identifier") != std::string::npos);
    EXPECT_TRUE(usda.find("name") != std::string::npos);
    EXPECT_TRUE(usda.find("version") != std::string::npos);
}

TEST(Stage_Sublayers) {
    Stage stage = Stage::create();

    // Initially empty
    EXPECT_EQ(stage.sublayer_count(), 0u);

    // Add sublayers
    stage.add_sublayer("./base.usd");
    stage.add_sublayer("./overrides.usd", LayerOffset(10.0, 2.0));

    EXPECT_EQ(stage.sublayer_count(), 2u);

    const SubLayer* sl0 = stage.sublayer(0);
    EXPECT_TRUE(sl0 != nullptr);
    if (sl0) {
        EXPECT_EQ(sl0->asset_path, "./base.usd");
        EXPECT_TRUE(sl0->layer_offset.is_identity());
    }

    const SubLayer* sl1 = stage.sublayer(1);
    EXPECT_TRUE(sl1 != nullptr);
    if (sl1) {
        EXPECT_EQ(sl1->asset_path, "./overrides.usd");
        EXPECT_FALSE(sl1->layer_offset.is_identity());
        EXPECT_EQ(sl1->layer_offset.offset, 10.0);
        EXPECT_EQ(sl1->layer_offset.scale, 2.0);
    }

    // Remove sublayer
    EXPECT_TRUE(stage.remove_sublayer("./base.usd"));
    EXPECT_EQ(stage.sublayer_count(), 1u);
}

TEST(Stage_Sublayers_Writer) {
    Stage stage = Stage::create();

    stage.add_sublayer("./base_layer.usd");
    stage.add_sublayer("./animation.usd", LayerOffset(100.0, 1.0));

    Prim prim("Root", "Xform");
    stage.add_root_prim(std::move(prim));

    std::string usda = stage.to_usda();
    printf("\nSublayers USDA:\n%s\n", usda.c_str());

    EXPECT_TRUE(usda.find("subLayers = [") != std::string::npos);
    EXPECT_TRUE(usda.find("@./base_layer.usd@") != std::string::npos);
    EXPECT_TRUE(usda.find("@./animation.usd@") != std::string::npos);
    EXPECT_TRUE(usda.find("offset = 100") != std::string::npos);
}

TEST(Prim_Property_Ordering) {
    Prim prim("Test", "Xform");

    // Add properties in specific order
    Attribute attr_c(TypeId::Float);
    attr_c.set_default(Value::from_float(3.0f));
    prim.set_attribute("c_prop", std::move(attr_c));

    Attribute attr_a(TypeId::Float);
    attr_a.set_default(Value::from_float(1.0f));
    prim.set_attribute("a_prop", std::move(attr_a));

    Attribute attr_b(TypeId::Float);
    attr_b.set_default(Value::from_float(2.0f));
    prim.set_attribute("b_prop", std::move(attr_b));

    // Should be in insertion order: c, a, b
    auto names = prim.property_names();
    EXPECT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "c_prop");
    EXPECT_EQ(names[1], "a_prop");
    EXPECT_EQ(names[2], "b_prop");

    // Reorder lexicographically
    prim.reorder_properties_lexicographic();
    names = prim.property_names();
    EXPECT_EQ(names[0], "a_prop");
    EXPECT_EQ(names[1], "b_prop");
    EXPECT_EQ(names[2], "c_prop");

    // Explicit reorder
    EXPECT_TRUE(prim.set_property_order({"b_prop", "c_prop", "a_prop"}));
    names = prim.property_names();
    EXPECT_EQ(names[0], "b_prop");
    EXPECT_EQ(names[1], "c_prop");
    EXPECT_EQ(names[2], "a_prop");

    // Invalid reorder (non-existent property)
    EXPECT_FALSE(prim.set_property_order({"z_prop"}));
}

TEST(Prim_Child_Ordering) {
    Prim parent("Parent", "Xform");

    // Add children in specific order
    Prim child_c("charlie");
    Prim child_a("alpha");
    Prim child_b("beta");

    parent.add_child(std::move(child_c));
    parent.add_child(std::move(child_a));
    parent.add_child(std::move(child_b));

    // Should be in insertion order
    auto names = parent.child_names();
    EXPECT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "charlie");
    EXPECT_EQ(names[1], "alpha");
    EXPECT_EQ(names[2], "beta");

    // Reorder lexicographically
    parent.reorder_children_lexicographic();
    names = parent.child_names();
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
    EXPECT_EQ(names[2], "charlie");

    // Explicit reorder
    EXPECT_TRUE(parent.set_child_order({"beta", "charlie", "alpha"}));
    names = parent.child_names();
    EXPECT_EQ(names[0], "beta");
    EXPECT_EQ(names[1], "charlie");
    EXPECT_EQ(names[2], "alpha");

    // Invalid reorder (non-existent child)
    EXPECT_FALSE(parent.set_child_order({"nonexistent"}));
}

TEST(Composition_Parser_VariantSelection) {
    const char* usda = R"(#usda 1.0

def Xform "Model" (
    variants = {
        lodVariant = "high"
    }
)
{
}
)";

    auto result = read_usda_string(usda);
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        auto prim_result = result.stage.get_prim_at_path(Path("/Model"));
        EXPECT_TRUE(prim_result.ok());
        if (prim_result.ok()) {
            EXPECT_EQ(prim_result.value()->get_variant_selection("lodVariant"), "high");
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n=== LightUSD Tests ===\n\n");

    // Tests are auto-registered and run

    printf("\n=== Results ===\n");
    printf("Tests: %d, Passed: %d, Failed: %d\n",
           g_test_count, g_pass_count, g_fail_count);

    return g_fail_count > 0 ? 1 : 0;
}
