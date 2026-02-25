/*
 * test_main.c - Unit tests for lightusd-c
 *
 * Simple test harness: each test function returns 0 on success, 1 on failure.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightusd/lightusd-c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) \
    static int test_##name(void)

#define RUN_TEST(name) \
    do { \
        g_tests_run++; \
        printf("  %-50s", #name); \
        if (test_##name() == 0) { \
            g_tests_passed++; \
            printf("[PASS]\n"); \
        } else { \
            printf("[FAIL]\n"); \
        } \
    } while(0)

#define ASSERT(cond) \
    do { if (!(cond)) { printf("\n    ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); return 1; } } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { printf("\n    ASSERT_EQ FAILED: %s != %s (line %d)\n", #a, #b, __LINE__); return 1; } } while(0)

#define ASSERT_STR_EQ(a, b) \
    do { if (strcmp((a), (b)) != 0) { printf("\n    ASSERT_STR_EQ FAILED: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); return 1; } } while(0)

/* ===================================================================
 * Version Tests
 * =================================================================== */

TEST(version_constants) {
    ASSERT_EQ(LUSD_VERSION_MAJOR, 0);
    ASSERT_EQ(LUSD_VERSION_MINOR, 1);
    ASSERT_EQ(LUSD_VERSION_PATCH, 0);

    uint32_t v = LUSD_MAKE_API_VERSION(0, 1, 0);
    ASSERT_EQ(LUSD_API_VERSION_MAJOR(v), 0U);
    ASSERT_EQ(LUSD_API_VERSION_MINOR(v), 1U);
    ASSERT_EQ(LUSD_API_VERSION_PATCH(v), 0U);
    return 0;
}

/* ===================================================================
 * Result Tests
 * =================================================================== */

TEST(result_to_string) {
    ASSERT_STR_EQ(lusdResultToString(LUSD_SUCCESS), "LUSD_SUCCESS");
    ASSERT_STR_EQ(lusdResultToString(LUSD_ERROR_OUT_OF_MEMORY), "LUSD_ERROR_OUT_OF_MEMORY");
    ASSERT_STR_EQ(lusdResultToString(LUSD_ERROR_USE_AFTER_FREE), "LUSD_ERROR_USE_AFTER_FREE");
    ASSERT_STR_EQ(lusdResultToString(LUSD_ERROR_FEATURE_NOT_PRESENT), "LUSD_ERROR_FEATURE_NOT_PRESENT");
    return 0;
}

/* ===================================================================
 * Instance Tests
 * =================================================================== */

