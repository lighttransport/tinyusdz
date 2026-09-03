#include "lightusd_json.h"
#include "lightusd_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>

/* ===== Utility Functions ===== */

static char *lightusd_json_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

static int lightusd_json_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int lightusd_json_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int lightusd_json_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int lightusd_json_hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* Global error message for parser */
static char g_error_message[256] = {0};

const char *lightusd_json_get_error_message(void) {
    return g_error_message;
}

static void lightusd_json_set_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(g_error_message, sizeof(g_error_message), format, args);
    va_end(args);
}

/* ===== JSON Value Implementation ===== */

lightusd_json_value_t *lightusd_json_value_create_null(void) {
    lightusd_json_value_t *value = calloc(1, sizeof(lightusd_json_value_t));
    if (value) {
        value->type = LIGHTUSD_JSON_NULL;
    }
    return value;
}

lightusd_json_value_t *lightusd_json_value_create_bool(int val) {
    lightusd_json_value_t *value = calloc(1, sizeof(lightusd_json_value_t));
    if (value) {
        value->type = LIGHTUSD_JSON_BOOL;
        value->data.bool_val = val ? 1 : 0;
    }
    return value;
}

lightusd_json_value_t *lightusd_json_value_create_number(double val) {
    lightusd_json_value_t *value = calloc(1, sizeof(lightusd_json_value_t));
    if (value) {
        value->type = LIGHTUSD_JSON_NUMBER;
        value->data.number_val = val;
    }
    return value;
}

lightusd_json_value_t *lightusd_json_value_create_string(const char *val) {
    lightusd_json_value_t *value = calloc(1, sizeof(lightusd_json_value_t));
    if (value) {
        value->type = LIGHTUSD_JSON_STRING;
        value->data.string_val = lightusd_json_strdup(val);
        if (!value->data.string_val && val) {
            free(value);
            return NULL;
        }
    }
    return value;
}

lightusd_json_value_t *lightusd_json_value_create_array(void) {
    lightusd_json_value_t *value = calloc(1, sizeof(lightusd_json_value_t));
    if (value) {
        value->type = LIGHTUSD_JSON_ARRAY;
        value->data.array_val = lightusd_json_array_create();
        if (!value->data.array_val) {
            free(value);
            return NULL;
        }
    }
    return value;
}

lightusd_json_value_t *lightusd_json_value_create_object(void) {
    lightusd_json_value_t *value = calloc(1, sizeof(lightusd_json_value_t));
    if (value) {
        value->type = LIGHTUSD_JSON_OBJECT;
        value->data.object_val = lightusd_json_object_create();
        if (!value->data.object_val) {
            free(value);
            return NULL;
        }
    }
    return value;
}

void lightusd_json_value_destroy(lightusd_json_value_t *value) {
    if (!value) return;

    switch (value->type) {
        case LIGHTUSD_JSON_STRING:
            free(value->data.string_val);
            break;
        case LIGHTUSD_JSON_ARRAY:
            lightusd_json_array_destroy(value->data.array_val);
            break;
        case LIGHTUSD_JSON_OBJECT:
            lightusd_json_object_destroy(value->data.object_val);
            break;
        default:
            break;
    }

    free(value);
}

/* Type checking functions */
lightusd_json_type_t lightusd_json_value_get_type(const lightusd_json_value_t *value) {
    return value ? value->type : LIGHTUSD_JSON_NULL;
}

int lightusd_json_value_is_null(const lightusd_json_value_t *value) {
    return value && value->type == LIGHTUSD_JSON_NULL;
}

int lightusd_json_value_is_bool(const lightusd_json_value_t *value) {
    return value && value->type == LIGHTUSD_JSON_BOOL;
}

int lightusd_json_value_is_number(const lightusd_json_value_t *value) {
    return value && value->type == LIGHTUSD_JSON_NUMBER;
}

int lightusd_json_value_is_string(const lightusd_json_value_t *value) {
    return value && value->type == LIGHTUSD_JSON_STRING;
}

int lightusd_json_value_is_array(const lightusd_json_value_t *value) {
    return value && value->type == LIGHTUSD_JSON_ARRAY;
}

int lightusd_json_value_is_object(const lightusd_json_value_t *value) {
    return value && value->type == LIGHTUSD_JSON_OBJECT;
}

/* Value extraction functions */
int lightusd_json_value_get_bool(const lightusd_json_value_t *value) {
    return (value && value->type == LIGHTUSD_JSON_BOOL) ? value->data.bool_val : 0;
}

double lightusd_json_value_get_number(const lightusd_json_value_t *value) {
    return (value && value->type == LIGHTUSD_JSON_NUMBER) ? value->data.number_val : 0.0;
}

const char *lightusd_json_value_get_string(const lightusd_json_value_t *value) {
    return (value && value->type == LIGHTUSD_JSON_STRING) ? value->data.string_val : NULL;
}

lightusd_json_array_t *lightusd_json_value_get_array(const lightusd_json_value_t *value) {
    return (value && value->type == LIGHTUSD_JSON_ARRAY) ? value->data.array_val : NULL;
}

lightusd_json_object_t *lightusd_json_value_get_object(const lightusd_json_value_t *value) {
    return (value && value->type == LIGHTUSD_JSON_OBJECT) ? value->data.object_val : NULL;
}

/* ===== JSON Array Implementation ===== */

lightusd_json_array_t *lightusd_json_array_create(void) {
    lightusd_json_array_t *array = calloc(1, sizeof(lightusd_json_array_t));
    if (array) {
        array->capacity = 8;
        array->values = malloc(array->capacity * sizeof(lightusd_json_value_t*));
        if (!array->values) {
            free(array);
            return NULL;
        }
    }
    return array;
}

void lightusd_json_array_destroy(lightusd_json_array_t *array) {
    if (!array) return;

    for (size_t i = 0; i < array->count; i++) {
        lightusd_json_value_destroy(array->values[i]);
    }

    free(array->values);
    free(array);
}

int lightusd_json_array_add(lightusd_json_array_t *array, lightusd_json_value_t *value) {
    if (!array || !value) return 0;

    if (array->count >= array->capacity) {
        size_t new_capacity = array->capacity * 2;
        lightusd_json_value_t **new_values = realloc(array->values,
                                                 new_capacity * sizeof(lightusd_json_value_t*));
        if (!new_values) return 0;

        array->values = new_values;
        array->capacity = new_capacity;
    }

    array->values[array->count++] = value;
    return 1;
}

lightusd_json_value_t *lightusd_json_array_get(const lightusd_json_array_t *array, size_t index) {
    if (!array || index >= array->count) return NULL;
    return array->values[index];
}

