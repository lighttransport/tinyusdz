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

TEST(Prim_AssetInfo_Parser) {
    const char* usda = R"(#usda 1.0

def Mesh "Model" (
    assetInfo = {
        string identifier = "@./source.usd@"
        string name = "TestModel"
        string version = "1.0"
    }
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
            EXPECT_EQ(prim->asset_info_count(), 3u);
            EXPECT_EQ(prim->asset_identifier(), "@./source.usd@");
            EXPECT_EQ(prim->asset_name(), "TestModel");
            EXPECT_EQ(prim->asset_version(), "1.0");
        }
    }
}

TEST(Stage_Sublayers_Parser) {
    const char* usda = R"(#usda 1.0
(
    subLayers = [
        @./base.usd@,
        @./animation.usd@ (offset = 100; scale = 2)
    ]
)

def Xform "Root"
{
}
)";

    auto result = read_usda_string(usda);
    if (!result.ok()) {
        printf("Parse error: %s\n", result.error_summary.c_str());
    }
    EXPECT_TRUE(result.ok());

    if (result.ok()) {
        EXPECT_EQ(result.stage.sublayer_count(), 2u);

        const SubLayer* sl0 = result.stage.sublayer(0);
        EXPECT_TRUE(sl0 != nullptr);
        if (sl0) {
            EXPECT_EQ(sl0->asset_path, "./base.usd");
            EXPECT_TRUE(sl0->layer_offset.is_identity());
        }

        const SubLayer* sl1 = result.stage.sublayer(1);
        EXPECT_TRUE(sl1 != nullptr);
        if (sl1) {
            EXPECT_EQ(sl1->asset_path, "./animation.usd");
            EXPECT_EQ(sl1->layer_offset.offset, 100.0);
            EXPECT_EQ(sl1->layer_offset.scale, 2.0);
        }
    }
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
// Clips Tests
// ============================================================================

TEST(Clips_ClipSet_Basic) {
    ClipSet cs;
    EXPECT_TRUE(cs.name().empty());
    EXPECT_TRUE(cs.asset_paths().empty());
    EXPECT_TRUE(cs.active().empty());
    EXPECT_TRUE(cs.times().empty());
    EXPECT_FALSE(cs.has_manifest());
    EXPECT_FALSE(cs.has_template());
    EXPECT_FALSE(cs.is_valid());

    // Set asset paths
    cs.set_asset_paths({"@./walk.usd@", "@./run.usd@"});
    EXPECT_EQ(cs.asset_paths().size(), 2u);
    EXPECT_TRUE(cs.is_valid());

    // Add active entries
    cs.add_active(0.0, 0);    // Walk at frame 0
    cs.add_active(24.0, 1);   // Run at frame 24
    EXPECT_EQ(cs.active().size(), 2u);

    // Add time mappings
    cs.add_time(0.0, 0.0);
    cs.add_time(24.0, 24.0);
    cs.add_time(48.0, 48.0);
    EXPECT_EQ(cs.times().size(), 3u);

    // Set prim path
    cs.set_prim_path(Path("/Character"));
    EXPECT_EQ(cs.prim_path().prim_part(), "/Character");

    // Manifest
    cs.set_manifest_asset_path("@./manifest.usd@");
    EXPECT_TRUE(cs.has_manifest());
    EXPECT_EQ(cs.manifest_asset_path(), "@./manifest.usd@");
}

TEST(Clips_ClipSet_Template) {
    ClipSet cs;

    // Set template path
    cs.set_template_asset_path("clips/frame.###.usd");
    cs.set_template_start_time(1.0);
    cs.set_template_end_time(10.0);
    cs.set_template_stride(1.0);

    EXPECT_TRUE(cs.has_template());
    EXPECT_TRUE(cs.is_valid());  // Valid because of template
    EXPECT_EQ(cs.template_start_time(), 1.0);
    EXPECT_EQ(cs.template_end_time(), 10.0);
    EXPECT_EQ(cs.template_stride(), 1.0);
}

