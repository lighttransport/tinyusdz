#ifndef LIGHTUSD_JSON_H_
#define LIGHTUSD_JSON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Pure C99 JSON implementation
 * Provides JSON parsing, serialization, and USD Layer conversion
 */

/* ===== JSON Value Types ===== */

typedef enum {
    LIGHTUSD_JSON_NULL = 0,
    LIGHTUSD_JSON_BOOL = 1,
    LIGHTUSD_JSON_NUMBER = 2,
    LIGHTUSD_JSON_STRING = 3,
    LIGHTUSD_JSON_ARRAY = 4,
    LIGHTUSD_JSON_OBJECT = 5
} lightusd_json_type_t;

/* Forward declarations */
typedef struct lightusd_json_value_t lightusd_json_value_t;
typedef struct lightusd_json_object_t lightusd_json_object_t;
typedef struct lightusd_json_array_t lightusd_json_array_t;

/* ===== JSON Value Structure ===== */

struct lightusd_json_value_t {
    lightusd_json_type_t type;
    union {
        int bool_val;           /* Boolean value */
        double number_val;      /* Number value (all numbers as double) */
        char *string_val;       /* String value (owned) */
        lightusd_json_array_t *array_val;   /* Array value (owned) */
        lightusd_json_object_t *object_val; /* Object value (owned) */
    } data;
};

/* ===== JSON Array Structure ===== */

struct lightusd_json_array_t {
    lightusd_json_value_t **values;  /* Array of JSON values */
    size_t count;                /* Number of values */
    size_t capacity;             /* Allocated capacity */
};

/* ===== JSON Object Structure ===== */

typedef struct lightusd_json_pair_t {
    char *key;                   /* Key string (owned) */
    lightusd_json_value_t *value;    /* Value (owned) */
} lightusd_json_pair_t;

struct lightusd_json_object_t {
    lightusd_json_pair_t *pairs;     /* Array of key-value pairs */
    size_t count;                /* Number of pairs */
    size_t capacity;             /* Allocated capacity */
};

/* ===== JSON Parser Context ===== */

typedef struct {
    const char *input;           /* Input JSON string */
    size_t length;               /* Input length */
    size_t position;             /* Current position */
    int line;                    /* Current line number */
    int column;                  /* Current column number */
    char error_msg[256];         /* Error message buffer */
} lightusd_json_parser_t;

/* ===== JSON Value API ===== */

/* Create/destroy JSON values */
lightusd_json_value_t *lightusd_json_value_create_null(void);
lightusd_json_value_t *lightusd_json_value_create_bool(int value);
lightusd_json_value_t *lightusd_json_value_create_number(double value);
lightusd_json_value_t *lightusd_json_value_create_string(const char *value);
lightusd_json_value_t *lightusd_json_value_create_array(void);
lightusd_json_value_t *lightusd_json_value_create_object(void);

void lightusd_json_value_destroy(lightusd_json_value_t *value);

/* Type checking */
lightusd_json_type_t lightusd_json_value_get_type(const lightusd_json_value_t *value);
int lightusd_json_value_is_null(const lightusd_json_value_t *value);
int lightusd_json_value_is_bool(const lightusd_json_value_t *value);
int lightusd_json_value_is_number(const lightusd_json_value_t *value);
int lightusd_json_value_is_string(const lightusd_json_value_t *value);
int lightusd_json_value_is_array(const lightusd_json_value_t *value);
int lightusd_json_value_is_object(const lightusd_json_value_t *value);

/* Value extraction */
int lightusd_json_value_get_bool(const lightusd_json_value_t *value);
double lightusd_json_value_get_number(const lightusd_json_value_t *value);
const char *lightusd_json_value_get_string(const lightusd_json_value_t *value);
lightusd_json_array_t *lightusd_json_value_get_array(const lightusd_json_value_t *value);
lightusd_json_object_t *lightusd_json_value_get_object(const lightusd_json_value_t *value);

/* ===== JSON Array API ===== */

