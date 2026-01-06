#ifndef TUSD_JSON_H_
#define TUSD_JSON_H_

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
    TUSD_JSON_NULL = 0,
    TUSD_JSON_BOOL = 1,
    TUSD_JSON_NUMBER = 2,
    TUSD_JSON_STRING = 3,
    TUSD_JSON_ARRAY = 4,
    TUSD_JSON_OBJECT = 5
} tusd_json_type_t;

/* Forward declarations */
typedef struct tusd_json_value_t tusd_json_value_t;
typedef struct tusd_json_object_t tusd_json_object_t;
typedef struct tusd_json_array_t tusd_json_array_t;

/* ===== JSON Value Structure ===== */

struct tusd_json_value_t {
    tusd_json_type_t type;
    union {
        int bool_val;           /* Boolean value */
        double number_val;      /* Number value (all numbers as double) */
        char *string_val;       /* String value (owned) */
        tusd_json_array_t *array_val;   /* Array value (owned) */
        tusd_json_object_t *object_val; /* Object value (owned) */
    } data;
};

/* ===== JSON Array Structure ===== */

struct tusd_json_array_t {
    tusd_json_value_t **values;  /* Array of JSON values */
    size_t count;                /* Number of values */
    size_t capacity;             /* Allocated capacity */
};

/* ===== JSON Object Structure ===== */

typedef struct tusd_json_pair_t {
    char *key;                   /* Key string (owned) */
    tusd_json_value_t *value;    /* Value (owned) */
} tusd_json_pair_t;

struct tusd_json_object_t {
    tusd_json_pair_t *pairs;     /* Array of key-value pairs */
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
} tusd_json_parser_t;

/* ===== JSON Value API ===== */

/* Create/destroy JSON values */
tusd_json_value_t *tusd_json_value_create_null(void);
tusd_json_value_t *tusd_json_value_create_bool(int value);
tusd_json_value_t *tusd_json_value_create_number(double value);
tusd_json_value_t *tusd_json_value_create_string(const char *value);
tusd_json_value_t *tusd_json_value_create_array(void);
tusd_json_value_t *tusd_json_value_create_object(void);

void tusd_json_value_destroy(tusd_json_value_t *value);

/* Type checking */
tusd_json_type_t tusd_json_value_get_type(const tusd_json_value_t *value);
int tusd_json_value_is_null(const tusd_json_value_t *value);
int tusd_json_value_is_bool(const tusd_json_value_t *value);
int tusd_json_value_is_number(const tusd_json_value_t *value);
int tusd_json_value_is_string(const tusd_json_value_t *value);
int tusd_json_value_is_array(const tusd_json_value_t *value);
int tusd_json_value_is_object(const tusd_json_value_t *value);

/* Value extraction */
int tusd_json_value_get_bool(const tusd_json_value_t *value);
double tusd_json_value_get_number(const tusd_json_value_t *value);
const char *tusd_json_value_get_string(const tusd_json_value_t *value);
tusd_json_array_t *tusd_json_value_get_array(const tusd_json_value_t *value);
tusd_json_object_t *tusd_json_value_get_object(const tusd_json_value_t *value);

/* ===== JSON Array API ===== */

tusd_json_array_t *tusd_json_array_create(void);
void tusd_json_array_destroy(tusd_json_array_t *array);

int tusd_json_array_add(tusd_json_array_t *array, tusd_json_value_t *value);
tusd_json_value_t *tusd_json_array_get(const tusd_json_array_t *array, size_t index);
size_t tusd_json_array_size(const tusd_json_array_t *array);

/* ===== JSON Object API ===== */

tusd_json_object_t *tusd_json_object_create(void);
void tusd_json_object_destroy(tusd_json_object_t *object);

int tusd_json_object_set(tusd_json_object_t *object, const char *key, tusd_json_value_t *value);
tusd_json_value_t *tusd_json_object_get(const tusd_json_object_t *object, const char *key);
int tusd_json_object_has_key(const tusd_json_object_t *object, const char *key);
size_t tusd_json_object_size(const tusd_json_object_t *object);

/* Get all keys */
char **tusd_json_object_get_keys(const tusd_json_object_t *object, size_t *count);

/* ===== JSON Parser API ===== */

/* Parse JSON from string */
tusd_json_value_t *tusd_json_parse(const char *json_string);
tusd_json_value_t *tusd_json_parse_length(const char *json_string, size_t length);

/* Get parse error information */
const char *tusd_json_get_error_message(void);

/* ===== JSON Serializer API ===== */

/* Serialize JSON to string */
char *tusd_json_serialize(const tusd_json_value_t *value);
char *tusd_json_serialize_pretty(const tusd_json_value_t *value, int indent_size);

/* Write JSON to file */
int tusd_json_write_file(const tusd_json_value_t *value, const char *filename);
int tusd_json_write_file_pretty(const tusd_json_value_t *value, const char *filename, int indent_size);

/* ===== USD Layer <-> JSON Conversion API ===== */

/* Include tusd_layer.h types */
struct tusd_layer_t;
struct tusd_primspec_t;
struct tusd_property_t;
struct tusd_value_t;

/* Convert USD Layer to JSON */
tusd_json_value_t *tusd_layer_to_json(const struct tusd_layer_t *layer);
tusd_json_value_t *tusd_primspec_to_json(const struct tusd_primspec_t *primspec);
tusd_json_value_t *tusd_property_to_json(const struct tusd_property_t *property);
tusd_json_value_t *tusd_value_to_json(const struct tusd_value_t *value);

/* Convert JSON to USD Layer */
struct tusd_layer_t *tusd_json_to_layer(const tusd_json_value_t *json);
struct tusd_primspec_t *tusd_json_to_primspec(const tusd_json_value_t *json);
struct tusd_property_t *tusd_json_to_property(const tusd_json_value_t *json);
struct tusd_value_t *tusd_json_to_value(const tusd_json_value_t *json);

/* High-level conversion functions */
char *tusd_layer_to_json_string(const struct tusd_layer_t *layer);
char *tusd_layer_to_json_string_pretty(const struct tusd_layer_t *layer, int indent_size);
struct tusd_layer_t *tusd_layer_from_json_string(const char *json_string);

/* File I/O for USD-JSON conversion */
int tusd_layer_save_json(const struct tusd_layer_t *layer, const char *filename);
int tusd_layer_save_json_pretty(const struct tusd_layer_t *layer, const char *filename, int indent_size);
struct tusd_layer_t *tusd_layer_load_json(const char *filename);

/* ===== Utility Functions ===== */

/* JSON string escaping */
char *tusd_json_escape_string(const char *str);
char *tusd_json_unescape_string(const char *str);

/* JSON validation */
int tusd_json_validate(const char *json_string);

/* Memory usage estimation */
size_t tusd_json_estimate_memory_usage(const tusd_json_value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* TUSD_JSON_H_ */