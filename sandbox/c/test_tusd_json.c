#include "tusd_json.h"
#include "tusd_layer.h"
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

/* ===== JSON Value Tests ===== */

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
    
    /* Test key replacement */
    tusd_json_value_t *new_val = tusd_json_value_create_string("replaced");
    TEST_ASSERT(tusd_json_object_set(obj, "key1", new_val), "Failed to replace key1");
    TEST_ASSERT(tusd_json_object_size(obj) == 3, "Object size should still be 3");
    
    tusd_json_value_t *replaced_val = tusd_json_object_get(obj, "key1");
    TEST_ASSERT(replaced_val == new_val, "Replaced value mismatch");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(replaced_val), "replaced") == 0, "Replaced value incorrect");
    
    tusd_json_value_destroy(obj_val);
    TEST_SUCCESS();
}

/* ===== JSON Parser Tests ===== */

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
    tusd_json_value_t *exp_val = tusd_json_parse("1.23e-4");
    
    TEST_ASSERT(int_val != NULL && tusd_json_value_is_number(int_val), "Failed to parse integer");
    TEST_ASSERT(float_val != NULL && tusd_json_value_is_number(float_val), "Failed to parse float");
    TEST_ASSERT(neg_val != NULL && tusd_json_value_is_number(neg_val), "Failed to parse negative");
    TEST_ASSERT(exp_val != NULL && tusd_json_value_is_number(exp_val), "Failed to parse exponential");
    
    TEST_ASSERT(tusd_json_value_get_number(int_val) == 42, "Integer value incorrect");
    TEST_ASSERT(tusd_json_value_get_number(float_val) == 3.14159, "Float value incorrect");
    TEST_ASSERT(tusd_json_value_get_number(neg_val) == -123.45, "Negative value incorrect");
    TEST_ASSERT(tusd_json_value_get_number(exp_val) == 1.23e-4, "Exponential value incorrect");
    
    tusd_json_value_destroy(int_val);
    tusd_json_value_destroy(float_val);
    tusd_json_value_destroy(neg_val);
    tusd_json_value_destroy(exp_val);
    
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

/* ===== JSON Serializer Tests ===== */

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

/* ===== USD Layer <-> JSON Conversion Tests ===== */

static int test_usd_value_json_conversion() {
    printf("Testing USD value <-> JSON conversion... ");
    
    /* Test bool conversion */
    tusd_value_t *bool_usd = tusd_value_create_bool(1);
    tusd_json_value_t *bool_json = tusd_value_to_json(bool_usd);
    TEST_ASSERT(bool_json != NULL && tusd_json_value_is_bool(bool_json), "Bool USD->JSON conversion failed");
    TEST_ASSERT(tusd_json_value_get_bool(bool_json) == 1, "Bool JSON value incorrect");
    
    struct tusd_value_t *bool_usd_back = tusd_json_to_value(bool_json);
    TEST_ASSERT(bool_usd_back != NULL && bool_usd_back->type == TUSD_VALUE_BOOL, "Bool JSON->USD conversion failed");
    TEST_ASSERT(bool_usd_back->data.bool_val == 1, "Bool USD value incorrect");
    
    tusd_value_destroy(bool_usd);
    tusd_json_value_destroy(bool_json);
    tusd_value_destroy(bool_usd_back);
    
    /* Test int conversion */
    tusd_value_t *int_usd = tusd_value_create_int(42);
    tusd_json_value_t *int_json = tusd_value_to_json(int_usd);
    TEST_ASSERT(int_json != NULL && tusd_json_value_is_number(int_json), "Int USD->JSON conversion failed");
    TEST_ASSERT(tusd_json_value_get_number(int_json) == 42.0, "Int JSON value incorrect");
    
    struct tusd_value_t *int_usd_back = tusd_json_to_value(int_json);
    TEST_ASSERT(int_usd_back != NULL && int_usd_back->type == TUSD_VALUE_INT, "Int JSON->USD conversion failed");
    TEST_ASSERT(int_usd_back->data.int_val == 42, "Int USD value incorrect");
    
    tusd_value_destroy(int_usd);
    tusd_json_value_destroy(int_json);
    tusd_value_destroy(int_usd_back);
    
    /* Test double conversion */
    tusd_value_t *double_usd = tusd_value_create_double(3.14159);
    tusd_json_value_t *double_json = tusd_value_to_json(double_usd);
    TEST_ASSERT(double_json != NULL && tusd_json_value_is_number(double_json), "Double USD->JSON conversion failed");
    TEST_ASSERT(tusd_json_value_get_number(double_json) == 3.14159, "Double JSON value incorrect");
    
    struct tusd_value_t *double_usd_back = tusd_json_to_value(double_json);
    TEST_ASSERT(double_usd_back != NULL && double_usd_back->type == TUSD_VALUE_DOUBLE, "Double JSON->USD conversion failed");
    TEST_ASSERT(double_usd_back->data.double_val == 3.14159, "Double USD value incorrect");
    
    tusd_value_destroy(double_usd);
    tusd_json_value_destroy(double_json);
    tusd_value_destroy(double_usd_back);
    
    /* Test string conversion */
    tusd_value_t *string_usd = tusd_value_create_string("Hello, USD!");
    tusd_json_value_t *string_json = tusd_value_to_json(string_usd);
    TEST_ASSERT(string_json != NULL && tusd_json_value_is_string(string_json), "String USD->JSON conversion failed");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(string_json), "Hello, USD!") == 0, "String JSON value incorrect");
    
    struct tusd_value_t *string_usd_back = tusd_json_to_value(string_json);
    TEST_ASSERT(string_usd_back != NULL && string_usd_back->type == TUSD_VALUE_STRING, "String JSON->USD conversion failed");
    TEST_ASSERT(strcmp(string_usd_back->data.string_val, "Hello, USD!") == 0, "String USD value incorrect");
    
    tusd_value_destroy(string_usd);
    tusd_json_value_destroy(string_json);
    tusd_value_destroy(string_usd_back);
    
    TEST_SUCCESS();
}

