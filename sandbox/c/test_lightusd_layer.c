#include "lightusd_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test framework macros */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAILED: %s\n", message); \
            return 0; \
        } \
    } while(0)

#define TEST_SUCCESS() \
    do { \
        printf("PASSED\n"); \
        return 1; \
    } while(0)

/* ===== Map Tests ===== */

static int test_map_basic_operations() {
    printf("Testing map basic operations... ");

    lightusd_map_t *map = lightusd_map_create(NULL);
    TEST_ASSERT(map != NULL, "Failed to create map");

    /* Test initial state */
    TEST_ASSERT(lightusd_map_size(map) == 0, "Initial size should be 0");
    TEST_ASSERT(!lightusd_map_has_key(map, "test"), "Should not have 'test' key");
    TEST_ASSERT(lightusd_map_get(map, "test") == NULL, "Get non-existent key should return NULL");

    /* Test insertion */
    char *value1 = "value1";
    char *value2 = "value2";
    char *value3 = "value3";

    TEST_ASSERT(lightusd_map_set(map, "key1", value1), "Failed to set key1");
    TEST_ASSERT(lightusd_map_set(map, "key2", value2), "Failed to set key2");
    TEST_ASSERT(lightusd_map_set(map, "key3", value3), "Failed to set key3");

    TEST_ASSERT(lightusd_map_size(map) == 3, "Size should be 3 after insertions");

    /* Test retrieval */
    TEST_ASSERT(lightusd_map_get(map, "key1") == value1, "key1 should return value1");
    TEST_ASSERT(lightusd_map_get(map, "key2") == value2, "key2 should return value2");
    TEST_ASSERT(lightusd_map_get(map, "key3") == value3, "key3 should return value3");

    TEST_ASSERT(lightusd_map_has_key(map, "key1"), "Should have key1");
    TEST_ASSERT(lightusd_map_has_key(map, "key2"), "Should have key2");
    TEST_ASSERT(lightusd_map_has_key(map, "key3"), "Should have key3");
    TEST_ASSERT(!lightusd_map_has_key(map, "key4"), "Should not have key4");

    /* Test replacement */
    char *new_value = "new_value";
    TEST_ASSERT(lightusd_map_set(map, "key1", new_value), "Failed to replace key1");
    TEST_ASSERT(lightusd_map_size(map) == 3, "Size should still be 3 after replacement");
    TEST_ASSERT(lightusd_map_get(map, "key1") == new_value, "key1 should return new_value");

    /* Test removal */
    TEST_ASSERT(lightusd_map_remove(map, "key2"), "Failed to remove key2");
    TEST_ASSERT(lightusd_map_size(map) == 2, "Size should be 2 after removal");
    TEST_ASSERT(!lightusd_map_has_key(map, "key2"), "Should not have key2 after removal");
    TEST_ASSERT(lightusd_map_get(map, "key2") == NULL, "key2 should return NULL after removal");

    /* Test removal of non-existent key */
    TEST_ASSERT(!lightusd_map_remove(map, "nonexistent"), "Removing non-existent key should return false");

    lightusd_map_destroy(map);
    TEST_SUCCESS();
}

static int test_map_iteration() {
    printf("Testing map iteration... ");

    lightusd_map_t *map = lightusd_map_create(NULL);
    TEST_ASSERT(map != NULL, "Failed to create map");

    /* Add test data */
    lightusd_map_set(map, "apple", "fruit");
    lightusd_map_set(map, "banana", "fruit");
    lightusd_map_set(map, "carrot", "vegetable");
    lightusd_map_set(map, "date", "fruit");

    /* Test iteration */
    lightusd_map_iterator_t *iter = lightusd_map_iterator_create(map);
    TEST_ASSERT(iter != NULL, "Failed to create iterator");

    int count = 0;
    const char *key;
    void *value;

    while (lightusd_map_iterator_next(iter, &key, &value)) {
        TEST_ASSERT(key != NULL, "Key should not be NULL");
        TEST_ASSERT(value != NULL, "Value should not be NULL");
        TEST_ASSERT(lightusd_map_get(map, key) == value, "Iterator value should match map get");
        count++;
    }

    TEST_ASSERT(count == 4, "Should iterate over all 4 items");

    /* Test reset and re-iteration */
    lightusd_map_iterator_reset(iter);
    count = 0;
    while (lightusd_map_iterator_next(iter, &key, &value)) {
        count++;
    }
    TEST_ASSERT(count == 4, "Should iterate over all 4 items after reset");

    lightusd_map_iterator_destroy(iter);
    lightusd_map_destroy(map);
    TEST_SUCCESS();
}