size_t lightusd_json_array_size(const lightusd_json_array_t *array) {
    return array ? array->count : 0;
}

/* ===== JSON Object Implementation ===== */

lightusd_json_object_t *lightusd_json_object_create(void) {
    lightusd_json_object_t *object = calloc(1, sizeof(lightusd_json_object_t));
    if (object) {
        object->capacity = 8;
        object->pairs = malloc(object->capacity * sizeof(lightusd_json_pair_t));
        if (!object->pairs) {
            free(object);
            return NULL;
        }
    }
    return object;
}

void lightusd_json_object_destroy(lightusd_json_object_t *object) {
    if (!object) return;

    for (size_t i = 0; i < object->count; i++) {
        free(object->pairs[i].key);
        lightusd_json_value_destroy(object->pairs[i].value);
    }

    free(object->pairs);
    free(object);
}

int lightusd_json_object_set(lightusd_json_object_t *object, const char *key, lightusd_json_value_t *value) {
    if (!object || !key || !value) return 0;

    /* Check if key already exists */
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->pairs[i].key, key) == 0) {
            /* Replace existing value */
            lightusd_json_value_destroy(object->pairs[i].value);
            object->pairs[i].value = value;
            return 1;
        }
    }

    /* Add new key-value pair */
    if (object->count >= object->capacity) {
        size_t new_capacity = object->capacity * 2;
        lightusd_json_pair_t *new_pairs = realloc(object->pairs,
                                              new_capacity * sizeof(lightusd_json_pair_t));
        if (!new_pairs) return 0;

        object->pairs = new_pairs;
        object->capacity = new_capacity;
    }

    object->pairs[object->count].key = lightusd_json_strdup(key);
    object->pairs[object->count].value = value;

    if (!object->pairs[object->count].key) {
        return 0;
    }

    object->count++;
    return 1;
}

lightusd_json_value_t *lightusd_json_object_get(const lightusd_json_object_t *object, const char *key) {
    if (!object || !key) return NULL;

    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->pairs[i].key, key) == 0) {
            return object->pairs[i].value;
        }
    }

    return NULL;
}

int lightusd_json_object_has_key(const lightusd_json_object_t *object, const char *key) {
    return lightusd_json_object_get(object, key) != NULL;
}

size_t lightusd_json_object_size(const lightusd_json_object_t *object) {
    return object ? object->count : 0;
}

char **lightusd_json_object_get_keys(const lightusd_json_object_t *object, size_t *count) {
    if (!object || !count) return NULL;

    *count = object->count;
    if (object->count == 0) return NULL;

    char **keys = malloc(object->count * sizeof(char*));
    if (!keys) return NULL;

    for (size_t i = 0; i < object->count; i++) {
        keys[i] = lightusd_json_strdup(object->pairs[i].key);
        if (!keys[i]) {
            /* Cleanup on failure */
            for (size_t j = 0; j < i; j++) {
                free(keys[j]);
            }
            free(keys);
            return NULL;
        }
    }

    return keys;
}

/* ===== JSON Parser Implementation ===== */

static void lightusd_json_parser_skip_whitespace(lightusd_json_parser_t *parser) {
    while (parser->position < parser->length &&
           lightusd_json_is_whitespace(parser->input[parser->position])) {
        if (parser->input[parser->position] == '\n') {
            parser->line++;
            parser->column = 1;
        } else {
            parser->column++;
        }
        parser->position++;
    }
}

static char lightusd_json_parser_peek(lightusd_json_parser_t *parser) {
    if (parser->position >= parser->length) return '\0';
    return parser->input[parser->position];
}

static char lightusd_json_parser_advance(lightusd_json_parser_t *parser) {
    if (parser->position >= parser->length) return '\0';

    char c = parser->input[parser->position++];
    parser->column++;
    return c;
}

static int lightusd_json_parser_expect(lightusd_json_parser_t *parser, char expected) {
    lightusd_json_parser_skip_whitespace(parser);

    if (lightusd_json_parser_peek(parser) != expected) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                "Expected '%c' at line %d, column %d", expected, parser->line, parser->column);
        return 0;
    }

    lightusd_json_parser_advance(parser);
    return 1;
}

static char *lightusd_json_parser_parse_string(lightusd_json_parser_t *parser) {
    if (!lightusd_json_parser_expect(parser, '"')) {
        return NULL;
    }

    size_t start = parser->position;
    size_t capacity = 32;
    char *result = malloc(capacity);
    size_t length = 0;

    if (!result) return NULL;

    while (parser->position < parser->length) {
        char c = lightusd_json_parser_advance(parser);

        if (c == '"') {
            result[length] = '\0';
            return result;
        }

        if (c == '\\') {
            if (parser->position >= parser->length) {
                free(result);
                snprintf(parser->error_msg, sizeof(parser->error_msg),
                        "Unterminated string escape at line %d", parser->line);
                return NULL;
            }

            char escape = lightusd_json_parser_advance(parser);
            switch (escape) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    /* Unicode escape */
                    if (parser->position + 4 > parser->length) {
                        free(result);
                        snprintf(parser->error_msg, sizeof(parser->error_msg),
                                "Invalid unicode escape at line %d", parser->line);
                        return NULL;
                    }

                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; i++) {
                        char hex = lightusd_json_parser_advance(parser);
                        if (!lightusd_json_is_hex_digit(hex)) {
                            free(result);
                            snprintf(parser->error_msg, sizeof(parser->error_msg),
                                    "Invalid unicode escape at line %d", parser->line);
                            return NULL;
                        }
                        codepoint = codepoint * 16 + lightusd_json_hex_to_int(hex);
                    }

                    /* Simple handling: only support ASCII range for now */
                    if (codepoint < 128) {
                        c = (char)codepoint;
                    } else {
                        c = '?'; /* Placeholder for non-ASCII */
                    }
                    break;
                }
                default:
                    free(result);
                    snprintf(parser->error_msg, sizeof(parser->error_msg),
                            "Invalid escape sequence '\\%c' at line %d", escape, parser->line);
                    return NULL;
            }
        }

        /* Ensure buffer capacity */
        if (length >= capacity - 1) {
            capacity *= 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                return NULL;
            }
            result = new_result;
        }

        result[length++] = c;
    }

    free(result);
    snprintf(parser->error_msg, sizeof(parser->error_msg),
            "Unterminated string at line %d", parser->line);
    return NULL;
}

