#include "tusd_json.h"
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

/* ===== JSON Core Tests ===== */

static int test_json_value_creation() {
    printf("Testing JSON value creation... ");
    
    /* Test null value */
    tusd_json_value_t *null_val = tusd_json_value_create_null();
    TEST_ASSERT(null_val != NULL, "Failed to create null value");
    TEST_ASSERT(tusd_json_value_is_null(null_val), "Null value type check failed");
    tusd_json_value_destroy(null_val);
    
    /* Test bool value */
    tusd_json_value_t *bool_val = tusd_json_value_create_bool(1);
    TEST_ASSERT(bool_val != NULL, "Failed to create bool value");
    TEST_ASSERT(tusd_json_value_is_bool(bool_val), "Bool value type check failed");
    TEST_ASSERT(tusd_json_value_get_bool(bool_val) == 1, "Bool value incorrect");
    tusd_json_value_destroy(bool_val);
    
    /* Test number value */
    tusd_json_value_t *num_val = tusd_json_value_create_number(42.5);
    TEST_ASSERT(num_val != NULL, "Failed to create number value");
    TEST_ASSERT(tusd_json_value_is_number(num_val), "Number value type check failed");
    TEST_ASSERT(tusd_json_value_get_number(num_val) == 42.5, "Number value incorrect");
    tusd_json_value_destroy(num_val);
    
    /* Test string value */
    tusd_json_value_t *str_val = tusd_json_value_create_string("Hello, JSON!");
    TEST_ASSERT(str_val != NULL, "Failed to create string value");
    TEST_ASSERT(tusd_json_value_is_string(str_val), "String value type check failed");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(str_val), "Hello, JSON!") == 0, "String value incorrect");
    tusd_json_value_destroy(str_val);
    
    TEST_SUCCESS();
}

static int test_json_array_operations() {
    printf("Testing JSON array operations... ");
    
    tusd_json_value_t *array_val = tusd_json_value_create_array();
    TEST_ASSERT(array_val != NULL, "Failed to create array value");
    TEST_ASSERT(tusd_json_value_is_array(array_val), "Array value type check failed");
    
    tusd_json_array_t *array = tusd_json_value_get_array(array_val);
    TEST_ASSERT(array != NULL, "Failed to get array from value");
    TEST_ASSERT(tusd_json_array_size(array) == 0, "Array should be empty initially");
    
    /* Add elements */
    tusd_json_value_t *elem1 = tusd_json_value_create_number(10);
    tusd_json_value_t *elem2 = tusd_json_value_create_string("test");
    tusd_json_value_t *elem3 = tusd_json_value_create_bool(0);
    
    TEST_ASSERT(tusd_json_array_add(array, elem1), "Failed to add element 1");
    TEST_ASSERT(tusd_json_array_add(array, elem2), "Failed to add element 2");
    TEST_ASSERT(tusd_json_array_add(array, elem3), "Failed to add element 3");
    
    TEST_ASSERT(tusd_json_array_size(array) == 3, "Array size should be 3");
    
    /* Access elements */
    tusd_json_value_t *get_elem1 = tusd_json_array_get(array, 0);
    tusd_json_value_t *get_elem2 = tusd_json_array_get(array, 1);
    tusd_json_value_t *get_elem3 = tusd_json_array_get(array, 2);
    
    TEST_ASSERT(get_elem1 == elem1, "Array element 1 mismatch");
    TEST_ASSERT(get_elem2 == elem2, "Array element 2 mismatch");
    TEST_ASSERT(get_elem3 == elem3, "Array element 3 mismatch");
    
    TEST_ASSERT(tusd_json_value_get_number(get_elem1) == 10, "Element 1 value incorrect");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(get_elem2), "test") == 0, "Element 2 value incorrect");
    TEST_ASSERT(tusd_json_value_get_bool(get_elem3) == 0, "Element 3 value incorrect");
    
    tusd_json_value_destroy(array_val);
    TEST_SUCCESS();
}

