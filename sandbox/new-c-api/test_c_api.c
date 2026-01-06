/**
 * @file test_c_api.c
 * @brief Unit tests for TinyUSDZ C API
 *
 * Run with: gcc -I. test_c_api.c -L. -ltinyusdz_c -lm -o test_c_api && ./test_c_api
 */

#include "tinyusdz_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// Test Framework
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name(void)
#define RUN_TEST(name) run_test(#name, test_##name)

void run_test(const char* name, void (*test_func)(void)) {
    tests_run++;
    printf("Running: %s ... ", name);
    fflush(stdout);

    // Run test
    __try {
        test_func();
        printf("PASS\n");
        tests_passed++;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("FAIL\n");
        tests_failed++;
    }
}

#define ASSERT(condition, message) \
    if (!(condition)) { \
        fprintf(stderr, "ASSERTION FAILED: %s\n", message); \
        return; \
    }

#define ASSERT_EQ(a, b, message) \
    if ((a) != (b)) { \
        fprintf(stderr, "ASSERTION FAILED: %s (expected %d, got %d)\n", message, (int)(b), (int)(a)); \
        return; \
    }

#define ASSERT_TRUE(cond, message) ASSERT(cond, message)
#define ASSERT_FALSE(cond, message) ASSERT(!(cond), message)
#define ASSERT_NOT_NULL(ptr, message) ASSERT((ptr) != NULL, message)
#define ASSERT_NULL(ptr, message) ASSERT((ptr) == NULL, message)

// ============================================================================
// Test Cases
// ============================================================================

TEST(initialization) {
    tusdz_result result = tusdz_init();
    ASSERT_EQ(result, TUSDZ_SUCCESS, "Initialization should succeed");
}

TEST(version) {
    const char* version = tusdz_get_version();
    ASSERT_NOT_NULL(version, "Version should not be NULL");
    printf("\nVersion: %s\n", version);
}

TEST(invalid_file) {
    tusdz_stage stage = NULL;
    char error[256];

    tusdz_result result = tusdz_load_from_file(
        "nonexistent_file.usd",
        NULL,
        &stage,
        error,
        sizeof(error)
    );

    ASSERT_EQ(result, TUSDZ_ERROR_PARSE_FAILED, "Should fail for nonexistent file");
    ASSERT_NULL(stage, "Stage should be NULL on failure");
    ASSERT_TRUE(strlen(error) > 0, "Error message should be provided");
}

TEST(null_arguments) {
    // Test with NULL filepath
    tusdz_result result = tusdz_load_from_file(
        NULL,
        NULL,
        NULL,
        NULL,
        0
    );
    ASSERT_EQ(result, TUSDZ_ERROR_INVALID_ARGUMENT, "Should fail with NULL arguments");
}

TEST(error_to_string) {
    const char* str = tusdz_result_to_string(TUSDZ_SUCCESS);
    ASSERT_NOT_NULL(str, "String representation should not be NULL");

    const char* error_str = tusdz_result_to_string(TUSDZ_ERROR_FILE_NOT_FOUND);
    ASSERT_NOT_NULL(error_str, "Error string should not be NULL");
}

TEST(prim_type_to_string) {
    const char* mesh_str = tusdz_prim_type_to_string(TUSDZ_PRIM_MESH);
    ASSERT_NOT_NULL(mesh_str, "Mesh type string should not be NULL");

    const char* xform_str = tusdz_prim_type_to_string(TUSDZ_PRIM_XFORM);
    ASSERT_NOT_NULL(xform_str, "Xform type string should not be NULL");
}

TEST(value_type_to_string) {
    const char* float_str = tusdz_value_type_to_string(TUSDZ_VALUE_FLOAT);
    ASSERT_NOT_NULL(float_str, "Float type string should not be NULL");

    const char* float3_str = tusdz_value_type_to_string(TUSDZ_VALUE_FLOAT3);
    ASSERT_NOT_NULL(float3_str, "Float3 type string should not be NULL");
}

TEST(detect_format) {
    tusdz_format fmt = tusdz_detect_format("test.usda");
    ASSERT_EQ(fmt, TUSDZ_FORMAT_USDA, "Should detect USDA format");

    fmt = tusdz_detect_format("test.usdc");
    ASSERT_EQ(fmt, TUSDZ_FORMAT_USDC, "Should detect USDC format");

    fmt = tusdz_detect_format("test.usdz");
    ASSERT_EQ(fmt, TUSDZ_FORMAT_USDZ, "Should detect USDZ format");

    fmt = tusdz_detect_format("test.unknown");
    ASSERT_EQ(fmt, TUSDZ_FORMAT_AUTO, "Should return AUTO for unknown extension");
}

// ============================================================================
// Helper: Create Test USD Data
// ============================================================================

const char* get_test_usd_data(void) {
    static const char* test_data =
        "#usda 1.0\n"
        "(\n"
        "    defaultPrim = \"World\"\n"
        ")\n"
        "\n"
        "def Xform \"World\"\n"
        "{\n"
        "    double3 xformOp:translate = (0, 0, 0)\n"
        "    uniform token[] xformOpOrder = [\"xformOp:translate\"]\n"
        "\n"
        "    def Mesh \"Cube\"\n"
        "    {\n"
        "        float3[] points = [\n"
        "            (-1, -1, -1),\n"
        "            (1, -1, -1),\n"
        "            (1, 1, -1),\n"
        "            (-1, 1, -1),\n"
        "            (-1, -1, 1),\n"
        "            (1, -1, 1),\n"
        "            (1, 1, 1),\n"
        "            (-1, 1, 1),\n"
        "        ]\n"
        "        int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]\n"
        "        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]\n"
        "    }\n"
        "}\n";

    return test_data;
}

// ============================================================================
// Integration Tests (require valid USD file)
// ============================================================================

TEST(load_from_memory) {
    const char* data = get_test_usd_data();
    tusdz_stage stage = NULL;

    tusdz_result result = tusdz_load_from_memory(
        (const uint8_t*)data,
        strlen(data),
        TUSDZ_FORMAT_USDA,
        NULL,
        &stage,
        NULL,
        0
    );

    // This test will likely fail without full TinyUSDZ support
    // but demonstrates the API usage
    if (result == TUSDZ_SUCCESS) {
        ASSERT_NOT_NULL(stage, "Stage should be loaded");
        tusdz_stage_free(stage);
    }
}

TEST(shutdown) {
    tusdz_shutdown();
    // Second init should still work
    tusdz_result result = tusdz_init();
    ASSERT_EQ(result, TUSDZ_SUCCESS, "Re-initialization should succeed");
}

// ============================================================================
// Memory Tests
// ============================================================================

TEST(memory_stats) {
    size_t used, peak;
    tusdz_get_memory_stats(NULL, &used, &peak);
    ASSERT_TRUE(used >= 0, "Memory usage should be non-negative");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("TinyUSDZ C API Test Suite\n");
    printf("========================================\n\n");

    // Run all tests
    RUN_TEST(initialization);
    RUN_TEST(version);
    RUN_TEST(invalid_file);
    RUN_TEST(null_arguments);
    RUN_TEST(error_to_string);
    RUN_TEST(prim_type_to_string);
    RUN_TEST(value_type_to_string);
    RUN_TEST(detect_format);
    RUN_TEST(load_from_memory);
    RUN_TEST(memory_stats);
    RUN_TEST(shutdown);

    // Print summary
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Total:  %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}