static lightusd_json_value_t *lightusd_json_parser_parse_number(lightusd_json_parser_t *parser) {
    size_t start = parser->position;

    /* Handle negative sign */
    if (lightusd_json_parser_peek(parser) == '-') {
        lightusd_json_parser_advance(parser);
    }

    /* Parse integer part */
    if (!lightusd_json_is_digit(lightusd_json_parser_peek(parser))) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                "Invalid number at line %d, column %d", parser->line, parser->column);
        return NULL;
    }

    if (lightusd_json_parser_peek(parser) == '0') {
        lightusd_json_parser_advance(parser);
    } else {
        while (lightusd_json_is_digit(lightusd_json_parser_peek(parser))) {
            lightusd_json_parser_advance(parser);
        }
    }

    /* Parse fractional part */
    if (lightusd_json_parser_peek(parser) == '.') {
        lightusd_json_parser_advance(parser);
        if (!lightusd_json_is_digit(lightusd_json_parser_peek(parser))) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Invalid number: missing digits after decimal point at line %d", parser->line);
            return NULL;
        }
        while (lightusd_json_is_digit(lightusd_json_parser_peek(parser))) {
            lightusd_json_parser_advance(parser);
        }
    }

    /* Parse exponent part */
    if (lightusd_json_parser_peek(parser) == 'e' || lightusd_json_parser_peek(parser) == 'E') {
        lightusd_json_parser_advance(parser);
        if (lightusd_json_parser_peek(parser) == '+' || lightusd_json_parser_peek(parser) == '-') {
            lightusd_json_parser_advance(parser);
        }
        if (!lightusd_json_is_digit(lightusd_json_parser_peek(parser))) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Invalid number: missing digits in exponent at line %d", parser->line);
            return NULL;
        }
        while (lightusd_json_is_digit(lightusd_json_parser_peek(parser))) {
            lightusd_json_parser_advance(parser);
        }
    }

    /* Extract number string and convert */
    size_t length = parser->position - start;
    char *number_str = malloc(length + 1);
    if (!number_str) return NULL;

    memcpy(number_str, parser->input + start, length);
    number_str[length] = '\0';

    char *endptr;
    double value = strtod(number_str, &endptr);

    if (endptr != number_str + length) {
        free(number_str);
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                "Invalid number format at line %d", parser->line);
        return NULL;
    }

    free(number_str);
    return lightusd_json_value_create_number(value);
}

/* Forward declaration for recursive parsing */
static lightusd_json_value_t *lightusd_json_parser_parse_value(lightusd_json_parser_t *parser);

static lightusd_json_value_t *lightusd_json_parser_parse_array(lightusd_json_parser_t *parser) {
    if (!lightusd_json_parser_expect(parser, '[')) {
        return NULL;
    }

    lightusd_json_value_t *array_value = lightusd_json_value_create_array();
    if (!array_value) return NULL;

    lightusd_json_array_t *array = array_value->data.array_val;

    lightusd_json_parser_skip_whitespace(parser);

    /* Handle empty array */
    if (lightusd_json_parser_peek(parser) == ']') {
        lightusd_json_parser_advance(parser);
        return array_value;
    }

    while (1) {
        lightusd_json_value_t *element = lightusd_json_parser_parse_value(parser);
        if (!element) {
            lightusd_json_value_destroy(array_value);
            return NULL;
        }

        if (!lightusd_json_array_add(array, element)) {
            lightusd_json_value_destroy(element);
            lightusd_json_value_destroy(array_value);
            return NULL;
        }

        lightusd_json_parser_skip_whitespace(parser);

        char c = lightusd_json_parser_peek(parser);
        if (c == ']') {
            lightusd_json_parser_advance(parser);
            break;
        } else if (c == ',') {
            lightusd_json_parser_advance(parser);
            lightusd_json_parser_skip_whitespace(parser);
        } else {
            lightusd_json_value_destroy(array_value);
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Expected ',' or ']' in array at line %d, column %d", parser->line, parser->column);
            return NULL;
        }
    }

    return array_value;
}

static lightusd_json_value_t *lightusd_json_parser_parse_object(lightusd_json_parser_t *parser) {
    if (!lightusd_json_parser_expect(parser, '{')) {
        return NULL;
    }

    lightusd_json_value_t *object_value = lightusd_json_value_create_object();
    if (!object_value) return NULL;

    lightusd_json_object_t *object = object_value->data.object_val;

    lightusd_json_parser_skip_whitespace(parser);

    /* Handle empty object */
    if (lightusd_json_parser_peek(parser) == '}') {
        lightusd_json_parser_advance(parser);
        return object_value;
    }

    while (1) {
        /* Parse key */
        char *key = lightusd_json_parser_parse_string(parser);
        if (!key) {
            lightusd_json_value_destroy(object_value);
            return NULL;
        }

        /* Expect colon */
        lightusd_json_parser_skip_whitespace(parser);
        if (!lightusd_json_parser_expect(parser, ':')) {
            free(key);
            lightusd_json_value_destroy(object_value);
            return NULL;
        }

        /* Parse value */
        lightusd_json_parser_skip_whitespace(parser);
        lightusd_json_value_t *value = lightusd_json_parser_parse_value(parser);
        if (!value) {
            free(key);
            lightusd_json_value_destroy(object_value);
            return NULL;
        }

        /* Add to object */
        if (!lightusd_json_object_set(object, key, value)) {
            free(key);
            lightusd_json_value_destroy(value);
            lightusd_json_value_destroy(object_value);
            return NULL;
        }

        free(key);

        lightusd_json_parser_skip_whitespace(parser);

        char c = lightusd_json_parser_peek(parser);
        if (c == '}') {
            lightusd_json_parser_advance(parser);
            break;
        } else if (c == ',') {
            lightusd_json_parser_advance(parser);
            lightusd_json_parser_skip_whitespace(parser);
        } else {
            lightusd_json_value_destroy(object_value);
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Expected ',' or '}' in object at line %d, column %d", parser->line, parser->column);
            return NULL;
        }
    }

    return object_value;
}

static lightusd_json_value_t *lightusd_json_parser_parse_value(lightusd_json_parser_t *parser) {
    lightusd_json_parser_skip_whitespace(parser);

    char c = lightusd_json_parser_peek(parser);

    switch (c) {
        case '"':
            {
                char *str = lightusd_json_parser_parse_string(parser);
                if (!str) return NULL;
                lightusd_json_value_t *value = lightusd_json_value_create_string(str);
                free(str);
                return value;
            }
        case '[':
            return lightusd_json_parser_parse_array(parser);
        case '{':
            return lightusd_json_parser_parse_object(parser);
        case 't':
            if (parser->position + 4 <= parser->length &&
                memcmp(parser->input + parser->position, "true", 4) == 0) {
                parser->position += 4;
                parser->column += 4;
                return lightusd_json_value_create_bool(1);
            }
            break;
        case 'f':
            if (parser->position + 5 <= parser->length &&
                memcmp(parser->input + parser->position, "false", 5) == 0) {
                parser->position += 5;
                parser->column += 5;
                return lightusd_json_value_create_bool(0);
            }
            break;
        case 'n':
            if (parser->position + 4 <= parser->length &&
                memcmp(parser->input + parser->position, "null", 4) == 0) {
                parser->position += 4;
                parser->column += 4;
                return lightusd_json_value_create_null();
            }
            break;
        default:
            if (c == '-' || lightusd_json_is_digit(c)) {
                return lightusd_json_parser_parse_number(parser);
            }
            break;
    }

    snprintf(parser->error_msg, sizeof(parser->error_msg),
            "Unexpected character '%c' at line %d, column %d", c, parser->line, parser->column);
    return NULL;
}