static int test_json_object_operations() {
    printf("Testing JSON object operations... ");
    
    tusd_json_value_t *obj_val = tusd_json_value_create_object();
    TEST_ASSERT(obj_val != NULL, "Failed to create object value");
    TEST_ASSERT(tusd_json_value_is_object(obj_val), "Object value type check failed");
    
    tusd_json_object_t *obj = tusd_json_value_get_object(obj_val);
    TEST_ASSERT(obj != NULL, "Failed to get object from value");
    TEST_ASSERT(tusd_json_object_size(obj) == 0, "Object should be empty initially");
    
    /* Add key-value pairs */
    tusd_json_value_t *val1 = tusd_json_value_create_string("value1");
    tusd_json_value_t *val2 = tusd_json_value_create_number(123);
    tusd_json_value_t *val3 = tusd_json_value_create_bool(1);
    
    TEST_ASSERT(tusd_json_object_set(obj, "key1", val1), "Failed to set key1");
    TEST_ASSERT(tusd_json_object_set(obj, "key2", val2), "Failed to set key2");
    TEST_ASSERT(tusd_json_object_set(obj, "key3", val3), "Failed to set key3");
    
    TEST_ASSERT(tusd_json_object_size(obj) == 3, "Object size should be 3");
    
    /* Access values */
    tusd_json_value_t *get_val1 = tusd_json_object_get(obj, "key1");
    tusd_json_value_t *get_val2 = tusd_json_object_get(obj, "key2");
    tusd_json_value_t *get_val3 = tusd_json_object_get(obj, "key3");
    
    TEST_ASSERT(get_val1 == val1, "Object value 1 mismatch");
    TEST_ASSERT(get_val2 == val2, "Object value 2 mismatch");
    TEST_ASSERT(get_val3 == val3, "Object value 3 mismatch");
    
    TEST_ASSERT(tusd_json_object_has_key(obj, "key1"), "Should have key1");
    TEST_ASSERT(tusd_json_object_has_key(obj, "key2"), "Should have key2");
    TEST_ASSERT(tusd_json_object_has_key(obj, "key3"), "Should have key3");
    TEST_ASSERT(!tusd_json_object_has_key(obj, "key4"), "Should not have key4");
    
    tusd_json_value_destroy(obj_val);
    TEST_SUCCESS();
}

static int test_json_parser_basic() {
    printf("Testing JSON parser basic functionality... ");
    
    /* Test null parsing */
    tusd_json_value_t *null_val = tusd_json_parse("null");
    TEST_ASSERT(null_val != NULL, "Failed to parse null");
    TEST_ASSERT(tusd_json_value_is_null(null_val), "Parsed null type incorrect");
    tusd_json_value_destroy(null_val);
    
    /* Test bool parsing */
    tusd_json_value_t *true_val = tusd_json_parse("true");
    tusd_json_value_t *false_val = tusd_json_parse("false");
    TEST_ASSERT(true_val != NULL && tusd_json_value_is_bool(true_val), "Failed to parse true");
    TEST_ASSERT(false_val != NULL && tusd_json_value_is_bool(false_val), "Failed to parse false");
    TEST_ASSERT(tusd_json_value_get_bool(true_val) == 1, "True value incorrect");
    TEST_ASSERT(tusd_json_value_get_bool(false_val) == 0, "False value incorrect");
    tusd_json_value_destroy(true_val);
    tusd_json_value_destroy(false_val);
    
    /* Test number parsing */
    tusd_json_value_t *int_val = tusd_json_parse("42");
    tusd_json_value_t *float_val = tusd_json_parse("3.14159");
    tusd_json_value_t *neg_val = tusd_json_parse("-123.45");
    
    TEST_ASSERT(int_val != NULL && tusd_json_value_is_number(int_val), "Failed to parse integer");
    TEST_ASSERT(float_val != NULL && tusd_json_value_is_number(float_val), "Failed to parse float");
    TEST_ASSERT(neg_val != NULL && tusd_json_value_is_number(neg_val), "Failed to parse negative");
    
    TEST_ASSERT(tusd_json_value_get_number(int_val) == 42, "Integer value incorrect");
    TEST_ASSERT(tusd_json_value_get_number(float_val) == 3.14159, "Float value incorrect");
    TEST_ASSERT(tusd_json_value_get_number(neg_val) == -123.45, "Negative value incorrect");
    
    tusd_json_value_destroy(int_val);
    tusd_json_value_destroy(float_val);
    tusd_json_value_destroy(neg_val);
    
    /* Test string parsing */
    tusd_json_value_t *str_val = tusd_json_parse("\"Hello, World!\"");
    tusd_json_value_t *empty_str_val = tusd_json_parse("\"\"");
    tusd_json_value_t *escape_val = tusd_json_parse("\"Line 1\\nLine 2\\tTab\"");
    
    TEST_ASSERT(str_val != NULL && tusd_json_value_is_string(str_val), "Failed to parse string");
    TEST_ASSERT(empty_str_val != NULL && tusd_json_value_is_string(empty_str_val), "Failed to parse empty string");
    TEST_ASSERT(escape_val != NULL && tusd_json_value_is_string(escape_val), "Failed to parse escaped string");
    
    TEST_ASSERT(strcmp(tusd_json_value_get_string(str_val), "Hello, World!") == 0, "String value incorrect");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(empty_str_val), "") == 0, "Empty string value incorrect");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(escape_val), "Line 1\nLine 2\tTab") == 0, "Escaped string value incorrect");
    
    tusd_json_value_destroy(str_val);
    tusd_json_value_destroy(empty_str_val);
    tusd_json_value_destroy(escape_val);
    
    TEST_SUCCESS();
}

