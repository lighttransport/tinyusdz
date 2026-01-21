// Crate Writer Round-trip Test
// Write USD file with crate-writer, read with OpenUSD C++ API
//
// SPDX-License-Identifier: Apache 2.0

#include <iostream>
#include <string>
#include <vector>
#include <cmath>

// OpenUSD headers
#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/vt/array.h>

// Crate writer headers
#include "crate-writer.hh"
#include "crate-format.hh"
#include "tinyusdz.hh"
#include "value-types.hh"

PXR_NAMESPACE_USING_DIRECTIVE

// Don't use 'using namespace tinyusdz' to avoid crate:: namespace conflicts
using tinyusdz::Path;
using tinyusdz::SpecType;
namespace value = tinyusdz::value;
using tinyusdz::experimental::CrateWriter;

// Test result tracker
struct TestResults {
    int passed = 0;
    int failed = 0;

    void pass(const std::string& test_name) {
        std::cout << "✓ PASS: " << test_name << std::endl;
        passed++;
    }

    void fail(const std::string& test_name, const std::string& reason) {
        std::cout << "✗ FAIL: " << test_name << " - " << reason << std::endl;
        failed++;
    }

    void summary() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Summary:" << std::endl;
        std::cout << "  Passed: " << passed << std::endl;
        std::cout << "  Failed: " << failed << std::endl;
        std::cout << "  Total:  " << (passed + failed) << std::endl;
        std::cout << "========================================" << std::endl;
    }

    int exit_code() const {
        return (failed == 0) ? 0 : 1;
    }
};