lightusd_json_value_t *lightusd_json_parse_length(const char *json_string, size_t length) {
    if (!json_string) return NULL;

    lightusd_json_parser_t parser = {0};
    parser.input = json_string;
    parser.length = length;
    parser.line = 1;
    parser.column = 1;

    lightusd_json_value_t *result = lightusd_json_parser_parse_value(&parser);

    if (result) {
        lightusd_json_parser_skip_whitespace(&parser);
        if (parser.position < parser.length) {
            snprintf(parser.error_msg, sizeof(parser.error_msg),
                    "Unexpected content after JSON value at line %d, column %d",
                    parser.line, parser.column);
            lightusd_json_value_destroy(result);
            return NULL;
        }
    }

    if (parser.error_msg[0]) {
        strcpy(g_error_message, parser.error_msg);
    }

    return result;
}

lightusd_json_value_t *lightusd_json_parse(const char *json_string) {
    if (!json_string) return NULL;
    return lightusd_json_parse_length(json_string, strlen(json_string));
}

/* ===== JSON Serializer Implementation ===== */

/* String builder for JSON serialization */
typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} lightusd_json_stringbuilder_t;

static lightusd_json_stringbuilder_t *lightusd_json_stringbuilder_create(void) {
    lightusd_json_stringbuilder_t *sb = malloc(sizeof(lightusd_json_stringbuilder_t));
    if (!sb) return NULL;

    sb->capacity = 256;
    sb->buffer = malloc(sb->capacity);
    sb->length = 0;

    if (!sb->buffer) {
        free(sb);
        return NULL;
    }

    return sb;
}

static void lightusd_json_stringbuilder_destroy(lightusd_json_stringbuilder_t *sb) {
    if (!sb) return;
    free(sb->buffer);
    free(sb);
}

static int lightusd_json_stringbuilder_append(lightusd_json_stringbuilder_t *sb, const char *str) {
    if (!sb || !str) return 0;

    size_t str_len = strlen(str);
    size_t new_length = sb->length + str_len;

    if (new_length >= sb->capacity) {
        size_t new_capacity = sb->capacity;
        while (new_capacity <= new_length) {
            new_capacity *= 2;
        }

        char *new_buffer = realloc(sb->buffer, new_capacity);
        if (!new_buffer) return 0;

        sb->buffer = new_buffer;
        sb->capacity = new_capacity;
    }

    memcpy(sb->buffer + sb->length, str, str_len);
    sb->length = new_length;
    sb->buffer[sb->length] = '\0';

    return 1;
}

static int lightusd_json_stringbuilder_append_char(lightusd_json_stringbuilder_t *sb, char c) {
    char str[2] = {c, '\0'};
    return lightusd_json_stringbuilder_append(sb, str);
}

static char *lightusd_json_stringbuilder_get_string(lightusd_json_stringbuilder_t *sb) {
    if (!sb) return NULL;

    char *result = malloc(sb->length + 1);
    if (result) {
        memcpy(result, sb->buffer, sb->length + 1);
    }
    return result;
}

char *lightusd_json_escape_string(const char *str) {
    if (!str) return NULL;

    lightusd_json_stringbuilder_t *sb = lightusd_json_stringbuilder_create();
    if (!sb) return NULL;

    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':
                lightusd_json_stringbuilder_append(sb, "\\\"");
                break;
            case '\\':
                lightusd_json_stringbuilder_append(sb, "\\\\");
                break;
            case '\b':
                lightusd_json_stringbuilder_append(sb, "\\b");
                break;
            case '\f':
                lightusd_json_stringbuilder_append(sb, "\\f");
                break;
            case '\n':
                lightusd_json_stringbuilder_append(sb, "\\n");
                break;
            case '\r':
                lightusd_json_stringbuilder_append(sb, "\\r");
                break;
            case '\t':
                lightusd_json_stringbuilder_append(sb, "\\t");
                break;
            default:
                if ((unsigned char)*p < 32) {
                    char unicode[8];
                    snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned char)*p);
                    lightusd_json_stringbuilder_append(sb, unicode);
                } else {
                    lightusd_json_stringbuilder_append_char(sb, *p);
                }
                break;
        }
    }

    char *result = lightusd_json_stringbuilder_get_string(sb);
    lightusd_json_stringbuilder_destroy(sb);
    return result;
}

static int lightusd_json_serialize_value_internal(const lightusd_json_value_t *value,
                                             lightusd_json_stringbuilder_t *sb,
                                             int indent_level,
                                             int indent_size);

static int lightusd_json_serialize_array_internal(const lightusd_json_array_t *array,
                                             lightusd_json_stringbuilder_t *sb,
                                             int indent_level,
                                             int indent_size) {
    if (!lightusd_json_stringbuilder_append(sb, "[")) return 0;

    if (array->count == 0) {
        return lightusd_json_stringbuilder_append(sb, "]");
    }

    for (size_t i = 0; i < array->count; i++) {
        if (indent_size > 0) {
            lightusd_json_stringbuilder_append(sb, "\n");
            for (int j = 0; j < (indent_level + 1) * indent_size; j++) {
                lightusd_json_stringbuilder_append_char(sb, ' ');
            }
        }

        if (!lightusd_json_serialize_value_internal(array->values[i], sb, indent_level + 1, indent_size)) {
            return 0;
        }

        if (i < array->count - 1) {
            lightusd_json_stringbuilder_append(sb, ",");
        }
    }

    if (indent_size > 0) {
        lightusd_json_stringbuilder_append(sb, "\n");
        for (int j = 0; j < indent_level * indent_size; j++) {
            lightusd_json_stringbuilder_append_char(sb, ' ');
        }
    }

    return lightusd_json_stringbuilder_append(sb, "]");
}