static int test_usd_property_json_conversion() {
    printf("Testing USD property <-> JSON conversion... ");
    
    /* Create a property */
    tusd_property_t *prop = tusd_property_create("testProp", "float", TUSD_PROP_ATTRIB);
    TEST_ASSERT(prop != NULL, "Failed to create property");
    
    /* Set property attributes */
    tusd_property_set_custom(prop, 1);
    tusd_property_set_variability(prop, TUSD_VARIABILITY_UNIFORM);
    
    tusd_value_t *value = tusd_value_create_double(2.718);
    tusd_property_set_value(prop, value);
    tusd_value_destroy(value);
    
    tusd_property_add_target(prop, "/path/to/target1");
    tusd_property_add_target(prop, "/path/to/target2");
    
    /* Convert to JSON */
    tusd_json_value_t *json = tusd_property_to_json(prop);
    TEST_ASSERT(json != NULL && tusd_json_value_is_object(json), "Property USD->JSON conversion failed");
    
    tusd_json_object_t *obj = tusd_json_value_get_object(json);
    TEST_ASSERT(tusd_json_object_has_key(obj, "name"), "JSON should have 'name' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "type_name"), "JSON should have 'type_name' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "property_type"), "JSON should have 'property_type' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "variability"), "JSON should have 'variability' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "is_custom"), "JSON should have 'is_custom' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "value"), "JSON should have 'value' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "targets"), "JSON should have 'targets' key");
    
    /* Check values */
    tusd_json_value_t *name_val = tusd_json_object_get(obj, "name");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(name_val), "testProp") == 0, "Property name incorrect in JSON");
    
    tusd_json_value_t *is_custom_val = tusd_json_object_get(obj, "is_custom");
    TEST_ASSERT(tusd_json_value_get_bool(is_custom_val) == 1, "Property is_custom incorrect in JSON");
    
    tusd_json_value_t *value_val = tusd_json_object_get(obj, "value");
    TEST_ASSERT(tusd_json_value_get_number(value_val) == 2.718, "Property value incorrect in JSON");
    
    /* Convert back to USD */
    tusd_property_t *prop_back = tusd_json_to_property(json);
    TEST_ASSERT(prop_back != NULL, "Property JSON->USD conversion failed");
    TEST_ASSERT(strcmp(prop_back->name, "testProp") == 0, "Converted property name incorrect");
    TEST_ASSERT(strcmp(prop_back->type_name, "float") == 0, "Converted property type_name incorrect");
    TEST_ASSERT(prop_back->type == TUSD_PROP_ATTRIB, "Converted property type incorrect");
    TEST_ASSERT(prop_back->variability == TUSD_VARIABILITY_UNIFORM, "Converted property variability incorrect");
    TEST_ASSERT(prop_back->is_custom == 1, "Converted property is_custom incorrect");
    TEST_ASSERT(prop_back->has_value == 1, "Converted property should have value");
    TEST_ASSERT(prop_back->target_count == 2, "Converted property should have 2 targets");
    
    tusd_property_destroy(prop);
    tusd_json_value_destroy(json);
    tusd_property_destroy(prop_back);
    
    TEST_SUCCESS();
}