TEST(Clips_ClipSets) {
    ClipSets sets;
    EXPECT_TRUE(sets.empty());
    EXPECT_EQ(sets.size(), 0u);
    EXPECT_FALSE(sets.has("default"));

    // Add a clip set
    ClipSet cs1;
    cs1.set_asset_paths({"@./walk.usd@"});
    cs1.add_active(0.0, 0);
    sets.set("default", cs1);

    EXPECT_FALSE(sets.empty());
    EXPECT_EQ(sets.size(), 1u);
    EXPECT_TRUE(sets.has("default"));

    // Get clip set
    const ClipSet* retrieved = sets.get("default");
    EXPECT_TRUE(retrieved != nullptr);
    EXPECT_EQ(retrieved->name(), "default");
    EXPECT_EQ(retrieved->asset_paths().size(), 1u);

    // Add another clip set
    ClipSet cs2;
    cs2.set_asset_paths({"@./run.usd@"});
    sets.set("run", cs2);
    EXPECT_EQ(sets.size(), 2u);

    // Get names
    auto names = sets.names();
    EXPECT_EQ(names.size(), 2u);

    // Remove
    EXPECT_TRUE(sets.remove("run"));
    EXPECT_EQ(sets.size(), 1u);
    EXPECT_FALSE(sets.has("run"));
}

TEST(Clips_TemplateExpansion) {
    // Test hash-based template
    EXPECT_EQ(expand_template_path("frame.###.usd", 1.0), "frame.001.usd");
    EXPECT_EQ(expand_template_path("frame.###.usd", 42.0), "frame.042.usd");
    EXPECT_EQ(expand_template_path("frame.####.usd", 123.0), "frame.0123.usd");

    // Test printf-style template
    EXPECT_EQ(expand_template_path("frame.%03d.usd", 1.0), "frame.001.usd");
    EXPECT_EQ(expand_template_path("frame.%04d.usd", 42.0), "frame.0042.usd");

    // Generate paths
    auto paths = generate_template_paths("frame.###.usd", 1.0, 3.0, 1.0);
    EXPECT_EQ(paths.size(), 3u);
    EXPECT_EQ(paths[0], "frame.001.usd");
    EXPECT_EQ(paths[1], "frame.002.usd");
    EXPECT_EQ(paths[2], "frame.003.usd");
}

TEST(Clips_TimeUtilities) {
    // Test lerp
    EXPECT_EQ(lerp_time(0.0, 10.0, 0.0), 0.0);
    EXPECT_EQ(lerp_time(0.0, 10.0, 0.5), 5.0);
    EXPECT_EQ(lerp_time(0.0, 10.0, 1.0), 10.0);

    // Test inverse_lerp
    EXPECT_EQ(inverse_lerp(0.0, 10.0, 0.0), 0.0);
    EXPECT_EQ(inverse_lerp(0.0, 10.0, 5.0), 0.5);
    EXPECT_EQ(inverse_lerp(0.0, 10.0, 10.0), 1.0);

    // Clamping
    EXPECT_EQ(inverse_lerp(0.0, 10.0, -5.0), 0.0);  // Below range
    EXPECT_EQ(inverse_lerp(0.0, 10.0, 15.0), 1.0);  // Above range
}

TEST(Clips_ClipResolver_Basic) {
    ClipSet cs;
    cs.set_asset_paths({"@./walk.usd@", "@./run.usd@"});
    cs.set_prim_path(Path("/Character"));

    // Active array: walk from 0-24, run from 24+
    cs.add_active(0.0, 0);   // Walk (index 0) at time 0
    cs.add_active(24.0, 1);  // Run (index 1) at time 24

    // Times array: identity mapping
    cs.add_time(0.0, 0.0);
    cs.add_time(48.0, 48.0);

    ClipResolver resolver;
    resolver.set_clip_set(cs);
    EXPECT_TRUE(resolver.is_valid());

    // Test active clip at different times
    EXPECT_EQ(resolver.active_clip_index(0.0), 0);   // Walk
    EXPECT_EQ(resolver.active_clip_index(12.0), 0);  // Still walk
    EXPECT_EQ(resolver.active_clip_index(24.0), 1);  // Run
    EXPECT_EQ(resolver.active_clip_index(36.0), 1);  // Still run

    // Test resolve
    ClipInfo info = resolver.resolve(0.0);
    EXPECT_TRUE(info.is_valid());
    EXPECT_EQ(info.clip_index, 0u);
    EXPECT_EQ(info.asset_path, "@./walk.usd@");
    EXPECT_EQ(info.prim_path.prim_part(), "/Character");

    info = resolver.resolve(30.0);
    EXPECT_TRUE(info.is_valid());
    EXPECT_EQ(info.clip_index, 1u);
    EXPECT_EQ(info.asset_path, "@./run.usd@");
}