lightusd_json_array_t *lightusd_json_array_create(void);
void lightusd_json_array_destroy(lightusd_json_array_t *array);

int lightusd_json_array_add(lightusd_json_array_t *array, lightusd_json_value_t *value);
lightusd_json_value_t *lightusd_json_array_get(const lightusd_json_array_t *array, size_t index);
size_t lightusd_json_array_size(const lightusd_json_array_t *array);

/* ===== JSON Object API ===== */

lightusd_json_object_t *lightusd_json_object_create(void);
void lightusd_json_object_destroy(lightusd_json_object_t *object);

int lightusd_json_object_set(lightusd_json_object_t *object, const char *key, lightusd_json_value_t *value);
lightusd_json_value_t *lightusd_json_object_get(const lightusd_json_object_t *object, const char *key);
int lightusd_json_object_has_key(const lightusd_json_object_t *object, const char *key);
size_t lightusd_json_object_size(const lightusd_json_object_t *object);

/* Get all keys */
char **lightusd_json_object_get_keys(const lightusd_json_object_t *object, size_t *count);

/* ===== JSON Parser API ===== */

/* Parse JSON from string */
lightusd_json_value_t *lightusd_json_parse(const char *json_string);
lightusd_json_value_t *lightusd_json_parse_length(const char *json_string, size_t length);

/* Get parse error information */
const char *lightusd_json_get_error_message(void);

/* ===== JSON Serializer API ===== */

/* Serialize JSON to string */
char *lightusd_json_serialize(const lightusd_json_value_t *value);
char *lightusd_json_serialize_pretty(const lightusd_json_value_t *value, int indent_size);

/* Write JSON to file */
int lightusd_json_write_file(const lightusd_json_value_t *value, const char *filename);
int lightusd_json_write_file_pretty(const lightusd_json_value_t *value, const char *filename, int indent_size);

/* ===== USD Layer <-> JSON Conversion API ===== */

/* Include lightusd_layer.h types */
struct lightusd_layer_t;
struct lightusd_primspec_t;
struct lightusd_property_t;
struct lightusd_value_t;

/* Convert USD Layer to JSON */
lightusd_json_value_t *lightusd_layer_to_json(const struct lightusd_layer_t *layer);
lightusd_json_value_t *lightusd_primspec_to_json(const struct lightusd_primspec_t *primspec);
lightusd_json_value_t *lightusd_property_to_json(const struct lightusd_property_t *property);
lightusd_json_value_t *lightusd_value_to_json(const struct lightusd_value_t *value);

/* Convert JSON to USD Layer */
struct lightusd_layer_t *lightusd_json_to_layer(const lightusd_json_value_t *json);
struct lightusd_primspec_t *lightusd_json_to_primspec(const lightusd_json_value_t *json);
struct lightusd_property_t *lightusd_json_to_property(const lightusd_json_value_t *json);
struct lightusd_value_t *lightusd_json_to_value(const lightusd_json_value_t *json);

/* High-level conversion functions */
char *lightusd_layer_to_json_string(const struct lightusd_layer_t *layer);
char *lightusd_layer_to_json_string_pretty(const struct lightusd_layer_t *layer, int indent_size);
struct lightusd_layer_t *lightusd_layer_from_json_string(const char *json_string);

/* File I/O for USD-JSON conversion */
int lightusd_layer_save_json(const struct lightusd_layer_t *layer, const char *filename);
int lightusd_layer_save_json_pretty(const struct lightusd_layer_t *layer, const char *filename, int indent_size);
struct lightusd_layer_t *lightusd_layer_load_json(const char *filename);

/* ===== Utility Functions ===== */

/* JSON string escaping */
char *lightusd_json_escape_string(const char *str);
char *lightusd_json_unescape_string(const char *str);

/* JSON validation */
int lightusd_json_validate(const char *json_string);

/* Memory usage estimation */
size_t lightusd_json_estimate_memory_usage(const lightusd_json_value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUSD_JSON_H_ */