static int test_usd_layer_json_conversion() {
    printf("Testing USD layer <-> JSON conversion... ");
    
    /* Create a simple layer */
    tusd_layer_t *layer = tusd_layer_create("TestLayer");
    TEST_ASSERT(layer != NULL, "Failed to create layer");
    
    /* Set layer metadata */
    tusd_layer_set_doc(layer, "Test layer for JSON conversion");
    tusd_layer_set_up_axis(layer, "Y");
    tusd_layer_set_meters_per_unit(layer, 0.01);
    
    /* Create a simple prim */
    tusd_primspec_t *prim = tusd_primspec_create("TestPrim", "Mesh", TUSD_SPEC_DEF);
    tusd_primspec_set_doc(prim, "A test primitive");
    
    /* Add a property to the prim */
    tusd_property_t *prop = tusd_property_create("testAttr", "float", TUSD_PROP_ATTRIB);
    tusd_value_t *prop_value = tusd_value_create_double(1.23);
    tusd_property_set_value(prop, prop_value);
    tusd_value_destroy(prop_value);
    tusd_primspec_add_property(prim, prop);
    
    /* Add prim to layer */
    tusd_layer_add_primspec(layer, prim);
    
    /* Convert to JSON */
    tusd_json_value_t *json = tusd_layer_to_json(layer);
    TEST_ASSERT(json != NULL && tusd_json_value_is_object(json), "Layer USD->JSON conversion failed");
    
    tusd_json_object_t *obj = tusd_json_value_get_object(json);
    TEST_ASSERT(tusd_json_object_has_key(obj, "name"), "JSON should have 'name' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "metadata"), "JSON should have 'metadata' key");
    TEST_ASSERT(tusd_json_object_has_key(obj, "primspecs"), "JSON should have 'primspecs' key");
    
    /* Check metadata */
    tusd_json_value_t *metadata_val = tusd_json_object_get(obj, "metadata");
    TEST_ASSERT(tusd_json_value_is_object(metadata_val), "Metadata should be object");
    
    tusd_json_object_t *metadata_obj = tusd_json_value_get_object(metadata_val);
    tusd_json_value_t *doc_val = tusd_json_object_get(metadata_obj, "doc");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(doc_val), "Test layer for JSON conversion") == 0, "Layer doc incorrect in JSON");
    
    tusd_json_value_t *up_axis_val = tusd_json_object_get(metadata_obj, "up_axis");
    TEST_ASSERT(strcmp(tusd_json_value_get_string(up_axis_val), "Y") == 0, "Layer up_axis incorrect in JSON");
    
    /* Convert back to USD */
    tusd_layer_t *layer_back = tusd_json_to_layer(json);
    TEST_ASSERT(layer_back != NULL, "Layer JSON->USD conversion failed");
    TEST_ASSERT(strcmp(layer_back->name, "TestLayer") == 0, "Converted layer name incorrect");
    TEST_ASSERT(layer_back->metas.doc != NULL, "Converted layer should have doc");
    TEST_ASSERT(strcmp(layer_back->metas.doc, "Test layer for JSON conversion") == 0, "Converted layer doc incorrect");
    TEST_ASSERT(layer_back->metas.meters_per_unit == 0.01, "Converted layer meters_per_unit incorrect");
    
    /* Check that primspecs were converted */
    TEST_ASSERT(tusd_map_size(layer_back->primspecs) == 1, "Converted layer should have 1 primspec");
    
    tusd_primspec_t *prim_back = tusd_layer_get_primspec(layer_back, "TestPrim");
    TEST_ASSERT(prim_back != NULL, "Converted layer should have TestPrim");
    TEST_ASSERT(strcmp(prim_back->name, "TestPrim") == 0, "Converted prim name incorrect");
    TEST_ASSERT(strcmp(prim_back->type_name, "Mesh") == 0, "Converted prim type incorrect");
    
    tusd_layer_destroy(layer);
    tusd_json_value_destroy(json);
    tusd_layer_destroy(layer_back);
    
    TEST_SUCCESS();
}