TEST(Clips_ClipResolver_TimeMapping) {
    ClipSet cs;
    cs.set_asset_paths({"@./anim.usd@"});
    cs.add_active(0.0, 0);

    // Non-linear time mapping: stage time 0-100 maps to clip time 0-50
    cs.add_time(0.0, 0.0);
    cs.add_time(100.0, 50.0);

    ClipResolver resolver;
    resolver.set_clip_set(cs);

    // Test time mapping
    EXPECT_EQ(resolver.map_time(0.0), 0.0);
    EXPECT_EQ(resolver.map_time(50.0), 25.0);
    EXPECT_EQ(resolver.map_time(100.0), 50.0);

    ClipInfo info = resolver.resolve(50.0);
    EXPECT_EQ(info.clip_time, 25.0);
}

TEST(Clips_ClipResolver_Boundaries) {
    ClipSet cs;
    cs.set_asset_paths({"@./a.usd@", "@./b.usd@", "@./c.usd@"});
    cs.add_active(0.0, 0);
    cs.add_active(10.0, 1);
    cs.add_active(20.0, 2);

    ClipResolver resolver;
    resolver.set_clip_set(cs);

    auto boundaries = resolver.clip_boundaries();
    EXPECT_EQ(boundaries.size(), 3u);
    EXPECT_EQ(boundaries[0], 0.0);
    EXPECT_EQ(boundaries[1], 10.0);
    EXPECT_EQ(boundaries[2], 20.0);
}

TEST(Clips_Prim_Integration) {
    Prim prim("Character", "SkelRoot");

    EXPECT_FALSE(prim.has_clips());
    EXPECT_TRUE(prim.clip_set_names().empty());

    // Add a clip set
    ClipSet cs;
    cs.set_asset_paths({"@./walk.usd@", "@./run.usd@"});
    cs.set_prim_path(Path("/Character"));
    cs.add_active(0.0, 0);
    cs.add_active(24.0, 1);

    prim.set_clip_set("default", cs);

    EXPECT_TRUE(prim.has_clips());
    EXPECT_TRUE(prim.has_clip_set("default"));
    EXPECT_EQ(prim.clip_set_names().size(), 1u);

    // Get clip set
    const ClipSet* retrieved = prim.get_clip_set("default");
    EXPECT_TRUE(retrieved != nullptr);
    EXPECT_EQ(retrieved->asset_paths().size(), 2u);

    // Create resolver from prim
    ClipResolver resolver = prim.create_clip_resolver();
    EXPECT_TRUE(resolver.is_valid());
    EXPECT_EQ(resolver.active_clip_index(0.0), 0);
    EXPECT_EQ(resolver.active_clip_index(24.0), 1);

    // Remove clip set
    EXPECT_TRUE(prim.remove_clip_set("default"));
    EXPECT_FALSE(prim.has_clips());
}

TEST(Clips_Copy_Move) {
    ClipSet cs1;
    cs1.set_name("test");
    cs1.set_asset_paths({"@./a.usd@"});
    cs1.add_active(0.0, 0);

    // Copy
    ClipSet cs2 = cs1;
    EXPECT_EQ(cs2.name(), "test");
    EXPECT_EQ(cs2.asset_paths().size(), 1u);

    // Move
    ClipSet cs3 = std::move(cs2);
    EXPECT_EQ(cs3.name(), "test");

    // ClipSets copy
    ClipSets sets1;
    sets1.set("default", cs1);

    ClipSets sets2 = sets1;
    EXPECT_TRUE(sets2.has("default"));

    // ClipResolver copy
    ClipResolver r1;
    r1.set_clip_set(cs1);

    ClipResolver r2 = r1;
    EXPECT_TRUE(r2.is_valid());
}

// ============================================================================
// TypedArray Tests
// ============================================================================

TEST(TypedArray_Basic) {
    TypedArray<int> arr;
    EXPECT_TRUE(arr.empty());
    EXPECT_EQ(arr.size(), 0u);

    arr.push_back(10);
    arr.push_back(20);
    arr.push_back(30);

    EXPECT_EQ(arr.size(), 3u);
    EXPECT_FALSE(arr.empty());
    EXPECT_EQ(arr[0], 10);
    EXPECT_EQ(arr[1], 20);
    EXPECT_EQ(arr[2], 30);

    EXPECT_EQ(arr.front(), 10);
    EXPECT_EQ(arr.back(), 30);

    arr.pop_back();
    EXPECT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr.back(), 20);
}

TEST(TypedArray_Initializer) {
    TypedArray<int> arr = {1, 2, 3, 4, 5};
    EXPECT_EQ(arr.size(), 5u);
    EXPECT_EQ(arr[0], 1);
    EXPECT_EQ(arr[4], 5);

    TypedArray<double> arr2(10, 3.14);
    EXPECT_EQ(arr2.size(), 10u);
    EXPECT_EQ(arr2[5], 3.14);
}