static int lightusd_json_serialize_object_internal(const lightusd_json_object_t *object,
                                              lightusd_json_stringbuilder_t *sb,
                                              int indent_level,
                                              int indent_size) {
    if (!lightusd_json_stringbuilder_append(sb, "{")) return 0;

    if (object->count == 0) {
        return lightusd_json_stringbuilder_append(sb, "}");
    }

    for (size_t i = 0; i < object->count; i++) {
        if (indent_size > 0) {
            lightusd_json_stringbuilder_append(sb, "\n");
            for (int j = 0; j < (indent_level + 1) * indent_size; j++) {
                lightusd_json_stringbuilder_append_char(sb, ' ');
            }
        }

        /* Serialize key */
        char *escaped_key = lightusd_json_escape_string(object->pairs[i].key);
        if (!escaped_key) return 0;

        lightusd_json_stringbuilder_append(sb, "\"");
        lightusd_json_stringbuilder_append(sb, escaped_key);
        lightusd_json_stringbuilder_append(sb, "\":");

        if (indent_size > 0) {
            lightusd_json_stringbuilder_append(sb, " ");
        }

        free(escaped_key);

        /* Serialize value */
        if (!lightusd_json_serialize_value_internal(object->pairs[i].value, sb, indent_level + 1, indent_size)) {
            return 0;
        }

        if (i < object->count - 1) {
            lightusd_json_stringbuilder_append(sb, ",");
        }
    }

    if (indent_size > 0) {
        lightusd_json_stringbuilder_append(sb, "\n");
        for (int j = 0; j < indent_level * indent_size; j++) {
            lightusd_json_stringbuilder_append_char(sb, ' ');
        }
    }

    return lightusd_json_stringbuilder_append(sb, "}");
}

static int lightusd_json_serialize_value_internal(const lightusd_json_value_t *value,
                                             lightusd_json_stringbuilder_t *sb,
                                             int indent_level,
                                             int indent_size) {
    if (!value) return 0;

    switch (value->type) {
        case LIGHTUSD_JSON_NULL:
            return lightusd_json_stringbuilder_append(sb, "null");

        case LIGHTUSD_JSON_BOOL:
            return lightusd_json_stringbuilder_append(sb, value->data.bool_val ? "true" : "false");

        case LIGHTUSD_JSON_NUMBER: {
            char number_str[64];
            snprintf(number_str, sizeof(number_str), "%.17g", value->data.number_val);
            return lightusd_json_stringbuilder_append(sb, number_str);
        }

        case LIGHTUSD_JSON_STRING: {
            char *escaped = lightusd_json_escape_string(value->data.string_val);
            if (!escaped) return 0;

            int result = lightusd_json_stringbuilder_append(sb, "\"") &&
                        lightusd_json_stringbuilder_append(sb, escaped) &&
                        lightusd_json_stringbuilder_append(sb, "\"");

            free(escaped);
            return result;
        }

        case LIGHTUSD_JSON_ARRAY:
            return lightusd_json_serialize_array_internal(value->data.array_val, sb, indent_level, indent_size);

        case LIGHTUSD_JSON_OBJECT:
            return lightusd_json_serialize_object_internal(value->data.object_val, sb, indent_level, indent_size);

        default:
            return 0;
    }
}

char *lightusd_json_serialize(const lightusd_json_value_t *value) {
    if (!value) return NULL;

    lightusd_json_stringbuilder_t *sb = lightusd_json_stringbuilder_create();
    if (!sb) return NULL;

    if (!lightusd_json_serialize_value_internal(value, sb, 0, 0)) {
        lightusd_json_stringbuilder_destroy(sb);
        return NULL;
    }

    char *result = lightusd_json_stringbuilder_get_string(sb);
    lightusd_json_stringbuilder_destroy(sb);
    return result;
}

char *lightusd_json_serialize_pretty(const lightusd_json_value_t *value, int indent_size) {
    if (!value) return NULL;

    lightusd_json_stringbuilder_t *sb = lightusd_json_stringbuilder_create();
    if (!sb) return NULL;

    if (!lightusd_json_serialize_value_internal(value, sb, 0, indent_size)) {
        lightusd_json_stringbuilder_destroy(sb);
        return NULL;
    }

    char *result = lightusd_json_stringbuilder_get_string(sb);
    lightusd_json_stringbuilder_destroy(sb);
    return result;
}

int lightusd_json_write_file(const lightusd_json_value_t *value, const char *filename) {
    char *json_str = lightusd_json_serialize(value);
    if (!json_str) return 0;

    FILE *file = fopen(filename, "w");
    if (!file) {
        free(json_str);
        return 0;
    }

    int result = fputs(json_str, file) != EOF;

    fclose(file);
    free(json_str);
    return result;
}

int lightusd_json_write_file_pretty(const lightusd_json_value_t *value, const char *filename, int indent_size) {
    char *json_str = lightusd_json_serialize_pretty(value, indent_size);
    if (!json_str) return 0;

    FILE *file = fopen(filename, "w");
    if (!file) {
        free(json_str);
        return 0;
    }

    int result = fputs(json_str, file) != EOF;

    fclose(file);
    free(json_str);
    return result;
}

/* ===== USD Layer <-> JSON Conversion Implementation ===== */

lightusd_json_value_t *lightusd_value_to_json(const struct lightusd_value_t *value) {
    if (!value) return lightusd_json_value_create_null();

    switch (value->type) {
        case LIGHTUSD_VALUE_NONE:
            return lightusd_json_value_create_null();

        case LIGHTUSD_VALUE_BOOL:
            return lightusd_json_value_create_bool(value->data.bool_val);

        case LIGHTUSD_VALUE_INT:
            return lightusd_json_value_create_number((double)value->data.int_val);

        case LIGHTUSD_VALUE_UINT:
            return lightusd_json_value_create_number((double)value->data.uint_val);

        case LIGHTUSD_VALUE_INT64:
            return lightusd_json_value_create_number((double)value->data.int64_val);

        case LIGHTUSD_VALUE_UINT64:
            return lightusd_json_value_create_number((double)value->data.uint64_val);

        case LIGHTUSD_VALUE_FLOAT:
            return lightusd_json_value_create_number((double)value->data.float_val);

        case LIGHTUSD_VALUE_DOUBLE:
            return lightusd_json_value_create_number(value->data.double_val);

        case LIGHTUSD_VALUE_STRING:
            return lightusd_json_value_create_string(value->data.string_val);

        case LIGHTUSD_VALUE_TOKEN:
            return lightusd_json_value_create_string(value->data.token_val);

        case LIGHTUSD_VALUE_ARRAY:
            /* TODO: Implement array conversion */
            return lightusd_json_value_create_null();

        default:
            return lightusd_json_value_create_null();
    }
}