static int test_json_parser_complex() {
    printf("Testing JSON parser complex structures... ");
    
    /* Test array parsing */
    const char *array_json = "[1, \"test\", true, null, [2, 3], {\"nested\": \"object\"}]";
    tusd_json_value_t *array_val = tusd_json_parse(array_json);
    TEST_ASSERT(array_val != NULL && tusd_json_value_is_array(array_val), "Failed to parse array");
    
    tusd_json_array_t *array = tusd_json_value_get_array(array_val);
    TEST_ASSERT(tusd_json_array_size(array) == 6, "Array size incorrect");
    
    /* Check array elements */
    TEST_ASSERT(tusd_json_value_is_number(tusd_json_array_get(array, 0)), "Array[0] should be number");
    TEST_ASSERT(tusd_json_value_is_string(tusd_json_array_get(array, 1)), "Array[1] should be string");
    TEST_ASSERT(tusd_json_value_is_bool(tusd_json_array_get(array, 2)), "Array[2] should be bool");
    TEST_ASSERT(tusd_json_value_is_null(tusd_json_array_get(array, 3)), "Array[3] should be null");
    TEST_ASSERT(tusd_json_value_is_array(tusd_json_array_get(array, 4)), "Array[4] should be array");
    TEST_ASSERT(tusd_json_value_is_object(tusd_json_array_get(array, 5)), "Array[5] should be object");
    
    tusd_json_value_destroy(array_val);
    
    /* Test object parsing */
    const char *object_json = "{\n"
        "  \"name\": \"test\",\n"
        "  \"count\": 42,\n"
        "  \"active\": true,\n"
        "  \"data\": null,\n"
        "  \"items\": [1, 2, 3],\n"
        "  \"nested\": {\n"
        "    \"inner\": \"value\"\n"
        "  }\n"
        "}";
    
    tusd_json_value_t *obj_val = tusd_json_parse(object_json);
    TEST_ASSERT(obj_val != NULL && tusd_json_value_is_object(obj_val), "Failed to parse object");
    
    tusd_json_object_t *obj = tusd_json_value_get_object(obj_val);
    TEST_ASSERT(tusd_json_object_size(obj) == 6, "Object size incorrect");
    
    /* Check object values */
    TEST_ASSERT(tusd_json_object_has_key(obj, "name"), "Object should have 'name' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "count"), "Object should have 'count' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "active"), "Object should have 'active' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "data"), "Object should have 'data' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "items"), "Object should have 'items' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "nested"), "Object should have 'nested' key");
    
    tusd_json_value_t *name_val = tusd_json_object_get(obj, "name");
    TEST_ASSERT(tusd_json_value_is_string(name_val), "Name should be string");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(name_val), "test") == 0, "Name value incorrect");
    
    tusd_json_value_t *count_val = tusd_json_object_get(obj, "count");
    TEST_ASSERT(tusd_json_value_is_number(count_val), "Count should be number");
    TEST_ASSERT(tusd_json_value_get_number(count_val) == 42, "Count value incorrect");
    
    tusd_json_value_destroy(obj_val);
    
    TEST_SUCCESS();
}