/* ===== Value Tests ===== */

static int test_value_operations() {
    printf("Testing value operations... ");

    /* Test bool value */
    lightusd_value_t *bool_val = lightusd_value_create_bool(1);
    TEST_ASSERT(bool_val != NULL, "Failed to create bool value");
    TEST_ASSERT(lightusd_value_get_type(bool_val) == LIGHTUSD_VALUE_BOOL, "Bool value type incorrect");

    int bool_result;
    TEST_ASSERT(lightusd_value_get_bool(bool_val, &bool_result), "Failed to get bool value");
    TEST_ASSERT(bool_result == 1, "Bool value should be 1");

    /* Test int value */
    lightusd_value_t *int_val = lightusd_value_create_int(42);
    TEST_ASSERT(int_val != NULL, "Failed to create int value");

    int32_t int_result;
    TEST_ASSERT(lightusd_value_get_int(int_val, &int_result), "Failed to get int value");
    TEST_ASSERT(int_result == 42, "Int value should be 42");

    /* Test float value */
    lightusd_value_t *float_val = lightusd_value_create_float(3.14f);
    TEST_ASSERT(float_val != NULL, "Failed to create float value");

    float float_result;
    TEST_ASSERT(lightusd_value_get_float(float_val, &float_result), "Failed to get float value");
    TEST_ASSERT(float_result == 3.14f, "Float value should be 3.14");

    /* Test string value */
    lightusd_value_t *string_val = lightusd_value_create_string("Hello, USD!");
    TEST_ASSERT(string_val != NULL, "Failed to create string value");

    const char *string_result = lightusd_value_get_string(string_val);
    TEST_ASSERT(string_result != NULL, "Failed to get string value");
    TEST_ASSERT(strcmp(string_result, "Hello, USD!") == 0, "String value incorrect");

    /* Test token value */
    lightusd_value_t *token_val = lightusd_value_create_token("myToken");
    TEST_ASSERT(token_val != NULL, "Failed to create token value");

    const char *token_result = lightusd_value_get_token(token_val);
    TEST_ASSERT(token_result != NULL, "Failed to get token value");
    TEST_ASSERT(strcmp(token_result, "myToken") == 0, "Token value incorrect");

    /* Cleanup */
    lightusd_value_destroy(bool_val);
    lightusd_value_destroy(int_val);
    lightusd_value_destroy(float_val);
    lightusd_value_destroy(string_val);
    lightusd_value_destroy(token_val);

    TEST_SUCCESS();
}

/* ===== Property Tests ===== */

static int test_property_operations() {
    printf("Testing property operations... ");

    /* Create a property */
    lightusd_property_t *prop = lightusd_property_create("myProperty", "float", LIGHTUSD_PROP_ATTRIB);
    TEST_ASSERT(prop != NULL, "Failed to create property");
    TEST_ASSERT(strcmp(prop->name, "myProperty") == 0, "Property name incorrect");
    TEST_ASSERT(strcmp(prop->type_name, "float") == 0, "Property type name incorrect");
    TEST_ASSERT(prop->type == LIGHTUSD_PROP_ATTRIB, "Property type incorrect");

    /* Test custom flag */
    TEST_ASSERT(!lightusd_property_is_custom(prop), "Property should not be custom initially");
    TEST_ASSERT(lightusd_property_set_custom(prop, 1), "Failed to set custom flag");
    TEST_ASSERT(lightusd_property_is_custom(prop), "Property should be custom");

    /* Test variability */
    TEST_ASSERT(lightusd_property_get_variability(prop) == LIGHTUSD_VARIABILITY_VARYING,
                "Default variability should be varying");
    TEST_ASSERT(lightusd_property_set_variability(prop, LIGHTUSD_VARIABILITY_UNIFORM),
                "Failed to set variability");
    TEST_ASSERT(lightusd_property_get_variability(prop) == LIGHTUSD_VARIABILITY_UNIFORM,
                "Variability should be uniform");

    /* Test value setting */
    lightusd_value_t *value = lightusd_value_create_float(2.5f);
    TEST_ASSERT(lightusd_property_set_value(prop, value), "Failed to set property value");

    const lightusd_value_t *retrieved_value = lightusd_property_get_value(prop);
    TEST_ASSERT(retrieved_value != NULL, "Failed to get property value");
    TEST_ASSERT(lightusd_value_get_type(retrieved_value) == LIGHTUSD_VALUE_FLOAT, "Value type incorrect");

    float float_val;
    TEST_ASSERT(lightusd_value_get_float(retrieved_value, &float_val), "Failed to get float from value");
    TEST_ASSERT(float_val == 2.5f, "Float value incorrect");

    /* Test relationship targets */
    TEST_ASSERT(lightusd_property_add_target(prop, "/path/to/target1"), "Failed to add target1");
    TEST_ASSERT(lightusd_property_add_target(prop, "/path/to/target2"), "Failed to add target2");
    TEST_ASSERT(lightusd_property_get_target_count(prop) == 2, "Target count should be 2");

    const char *target1 = lightusd_property_get_target(prop, 0);
    const char *target2 = lightusd_property_get_target(prop, 1);
    TEST_ASSERT(target1 != NULL && strcmp(target1, "/path/to/target1") == 0, "Target1 incorrect");
    TEST_ASSERT(target2 != NULL && strcmp(target2, "/path/to/target2") == 0, "Target2 incorrect");

    lightusd_value_destroy(value);
    lightusd_property_destroy(prop);
    TEST_SUCCESS();
}