TEST(TypedArray_Iterator) {
    TypedArray<int> arr = {10, 20, 30, 40, 50};

    // Forward iteration
    int sum = 0;
    for (auto it = arr.begin(); it != arr.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 150);

    // Range-based for
    sum = 0;
    for (int val : arr) {
        sum += val;
    }
    EXPECT_EQ(sum, 150);

    // Iterator arithmetic
    auto it = arr.begin();
    EXPECT_EQ(*it, 10);
    it += 2;
    EXPECT_EQ(*it, 30);
    it -= 1;
    EXPECT_EQ(*it, 20);

    EXPECT_EQ(arr.end() - arr.begin(), 5);
}

TEST(TypedArray_Resize) {
    TypedArray<int> arr;
    arr.resize(100);
    EXPECT_EQ(arr.size(), 100u);
    EXPECT_EQ(arr[50], 0);  // Default initialized

    arr.resize(200, 42);
    EXPECT_EQ(arr.size(), 200u);
    EXPECT_EQ(arr[150], 42);  // New elements are 42
    EXPECT_EQ(arr[50], 0);    // Old elements unchanged

    arr.resize(50);
    EXPECT_EQ(arr.size(), 50u);
}

TEST(TypedArray_Reserve) {
    TypedArray<int> arr;
    arr.reserve(1000);
    EXPECT_TRUE(arr.capacity() >= 1000u);
    EXPECT_EQ(arr.size(), 0u);  // Size unchanged

    for (int i = 0; i < 100; ++i) {
        arr.push_back(i);
    }
    EXPECT_EQ(arr.size(), 100u);
}

TEST(TypedArray_InsertErase) {
    TypedArray<int> arr = {1, 2, 3, 4, 5};

    // Insert at beginning
    arr.insert(arr.begin(), 0);
    EXPECT_EQ(arr.size(), 6u);
    EXPECT_EQ(arr[0], 0);
    EXPECT_EQ(arr[1], 1);

    // Insert in middle
    arr.insert(arr.begin() + 3, 100);
    EXPECT_EQ(arr.size(), 7u);
    EXPECT_EQ(arr[3], 100);

    // Erase
    arr.erase(arr.begin());
    EXPECT_EQ(arr.size(), 6u);
    EXPECT_EQ(arr[0], 1);

    // Erase range
    arr.erase(arr.begin() + 1, arr.begin() + 3);
    EXPECT_EQ(arr.size(), 4u);
}

TEST(TypedArray_CopyMove) {
    TypedArray<int> arr1 = {1, 2, 3, 4, 5};

    // Copy
    TypedArray<int> arr2 = arr1;
    EXPECT_EQ(arr2.size(), 5u);
    EXPECT_EQ(arr2[2], 3);
    arr2[2] = 100;
    EXPECT_EQ(arr1[2], 3);  // Original unchanged

    // Move
    TypedArray<int> arr3 = std::move(arr1);
    EXPECT_EQ(arr3.size(), 5u);
    EXPECT_EQ(arr3[2], 3);
    EXPECT_TRUE(arr1.empty());
}

TEST(TypedArray_SBO) {
    // TypedArray<int, 16> should fit 4 ints in SBO (4 * 4 = 16 bytes)
    TypedArray<int, 16> arr;

    // Add elements within SBO
    arr.push_back(1);
    arr.push_back(2);
    arr.push_back(3);
    arr.push_back(4);

    EXPECT_EQ(arr.size(), 4u);
    EXPECT_TRUE(arr.is_contiguous());
    EXPECT_TRUE(arr.data() != nullptr);

    // Add more to exceed SBO
    for (int i = 0; i < 100; ++i) {
        arr.push_back(i);
    }

    EXPECT_EQ(arr.size(), 104u);
    EXPECT_EQ(arr[0], 1);
    EXPECT_EQ(arr[4], 0);
}

TEST(TypedArray_LargeChunked) {
    // Create a large array that will use chunked storage
    TypedArray<int> arr;
    arr.set_chunk_size(1024);  // 1KB chunks for testing

    // Add many elements
    size_t count = 10000;
    for (size_t i = 0; i < count; ++i) {
        arr.push_back(static_cast<int>(i));
    }

    EXPECT_EQ(arr.size(), count);

    // Verify all values
    bool all_correct = true;
    for (size_t i = 0; i < count; ++i) {
        if (arr[i] != static_cast<int>(i)) {
            all_correct = false;
            break;
        }
    }
    EXPECT_TRUE(all_correct);

    // Test iteration
    int sum = 0;
    for (size_t i = 0; i < 100; ++i) {
        sum += arr[i];
    }
    EXPECT_EQ(sum, 4950);  // Sum 0..99
}