lightusd_json_value_t *lightusd_property_to_json(const lightusd_property_t *property) {
    if (!property) return lightusd_json_value_create_null();

    lightusd_json_value_t *json_obj = lightusd_json_value_create_object();
    lightusd_json_object_t *obj = lightusd_json_value_get_object(json_obj);

    /* Basic property information */
    lightusd_json_object_set(obj, "name", lightusd_json_value_create_string(property->name));
    lightusd_json_object_set(obj, "type_name", lightusd_json_value_create_string(property->type_name));
    lightusd_json_object_set(obj, "property_type", lightusd_json_value_create_string(lightusd_property_type_to_string(property->type)));
    lightusd_json_object_set(obj, "variability", lightusd_json_value_create_string(lightusd_variability_to_string(property->variability)));
    lightusd_json_object_set(obj, "is_custom", lightusd_json_value_create_bool(property->is_custom));

    /* Property value */
    if (property->has_value) {
        lightusd_json_object_set(obj, "value", lightusd_value_to_json(&property->value));
    }

    /* Relationship targets */
    if (property->target_count > 0) {
        lightusd_json_value_t *targets_array = lightusd_json_value_create_array();
        lightusd_json_array_t *targets = lightusd_json_value_get_array(targets_array);

        for (size_t i = 0; i < property->target_count; i++) {
            lightusd_json_array_add(targets, lightusd_json_value_create_string(property->target_paths[i]));
        }

        lightusd_json_object_set(obj, "targets", targets_array);
    }

    return json_obj;
}

lightusd_json_value_t *lightusd_primspec_to_json(const lightusd_primspec_t *primspec) {
    if (!primspec) return lightusd_json_value_create_null();

    lightusd_json_value_t *json_obj = lightusd_json_value_create_object();
    lightusd_json_object_t *obj = lightusd_json_value_get_object(json_obj);

    /* Basic prim information */
    lightusd_json_object_set(obj, "name", lightusd_json_value_create_string(primspec->name));
    lightusd_json_object_set(obj, "type_name", lightusd_json_value_create_string(primspec->type_name));
    lightusd_json_object_set(obj, "specifier", lightusd_json_value_create_string(lightusd_specifier_to_string(primspec->specifier)));

    /* Documentation and comment */
    if (primspec->doc) {
        lightusd_json_object_set(obj, "doc", lightusd_json_value_create_string(primspec->doc));
    }
    if (primspec->comment) {
        lightusd_json_object_set(obj, "comment", lightusd_json_value_create_string(primspec->comment));
    }

    /* Properties */
    if (primspec->properties && lightusd_map_size(primspec->properties) > 0) {
        lightusd_json_value_t *props_obj = lightusd_json_value_create_object();
        lightusd_json_object_t *props = lightusd_json_value_get_object(props_obj);

        lightusd_map_iterator_t *iter = lightusd_map_iterator_create(primspec->properties);
        const char *key;
        void *value;

        while (lightusd_map_iterator_next(iter, &key, &value)) {
            lightusd_property_t *prop = (lightusd_property_t*)value;
            lightusd_json_object_set(props, key, lightusd_property_to_json(prop));
        }

        lightusd_map_iterator_destroy(iter);
        lightusd_json_object_set(obj, "properties", props_obj);
    }

    /* Children */
    if (primspec->children && lightusd_map_size(primspec->children) > 0) {
        lightusd_json_value_t *children_obj = lightusd_json_value_create_object();
        lightusd_json_object_t *children = lightusd_json_value_get_object(children_obj);

        lightusd_map_iterator_t *iter = lightusd_map_iterator_create(primspec->children);
        const char *key;
        void *value;

        while (lightusd_map_iterator_next(iter, &key, &value)) {
            lightusd_primspec_t *child = (lightusd_primspec_t*)value;
            lightusd_json_object_set(children, key, lightusd_primspec_to_json(child));
        }

        lightusd_map_iterator_destroy(iter);
        lightusd_json_object_set(obj, "children", children_obj);
    }

    return json_obj;
}

lightusd_json_value_t *lightusd_layer_to_json(const lightusd_layer_t *layer) {
    if (!layer) return lightusd_json_value_create_null();

    lightusd_json_value_t *json_obj = lightusd_json_value_create_object();
    lightusd_json_object_t *obj = lightusd_json_value_get_object(json_obj);

    /* Basic layer information */
    lightusd_json_object_set(obj, "name", lightusd_json_value_create_string(layer->name));

    if (layer->file_path) {
        lightusd_json_object_set(obj, "file_path", lightusd_json_value_create_string(layer->file_path));
    }

    /* Layer metadata */
    lightusd_json_value_t *metadata_obj = lightusd_json_value_create_object();
    lightusd_json_object_t *metadata = lightusd_json_value_get_object(metadata_obj);

    if (layer->metas.doc) {
        lightusd_json_object_set(metadata, "doc", lightusd_json_value_create_string(layer->metas.doc));
    }
    if (layer->metas.comment) {
        lightusd_json_object_set(metadata, "comment", lightusd_json_value_create_string(layer->metas.comment));
    }

    if (layer->metas.up_axis.type == LIGHTUSD_VALUE_STRING && layer->metas.up_axis.data.string_val) {
        lightusd_json_object_set(metadata, "up_axis", lightusd_json_value_create_string(layer->metas.up_axis.data.string_val));
    }

    lightusd_json_object_set(metadata, "meters_per_unit", lightusd_json_value_create_number(layer->metas.meters_per_unit));
    lightusd_json_object_set(metadata, "time_codes_per_second", lightusd_json_value_create_number(layer->metas.time_codes_per_second));
    lightusd_json_object_set(metadata, "start_time_code", lightusd_json_value_create_number(layer->metas.start_time_code));
    lightusd_json_object_set(metadata, "end_time_code", lightusd_json_value_create_number(layer->metas.end_time_code));

    lightusd_json_object_set(obj, "metadata", metadata_obj);

    /* PrimSpecs */
    if (layer->primspecs && lightusd_map_size(layer->primspecs) > 0) {
        lightusd_json_value_t *primspecs_obj = lightusd_json_value_create_object();
        lightusd_json_object_t *primspecs = lightusd_json_value_get_object(primspecs_obj);

        lightusd_map_iterator_t *iter = lightusd_map_iterator_create(layer->primspecs);
        const char *key;
        void *value;

        while (lightusd_map_iterator_next(iter, &key, &value)) {
            lightusd_primspec_t *primspec = (lightusd_primspec_t*)value;
            lightusd_json_object_set(primspecs, key, lightusd_primspec_to_json(primspec));
        }

        lightusd_map_iterator_destroy(iter);
        lightusd_json_object_set(obj, "primspecs", primspecs_obj);
    }

    return json_obj;
}

/* JSON to USD Layer conversion */
struct lightusd_value_t *lightusd_json_to_value(const lightusd_json_value_t *json) {
    if (!json) return NULL;