// Helper to create test file with crate-writer
bool CreateTestFile(const std::string& filepath, std::string* err) {
    CrateWriter writer(filepath);

    // Disable compression for initial testing to isolate issues
    CrateWriter::Options opts;
    opts.enable_compression = false;
    writer.SetOptions(opts);

    if (!writer.Open(err)) {
        return false;
    }

    // Test 1: Root prim with metadata
    {
        Path root_path("/", "");
        tinyusdz::crate::FieldValuePairVector fields;

        // Add specifier
        tinyusdz::crate::CrateValue specifier_val;
        specifier_val.Set(std::string("def"));
        fields.push_back({"specifier", specifier_val});

        // Add typename
        tinyusdz::crate::CrateValue typename_val;
        typename_val.Set(std::string(""));
        fields.push_back({"typeName", typename_val});

        if (!writer.AddSpec(root_path, SpecType::Prim, fields, err)) {
            return false;
        }
    }

    // Test 2: Xform prim with transform
    {
        Path xform_path("/World", "");
        tinyusdz::crate::FieldValuePairVector fields;

        tinyusdz::crate::CrateValue specifier_val;
        specifier_val.Set(std::string("def"));
        fields.push_back({"specifier", specifier_val});

        tinyusdz::crate::CrateValue typename_val;
        typename_val.Set(std::string("Xform"));
        fields.push_back({"typeName", typename_val});

        if (!writer.AddSpec(xform_path, SpecType::Prim, fields, err)) {
            return false;
        }
    }

    // Test 3: Mesh prim with attributes
    {
        Path mesh_path("/World/Mesh", "");
        tinyusdz::crate::FieldValuePairVector fields;

        tinyusdz::crate::CrateValue specifier_val;
        specifier_val.Set(std::string("def"));
        fields.push_back({"specifier", specifier_val});

        tinyusdz::crate::CrateValue typename_val;
        typename_val.Set(std::string("Mesh"));
        fields.push_back({"typeName", typename_val});

        if (!writer.AddSpec(mesh_path, SpecType::Prim, fields, err)) {
            return false;
        }
    }

    // Test 4: Points attribute (float3 array)
    {
        Path points_path("/World/Mesh", "points");
        tinyusdz::crate::FieldValuePairVector fields;

        // Attribute value - array of float3
        std::vector<value::float3> points;
        points.push_back(value::float3{{0.0f, 0.0f, 0.0f}});
        points.push_back(value::float3{{1.0f, 0.0f, 0.0f}});
        points.push_back(value::float3{{0.0f, 1.0f, 0.0f}});
        points.push_back(value::float3{{0.0f, 0.0f, 1.0f}});

        tinyusdz::crate::CrateValue points_val;
        points_val.Set(points);
        fields.push_back({"default", points_val});

        // Type name
        tinyusdz::crate::CrateValue typename_val;
        typename_val.Set(value::token("point3f[]"));
        fields.push_back({"typeName", typename_val});

        if (!writer.AddSpec(points_path, SpecType::Attribute, fields, err)) {
            return false;
        }
    }

    // Test 5: Scalar attributes
    {
        // Int attribute
        Path int_attr_path("/World/Mesh", "testInt");
        tinyusdz::crate::FieldValuePairVector int_fields;

        tinyusdz::crate::CrateValue int_val;
        int_val.Set(int32_t(42));
        int_fields.push_back({"default", int_val});

        tinyusdz::crate::CrateValue int_typename;
        int_typename.Set(value::token("int"));
        int_fields.push_back({"typeName", int_typename});

        if (!writer.AddSpec(int_attr_path, SpecType::Attribute, int_fields, err)) {
            return false;
        }

        // Float attribute
        Path float_attr_path("/World/Mesh", "testFloat");
        tinyusdz::crate::FieldValuePairVector float_fields;

        tinyusdz::crate::CrateValue float_val;
        float_val.Set(3.14159f);
        float_fields.push_back({"default", float_val});

        tinyusdz::crate::CrateValue float_typename;
        float_typename.Set(value::token("float"));
        float_fields.push_back({"typeName", float_typename});

        if (!writer.AddSpec(float_attr_path, SpecType::Attribute, float_fields, err)) {
            return false;
        }

        // String attribute
        Path string_attr_path("/World/Mesh", "testString");
        tinyusdz::crate::FieldValuePairVector string_fields;

        tinyusdz::crate::CrateValue string_val;
        string_val.Set(std::string("Hello USD"));
        string_fields.push_back({"default", string_val});

        tinyusdz::crate::CrateValue string_typename;
        string_typename.Set(value::token("string"));
        string_fields.push_back({"typeName", string_typename});

        if (!writer.AddSpec(string_attr_path, SpecType::Attribute, string_fields, err)) {
            return false;
        }
    }

    if (!writer.Finalize(err)) {
        return false;
    }

    writer.Close();
    return true;
}