TEST(TypedArray_EmplaceBack) {
    struct Point {
        int x, y;
        Point() : x(0), y(0) {}
        Point(int x_, int y_) : x(x_), y(y_) {}
    };

    TypedArray<Point> arr;
    arr.emplace_back(10, 20);
    arr.emplace_back(30, 40);

    EXPECT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr[0].x, 10);
    EXPECT_EQ(arr[0].y, 20);
    EXPECT_EQ(arr[1].x, 30);
    EXPECT_EQ(arr[1].y, 40);
}

TEST(TypedArray_Clear) {
    TypedArray<int> arr = {1, 2, 3, 4, 5};
    EXPECT_EQ(arr.size(), 5u);

    arr.clear();
    EXPECT_TRUE(arr.empty());
    EXPECT_EQ(arr.size(), 0u);

    // Can reuse after clear
    arr.push_back(100);
    EXPECT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0], 100);
}

TEST(TypedArray_At) {
    TypedArray<int> arr = {1, 2, 3};

    EXPECT_EQ(arr.at(0), 1);
    EXPECT_EQ(arr.at(1), 2);
    EXPECT_EQ(arr.at(2), 3);

    bool threw = false;
    try {
        arr.at(10);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(TypedArray_Comparison) {
    TypedArray<int> arr1 = {1, 2, 3};
    TypedArray<int> arr2 = {1, 2, 3};
    TypedArray<int> arr3 = {1, 2, 4};
    TypedArray<int> arr4 = {1, 2};

    EXPECT_TRUE(arr1 == arr2);
    EXPECT_FALSE(arr1 != arr2);
    EXPECT_FALSE(arr1 == arr3);
    EXPECT_FALSE(arr1 == arr4);
}

TEST(Buffer_Basic) {
    Buffer<16> buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(buf.mode(), Buffer<16>::StorageMode::Inline);

    buf.resize(10);
    EXPECT_EQ(buf.size(), 10u);
    EXPECT_EQ(buf.mode(), Buffer<16>::StorageMode::Inline);

    // Fill with data
    for (size_t i = 0; i < 10; ++i) {
        buf[i] = static_cast<uint8_t>(i * 10);
    }

    EXPECT_EQ(buf[5], 50);
}

TEST(Buffer_Chunked) {
    Buffer<16> buf(1024 * 100, Buffer<16>::StorageMode::Chunked);  // 100KB

    EXPECT_EQ(buf.size(), 1024u * 100);
    EXPECT_EQ(buf.mode(), Buffer<16>::StorageMode::Chunked);
    EXPECT_TRUE(buf.chunk_count() > 1);

    // Write and read across chunks
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<uint8_t>(i % 256);
    }

    bool all_correct = true;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] != static_cast<uint8_t>(i % 256)) {
            all_correct = false;
            break;
        }
    }
    EXPECT_TRUE(all_correct);
}

TEST(Buffer_Contiguous) {
    Buffer<16> buf(1000, Buffer<16>::StorageMode::Contiguous);

    EXPECT_EQ(buf.size(), 1000u);
    EXPECT_EQ(buf.mode(), Buffer<16>::StorageMode::Contiguous);
    EXPECT_TRUE(buf.is_contiguous());
    EXPECT_TRUE(buf.data() != nullptr);

    for (size_t i = 0; i < 1000; ++i) {
        buf[i] = static_cast<uint8_t>(i % 256);
    }

    EXPECT_EQ(buf[500], static_cast<uint8_t>(500 % 256));
}

TEST(Buffer_CopyMove) {
    Buffer<16> buf1(100, Buffer<16>::StorageMode::Chunked);
    for (size_t i = 0; i < 100; ++i) {
        buf1[i] = static_cast<uint8_t>(i);
    }

    // Copy
    Buffer<16> buf2 = buf1;
    EXPECT_EQ(buf2.size(), 100u);
    EXPECT_EQ(buf2[50], 50);

    // Move
    Buffer<16> buf3 = std::move(buf1);
    EXPECT_EQ(buf3.size(), 100u);
    EXPECT_EQ(buf3[50], 50);
    EXPECT_TRUE(buf1.empty());
}

// ============================================================================
// PCP (Prim Cache Populate) Tests
// ============================================================================