    switch (json->type) {
        case LIGHTUSD_JSON_NULL:
            return NULL;

        case LIGHTUSD_JSON_BOOL:
            return lightusd_value_create_bool(json->data.bool_val);

        case LIGHTUSD_JSON_NUMBER: {
            double val = json->data.number_val;
            /* Try to determine if it's an integer or float */
            if (val == floor(val) && val >= INT32_MIN && val <= INT32_MAX) {
                return lightusd_value_create_int((int32_t)val);
            } else {
                return lightusd_value_create_double(val);
            }
        }

        case LIGHTUSD_JSON_STRING:
            return lightusd_value_create_string(json->data.string_val);

        default:
            return NULL;
    }
}

lightusd_property_t *lightusd_json_to_property(const lightusd_json_value_t *json) {
    if (!json || !lightusd_json_value_is_object(json)) return NULL;

    lightusd_json_object_t *obj = lightusd_json_value_get_object(json);

    /* Get required fields */
    lightusd_json_value_t *name_val = lightusd_json_object_get(obj, "name");
    lightusd_json_value_t *type_name_val = lightusd_json_object_get(obj, "type_name");
    lightusd_json_value_t *prop_type_val = lightusd_json_object_get(obj, "property_type");

    if (!name_val || !type_name_val || !prop_type_val ||
        !lightusd_json_value_is_string(name_val) ||
        !lightusd_json_value_is_string(type_name_val) ||
        !lightusd_json_value_is_string(prop_type_val)) {
        return NULL;
    }

    const char *name = lightusd_json_value_get_string(name_val);
    const char *type_name = lightusd_json_value_get_string(type_name_val);
    const char *prop_type_str = lightusd_json_value_get_string(prop_type_val);

    /* Convert property type string to enum */
    lightusd_property_type_t prop_type = LIGHTUSD_PROP_ATTRIB;
    if (strcmp(prop_type_str, "relation") == 0) {
        prop_type = LIGHTUSD_PROP_RELATION;
    }

    lightusd_property_t *property = lightusd_property_create(name, type_name, prop_type);
    if (!property) return NULL;

    /* Set optional fields */
    lightusd_json_value_t *variability_val = lightusd_json_object_get(obj, "variability");
    if (variability_val && lightusd_json_value_is_string(variability_val)) {
        const char *variability_str = lightusd_json_value_get_string(variability_val);
        if (strcmp(variability_str, "uniform") == 0) {
            lightusd_property_set_variability(property, LIGHTUSD_VARIABILITY_UNIFORM);
        } else if (strcmp(variability_str, "config") == 0) {
            lightusd_property_set_variability(property, LIGHTUSD_VARIABILITY_CONFIG);
        }
    }

    lightusd_json_value_t *is_custom_val = lightusd_json_object_get(obj, "is_custom");
    if (is_custom_val && lightusd_json_value_is_bool(is_custom_val)) {
        lightusd_property_set_custom(property, lightusd_json_value_get_bool(is_custom_val));
    }

    /* Set value */
    lightusd_json_value_t *value_val = lightusd_json_object_get(obj, "value");
    if (value_val) {
        struct lightusd_value_t *usd_value = lightusd_json_to_value(value_val);
        if (usd_value) {
            lightusd_property_set_value(property, usd_value);
            lightusd_value_destroy(usd_value);
        }
    }

    /* Set targets */
    lightusd_json_value_t *targets_val = lightusd_json_object_get(obj, "targets");
    if (targets_val && lightusd_json_value_is_array(targets_val)) {
        lightusd_json_array_t *targets_array = lightusd_json_value_get_array(targets_val);
        for (size_t i = 0; i < lightusd_json_array_size(targets_array); i++) {
            lightusd_json_value_t *target_val = lightusd_json_array_get(targets_array, i);
            if (target_val && lightusd_json_value_is_string(target_val)) {
                lightusd_property_add_target(property, lightusd_json_value_get_string(target_val));
            }
        }
    }

    return property;
}

lightusd_primspec_t *lightusd_json_to_primspec(const lightusd_json_value_t *json) {
    if (!json || !lightusd_json_value_is_object(json)) return NULL;

    lightusd_json_object_t *obj = lightusd_json_value_get_object(json);

    /* Get required fields */
    lightusd_json_value_t *name_val = lightusd_json_object_get(obj, "name");
    lightusd_json_value_t *type_name_val = lightusd_json_object_get(obj, "type_name");
    lightusd_json_value_t *specifier_val = lightusd_json_object_get(obj, "specifier");

    if (!name_val || !type_name_val || !specifier_val ||
        !lightusd_json_value_is_string(name_val) ||
        !lightusd_json_value_is_string(type_name_val) ||
        !lightusd_json_value_is_string(specifier_val)) {
        return NULL;
    }

    const char *name = lightusd_json_value_get_string(name_val);
    const char *type_name = lightusd_json_value_get_string(type_name_val);
    const char *specifier_str = lightusd_json_value_get_string(specifier_val);

    /* Convert specifier string to enum */
    lightusd_specifier_t specifier = LIGHTUSD_SPEC_DEF;
    if (strcmp(specifier_str, "over") == 0) {
        specifier = LIGHTUSD_SPEC_OVER;
    } else if (strcmp(specifier_str, "class") == 0) {
        specifier = LIGHTUSD_SPEC_CLASS;
    }

    lightusd_primspec_t *primspec = lightusd_primspec_create(name, type_name, specifier);
    if (!primspec) return NULL;

    /* Set optional fields */
    lightusd_json_value_t *doc_val = lightusd_json_object_get(obj, "doc");
    if (doc_val && lightusd_json_value_is_string(doc_val)) {
        lightusd_primspec_set_doc(primspec, lightusd_json_value_get_string(doc_val));
    }

    lightusd_json_value_t *comment_val = lightusd_json_object_get(obj, "comment");
    if (comment_val && lightusd_json_value_is_string(comment_val)) {
        lightusd_primspec_set_comment(primspec, lightusd_json_value_get_string(comment_val));
    }

    /* Load properties */
    lightusd_json_value_t *props_val = lightusd_json_object_get(obj, "properties");
    if (props_val && lightusd_json_value_is_object(props_val)) {
        lightusd_json_object_t *props_obj = lightusd_json_value_get_object(props_val);

        for (size_t i = 0; i < props_obj->count; i++) {
            lightusd_property_t *prop = lightusd_json_to_property(props_obj->pairs[i].value);
            if (prop) {
                lightusd_primspec_add_property(primspec, prop);
            }
        }
    }

    /* Load children */
    lightusd_json_value_t *children_val = lightusd_json_object_get(obj, "children");
    if (children_val && lightusd_json_value_is_object(children_val)) {
        lightusd_json_object_t *children_obj = lightusd_json_value_get_object(children_val);

        for (size_t i = 0; i < children_obj->count; i++) {
            lightusd_primspec_t *child = lightusd_json_to_primspec(children_obj->pairs[i].value);
            if (child) {
                lightusd_primspec_add_child(primspec, child);
            }
        }
    }

    return primspec;
}