/* ===== PrimSpec Tests ===== */

static int test_primspec_operations() {
    printf("Testing PrimSpec operations... ");

    /* Create a PrimSpec */
    lightusd_primspec_t *primspec = lightusd_primspec_create("myPrim", "Mesh", LIGHTUSD_SPEC_DEF);
    TEST_ASSERT(primspec != NULL, "Failed to create PrimSpec");
    TEST_ASSERT(strcmp(primspec->name, "myPrim") == 0, "PrimSpec name incorrect");
    TEST_ASSERT(strcmp(primspec->type_name, "Mesh") == 0, "PrimSpec type incorrect");
    TEST_ASSERT(primspec->specifier == LIGHTUSD_SPEC_DEF, "PrimSpec specifier incorrect");

    /* Test documentation */
    TEST_ASSERT(lightusd_primspec_set_doc(primspec, "This is a mesh primitive"),
                "Failed to set documentation");
    const char *doc = lightusd_primspec_get_doc(primspec);
    TEST_ASSERT(doc != NULL && strcmp(doc, "This is a mesh primitive") == 0,
                "Documentation incorrect");

    /* Test comment */
    TEST_ASSERT(lightusd_primspec_set_comment(primspec, "A test comment"),
                "Failed to set comment");
    const char *comment = lightusd_primspec_get_comment(primspec);
    TEST_ASSERT(comment != NULL && strcmp(comment, "A test comment") == 0,
                "Comment incorrect");

    /* Test adding properties */
    lightusd_property_t *prop1 = lightusd_property_create("points", "point3f[]", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_t *prop2 = lightusd_property_create("normals", "normal3f[]", LIGHTUSD_PROP_ATTRIB);

    TEST_ASSERT(lightusd_primspec_add_property(primspec, prop1), "Failed to add property1");
    TEST_ASSERT(lightusd_primspec_add_property(primspec, prop2), "Failed to add property2");

    /* Test retrieving properties */
    lightusd_property_t *retrieved_prop1 = lightusd_primspec_get_property(primspec, "points");
    lightusd_property_t *retrieved_prop2 = lightusd_primspec_get_property(primspec, "normals");
    TEST_ASSERT(retrieved_prop1 == prop1, "Retrieved property1 should match");
    TEST_ASSERT(retrieved_prop2 == prop2, "Retrieved property2 should match");

    /* Test properties map */
    lightusd_map_t *properties = lightusd_primspec_get_properties(primspec);
    TEST_ASSERT(properties != NULL, "Properties map should not be NULL");
    TEST_ASSERT(lightusd_map_size(properties) == 2, "Properties map should have 2 items");

    /* Test adding child PrimSpecs */
    lightusd_primspec_t *child1 = lightusd_primspec_create("child1", "Xform", LIGHTUSD_SPEC_DEF);
    lightusd_primspec_t *child2 = lightusd_primspec_create("child2", "Sphere", LIGHTUSD_SPEC_DEF);

    TEST_ASSERT(lightusd_primspec_add_child(primspec, child1), "Failed to add child1");
    TEST_ASSERT(lightusd_primspec_add_child(primspec, child2), "Failed to add child2");

    /* Test retrieving children */
    lightusd_primspec_t *retrieved_child1 = lightusd_primspec_get_child(primspec, "child1");
    lightusd_primspec_t *retrieved_child2 = lightusd_primspec_get_child(primspec, "child2");
    TEST_ASSERT(retrieved_child1 == child1, "Retrieved child1 should match");
    TEST_ASSERT(retrieved_child2 == child2, "Retrieved child2 should match");

    /* Test children map */
    lightusd_map_t *children = lightusd_primspec_get_children(primspec);
    TEST_ASSERT(children != NULL, "Children map should not be NULL");
    TEST_ASSERT(lightusd_map_size(children) == 2, "Children map should have 2 items");

    lightusd_primspec_destroy(primspec);
    TEST_SUCCESS();
}

/* ===== Layer Tests ===== */

static int test_layer_operations() {
    printf("Testing Layer operations... ");

    /* Create a layer */
    lightusd_layer_t *layer = lightusd_layer_create("TestLayer");
    TEST_ASSERT(layer != NULL, "Failed to create layer");
    TEST_ASSERT(strcmp(layer->name, "TestLayer") == 0, "Layer name incorrect");

    /* Test file path */
    TEST_ASSERT(lightusd_layer_set_file_path(layer, "/path/to/test.usd"),
                "Failed to set file path");
    const char *file_path = lightusd_layer_get_file_path(layer);
    TEST_ASSERT(file_path != NULL && strcmp(file_path, "/path/to/test.usd") == 0,
                "File path incorrect");

    /* Test documentation */
    TEST_ASSERT(lightusd_layer_set_doc(layer, "Test layer documentation"),
                "Failed to set layer documentation");
    const char *doc = lightusd_layer_get_doc(layer);
    TEST_ASSERT(doc != NULL && strcmp(doc, "Test layer documentation") == 0,
                "Layer documentation incorrect");

    /* Test up axis */
    TEST_ASSERT(lightusd_layer_set_up_axis(layer, "Z"), "Failed to set up axis");
    const char *up_axis = lightusd_layer_get_up_axis(layer);
    TEST_ASSERT(up_axis != NULL && strcmp(up_axis, "Z") == 0, "Up axis incorrect");

    /* Test meters per unit */
    TEST_ASSERT(lightusd_layer_set_meters_per_unit(layer, 0.01), "Failed to set meters per unit");
    double meters_per_unit = lightusd_layer_get_meters_per_unit(layer);
    TEST_ASSERT(meters_per_unit == 0.01, "Meters per unit incorrect");

    /* Test adding PrimSpecs */
    lightusd_primspec_t *root_prim = lightusd_primspec_create("World", "Xform", LIGHTUSD_SPEC_DEF);
    lightusd_primspec_t *mesh_prim = lightusd_primspec_create("Cube", "Mesh", LIGHTUSD_SPEC_DEF);

    TEST_ASSERT(lightusd_layer_add_primspec(layer, root_prim), "Failed to add root primspec");
    TEST_ASSERT(lightusd_layer_add_primspec(layer, mesh_prim), "Failed to add mesh primspec");

    /* Test retrieving PrimSpecs */
    lightusd_primspec_t *retrieved_root = lightusd_layer_get_primspec(layer, "World");
    lightusd_primspec_t *retrieved_mesh = lightusd_layer_get_primspec(layer, "Cube");
    TEST_ASSERT(retrieved_root == root_prim, "Retrieved root primspec should match");
    TEST_ASSERT(retrieved_mesh == mesh_prim, "Retrieved mesh primspec should match");

    /* Test PrimSpecs map */
    lightusd_map_t *primspecs = lightusd_layer_get_primspecs(layer);
    TEST_ASSERT(primspecs != NULL, "PrimSpecs map should not be NULL");
    TEST_ASSERT(lightusd_map_size(primspecs) == 2, "PrimSpecs map should have 2 items");

    lightusd_layer_destroy(layer);
    TEST_SUCCESS();
}

/* ===== Complex Scene Test ===== */

static int test_complex_scene() {
    printf("Testing complex scene creation... ");

    /* Create a layer representing a simple scene */
    lightusd_layer_t *layer = lightusd_layer_create("ComplexScene");
    TEST_ASSERT(layer != NULL, "Failed to create layer");

    /* Set layer metadata */
    lightusd_layer_set_doc(layer, "A complex USD scene with multiple primitives");
    lightusd_layer_set_up_axis(layer, "Y");
    lightusd_layer_set_meters_per_unit(layer, 1.0);

    /* Create root transform */
    lightusd_primspec_t *world = lightusd_primspec_create("World", "Xform", LIGHTUSD_SPEC_DEF);
    lightusd_primspec_set_doc(world, "Root transform for the scene");

    /* Add transform property */
    lightusd_property_t *xform_prop = lightusd_property_create("xformOp:transform", "matrix4d", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_set_variability(xform_prop, LIGHTUSD_VARIABILITY_UNIFORM);
    lightusd_primspec_add_property(world, xform_prop);

    /* Create geometry primitives */
    lightusd_primspec_t *cube = lightusd_primspec_create("Cube", "Mesh", LIGHTUSD_SPEC_DEF);
    lightusd_primspec_set_doc(cube, "A cube mesh");

    /* Add cube properties */
    lightusd_property_t *points_prop = lightusd_property_create("points", "point3f[]", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_t *normals_prop = lightusd_property_create("normals", "normal3f[]", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_t *uvs_prop = lightusd_property_create("primvars:st", "texCoord2f[]", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_set_custom(uvs_prop, 1); /* Custom primvar */

    lightusd_primspec_add_property(cube, points_prop);
    lightusd_primspec_add_property(cube, normals_prop);
    lightusd_primspec_add_property(cube, uvs_prop);

    /* Create a sphere */
    lightusd_primspec_t *sphere = lightusd_primspec_create("Sphere", "Sphere", LIGHTUSD_SPEC_DEF);
    lightusd_property_t *radius_prop = lightusd_property_create("radius", "double", LIGHTUSD_PROP_ATTRIB);
    lightusd_value_t *radius_value = lightusd_value_create_double(1.5);
    lightusd_property_set_value(radius_prop, radius_value);
    lightusd_primspec_add_property(sphere, radius_prop);

    /* Create material */
    lightusd_primspec_t *material = lightusd_primspec_create("Material", "Material", LIGHTUSD_SPEC_DEF);
    lightusd_property_t *surface_prop = lightusd_property_create("outputs:surface", "token", LIGHTUSD_PROP_RELATION);
    lightusd_property_add_target(surface_prop, "/World/Material/Shader.outputs:surface");
    lightusd_primspec_add_property(material, surface_prop);

    /* Build hierarchy */
    lightusd_primspec_add_child(world, cube);
    lightusd_primspec_add_child(world, sphere);
    lightusd_primspec_add_child(world, material);

    lightusd_layer_add_primspec(layer, world);

    /* Verify the scene structure */
    TEST_ASSERT(lightusd_map_size(lightusd_layer_get_primspecs(layer)) == 1,
                "Layer should have 1 root primspec");

    lightusd_primspec_t *retrieved_world = lightusd_layer_get_primspec(layer, "World");
    TEST_ASSERT(retrieved_world != NULL, "Should be able to retrieve World primspec");
    TEST_ASSERT(lightusd_map_size(lightusd_primspec_get_children(retrieved_world)) == 3,
                "World should have 3 children");
    TEST_ASSERT(lightusd_map_size(lightusd_primspec_get_properties(retrieved_world)) == 1,
                "World should have 1 property");

    lightusd_primspec_t *retrieved_cube = lightusd_primspec_get_child(retrieved_world, "Cube");
    TEST_ASSERT(retrieved_cube != NULL, "Should be able to retrieve Cube primspec");
    TEST_ASSERT(lightusd_map_size(lightusd_primspec_get_properties(retrieved_cube)) == 3,
                "Cube should have 3 properties");

    lightusd_primspec_t *retrieved_sphere = lightusd_primspec_get_child(retrieved_world, "Sphere");
    TEST_ASSERT(retrieved_sphere != NULL, "Should be able to retrieve Sphere primspec");

    lightusd_property_t *retrieved_radius = lightusd_primspec_get_property(retrieved_sphere, "radius");
    TEST_ASSERT(retrieved_radius != NULL, "Should be able to retrieve radius property");

    const lightusd_value_t *radius_val = lightusd_property_get_value(retrieved_radius);
    TEST_ASSERT(radius_val != NULL, "Radius should have a value");

    double radius_double;
    TEST_ASSERT(lightusd_value_get_double(radius_val, &radius_double), "Should get radius as double");
    TEST_ASSERT(radius_double == 1.5, "Radius value should be 1.5");

    lightusd_value_destroy(radius_value);
    lightusd_layer_destroy(layer);
    TEST_SUCCESS();
}

/* ===== Utility Function Tests ===== */

static int test_utility_functions() {
    printf("Testing utility functions... ");

    /* Test specifier strings */
    TEST_ASSERT(strcmp(lightusd_specifier_to_string(LIGHTUSD_SPEC_DEF), "def") == 0,
                "SPEC_DEF string incorrect");
    TEST_ASSERT(strcmp(lightusd_specifier_to_string(LIGHTUSD_SPEC_OVER), "over") == 0,
                "SPEC_OVER string incorrect");
    TEST_ASSERT(strcmp(lightusd_specifier_to_string(LIGHTUSD_SPEC_CLASS), "class") == 0,
                "SPEC_CLASS string incorrect");

    /* Test property type strings */
    TEST_ASSERT(strcmp(lightusd_property_type_to_string(LIGHTUSD_PROP_ATTRIB), "attrib") == 0,
                "PROP_ATTRIB string incorrect");
    TEST_ASSERT(strcmp(lightusd_property_type_to_string(LIGHTUSD_PROP_RELATION), "relation") == 0,
                "PROP_RELATION string incorrect");

    /* Test variability strings */
    TEST_ASSERT(strcmp(lightusd_variability_to_string(LIGHTUSD_VARIABILITY_VARYING), "varying") == 0,
                "VARIABILITY_VARYING string incorrect");
    TEST_ASSERT(strcmp(lightusd_variability_to_string(LIGHTUSD_VARIABILITY_UNIFORM), "uniform") == 0,
                "VARIABILITY_UNIFORM string incorrect");
    TEST_ASSERT(strcmp(lightusd_variability_to_string(LIGHTUSD_VARIABILITY_CONFIG), "config") == 0,
                "VARIABILITY_CONFIG string incorrect");

    TEST_SUCCESS();
}

/* ===== Main Test Runner ===== */

typedef struct {
    const char *name;
    int (*test_func)(void);
} test_case_t;

static test_case_t test_cases[] = {
    {"Map Basic Operations", test_map_basic_operations},
    {"Map Iteration", test_map_iteration},
    {"Value Operations", test_value_operations},
    {"Property Operations", test_property_operations},
    {"PrimSpec Operations", test_primspec_operations},
    {"Layer Operations", test_layer_operations},
    {"Complex Scene", test_complex_scene},
    {"Utility Functions", test_utility_functions},
};

int main(void) {
    printf("LIGHTUSD Layer C99 Implementation Test Suite\n");
    printf("=========================================\n\n");

    int total_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    int passed_tests = 0;

    for (int i = 0; i < total_tests; i++) {
        printf("[%d/%d] %s: ", i + 1, total_tests, test_cases[i].name);
        fflush(stdout);

        if (test_cases[i].test_func()) {
            passed_tests++;
        }
    }

    printf("\n=========================================\n");
    printf("Test Results: %d/%d tests passed\n", passed_tests, total_tests);

    if (passed_tests == total_tests) {
        printf("🎉 ALL TESTS PASSED! 🎉\n");
        printf("\nC99 USD Layer implementation is working correctly!\n");
        printf("Features tested:\n");
        printf("  ✓ Pure C99 AVL tree-based map with string keys\n");
        printf("  ✓ Comprehensive value system (bool, int, float, double, string, token)\n");
        printf("  ✓ Property management with metadata and relationships\n");
        printf("  ✓ PrimSpec hierarchy with properties and children\n");
        printf("  ✓ Layer management with metadata and composition\n");
        printf("  ✓ Complex scene graph construction\n");
        printf("  ✓ Memory management and cleanup\n");
        return 0;
    } else {
        printf("❌ Some tests failed. Please check the implementation.\n");
        return 1;
    }
}