TEST(PCP_LayerStack_Basic) {
    // Test basic layer stack creation
    LayerRegistry registry;

    // Create a simple layer
    Layer layer;
    layer.set_identifier("test_layer.usda");

    // Test identifier
    PcpLayerStackIdentifier id;
    id.root_layer_path = "test_layer.usda";
    EXPECT_TRUE(id.is_valid());
    EXPECT_TRUE(id.session_layer_path.empty());

    // Test identifier with session
    PcpLayerStackIdentifier id2;
    id2.root_layer_path = "test.usda";
    id2.session_layer_path = "session.usda";
    EXPECT_TRUE(id2.is_valid());
    EXPECT_FALSE(id2.session_layer_path.empty());

    // Invalid identifier (no root)
    PcpLayerStackIdentifier id3;
    EXPECT_FALSE(id3.is_valid());
}

TEST(PCP_Node_Basic) {
    // Test PcpNode creation
    PcpNode node;
    node.arc_type = CompositionArcType::None;
    node.site_path = Path("/World");
    node.layer_stack = nullptr;
    node.sibling_index = 0;
    node.depth = 0;
    node.has_specs = true;
    node.is_culled = false;

    EXPECT_TRUE(node.is_root());
    EXPECT_EQ(node.site_path.prim_part(), "/World");
    EXPECT_EQ(node.depth, 0);
    EXPECT_TRUE(node.has_specs);
    EXPECT_TRUE(node.children.empty());
}

TEST(PCP_Node_Strength_Order) {
    // Test that children maintain LIVRPS strength order
    PcpNode root;
    root.arc_type = CompositionArcType::None;
    root.site_path = Path("/World");

    // Insert specialize (weakest)
    PcpNode spec_node;
    spec_node.arc_type = CompositionArcType::Specialize;
    spec_node.sibling_index = 0;
    spec_node.depth = 1;
    root.insert_child(spec_node);

    // Insert inherit (stronger than specialize)
    PcpNode inherit_node;
    inherit_node.arc_type = CompositionArcType::Inherit;
    inherit_node.sibling_index = 0;
    inherit_node.depth = 1;
    root.insert_child(inherit_node);

    // Insert reference (between inherit and specialize)
    PcpNode ref_node;
    ref_node.arc_type = CompositionArcType::Reference;
    ref_node.sibling_index = 0;
    ref_node.depth = 1;
    root.insert_child(ref_node);

    // Check order: Inherit < Reference < Specialize
    EXPECT_EQ(root.children.size(), 3u);
    if (root.children.size() == 3) {
        EXPECT_EQ(root.children[0].arc_type, CompositionArcType::Inherit);
        EXPECT_EQ(root.children[1].arc_type, CompositionArcType::Reference);
        EXPECT_EQ(root.children[2].arc_type, CompositionArcType::Specialize);
    }
}

TEST(PCP_PrimIndex_Basic) {
    // Test basic PcpPrimIndex creation
    PcpPrimIndex index;
    index.set_path(Path("/World/Model"));

    EXPECT_EQ(index.path().prim_part(), "/World/Model");
    EXPECT_FALSE(index.is_valid());  // Not finalized yet
    EXPECT_FALSE(index.has_specs());  // No specs yet
    EXPECT_TRUE(index.child_names().empty());
    EXPECT_TRUE(index.property_names().empty());
    EXPECT_FALSE(index.has_errors());
}

TEST(PCP_PrimIndex_WithSpecs) {
    // Test PcpPrimIndex with prim stack entries
    PcpPrimIndex index;
    index.set_path(Path("/World/Model"));

    // Create a layer to use in prim stack
    Layer layer;
    layer.set_identifier("model.usda");

    // Add prim stack entry
    PrimStackEntry entry;
    entry.layer = &layer;
    entry.path = Path("/World/Model");
    index.add_prim_stack_entry(entry);

    // Set child and property names
    std::vector<Token> children = {Token("Child1"), Token("Child2")};
    std::vector<Token> props = {Token("xformOp:translate"), Token("visibility")};
    index.set_child_names(children);
    index.set_property_names(props);

    // Finalize
    index.finalize();

    EXPECT_TRUE(index.is_valid());
    EXPECT_TRUE(index.has_specs());
    EXPECT_EQ(index.prim_stack_size(), 1u);
    EXPECT_EQ(index.child_names().size(), 2u);
    EXPECT_EQ(index.property_names().size(), 2u);

    EXPECT_TRUE(index.has_child(Token("Child1")));
    EXPECT_TRUE(index.has_child(Token("Child2")));
    EXPECT_FALSE(index.has_child(Token("NonExistent")));

    EXPECT_TRUE(index.has_property(Token("visibility")));
    EXPECT_FALSE(index.has_property(Token("unknown")));
}