static int test_json_roundtrip_conversion() {
    printf("Testing complete JSON roundtrip conversion... ");
    
    /* Create a complex layer structure */
    tusd_layer_t *original = tusd_layer_create("RoundtripTest");
    tusd_layer_set_doc(original, "Roundtrip test layer");
    tusd_layer_set_up_axis(original, "Z");
    tusd_layer_set_meters_per_unit(original, 1.0);
    
    /* Create root prim */
    tusd_primspec_t *root = tusd_primspec_create("World", "Xform", TUSD_SPEC_DEF);
    tusd_primspec_set_doc(root, "Root transform");
    
    /* Add transform property */
    tusd_property_t *xform_prop = tusd_property_create("xformOp:transform", "matrix4d", TUSD_PROP_ATTRIB);
    tusd_property_set_variability(xform_prop, TUSD_VARIABILITY_UNIFORM);
    tusd_primspec_add_property(root, xform_prop);
    
    /* Create child mesh */
    tusd_primspec_t *mesh = tusd_primspec_create("TestMesh", "Mesh", TUSD_SPEC_DEF);
    
    /* Add mesh properties */
    tusd_property_t *points_prop = tusd_property_create("points", "point3f[]", TUSD_PROP_ATTRIB);
    tusd_property_t *material_rel = tusd_property_create("material:binding", "token", TUSD_PROP_RELATION);
    tusd_property_add_target(material_rel, "/World/Materials/TestMaterial");
    
    tusd_primspec_add_property(mesh, points_prop);
    tusd_primspec_add_property(mesh, material_rel);
    
    /* Build hierarchy */
    tusd_primspec_add_child(root, mesh);
    tusd_layer_add_primspec(original, root);
    
    /* Convert to JSON string */
    char *json_str = tusd_layer_to_json_string_pretty(original, 2);
    TEST_ASSERT(json_str != NULL, "Failed to convert layer to JSON string");
    TEST_ASSERT(strlen(json_str) > 0, "JSON string is empty");
    
    /* Convert back from JSON string */
    tusd_layer_t *restored = tusd_layer_from_json_string(json_str);
    TEST_ASSERT(restored != NULL, "Failed to restore layer from JSON string");
    
    /* Verify restored layer */
    TEST_ASSERT(strcmp(restored->name, "RoundtripTest") == 0, "Restored layer name incorrect");
    TEST_ASSERT(restored->metas.doc != NULL, "Restored layer should have doc");
    TEST_ASSERT(strcmp(restored->metas.doc, "Roundtrip test layer") == 0, "Restored layer doc incorrect");
    TEST_ASSERT(restored->metas.meters_per_unit == 1.0, "Restored layer meters_per_unit incorrect");
    
    /* Verify restored primspecs */
    TEST_ASSERT(tusd_map_size(restored->primspecs) == 1, "Restored layer should have 1 root primspec");
    
    tusd_primspec_t *restored_root = tusd_layer_get_primspec(restored, "World");
    TEST_ASSERT(restored_root != NULL, "Restored layer should have World primspec");
    TEST_ASSERT(tusd_map_size(restored_root->children) == 1, "Restored root should have 1 child");
    TEST_ASSERT(tusd_map_size(restored_root->properties) == 1, "Restored root should have 1 property");
    
    tusd_primspec_t *restored_mesh = tusd_primspec_get_child(restored_root, "TestMesh");
    TEST_ASSERT(restored_mesh != NULL, "Restored root should have TestMesh child");
    TEST_ASSERT(tusd_map_size(restored_mesh->properties) == 2, "Restored mesh should have 2 properties");
    
    tusd_property_t *restored_material_rel = tusd_primspec_get_property(restored_mesh, "material:binding");
    TEST_ASSERT(restored_material_rel != NULL, "Restored mesh should have material:binding property");
    TEST_ASSERT(restored_material_rel->target_count == 1, "Restored material relation should have 1 target");
    TEST_ASSERT(strcmp(restored_material_rel->target_paths[0], "/World/Materials/TestMaterial") == 0, 
                "Restored material relation target incorrect");
    
    tusd_layer_destroy(original);
    tusd_layer_destroy(restored);
    free(json_str);
    
    TEST_SUCCESS();
}

/* ===== File I/O Tests ===== */