TEST(instance_create_destroy) {
    LusdInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.apiVersion = LUSD_API_VERSION;

    LusdInstance inst = NULL;
    LusdResult res = lusdCreateInstance(&ci, NULL, &inst);
    ASSERT_EQ(res, LUSD_SUCCESS);
    ASSERT(inst != NULL);

    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(instance_create_bad_stype) {
    LusdInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = (LusdStructureType)999;

    LusdInstance inst = NULL;
    LusdResult res = lusdCreateInstance(&ci, NULL, &inst);
    ASSERT_EQ(res, LUSD_ERROR_INVALID_STRUCTURE_TYPE);
    return 0;
}

TEST(instance_null_args) {
    LusdResult res = lusdCreateInstance(NULL, NULL, NULL);
    ASSERT_EQ(res, LUSD_ERROR_INVALID_ARGUMENT);
    return 0;
}

TEST(instance_last_error) {
    LusdInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.apiVersion = LUSD_API_VERSION;

    LusdInstance inst = NULL;
    lusdCreateInstance(&ci, NULL, &inst);

    const char* err = lusdGetLastError(inst);
    ASSERT(err != NULL);
    ASSERT_STR_EQ(err, "");

    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Custom Allocator Tests
 * =================================================================== */

static int g_alloc_count = 0;
static int g_free_count = 0;

static void* test_alloc(void* ud, size_t size, size_t align) {
    (void)ud; (void)align;
    g_alloc_count++;
    return malloc(size);
}

static void* test_realloc(void* ud, void* ptr, size_t size, size_t align) {
    (void)ud; (void)align;
    return realloc(ptr, size);
}

static void test_free(void* ud, void* ptr) {
    (void)ud;
    if (ptr) g_free_count++;
    free(ptr);
}

TEST(custom_allocator) {
    g_alloc_count = 0;
    g_free_count = 0;

    LusdAllocationCallbacks alloc;
    alloc.pUserData = NULL;
    alloc.pfnAllocation = test_alloc;
    alloc.pfnReallocation = test_realloc;
    alloc.pfnFree = test_free;

    LusdInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.apiVersion = LUSD_API_VERSION;

    LusdInstance inst = NULL;
    LusdResult res = lusdCreateInstance(&ci, &alloc, &inst);
    ASSERT_EQ(res, LUSD_SUCCESS);
    ASSERT(g_alloc_count > 0);

    lusdDestroyInstance(inst, &alloc);
    /* Verify that frees occurred */
    ASSERT(g_free_count > 0);
    return 0;
}

/* ===================================================================
 * Token Tests
 * =================================================================== */

static LusdInstance create_test_instance(void) {
    LusdInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.apiVersion = LUSD_API_VERSION;
    LusdInstance inst = NULL;
    lusdCreateInstance(&ci, NULL, &inst);
    return inst;
}

TEST(token_create_and_get) {
    LusdInstance inst = create_test_instance();

    LusdToken tok = NULL;
    LusdResult res = lusdCreateToken(inst, "hello", &tok);
    ASSERT_EQ(res, LUSD_SUCCESS);
    ASSERT(tok != NULL);

    const char* text = lusdTokenGetText(tok);
    ASSERT_STR_EQ(text, "hello");

    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(token_interning) {
    LusdInstance inst = create_test_instance();

    LusdToken tok1, tok2, tok3;
    lusdCreateToken(inst, "world", &tok1);
    lusdCreateToken(inst, "world", &tok2);
    lusdCreateToken(inst, "other", &tok3);

    /* Same string -> same handle (pointer equality) */
    ASSERT(lusdTokenEqual(tok1, tok2));
    /* Different string -> different handle */
    ASSERT(!lusdTokenEqual(tok1, tok3));

    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(token_hash) {
    LusdInstance inst = create_test_instance();

    LusdToken tok;
    lusdCreateToken(inst, "test", &tok);

    uint64_t h = lusdTokenHash(tok);
    ASSERT(h != 0);

    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Path Tests
 * =================================================================== */

TEST(path_create) {
    LusdInstance inst = create_test_instance();

    LusdPath path;
    LusdResult res = lusdCreatePath(inst, "/World/Mesh", &path);
    ASSERT_EQ(res, LUSD_SUCCESS);
    ASSERT_STR_EQ(lusdPathGetText(path), "/World/Mesh");
    ASSERT(lusdPathIsAbsolute(path));
    ASSERT(!lusdPathIsRoot(path));

    lusdDestroyPath(inst, path);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_root) {
    LusdInstance inst = create_test_instance();

    LusdPath root;
    lusdCreateRootPath(inst, &root);
    ASSERT(lusdPathIsRoot(root));
    ASSERT(lusdPathIsAbsolute(root));
    ASSERT_STR_EQ(lusdPathGetText(root), "/");

    lusdDestroyPath(inst, root);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_property) {
    LusdInstance inst = create_test_instance();

    LusdPath path;
    lusdCreatePath(inst, "/Mesh.points", &path);
    ASSERT(lusdPathIsPropertyPath(path));
    ASSERT_STR_EQ(lusdPathGetPropertyName(path), "points");

    LusdPath primPath;
    lusdPathGetPrimPath(inst, path, &primPath);
    ASSERT_STR_EQ(lusdPathGetText(primPath), "/Mesh");

    lusdDestroyPath(inst, primPath);
    lusdDestroyPath(inst, path);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_parent) {
    LusdInstance inst = create_test_instance();

    LusdPath path;
    lusdCreatePath(inst, "/World/Mesh", &path);

    LusdPath parent;
    lusdPathGetParent(inst, path, &parent);
    ASSERT_STR_EQ(lusdPathGetText(parent), "/World");

    LusdPath grandparent;
    lusdPathGetParent(inst, parent, &grandparent);
    ASSERT_STR_EQ(lusdPathGetText(grandparent), "/");

    lusdDestroyPath(inst, grandparent);
    lusdDestroyPath(inst, parent);
    lusdDestroyPath(inst, path);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_append_child) {
    LusdInstance inst = create_test_instance();

    LusdPath root;
    lusdCreateRootPath(inst, &root);

    LusdPath child;
    lusdPathAppendChild(inst, root, "World", &child);
    ASSERT_STR_EQ(lusdPathGetText(child), "/World");

    LusdPath grandchild;
    lusdPathAppendChild(inst, child, "Mesh", &grandchild);
    ASSERT_STR_EQ(lusdPathGetText(grandchild), "/World/Mesh");

    lusdDestroyPath(inst, grandchild);
    lusdDestroyPath(inst, child);
    lusdDestroyPath(inst, root);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_append_property) {
    LusdInstance inst = create_test_instance();

    LusdPath prim;
    lusdCreatePath(inst, "/Mesh", &prim);

    LusdPath propPath;
    lusdPathAppendProperty(inst, prim, "points", &propPath);
    ASSERT_STR_EQ(lusdPathGetText(propPath), "/Mesh.points");
    ASSERT(lusdPathIsPropertyPath(propPath));

    lusdDestroyPath(inst, propPath);
    lusdDestroyPath(inst, prim);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_equality) {
    LusdInstance inst = create_test_instance();

    LusdPath a, b, c;
    lusdCreatePath(inst, "/World/Mesh", &a);
    lusdCreatePath(inst, "/World/Mesh", &b);
    lusdCreatePath(inst, "/World/Other", &c);

    ASSERT(lusdPathEqual(a, b));
    ASSERT(!lusdPathEqual(a, c));

    lusdDestroyPath(inst, a);
    lusdDestroyPath(inst, b);
    lusdDestroyPath(inst, c);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_has_prefix) {
    LusdInstance inst = create_test_instance();

    LusdPath ancestor, descendant;
    lusdCreatePath(inst, "/World", &ancestor);
    lusdCreatePath(inst, "/World/Mesh/SubMesh", &descendant);

    ASSERT(lusdPathHasPrefix(descendant, ancestor));
    ASSERT(!lusdPathHasPrefix(ancestor, descendant));

    lusdDestroyPath(inst, ancestor);
    lusdDestroyPath(inst, descendant);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(path_element_name) {
    LusdInstance inst = create_test_instance();

    LusdPath path;
    lusdCreatePath(inst, "/World/Mesh", &path);
    ASSERT_STR_EQ(lusdPathGetElementName(path), "Mesh");

    lusdDestroyPath(inst, path);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Value Tests - Scalars
 * =================================================================== */

TEST(value_bool) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    ASSERT_EQ(lusdCreateValueBool(inst, true, &v), LUSD_SUCCESS);
    ASSERT_EQ(lusdValueGetType(v), LUSD_VALUE_TYPE_BOOL);
    ASSERT(!lusdValueIsArray(v));

    bool val;
    ASSERT_EQ(lusdValueGetBool(v, &val), LUSD_SUCCESS);
    ASSERT_EQ(val, true);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_int32) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    lusdCreateValueInt32(inst, 42, &v);

    int32_t val;
    ASSERT_EQ(lusdValueGetInt32(v, &val), LUSD_SUCCESS);
    ASSERT_EQ(val, 42);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_float) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    lusdCreateValueFloat(inst, 3.14f, &v);

    float val;
    ASSERT_EQ(lusdValueGetFloat(v, &val), LUSD_SUCCESS);
    ASSERT(fabsf(val - 3.14f) < 1e-6f);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_double) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    lusdCreateValueDouble(inst, 2.718281828, &v);

    double val;
    ASSERT_EQ(lusdValueGetDouble(v, &val), LUSD_SUCCESS);
    ASSERT(fabs(val - 2.718281828) < 1e-9);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_string) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    lusdCreateValueString(inst, "hello world", &v);
    ASSERT_EQ(lusdValueGetType(v), LUSD_VALUE_TYPE_STRING);

    const char* str;
    ASSERT_EQ(lusdValueGetString(v, &str), LUSD_SUCCESS);
    ASSERT_STR_EQ(str, "hello world");

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_float3) {
    LusdInstance inst = create_test_instance();

    LusdFloat3 f3 = {1.0f, 2.0f, 3.0f};
    LusdValue v;
    lusdCreateValueFloat3(inst, f3, &v);
    ASSERT_EQ(lusdValueGetType(v), LUSD_VALUE_TYPE_FLOAT3);

    LusdFloat3 result;
    ASSERT_EQ(lusdValueGetFloat3(v, &result), LUSD_SUCCESS);
    ASSERT(fabsf(result.x - 1.0f) < 1e-6f);
    ASSERT(fabsf(result.y - 2.0f) < 1e-6f);
    ASSERT(fabsf(result.z - 3.0f) < 1e-6f);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_matrix4d) {
    LusdInstance inst = create_test_instance();

    LusdMatrix4d mat;
    memset(&mat, 0, sizeof(mat));
    mat.m[0][0] = 1.0; mat.m[1][1] = 1.0; mat.m[2][2] = 1.0; mat.m[3][3] = 1.0;

    LusdValue v;
    lusdCreateValueMatrix4d(inst, &mat, &v);
    ASSERT_EQ(lusdValueGetType(v), LUSD_VALUE_TYPE_MATRIX4D);

    LusdMatrix4d result;
    ASSERT_EQ(lusdValueGetMatrix4d(v, &result), LUSD_SUCCESS);
    ASSERT(fabs(result.m[0][0] - 1.0) < 1e-9);
    ASSERT(fabs(result.m[3][3] - 1.0) < 1e-9);
    ASSERT(fabs(result.m[0][1]) < 1e-9);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_type_mismatch) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    lusdCreateValueFloat(inst, 1.0f, &v);

    int32_t val;
    ASSERT_EQ(lusdValueGetInt32(v, &val), LUSD_ERROR_TYPE_MISMATCH);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_token) {
    LusdInstance inst = create_test_instance();

    LusdToken tok;
    lusdCreateToken(inst, "myToken", &tok);

    LusdValue v;
    lusdCreateValueToken(inst, tok, &v);

    LusdToken result;
    ASSERT_EQ(lusdValueGetToken(v, &result), LUSD_SUCCESS);
    ASSERT(lusdTokenEqual(tok, result));

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Value Tests - Arrays
 * =================================================================== */

TEST(value_array_float3) {
    LusdInstance inst = create_test_instance();

    LusdFloat3 data[] = {
        {1.0f, 2.0f, 3.0f},
        {4.0f, 5.0f, 6.0f},
        {7.0f, 8.0f, 9.0f}
    };

    LusdValue v;
    ASSERT_EQ(lusdCreateValueArrayFloat3(inst, 3, data, &v), LUSD_SUCCESS);
    ASSERT(lusdValueIsArray(v));
    ASSERT_EQ(lusdValueGetArraySize(v), 3U);

    uint64_t count;
    const LusdFloat3* ptr;
    ASSERT_EQ(lusdValueGetArrayPtrFloat3(v, &count, &ptr), LUSD_SUCCESS);
    ASSERT_EQ(count, 3U);
    ASSERT(fabsf(ptr[0].x - 1.0f) < 1e-6f);
    ASSERT(fabsf(ptr[1].y - 5.0f) < 1e-6f);
    ASSERT(fabsf(ptr[2].z - 9.0f) < 1e-6f);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_array_int32) {
    LusdInstance inst = create_test_instance();

    int32_t data[] = {10, 20, 30, 40, 50};
    LusdValue v;
    ASSERT_EQ(lusdCreateValueArrayInt32(inst, 5, data, &v), LUSD_SUCCESS);

    uint64_t count;
    const int32_t* ptr;
    ASSERT_EQ(lusdValueGetArrayPtrInt32(v, &count, &ptr), LUSD_SUCCESS);
    ASSERT_EQ(count, 5U);
    ASSERT_EQ(ptr[0], 10);
    ASSERT_EQ(ptr[4], 50);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_array_type_mismatch) {
    LusdInstance inst = create_test_instance();

    float data[] = {1.0f, 2.0f};
    LusdValue v;
    lusdCreateValueArrayFloat(inst, 2, data, &v);

    uint64_t count;
    const int32_t* ptr;
    ASSERT_EQ(lusdValueGetArrayPtrInt32(v, &count, &ptr), LUSD_ERROR_TYPE_MISMATCH);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(value_array_empty) {
    LusdInstance inst = create_test_instance();

    LusdValue v;
    ASSERT_EQ(lusdCreateValueArrayFloat(inst, 0, NULL, &v), LUSD_SUCCESS);
    ASSERT(lusdValueIsArray(v));
    ASSERT_EQ(lusdValueGetArraySize(v), 0U);

    lusdDestroyValue(inst, v);
    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Value Type Name Tests
 * =================================================================== */

TEST(value_type_name) {
    ASSERT_STR_EQ(lusdValueTypeGetName(LUSD_VALUE_TYPE_FLOAT3), "float3");
    ASSERT_STR_EQ(lusdValueTypeGetName(LUSD_VALUE_TYPE_MATRIX4D), "matrix4d");
    ASSERT_STR_EQ(lusdValueTypeGetName(LUSD_VALUE_TYPE_STRING), "string");
    ASSERT_STR_EQ(lusdValueTypeGetName(LUSD_VALUE_TYPE_TOKEN), "token");
    ASSERT_STR_EQ(lusdValueTypeGetName(LUSD_VALUE_TYPE_COLOR3F), "color3f");
    return 0;
}

/* ===================================================================
 * Handle Table Tests
 * =================================================================== */

TEST(handle_table_basic) {
    LusdInstance inst = create_test_instance();

    /* The handle table is tested implicitly through tokens and values.
     * Here we just verify the instance was created successfully. */
    ASSERT(inst != NULL);

    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Stub API Tests
 * =================================================================== */

TEST(stub_stage_returns_not_present) {
    /* Previously a stub test; lusdCreateStage is now implemented.
     * Verify it succeeds and clean up properly. */
    LusdInstance inst = create_test_instance();

    LusdStageCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = LUSD_STRUCTURE_TYPE_STAGE_CREATE_INFO;
    LusdStage stage = NULL;
    ASSERT_EQ(lusdCreateStage(inst, &ci, &stage), LUSD_SUCCESS);
    lusdDestroyStage(inst, stage);

    lusdDestroyInstance(inst, NULL);
    return 0;
}

TEST(stub_prim_returns_not_present) {
    /* Previously a stub test; lusdCreatePrim is now implemented.
     * A NULL pName must return INVALID_ARGUMENT. */
    LusdInstance inst = create_test_instance();

    LusdPrimCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = LUSD_STRUCTURE_TYPE_PRIM_CREATE_INFO;
    /* ci.pName is NULL — must be rejected */
    LusdPrim prim = NULL;
    ASSERT_EQ(lusdCreatePrim(inst, &ci, &prim), LUSD_ERROR_INVALID_ARGUMENT);

    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Diagnostics Tests
 * =================================================================== */

static int g_diag_called = 0;
static LusdDiagnosticSeverity g_diag_severity;

static void test_diag_callback(LusdDiagnosticSeverity severity, const char* pMessage, void* pUserData) {
    (void)pMessage; (void)pUserData;
    g_diag_called++;
    g_diag_severity = severity;
}

TEST(diagnostic_callback) {
    LusdInstance inst = create_test_instance();

    g_diag_called = 0;
    ASSERT_EQ(lusdInstanceSetDiagnosticCallback(inst, test_diag_callback, NULL), LUSD_SUCCESS);

    /* Diagnostic callback is set; it would be called by internal code.
     * We can't easily trigger it from test, just verify it was set. */

    lusdDestroyInstance(inst, NULL);
    return 0;
}

/* ===================================================================
 * Main
 * =================================================================== */

int main(void) {
    printf("lightusd-c unit tests\n");
    printf("=====================\n\n");

    printf("Version:\n");
    RUN_TEST(version_constants);

    printf("\nResult:\n");
    RUN_TEST(result_to_string);

    printf("\nInstance:\n");
    RUN_TEST(instance_create_destroy);
    RUN_TEST(instance_create_bad_stype);
    RUN_TEST(instance_null_args);
    RUN_TEST(instance_last_error);
    RUN_TEST(custom_allocator);

    printf("\nToken:\n");
    RUN_TEST(token_create_and_get);
    RUN_TEST(token_interning);
    RUN_TEST(token_hash);

    printf("\nPath:\n");
    RUN_TEST(path_create);
    RUN_TEST(path_root);
    RUN_TEST(path_property);
    RUN_TEST(path_parent);
    RUN_TEST(path_append_child);
    RUN_TEST(path_append_property);
    RUN_TEST(path_equality);
    RUN_TEST(path_has_prefix);
    RUN_TEST(path_element_name);

    printf("\nValue (scalars):\n");
    RUN_TEST(value_bool);
    RUN_TEST(value_int32);
    RUN_TEST(value_float);
    RUN_TEST(value_double);
    RUN_TEST(value_string);
    RUN_TEST(value_float3);
    RUN_TEST(value_matrix4d);
    RUN_TEST(value_type_mismatch);
    RUN_TEST(value_token);

    printf("\nValue (arrays):\n");
    RUN_TEST(value_array_float3);
    RUN_TEST(value_array_int32);
    RUN_TEST(value_array_type_mismatch);
    RUN_TEST(value_array_empty);

    printf("\nValue type names:\n");
    RUN_TEST(value_type_name);

    printf("\nHandle table:\n");
    RUN_TEST(handle_table_basic);

    printf("\nStub APIs:\n");
    RUN_TEST(stub_stage_returns_not_present);
    RUN_TEST(stub_prim_returns_not_present);

    printf("\nDiagnostics:\n");
    RUN_TEST(diagnostic_callback);

    printf("\n=====================\n");
    printf("Results: %d/%d passed\n", g_tests_passed, g_tests_run);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