TEST(PCP_PrimIndex_Deduplication) {
    // Test that finalize() deduplicates child and property names
    PcpPrimIndex index;
    index.set_path(Path("/World"));

    // Set duplicate names
    std::vector<Token> children = {Token("A"), Token("B"), Token("A"), Token("C"), Token("B")};
    std::vector<Token> props = {Token("x"), Token("y"), Token("x")};
    index.set_child_names(children);
    index.set_property_names(props);

    index.finalize();

    // Should have deduplicated while preserving order
    EXPECT_EQ(index.child_names().size(), 3u);  // A, B, C
    EXPECT_EQ(index.property_names().size(), 2u);  // x, y
}

TEST(PCP_PrimIndex_Errors) {
    // Test error handling
    PcpPrimIndex index;
    index.set_path(Path("/World"));

    EXPECT_FALSE(index.has_errors());

    PcpError error;
    error.type = PcpErrorType::ArcCycle;
    error.site_path = Path("/World");
    error.layer_id = "test.usda";
    error.message = "Composition cycle detected";

    index.add_error(error);

    EXPECT_TRUE(index.has_errors());
    EXPECT_EQ(index.errors().size(), 1u);
    EXPECT_EQ(index.errors()[0].type, PcpErrorType::ArcCycle);
}

TEST(PCP_Cache_Basic) {
    // Test basic PcpCache creation
    PcpCacheConfig config;
    config.root_layer_path = "/nonexistent/test.usda";
    config.include_payloads = false;

    PcpCache cache(config);

    EXPECT_EQ(cache.config().root_layer_path, "/nonexistent/test.usda");
    EXPECT_FALSE(cache.config().include_payloads);
    EXPECT_EQ(cache.cached_prim_index_count(), 0u);
    EXPECT_TRUE(cache.layer_registry() != nullptr);
}

TEST(PCP_Cache_PayloadControl) {
    // Test payload inclusion/exclusion
    PcpCacheConfig config;
    config.root_layer_path = "test.usda";
    config.include_payloads = false;

    PcpCache cache(config);

    Path path1("/World/HeavyAsset");
    Path path2("/World/Light");

    // Initially not included
    EXPECT_FALSE(cache.is_payload_included(path1));
    EXPECT_FALSE(cache.is_payload_included(path2));

    // Request payload
    cache.request_payloads({path1});
    EXPECT_TRUE(cache.is_payload_included(path1));
    EXPECT_FALSE(cache.is_payload_included(path2));

    // Request exclusion
    cache.request_payloads_exclusion({path1});
    EXPECT_FALSE(cache.is_payload_included(path1));

    // Get included payloads
    cache.request_payloads({path1, path2});
    auto included = cache.get_included_payloads();
    EXPECT_EQ(included.size(), 2u);
}

TEST(PCP_Cache_VariantControl) {
    // Test variant selection control
    PcpCacheConfig config;
    config.root_layer_path = "test.usda";
    config.variant_fallbacks["LOD"] = "medium";

    PcpCache cache(config);

    Path prim_path("/World/Model");
    Token lod_set("LOD");
    Token color_set("displayColor");

    // Get fallback
    EXPECT_EQ(cache.get_variant_fallback(lod_set).str(), "medium");
    EXPECT_TRUE(cache.get_variant_fallback(color_set).empty());

    // Get selection returns fallback when no explicit selection
    EXPECT_EQ(cache.get_variant_selection(prim_path, lod_set).str(), "medium");

    // Set variant selection overrides fallback
    cache.set_variant_selection(prim_path, lod_set, Token("high"));
    EXPECT_EQ(cache.get_variant_selection(prim_path, lod_set).str(), "high");

    // Clear explicit selection reverts to fallback
    cache.clear_variant_selection(prim_path, lod_set);
    EXPECT_EQ(cache.get_variant_selection(prim_path, lod_set).str(), "medium");

    // Selection for variant without fallback
    cache.set_variant_selection(prim_path, color_set, Token("red"));
    EXPECT_EQ(cache.get_variant_selection(prim_path, color_set).str(), "red");

    // Clear returns empty when no fallback
    cache.clear_variant_selection(prim_path, color_set);
    EXPECT_TRUE(cache.get_variant_selection(prim_path, color_set).empty());
}