// Read and verify with OpenUSD
bool VerifyWithOpenUSD(const std::string& filepath, TestResults& results) {
    auto stage = UsdStage::Open(filepath);
    if (!stage) {
        results.fail("Open stage", "Failed to open with OpenUSD");
        return false;
    }
    results.pass("Open stage");

    // Test: Root prim exists
    auto root = stage->GetPseudoRoot();
    if (!root) {
        results.fail("Root prim", "Root not found");
        return false;
    }
    results.pass("Root prim exists");

    // Test: World prim exists
    auto world_prim = stage->GetPrimAtPath(SdfPath("/World"));
    if (!world_prim || !world_prim.IsValid()) {
        results.fail("World prim", "World prim not found");
        return false;
    }
    results.pass("World prim exists");

    // Test: World is Xform
    if (world_prim.GetTypeName() != TfToken("Xform")) {
        results.fail("World type", "Expected Xform, got " + world_prim.GetTypeName().GetString());
        return false;
    }
    results.pass("World prim is Xform");

    // Test: Mesh prim exists
    auto mesh_prim = stage->GetPrimAtPath(SdfPath("/World/Mesh"));
    if (!mesh_prim || !mesh_prim.IsValid()) {
        results.fail("Mesh prim", "Mesh prim not found");
        return false;
    }
    results.pass("Mesh prim exists");

    // Test: Mesh is Mesh type
    if (mesh_prim.GetTypeName() != TfToken("Mesh")) {
        results.fail("Mesh type", "Expected Mesh, got " + mesh_prim.GetTypeName().GetString());
        return false;
    }
    results.pass("Mesh prim is Mesh type");

    // Test: Points attribute
    auto points_attr = mesh_prim.GetAttribute(TfToken("points"));
    if (!points_attr || !points_attr.IsValid()) {
        results.fail("Points attribute", "Attribute not found");
        return false;
    }
    results.pass("Points attribute exists");

    VtVec3fArray points_value;
    if (!points_attr.Get(&points_value)) {
        results.fail("Points value", "Failed to get value");
        return false;
    }

    if (points_value.size() != 4) {
        results.fail("Points count", "Expected 4 points, got " + std::to_string(points_value.size()));
        return false;
    }
    results.pass("Points array size correct");

    // Verify point values
    const float epsilon = 1e-6f;
    bool points_correct = true;
    if (std::abs(points_value[0][0] - 0.0f) > epsilon ||
        std::abs(points_value[0][1] - 0.0f) > epsilon ||
        std::abs(points_value[0][2] - 0.0f) > epsilon) {
        points_correct = false;
    }
    if (std::abs(points_value[1][0] - 1.0f) > epsilon) {
        points_correct = false;
    }

    if (!points_correct) {
        results.fail("Points values", "Point values don't match expected");
        return false;
    }
    results.pass("Points values correct");

    // Test: testInt attribute
    auto int_attr = mesh_prim.GetAttribute(TfToken("testInt"));
    if (!int_attr || !int_attr.IsValid()) {
        results.fail("testInt attribute", "Attribute not found");
        return false;
    }

    int int_value;
    if (!int_attr.Get(&int_value)) {
        results.fail("testInt value", "Failed to get value");
        return false;
    }

    if (int_value != 42) {
        results.fail("testInt value", "Expected 42, got " + std::to_string(int_value));
        return false;
    }
    results.pass("testInt attribute correct");

    // Test: testFloat attribute
    auto float_attr = mesh_prim.GetAttribute(TfToken("testFloat"));
    if (!float_attr || !float_attr.IsValid()) {
        results.fail("testFloat attribute", "Attribute not found");
        return false;
    }

    float float_value;
    if (!float_attr.Get(&float_value)) {
        results.fail("testFloat value", "Failed to get value");
        return false;
    }

    if (std::abs(float_value - 3.14159f) > epsilon) {
        results.fail("testFloat value", "Value doesn't match expected");
        return false;
    }
    results.pass("testFloat attribute correct");

    // Test: testString attribute
    auto string_attr = mesh_prim.GetAttribute(TfToken("testString"));
    if (!string_attr || !string_attr.IsValid()) {
        results.fail("testString attribute", "Attribute not found");
        return false;
    }

    std::string string_value;
    if (!string_attr.Get(&string_value)) {
        results.fail("testString value", "Failed to get value");
        return false;
    }

    if (string_value != "Hello USD") {
        results.fail("testString value", "Expected 'Hello USD', got '" + string_value + "'");
        return false;
    }
    results.pass("testString attribute correct");

    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Crate Writer Round-trip Test" << std::endl;
    std::cout << "========================================\n" << std::endl;

    TestResults results;

    const std::string test_file = "test_output.usdc";

    // Step 1: Create test file with crate-writer
    std::cout << "Step 1: Creating test file with crate-writer..." << std::endl;
    std::string err;
    if (!CreateTestFile(test_file, &err)) {
        std::cerr << "FATAL: Failed to create test file: " << err << std::endl;
        return 1;
    }
    results.pass("Create test file");
    std::cout << "Created: " << test_file << "\n" << std::endl;

    // Step 2: Verify with OpenUSD
    std::cout << "Step 2: Verifying with OpenUSD..." << std::endl;
    if (!VerifyWithOpenUSD(test_file, results)) {
        std::cerr << "Verification failed with errors\n" << std::endl;
    }

    // Print summary
    results.summary();

    return results.exit_code();
}