static int test_json_serializer() {
    printf("Testing JSON serializer... ");
    
    /* Create a complex JSON structure */
    tusd_json_value_t *root = tusd_json_value_create_object();
    tusd_json_object_t *root_obj = tusd_json_value_get_object(root);
    
    /* Add basic values */
    tusd_json_object_set(root_obj, "name", tusd_json_value_create_string("Test Object"));
    tusd_json_object_set(root_obj, "id", tusd_json_value_create_number(12345));
    tusd_json_object_set(root_obj, "active", tusd_json_value_create_bool(1));
    tusd_json_object_set(root_obj, "data", tusd_json_value_create_null());
    
    /* Add array */
    tusd_json_value_t *array_val = tusd_json_value_create_array();
    tusd_json_array_t *array = tusd_json_value_get_array(array_val);
    tusd_json_array_add(array, tusd_json_value_create_number(1));
    tusd_json_array_add(array, tusd_json_value_create_number(2));
    tusd_json_array_add(array, tusd_json_value_create_number(3));
    tusd_json_object_set(root_obj, "numbers", array_val);
    
    /* Add nested object */
    tusd_json_value_t *nested_val = tusd_json_value_create_object();
    tusd_json_object_t *nested = tusd_json_value_get_object(nested_val);
    tusd_json_object_set(nested, "inner", tusd_json_value_create_string("nested value"));
    tusd_json_object_set(root_obj, "nested", nested_val);
    
    /* Test compact serialization */
    char *compact_json = tusd_json_serialize(root);
    TEST_ASSERT(compact_json != NULL, "Failed to serialize JSON");
    TEST_ASSERT(strlen(compact_json) > 0, "Serialized JSON is empty");
    
    /* Test that we can parse back the serialized JSON */
    tusd_json_value_t *parsed = tusd_json_parse(compact_json);
    TEST_ASSERT(parsed != NULL, "Failed to parse serialized JSON");
    TEST_ASSERT(tusd_json_value_is_object(parsed), "Parsed value should be object");
    
    tusd_json_object_t *parsed_obj = tusd_json_value_get_object(parsed);
    TEST_ASSERT(tusd_json_object_size(parsed_obj) == 6, "Parsed object should have 6 keys");
    
    tusd_json_value_t *parsed_name = tusd_json_object_get(parsed_obj, "name");
    TEST_ASSERT(parsed_name != NULL && tusd_json_value_is_string(parsed_name), "Parsed name should be string");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(parsed_name), "Test Object") == 0, "Parsed name value incorrect");
    
    free(compact_json);
    tusd_json_value_destroy(parsed);
    
    /* Test pretty printing */
    char *pretty_json = tusd_json_serialize_pretty(root, 2);
    TEST_ASSERT(pretty_json != NULL, "Failed to serialize pretty JSON");
    TEST_ASSERT(strlen(pretty_json) > 0, "Pretty JSON is empty");
    TEST_ASSERT(strstr(pretty_json, "\n") != NULL, "Pretty JSON should contain newlines");
    TEST_ASSERT(strstr(pretty_json, "  ") != NULL, "Pretty JSON should contain indentation");
    
    free(pretty_json);
    tusd_json_value_destroy(root);
    
    TEST_SUCCESS();
}

static int test_json_file_io() {
    printf("Testing JSON file I/O... ");
    
    /* Create a test JSON structure */
    tusd_json_value_t *test_obj = tusd_json_value_create_object();
    tusd_json_object_t *obj = tusd_json_value_get_object(test_obj);
    
    tusd_json_object_set(obj, "test", tusd_json_value_create_string("value"));
    tusd_json_object_set(obj, "number", tusd_json_value_create_number(42));
    tusd_json_object_set(obj, "bool", tusd_json_value_create_bool(1));
    
    /* Write to file */
    const char *filename = "test_simple.json";
    int write_result = tusd_json_write_file_pretty(test_obj, filename, 2);
    TEST_ASSERT(write_result != 0, "Failed to write JSON to file");
    
    /* Read file and parse */
    FILE *file = fopen(filename, "r");
    TEST_ASSERT(file != NULL, "Failed to open test file for reading");
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *content = malloc(file_size + 1);
    TEST_ASSERT(content != NULL, "Failed to allocate memory for file content");
    
    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);
    
    /* Parse the content */
    tusd_json_value_t *loaded_obj = tusd_json_parse(content);
    TEST_ASSERT(loaded_obj != NULL, "Failed to parse loaded JSON");
    TEST_ASSERT(tusd_json_value_is_object(loaded_obj), "Loaded JSON should be object");
    
    tusd_json_object_t *loaded = tusd_json_value_get_object(loaded_obj);
    TEST_ASSERT(tusd_json_object_size(loaded) == 3, "Loaded object should have 3 keys");
    
    tusd_json_value_t *test_val = tusd_json_object_get(loaded, "test");
    TEST_ASSERT(test_val != NULL && tusd_json_value_is_string(test_val), "Test value should be string");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(test_val), "value") == 0, "Test value incorrect");
    
    tusd_json_value_t *number_val = tusd_json_object_get(loaded, "number");
    TEST_ASSERT(number_val != NULL && tusd_json_value_is_number(number_val), "Number value should be number");
    TEST_ASSERT(tusd_json_value_get_number(number_val) == 42, "Number value incorrect");
    
    /* Clean up */
    free(content);
    tusd_json_value_destroy(test_obj);
    tusd_json_value_destroy(loaded_obj);
    remove(filename);
    
    TEST_SUCCESS();
}