TEST(PCP_Cache_Invalidation) {
    // Test cache invalidation
    PcpCacheConfig config;
    config.root_layer_path = "test.usda";

    PcpCache cache(config);

    // Initially empty
    EXPECT_EQ(cache.cached_prim_index_count(), 0u);

    // Clear should work on empty cache
    cache.clear_prim_indexes();
    EXPECT_EQ(cache.cached_prim_index_count(), 0u);

    // Full clear
    cache.clear();
    EXPECT_EQ(cache.cached_prim_index_count(), 0u);
    EXPECT_EQ(cache.cached_layer_stack_count(), 0u);
}

TEST(PCP_LayerOffset_Basic) {
    // Test LayerOffset
    LayerOffset offset1;  // Identity
    EXPECT_TRUE(offset1.is_identity());
    EXPECT_EQ(offset1.offset, 0.0);
    EXPECT_EQ(offset1.scale, 1.0);

    LayerOffset offset2(100.0, 2.0);
    EXPECT_FALSE(offset2.is_identity());
    EXPECT_EQ(offset2.offset, 100.0);
    EXPECT_EQ(offset2.scale, 2.0);

    // Test equality
    LayerOffset offset3(100.0, 2.0);
    EXPECT_TRUE(offset2 == offset3);
    EXPECT_FALSE(offset1 == offset2);

    // Test copy
    LayerOffset offset4 = offset2;
    EXPECT_EQ(offset4.offset, 100.0);
    EXPECT_EQ(offset4.scale, 2.0);
}

TEST(PCP_LayerRegistry_Basic) {
    // Test LayerRegistry basic operations
    LayerRegistry registry;

    EXPECT_EQ(registry.size(), 0u);
    EXPECT_TRUE(registry.get_all_layers().empty());
    EXPECT_TRUE(registry.get_all_identifiers().empty());

    // Search paths
    registry.add_search_paths({"/path1", "/path2"});
    EXPECT_EQ(registry.search_paths().size(), 2u);

    registry.set_search_paths({"/new/path"});
    EXPECT_EQ(registry.search_paths().size(), 1u);

    // Clear
    registry.clear();
    EXPECT_EQ(registry.size(), 0u);
}

TEST(Path_VariantSelection_Basic) {
    // Test append_variant_selection
    Path base("/World/Model");
    Path with_variant = base.append_variant_selection("LOD", "high");

    EXPECT_TRUE(with_variant.is_valid());
    EXPECT_EQ(with_variant.prim_part(), "/World/Model{LOD=high}");
    EXPECT_TRUE(with_variant.has_variant_selections());
}

TEST(Path_VariantSelection_Multiple) {
    // Test multiple variant selections
    Path p("/World/Model");
    p = p.append_variant_selection("LOD", "high");
    p = p.append_variant_selection("color", "red");

    EXPECT_TRUE(p.is_valid());
    EXPECT_EQ(p.prim_part(), "/World/Model{LOD=high}{color=red}");
    EXPECT_TRUE(p.has_variant_selections());

    // Get all variant selections
    auto selections = p.get_variant_selections();
    EXPECT_EQ(selections.size(), 2u);
    if (selections.size() >= 2) {
        EXPECT_EQ(selections[0].first, "LOD");
        EXPECT_EQ(selections[0].second, "high");
        EXPECT_EQ(selections[1].first, "color");
        EXPECT_EQ(selections[1].second, "red");
    }
}

TEST(Path_VariantSelection_Strip) {
    // Test stripping variant selections
    Path p("/World/Model{LOD=high}/Child{color=red}");
    EXPECT_TRUE(p.has_variant_selections());

    Path stripped = p.strip_variant_selections();
    EXPECT_EQ(stripped.prim_part(), "/World/Model/Child");
    EXPECT_FALSE(stripped.has_variant_selections());

    // Test prim_path_without_variants
    std::string without = p.prim_path_without_variants();
    EXPECT_EQ(without, "/World/Model/Child");
}

TEST(Path_VariantSelection_NoVariants) {
    // Test path without variants
    Path p("/World/Model");
    EXPECT_FALSE(p.has_variant_selections());

    auto selections = p.get_variant_selections();
    EXPECT_TRUE(selections.empty());

    Path stripped = p.strip_variant_selections();
    EXPECT_EQ(stripped.prim_part(), "/World/Model");
}

TEST(Path_VariantSelection_Invalid) {
    // Test invalid variant selection appends
    Path p("/World/Model.property");  // property path
    Path result = p.append_variant_selection("LOD", "high");
    EXPECT_FALSE(result.is_valid());  // Cannot append variant to property path

    // Empty variant set/name
    Path base("/World/Model");
    EXPECT_FALSE(base.append_variant_selection("", "high").is_valid());
    EXPECT_FALSE(base.append_variant_selection("LOD", "").is_valid());
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