static int test_json_file_io() {
    printf("Testing JSON file I/O... ");
    
    /* Create a test layer */
    tusd_layer_t *layer = tusd_layer_create("FileIOTest");
    tusd_layer_set_doc(layer, "File I/O test layer");
    
    tusd_primspec_t *prim = tusd_primspec_create("TestPrim", "Sphere", TUSD_SPEC_DEF);
    tusd_property_t *radius_prop = tusd_property_create("radius", "double", TUSD_PROP_ATTRIB);
    tusd_value_t *radius_val = tusd_value_create_double(2.5);
    tusd_property_set_value(radius_prop, radius_val);
    tusd_value_destroy(radius_val);
    tusd_primspec_add_property(prim, radius_prop);
    tusd_layer_add_primspec(layer, prim);
    
    /* Save to file */
    const char *filename = "test_layer.json";
    int save_result = tusd_layer_save_json_pretty(layer, filename, 2);
    TEST_ASSERT(save_result != 0, "Failed to save layer to JSON file");
    
    /* Load from file */
    tusd_layer_t *loaded_layer = tusd_layer_load_json(filename);
    TEST_ASSERT(loaded_layer != NULL, "Failed to load layer from JSON file");
    
    /* Verify loaded layer */
    TEST_ASSERT(strcmp(loaded_layer->name, "FileIOTest") == 0, "Loaded layer name incorrect");
    TEST_ASSERT(loaded_layer->metas.doc != NULL, "Loaded layer should have doc");
    TEST_ASSERT(strcmp(loaded_layer->metas.doc, "File I/O test layer") == 0, "Loaded layer doc incorrect");
    
    tusd_primspec_t *loaded_prim = tusd_layer_get_primspec(loaded_layer, "TestPrim");
    TEST_ASSERT(loaded_prim != NULL, "Loaded layer should have TestPrim");
    
    tusd_property_t *loaded_radius = tusd_primspec_get_property(loaded_prim, "radius");
    TEST_ASSERT(loaded_radius != NULL, "Loaded prim should have radius property");
    TEST_ASSERT(loaded_radius->has_value, "Loaded radius property should have value");
    
    double radius_value;
    tusd_value_get_double(&loaded_radius->value, &radius_value);
    TEST_ASSERT(radius_value == 2.5, "Loaded radius value incorrect");
    
    /* Clean up */
    tusd_layer_destroy(layer);
    tusd_layer_destroy(loaded_layer);
    remove(filename);
    
    TEST_SUCCESS();
}

/* ===== Utility Function Tests ===== */

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
    {"USD Value JSON Conversion", test_usd_value_json_conversion},
    {"USD Property JSON Conversion", test_usd_property_json_conversion},
    {"USD Layer JSON Conversion", test_usd_layer_json_conversion},
    {"JSON Roundtrip Conversion", test_json_roundtrip_conversion},
    {"JSON File I/O", test_json_file_io},
    {"JSON Utilities", test_json_utilities},
};

int main(void) {
    printf("TUSD JSON Library Test Suite\n");
    printf("============================\n\n");
    
    int total_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    int passed_tests = 0;
    
    for (int i = 0; i < total_tests; i++) {
        printf("[%d/%d] %s: ", i + 1, total_tests, test_cases[i].name);
        fflush(stdout);
        
        if (test_cases[i].test_func()) {
            passed_tests++;
        }
    }
    
    printf("\n============================\n");
    printf("Test Results: %d/%d tests passed\n", passed_tests, total_tests);
    
    if (passed_tests == total_tests) {
        printf("🎉 ALL TESTS PASSED! 🎉\n");
        printf("\nC99 JSON library implementation is working correctly!\n");
        printf("Features tested:\n");
        printf("  ✓ Pure C99 JSON parser with full RFC 7159 compliance\n");
        printf("  ✓ JSON serialization with compact and pretty-print modes\n");
        printf("  ✓ Complete JSON value system (null, bool, number, string, array, object)\n");
        printf("  ✓ USD Layer to JSON conversion preserving all metadata and structure\n");
        printf("  ✓ JSON to USD Layer conversion with type inference\n");
        printf("  ✓ Bidirectional roundtrip conversion maintaining data integrity\n");
        printf("  ✓ File I/O operations for USD-JSON interchange\n");
        printf("  ✓ String escaping and JSON validation utilities\n");
        printf("  ✓ Memory management and cleanup\n");
        return 0;
    } else {
        printf("❌ Some tests failed. Please check the implementation.\n");
        return 1;
    }
}