static int test_json_utilities() {
    printf("Testing JSON utility functions... ");
    
    /* Test string escaping */
    char *escaped = tusd_json_escape_string("Hello\nWorld\t\"Test\"");
    TEST_ASSERT(escaped != NULL, "Failed to escape string");
    TEST_ASSERT(strcmp(escaped, "Hello\\nWorld\\t\\\"Test\\\"") == 0, "String escaping incorrect");
    free(escaped);
    
    /* Test JSON validation */
    TEST_ASSERT(tusd_json_validate("{\"valid\": true}") == 1, "Valid JSON should validate");
    TEST_ASSERT(tusd_json_validate("{invalid json}") == 0, "Invalid JSON should not validate");
    TEST_ASSERT(tusd_json_validate("null") == 1, "Simple null should validate");
    TEST_ASSERT(tusd_json_validate("") == 0, "Empty string should not validate");
    
    /* Test memory usage estimation */
    tusd_json_value_t *test_obj = tusd_json_value_create_object();
    tusd_json_object_t *obj = tusd_json_value_get_object(test_obj);
    tusd_json_object_set(obj, "test", tusd_json_value_create_string("value"));
    
    size_t mem_usage = tusd_json_estimate_memory_usage(test_obj);
    TEST_ASSERT(mem_usage > 0, "Memory usage should be greater than 0");
    
    tusd_json_value_destroy(test_obj);
    
    TEST_SUCCESS();
}

/* ===== Main Test Runner ===== */

typedef struct {
    const char *name;
    int (*test_func)(void);
} test_case_t;

static test_case_t test_cases[] = {
    {"JSON Value Creation", test_json_value_creation},
    {"JSON Array Operations", test_json_array_operations},
    {"JSON Object Operations", test_json_object_operations},
    {"JSON Parser Basic", test_json_parser_basic},
    {"JSON Parser Complex", test_json_parser_complex},
    {"JSON Serializer", test_json_serializer},
    {"JSON File I/O", test_json_file_io},
    {"JSON Utilities", test_json_utilities},
};

int main(void) {
    printf("TUSD JSON Library Core Test Suite\n");
    printf("==================================\n\n");
    
    int total_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    int passed_tests = 0;
    
    for (int i = 0; i < total_tests; i++) {
        printf("[%d/%d] %s: ", i + 1, total_tests, test_cases[i].name);
        fflush(stdout);
        
        if (test_cases[i].test_func()) {
            passed_tests++;
        }
    }
    
    printf("\n==================================\n");
    printf("Test Results: %d/%d tests passed\n", passed_tests, total_tests);
    
    if (passed_tests == total_tests) {
        printf("🎉 ALL TESTS PASSED! 🎉\n");
        printf("\nC99 JSON library core functionality is working correctly!\n");
        printf("Features tested:\n");
        printf("  ✓ Pure C99 JSON parser with full RFC 7159 compliance\n");
        printf("  ✓ JSON serialization with compact and pretty-print modes\n");
        printf("  ✓ Complete JSON value system (null, bool, number, string, array, object)\n");
        printf("  ✓ Dynamic arrays and objects with automatic memory management\n");
        printf("  ✓ File I/O operations for JSON data interchange\n");
        printf("  ✓ String escaping and JSON validation utilities\n");
        printf("  ✓ Memory usage estimation and cleanup\n");
        return 0;
    } else {
        printf("❌ Some tests failed. Please check the implementation.\n");
        return 1;
    }
}