lightusd_layer_t *lightusd_json_to_layer(const lightusd_json_value_t *json) {
    if (!json || !lightusd_json_value_is_object(json)) return NULL;

    lightusd_json_object_t *obj = lightusd_json_value_get_object(json);

    /* Get layer name */
    lightusd_json_value_t *name_val = lightusd_json_object_get(obj, "name");
    if (!name_val || !lightusd_json_value_is_string(name_val)) {
        return NULL;
    }

    const char *name = lightusd_json_value_get_string(name_val);
    lightusd_layer_t *layer = lightusd_layer_create(name);
    if (!layer) return NULL;

    /* Set file path */
    lightusd_json_value_t *file_path_val = lightusd_json_object_get(obj, "file_path");
    if (file_path_val && lightusd_json_value_is_string(file_path_val)) {
        lightusd_layer_set_file_path(layer, lightusd_json_value_get_string(file_path_val));
    }

    /* Load metadata */
    lightusd_json_value_t *metadata_val = lightusd_json_object_get(obj, "metadata");
    if (metadata_val && lightusd_json_value_is_object(metadata_val)) {
        lightusd_json_object_t *metadata_obj = lightusd_json_value_get_object(metadata_val);

        lightusd_json_value_t *doc_val = lightusd_json_object_get(metadata_obj, "doc");
        if (doc_val && lightusd_json_value_is_string(doc_val)) {
            lightusd_layer_set_doc(layer, lightusd_json_value_get_string(doc_val));
        }

        lightusd_json_value_t *up_axis_val = lightusd_json_object_get(metadata_obj, "up_axis");
        if (up_axis_val && lightusd_json_value_is_string(up_axis_val)) {
            lightusd_layer_set_up_axis(layer, lightusd_json_value_get_string(up_axis_val));
        }

        lightusd_json_value_t *meters_per_unit_val = lightusd_json_object_get(metadata_obj, "meters_per_unit");
        if (meters_per_unit_val && lightusd_json_value_is_number(meters_per_unit_val)) {
            lightusd_layer_set_meters_per_unit(layer, lightusd_json_value_get_number(meters_per_unit_val));
        }
    }

    /* Load primspecs */
    lightusd_json_value_t *primspecs_val = lightusd_json_object_get(obj, "primspecs");
    if (primspecs_val && lightusd_json_value_is_object(primspecs_val)) {
        lightusd_json_object_t *primspecs_obj = lightusd_json_value_get_object(primspecs_val);

        for (size_t i = 0; i < primspecs_obj->count; i++) {
            lightusd_primspec_t *primspec = lightusd_json_to_primspec(primspecs_obj->pairs[i].value);
            if (primspec) {
                lightusd_layer_add_primspec(layer, primspec);
            }
        }
    }

    return layer;
}

/* High-level conversion functions */
char *lightusd_layer_to_json_string(const lightusd_layer_t *layer) {
    lightusd_json_value_t *json = lightusd_layer_to_json(layer);
    if (!json) return NULL;

    char *result = lightusd_json_serialize(json);
    lightusd_json_value_destroy(json);
    return result;
}

char *lightusd_layer_to_json_string_pretty(const lightusd_layer_t *layer, int indent_size) {
    lightusd_json_value_t *json = lightusd_layer_to_json(layer);
    if (!json) return NULL;

    char *result = lightusd_json_serialize_pretty(json, indent_size);
    lightusd_json_value_destroy(json);
    return result;
}

lightusd_layer_t *lightusd_layer_from_json_string(const char *json_string) {
    lightusd_json_value_t *json = lightusd_json_parse(json_string);
    if (!json) return NULL;

    lightusd_layer_t *layer = lightusd_json_to_layer(json);
    lightusd_json_value_destroy(json);
    return layer;
}

int lightusd_layer_save_json(const lightusd_layer_t *layer, const char *filename) {
    lightusd_json_value_t *json = lightusd_layer_to_json(layer);
    if (!json) return 0;

    int result = lightusd_json_write_file(json, filename);
    lightusd_json_value_destroy(json);
    return result;
}

int lightusd_layer_save_json_pretty(const lightusd_layer_t *layer, const char *filename, int indent_size) {
    lightusd_json_value_t *json = lightusd_layer_to_json(layer);
    if (!json) return 0;

    int result = lightusd_json_write_file_pretty(json, filename, indent_size);
    lightusd_json_value_destroy(json);
    return result;
}

lightusd_layer_t *lightusd_layer_load_json(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;

    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return NULL;
    }

    /* Read file content */
    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    lightusd_layer_t *layer = lightusd_layer_from_json_string(content);
    free(content);
    return layer;
}

/* ===== Utility Functions ===== */

int lightusd_json_validate(const char *json_string) {
    lightusd_json_value_t *value = lightusd_json_parse(json_string);
    if (value) {
        lightusd_json_value_destroy(value);
        return 1;
    }
    return 0;
}

size_t lightusd_json_estimate_memory_usage(const lightusd_json_value_t *value) {
    if (!value) return 0;

    size_t size = sizeof(lightusd_json_value_t);

    switch (value->type) {
        case LIGHTUSD_JSON_STRING:
            if (value->data.string_val) {
                size += strlen(value->data.string_val) + 1;
            }
            break;

        case LIGHTUSD_JSON_ARRAY:
            if (value->data.array_val) {
                size += sizeof(lightusd_json_array_t);
                size += value->data.array_val->capacity * sizeof(lightusd_json_value_t*);
                for (size_t i = 0; i < value->data.array_val->count; i++) {
                    size += lightusd_json_estimate_memory_usage(value->data.array_val->values[i]);
                }
            }
            break;

        case LIGHTUSD_JSON_OBJECT:
            if (value->data.object_val) {
                size += sizeof(lightusd_json_object_t);
                size += value->data.object_val->capacity * sizeof(lightusd_json_pair_t);
                for (size_t i = 0; i < value->data.object_val->count; i++) {
                    if (value->data.object_val->pairs[i].key) {
                        size += strlen(value->data.object_val->pairs[i].key) + 1;
                    }
                    size += lightusd_json_estimate_memory_usage(value->data.object_val->pairs[i].value);
                }
            }
            break;

        default:
            break;
    }

    return size;
}