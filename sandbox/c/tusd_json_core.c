#include "tusd_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>

/* ===== Utility Functions ===== */

static char *tusd_json_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

static int tusd_json_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int tusd_json_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int tusd_json_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int tusd_json_hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* Global error message for parser */
static char g_error_message[256] = {0};

const char *tusd_json_get_error_message(void) {
    return g_error_message;
}

/* ===== JSON Value Implementation ===== */

tusd_json_value_t *tusd_json_value_create_null(void) {
    tusd_json_value_t *value = calloc(1, sizeof(tusd_json_value_t));
    if (value) {
        value->type = TUSD_JSON_NULL;
    }
    return value;
}

tusd_json_value_t *tusd_json_value_create_bool(int val) {
    tusd_json_value_t *value = calloc(1, sizeof(tusd_json_value_t));
    if (value) {
        value->type = TUSD_JSON_BOOL;
        value->data.bool_val = val ? 1 : 0;
    }
    return value;
}

tusd_json_value_t *tusd_json_value_create_number(double val) {
    tusd_json_value_t *value = calloc(1, sizeof(tusd_json_value_t));
    if (value) {
        value->type = TUSD_JSON_NUMBER;
        value->data.number_val = val;
    }
    return value;
}

tusd_json_value_t *tusd_json_value_create_string(const char *val) {
    tusd_json_value_t *value = calloc(1, sizeof(tusd_json_value_t));
    if (value) {
        value->type = TUSD_JSON_STRING;
        value->data.string_val = tusd_json_strdup(val);
        if (!value->data.string_val && val) {
            free(value);
            return NULL;
        }
    }
    return value;
}

tusd_json_value_t *tusd_json_value_create_array(void) {
    tusd_json_value_t *value = calloc(1, sizeof(tusd_json_value_t));
    if (value) {
        value->type = TUSD_JSON_ARRAY;
        value->data.array_val = tusd_json_array_create();
        if (!value->data.array_val) {
            free(value);
            return NULL;
        }
    }
    return value;
}

tusd_json_value_t *tusd_json_value_create_object(void) {
    tusd_json_value_t *value = calloc(1, sizeof(tusd_json_value_t));
    if (value) {
        value->type = TUSD_JSON_OBJECT;
        value->data.object_val = tusd_json_object_create();
        if (!value->data.object_val) {
            free(value);
            return NULL;
        }
    }
    return value;
}

void tusd_json_value_destroy(tusd_json_value_t *value) {
    if (!value) return;
    
    switch (value->type) {
        case TUSD_JSON_STRING:
            free(value->data.string_val);
            break;
        case TUSD_JSON_ARRAY:
            tusd_json_array_destroy(value->data.array_val);
            break;
        case TUSD_JSON_OBJECT:
            tusd_json_object_destroy(value->data.object_val);
            break;
        default:
            break;
    }
    
    free(value);
}

/* Type checking functions */
tusd_json_type_t tusd_json_value_get_type(const tusd_json_value_t *value) {
    return value ? value->type : TUSD_JSON_NULL;
}

int tusd_json_value_is_null(const tusd_json_value_t *value) {
    return value && value->type == TUSD_JSON_NULL;
}

int tusd_json_value_is_bool(const tusd_json_value_t *value) {
    return value && value->type == TUSD_JSON_BOOL;
}

int tusd_json_value_is_number(const tusd_json_value_t *value) {
    return value && value->type == TUSD_JSON_NUMBER;
}

int tusd_json_value_is_string(const tusd_json_value_t *value) {
    return value && value->type == TUSD_JSON_STRING;
}

int tusd_json_value_is_array(const tusd_json_value_t *value) {
    return value && value->type == TUSD_JSON_ARRAY;
}

int tusd_json_value_is_object(const tusd_json_value_t *value) {
    return value && value->type == TUSD_JSON_OBJECT;
}

/* Value extraction functions */
int tusd_json_value_get_bool(const tusd_json_value_t *value) {
    return (value && value->type == TUSD_JSON_BOOL) ? value->data.bool_val : 0;
}

double tusd_json_value_get_number(const tusd_json_value_t *value) {
    return (value && value->type == TUSD_JSON_NUMBER) ? value->data.number_val : 0.0;
}

const char *tusd_json_value_get_string(const tusd_json_value_t *value) {
    return (value && value->type == TUSD_JSON_STRING) ? value->data.string_val : NULL;
}

tusd_json_array_t *tusd_json_value_get_array(const tusd_json_value_t *value) {
    return (value && value->type == TUSD_JSON_ARRAY) ? value->data.array_val : NULL;
}

tusd_json_object_t *tusd_json_value_get_object(const tusd_json_value_t *value) {
    return (value && value->type == TUSD_JSON_OBJECT) ? value->data.object_val : NULL;
}

/* ===== JSON Array Implementation ===== */

tusd_json_array_t *tusd_json_array_create(void) {
    tusd_json_array_t *array = calloc(1, sizeof(tusd_json_array_t));
    if (array) {
        array->capacity = 8;
        array->values = malloc(array->capacity * sizeof(tusd_json_value_t*));
        if (!array->values) {
            free(array);
            return NULL;
        }
    }
    return array;
}

void tusd_json_array_destroy(tusd_json_array_t *array) {
    if (!array) return;
    
    for (size_t i = 0; i < array->count; i++) {
        tusd_json_value_destroy(array->values[i]);
    }
    
    free(array->values);
    free(array);
}

int tusd_json_array_add(tusd_json_array_t *array, tusd_json_value_t *value) {
    if (!array || !value) return 0;
    
    if (array->count >= array->capacity) {
        size_t new_capacity = array->capacity * 2;
        tusd_json_value_t **new_values = realloc(array->values, 
                                                 new_capacity * sizeof(tusd_json_value_t*));
        if (!new_values) return 0;
        
        array->values = new_values;
        array->capacity = new_capacity;
    }
    
    array->values[array->count++] = value;
    return 1;
}

tusd_json_value_t *tusd_json_array_get(const tusd_json_array_t *array, size_t index) {
    if (!array || index >= array->count) return NULL;
    return array->values[index];
}

size_t tusd_json_array_size(const tusd_json_array_t *array) {
    return array ? array->count : 0;
}

/* ===== JSON Object Implementation ===== */

tusd_json_object_t *tusd_json_object_create(void) {
    tusd_json_object_t *object = calloc(1, sizeof(tusd_json_object_t));
    if (object) {
        object->capacity = 8;
        object->pairs = malloc(object->capacity * sizeof(tusd_json_pair_t));
        if (!object->pairs) {
            free(object);
            return NULL;
        }
    }
    return object;
}

void tusd_json_object_destroy(tusd_json_object_t *object) {
    if (!object) return;
    
    for (size_t i = 0; i < object->count; i++) {
        free(object->pairs[i].key);
        tusd_json_value_destroy(object->pairs[i].value);
    }
    
    free(object->pairs);
    free(object);
}

int tusd_json_object_set(tusd_json_object_t *object, const char *key, tusd_json_value_t *value) {
    if (!object || !key || !value) return 0;
    
    /* Check if key already exists */
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->pairs[i].key, key) == 0) {
            /* Replace existing value */
            tusd_json_value_destroy(object->pairs[i].value);
            object->pairs[i].value = value;
            return 1;
        }
    }
    
    /* Add new key-value pair */
    if (object->count >= object->capacity) {
        size_t new_capacity = object->capacity * 2;
        tusd_json_pair_t *new_pairs = realloc(object->pairs, 
                                              new_capacity * sizeof(tusd_json_pair_t));
        if (!new_pairs) return 0;
        
        object->pairs = new_pairs;
        object->capacity = new_capacity;
    }
    
    object->pairs[object->count].key = tusd_json_strdup(key);
    object->pairs[object->count].value = value;
    
    if (!object->pairs[object->count].key) {
        return 0;
    }
    
    object->count++;
    return 1;
}

tusd_json_value_t *tusd_json_object_get(const tusd_json_object_t *object, const char *key) {
    if (!object || !key) return NULL;
    
    for (size_t i = 0; i < object->count; i++) {
        if (strcmp(object->pairs[i].key, key) == 0) {
            return object->pairs[i].value;
        }
    }
    
    return NULL;
}

int tusd_json_object_has_key(const tusd_json_object_t *object, const char *key) {
    return tusd_json_object_get(object, key) != NULL;
}

size_t tusd_json_object_size(const tusd_json_object_t *object) {
    return object ? object->count : 0;
}

char **tusd_json_object_get_keys(const tusd_json_object_t *object, size_t *count) {
    if (!object || !count) return NULL;
    
    *count = object->count;
    if (object->count == 0) return NULL;
    
    char **keys = malloc(object->count * sizeof(char*));
    if (!keys) return NULL;
    
    for (size_t i = 0; i < object->count; i++) {
        keys[i] = tusd_json_strdup(object->pairs[i].key);
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

/* ===== JSON Parser Implementation - Continue from full implementation ===== */

static void tusd_json_parser_skip_whitespace(tusd_json_parser_t *parser) {
    while (parser->position < parser->length && 
           tusd_json_is_whitespace(parser->input[parser->position])) {
        if (parser->input[parser->position] == '\n') {
            parser->line++;
            parser->column = 1;
        } else {
            parser->column++;
        }
        parser->position++;
    }
}

static char tusd_json_parser_peek(tusd_json_parser_t *parser) {
    if (parser->position >= parser->length) return '\0';
    return parser->input[parser->position];
}

static char tusd_json_parser_advance(tusd_json_parser_t *parser) {
    if (parser->position >= parser->length) return '\0';
    
    char c = parser->input[parser->position++];
    parser->column++;
    return c;
}

static int tusd_json_parser_expect(tusd_json_parser_t *parser, char expected) {
    tusd_json_parser_skip_whitespace(parser);
    
    if (tusd_json_parser_peek(parser) != expected) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                "Expected '%c' at line %d, column %d", expected, parser->line, parser->column);
        return 0;
    }
    
    tusd_json_parser_advance(parser);
    return 1;
}

static char *tusd_json_parser_parse_string(tusd_json_parser_t *parser) {
    if (!tusd_json_parser_expect(parser, '"')) {
        return NULL;
    }
    
    size_t capacity = 32;
    char *result = malloc(capacity);
    size_t length = 0;
    
    if (!result) return NULL;
    
    while (parser->position < parser->length) {
        char c = tusd_json_parser_advance(parser);
        
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
            
            char escape = tusd_json_parser_advance(parser);
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
                        char hex = tusd_json_parser_advance(parser);
                        if (!tusd_json_is_hex_digit(hex)) {
                            free(result);
                            snprintf(parser->error_msg, sizeof(parser->error_msg),
                                    "Invalid unicode escape at line %d", parser->line);
                            return NULL;
                        }
                        codepoint = codepoint * 16 + tusd_json_hex_to_int(hex);
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

static tusd_json_value_t *tusd_json_parser_parse_number(tusd_json_parser_t *parser) {
    size_t start = parser->position;
    
    /* Handle negative sign */
    if (tusd_json_parser_peek(parser) == '-') {
        tusd_json_parser_advance(parser);
    }
    
    /* Parse integer part */
    if (!tusd_json_is_digit(tusd_json_parser_peek(parser))) {
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                "Invalid number at line %d, column %d", parser->line, parser->column);
        return NULL;
    }
    
    if (tusd_json_parser_peek(parser) == '0') {
        tusd_json_parser_advance(parser);
    } else {
        while (tusd_json_is_digit(tusd_json_parser_peek(parser))) {
            tusd_json_parser_advance(parser);
        }
    }
    
    /* Parse fractional part */
    if (tusd_json_parser_peek(parser) == '.') {
        tusd_json_parser_advance(parser);
        if (!tusd_json_is_digit(tusd_json_parser_peek(parser))) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Invalid number: missing digits after decimal point at line %d", parser->line);
            return NULL;
        }
        while (tusd_json_is_digit(tusd_json_parser_peek(parser))) {
            tusd_json_parser_advance(parser);
        }
    }
    
    /* Parse exponent part */
    if (tusd_json_parser_peek(parser) == 'e' || tusd_json_parser_peek(parser) == 'E') {
        tusd_json_parser_advance(parser);
        if (tusd_json_parser_peek(parser) == '+' || tusd_json_parser_peek(parser) == '-') {
            tusd_json_parser_advance(parser);
        }
        if (!tusd_json_is_digit(tusd_json_parser_peek(parser))) {
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Invalid number: missing digits in exponent at line %d", parser->line);
            return NULL;
        }
        while (tusd_json_is_digit(tusd_json_parser_peek(parser))) {
            tusd_json_parser_advance(parser);
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
    return tusd_json_value_create_number(value);
}

/* Forward declaration for recursive parsing */
static tusd_json_value_t *tusd_json_parser_parse_value(tusd_json_parser_t *parser);

static tusd_json_value_t *tusd_json_parser_parse_array(tusd_json_parser_t *parser) {
    if (!tusd_json_parser_expect(parser, '[')) {
        return NULL;
    }
    
    tusd_json_value_t *array_value = tusd_json_value_create_array();
    if (!array_value) return NULL;
    
    tusd_json_array_t *array = array_value->data.array_val;
    
    tusd_json_parser_skip_whitespace(parser);
    
    /* Handle empty array */
    if (tusd_json_parser_peek(parser) == ']') {
        tusd_json_parser_advance(parser);
        return array_value;
    }
    
    while (1) {
        tusd_json_value_t *element = tusd_json_parser_parse_value(parser);
        if (!element) {
            tusd_json_value_destroy(array_value);
            return NULL;
        }
        
        if (!tusd_json_array_add(array, element)) {
            tusd_json_value_destroy(element);
            tusd_json_value_destroy(array_value);
            return NULL;
        }
        
        tusd_json_parser_skip_whitespace(parser);
        
        char c = tusd_json_parser_peek(parser);
        if (c == ']') {
            tusd_json_parser_advance(parser);
            break;
        } else if (c == ',') {
            tusd_json_parser_advance(parser);
            tusd_json_parser_skip_whitespace(parser);
        } else {
            tusd_json_value_destroy(array_value);
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Expected ',' or ']' in array at line %d, column %d", parser->line, parser->column);
            return NULL;
        }
    }
    
    return array_value;
}

static tusd_json_value_t *tusd_json_parser_parse_object(tusd_json_parser_t *parser) {
    if (!tusd_json_parser_expect(parser, '{')) {
        return NULL;
    }
    
    tusd_json_value_t *object_value = tusd_json_value_create_object();
    if (!object_value) return NULL;
    
    tusd_json_object_t *object = object_value->data.object_val;
    
    tusd_json_parser_skip_whitespace(parser);
    
    /* Handle empty object */
    if (tusd_json_parser_peek(parser) == '}') {
        tusd_json_parser_advance(parser);
        return object_value;
    }
    
    while (1) {
        /* Parse key */
        char *key = tusd_json_parser_parse_string(parser);
        if (!key) {
            tusd_json_value_destroy(object_value);
            return NULL;
        }
        
        /* Expect colon */
        tusd_json_parser_skip_whitespace(parser);
        if (!tusd_json_parser_expect(parser, ':')) {
            free(key);
            tusd_json_value_destroy(object_value);
            return NULL;
        }
        
        /* Parse value */
        tusd_json_parser_skip_whitespace(parser);
        tusd_json_value_t *value = tusd_json_parser_parse_value(parser);
        if (!value) {
            free(key);
            tusd_json_value_destroy(object_value);
            return NULL;
        }
        
        /* Add to object */
        if (!tusd_json_object_set(object, key, value)) {
            free(key);
            tusd_json_value_destroy(value);
            tusd_json_value_destroy(object_value);
            return NULL;
        }
        
        free(key);
        
        tusd_json_parser_skip_whitespace(parser);
        
        char c = tusd_json_parser_peek(parser);
        if (c == '}') {
            tusd_json_parser_advance(parser);
            break;
        } else if (c == ',') {
            tusd_json_parser_advance(parser);
            tusd_json_parser_skip_whitespace(parser);
        } else {
            tusd_json_value_destroy(object_value);
            snprintf(parser->error_msg, sizeof(parser->error_msg),
                    "Expected ',' or '}' in object at line %d, column %d", parser->line, parser->column);
            return NULL;
        }
    }
    
    return object_value;
}

static tusd_json_value_t *tusd_json_parser_parse_value(tusd_json_parser_t *parser) {
    tusd_json_parser_skip_whitespace(parser);
    
    char c = tusd_json_parser_peek(parser);
    
    switch (c) {
        case '"':
            {
                char *str = tusd_json_parser_parse_string(parser);
                if (!str) return NULL;
                tusd_json_value_t *value = tusd_json_value_create_string(str);
                free(str);
                return value;
            }
        case '[':
            return tusd_json_parser_parse_array(parser);
        case '{':
            return tusd_json_parser_parse_object(parser);
        case 't':
            if (parser->position + 4 <= parser->length && 
                memcmp(parser->input + parser->position, "true", 4) == 0) {
                parser->position += 4;
                parser->column += 4;
                return tusd_json_value_create_bool(1);
            }
            break;
        case 'f':
            if (parser->position + 5 <= parser->length && 
                memcmp(parser->input + parser->position, "false", 5) == 0) {
                parser->position += 5;
                parser->column += 5;
                return tusd_json_value_create_bool(0);
            }
            break;
        case 'n':
            if (parser->position + 4 <= parser->length && 
                memcmp(parser->input + parser->position, "null", 4) == 0) {
                parser->position += 4;
                parser->column += 4;
                return tusd_json_value_create_null();
            }
            break;
        default:
            if (c == '-' || tusd_json_is_digit(c)) {
                return tusd_json_parser_parse_number(parser);
            }
            break;
    }
    
    snprintf(parser->error_msg, sizeof(parser->error_msg),
            "Unexpected character '%c' at line %d, column %d", c, parser->line, parser->column);
    return NULL;
}

tusd_json_value_t *tusd_json_parse_length(const char *json_string, size_t length) {
    if (!json_string) return NULL;
    
    tusd_json_parser_t parser = {0};
    parser.input = json_string;
    parser.length = length;
    parser.line = 1;
    parser.column = 1;
    
    tusd_json_value_t *result = tusd_json_parser_parse_value(&parser);
    
    if (result) {
        tusd_json_parser_skip_whitespace(&parser);
        if (parser.position < parser.length) {
            snprintf(parser.error_msg, sizeof(parser.error_msg),
                    "Unexpected content after JSON value at line %d, column %d", 
                    parser.line, parser.column);
            tusd_json_value_destroy(result);
            return NULL;
        }
    }
    
    if (parser.error_msg[0]) {
        strcpy(g_error_message, parser.error_msg);
    }
    
    return result;
}

tusd_json_value_t *tusd_json_parse(const char *json_string) {
    if (!json_string) return NULL;
    return tusd_json_parse_length(json_string, strlen(json_string));
}

/* ===== JSON Serializer Implementation ===== */

/* String builder for JSON serialization */
typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} tusd_json_stringbuilder_t;

static tusd_json_stringbuilder_t *tusd_json_stringbuilder_create(void) {
    tusd_json_stringbuilder_t *sb = malloc(sizeof(tusd_json_stringbuilder_t));
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

static void tusd_json_stringbuilder_destroy(tusd_json_stringbuilder_t *sb) {
    if (!sb) return;
    free(sb->buffer);
    free(sb);
}

static int tusd_json_stringbuilder_append(tusd_json_stringbuilder_t *sb, const char *str) {
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

static int tusd_json_stringbuilder_append_char(tusd_json_stringbuilder_t *sb, char c) {
    char str[2] = {c, '\0'};
    return tusd_json_stringbuilder_append(sb, str);
}

static char *tusd_json_stringbuilder_get_string(tusd_json_stringbuilder_t *sb) {
    if (!sb) return NULL;
    
    char *result = malloc(sb->length + 1);
    if (result) {
        memcpy(result, sb->buffer, sb->length + 1);
    }
    return result;
}

char *tusd_json_escape_string(const char *str) {
    if (!str) return NULL;
    
    tusd_json_stringbuilder_t *sb = tusd_json_stringbuilder_create();
    if (!sb) return NULL;
    
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':
                tusd_json_stringbuilder_append(sb, "\\\"");
                break;
            case '\\':
                tusd_json_stringbuilder_append(sb, "\\\\");
                break;
            case '\b':
                tusd_json_stringbuilder_append(sb, "\\b");
                break;
            case '\f':
                tusd_json_stringbuilder_append(sb, "\\f");
                break;
            case '\n':
                tusd_json_stringbuilder_append(sb, "\\n");
                break;
            case '\r':
                tusd_json_stringbuilder_append(sb, "\\r");
                break;
            case '\t':
                tusd_json_stringbuilder_append(sb, "\\t");
                break;
            default:
                if ((unsigned char)*p < 32) {
                    char unicode[8];
                    snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned char)*p);
                    tusd_json_stringbuilder_append(sb, unicode);
                } else {
                    tusd_json_stringbuilder_append_char(sb, *p);
                }
                break;
        }
    }
    
    char *result = tusd_json_stringbuilder_get_string(sb);
    tusd_json_stringbuilder_destroy(sb);
    return result;
}

static int tusd_json_serialize_value_internal(const tusd_json_value_t *value, 
                                             tusd_json_stringbuilder_t *sb, 
                                             int indent_level, 
                                             int indent_size);

static int tusd_json_serialize_array_internal(const tusd_json_array_t *array, 
                                             tusd_json_stringbuilder_t *sb,
                                             int indent_level,
                                             int indent_size) {
    if (!tusd_json_stringbuilder_append(sb, "[")) return 0;
    
    if (array->count == 0) {
        return tusd_json_stringbuilder_append(sb, "]");
    }
    
    for (size_t i = 0; i < array->count; i++) {
        if (indent_size > 0) {
            tusd_json_stringbuilder_append(sb, "\n");
            for (int j = 0; j < (indent_level + 1) * indent_size; j++) {
                tusd_json_stringbuilder_append_char(sb, ' ');
            }
        }
        
        if (!tusd_json_serialize_value_internal(array->values[i], sb, indent_level + 1, indent_size)) {
            return 0;
        }
        
        if (i < array->count - 1) {
            tusd_json_stringbuilder_append(sb, ",");
        }
    }
    
    if (indent_size > 0) {
        tusd_json_stringbuilder_append(sb, "\n");
        for (int j = 0; j < indent_level * indent_size; j++) {
            tusd_json_stringbuilder_append_char(sb, ' ');
        }
    }
    
    return tusd_json_stringbuilder_append(sb, "]");
}

static int tusd_json_serialize_object_internal(const tusd_json_object_t *object,
                                              tusd_json_stringbuilder_t *sb,
                                              int indent_level,
                                              int indent_size) {
    if (!tusd_json_stringbuilder_append(sb, "{")) return 0;
    
    if (object->count == 0) {
        return tusd_json_stringbuilder_append(sb, "}");
    }
    
    for (size_t i = 0; i < object->count; i++) {
        if (indent_size > 0) {
            tusd_json_stringbuilder_append(sb, "\n");
            for (int j = 0; j < (indent_level + 1) * indent_size; j++) {
                tusd_json_stringbuilder_append_char(sb, ' ');
            }
        }
        
        /* Serialize key */
        char *escaped_key = tusd_json_escape_string(object->pairs[i].key);
        if (!escaped_key) return 0;
        
        tusd_json_stringbuilder_append(sb, "\"");
        tusd_json_stringbuilder_append(sb, escaped_key);
        tusd_json_stringbuilder_append(sb, "\":");
        
        if (indent_size > 0) {
            tusd_json_stringbuilder_append(sb, " ");
        }
        
        free(escaped_key);
        
        /* Serialize value */
        if (!tusd_json_serialize_value_internal(object->pairs[i].value, sb, indent_level + 1, indent_size)) {
            return 0;
        }
        
        if (i < object->count - 1) {
            tusd_json_stringbuilder_append(sb, ",");
        }
    }
    
    if (indent_size > 0) {
        tusd_json_stringbuilder_append(sb, "\n");
        for (int j = 0; j < indent_level * indent_size; j++) {
            tusd_json_stringbuilder_append_char(sb, ' ');
        }
    }
    
    return tusd_json_stringbuilder_append(sb, "}");
}

static int tusd_json_serialize_value_internal(const tusd_json_value_t *value, 
                                             tusd_json_stringbuilder_t *sb, 
                                             int indent_level, 
                                             int indent_size) {
    if (!value) return 0;
    
    switch (value->type) {
        case TUSD_JSON_NULL:
            return tusd_json_stringbuilder_append(sb, "null");
            
        case TUSD_JSON_BOOL:
            return tusd_json_stringbuilder_append(sb, value->data.bool_val ? "true" : "false");
            
        case TUSD_JSON_NUMBER: {
            char number_str[64];
            snprintf(number_str, sizeof(number_str), "%.17g", value->data.number_val);
            return tusd_json_stringbuilder_append(sb, number_str);
        }
        
        case TUSD_JSON_STRING: {
            char *escaped = tusd_json_escape_string(value->data.string_val);
            if (!escaped) return 0;
            
            int result = tusd_json_stringbuilder_append(sb, "\"") &&
                        tusd_json_stringbuilder_append(sb, escaped) &&
                        tusd_json_stringbuilder_append(sb, "\"");
            
            free(escaped);
            return result;
        }
        
        case TUSD_JSON_ARRAY:
            return tusd_json_serialize_array_internal(value->data.array_val, sb, indent_level, indent_size);
            
        case TUSD_JSON_OBJECT:
            return tusd_json_serialize_object_internal(value->data.object_val, sb, indent_level, indent_size);
            
        default:
            return 0;
    }
}

char *tusd_json_serialize(const tusd_json_value_t *value) {
    if (!value) return NULL;
    
    tusd_json_stringbuilder_t *sb = tusd_json_stringbuilder_create();
    if (!sb) return NULL;
    
    if (!tusd_json_serialize_value_internal(value, sb, 0, 0)) {
        tusd_json_stringbuilder_destroy(sb);
        return NULL;
    }
    
    char *result = tusd_json_stringbuilder_get_string(sb);
    tusd_json_stringbuilder_destroy(sb);
    return result;
}

char *tusd_json_serialize_pretty(const tusd_json_value_t *value, int indent_size) {
    if (!value) return NULL;
    
    tusd_json_stringbuilder_t *sb = tusd_json_stringbuilder_create();
    if (!sb) return NULL;
    
    if (!tusd_json_serialize_value_internal(value, sb, 0, indent_size)) {
        tusd_json_stringbuilder_destroy(sb);
        return NULL;
    }
    
    char *result = tusd_json_stringbuilder_get_string(sb);
    tusd_json_stringbuilder_destroy(sb);
    return result;
}

int tusd_json_write_file(const tusd_json_value_t *value, const char *filename) {
    char *json_str = tusd_json_serialize(value);
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

int tusd_json_write_file_pretty(const tusd_json_value_t *value, const char *filename, int indent_size) {
    char *json_str = tusd_json_serialize_pretty(value, indent_size);
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

/* ===== Utility Functions ===== */

int tusd_json_validate(const char *json_string) {
    tusd_json_value_t *value = tusd_json_parse(json_string);
    if (value) {
        tusd_json_value_destroy(value);
        return 1;
    }
    return 0;
}

size_t tusd_json_estimate_memory_usage(const tusd_json_value_t *value) {
    if (!value) return 0;
    
    size_t size = sizeof(tusd_json_value_t);
    
    switch (value->type) {
        case TUSD_JSON_STRING:
            if (value->data.string_val) {
                size += strlen(value->data.string_val) + 1;
            }
            break;
            
        case TUSD_JSON_ARRAY:
            if (value->data.array_val) {
                size += sizeof(tusd_json_array_t);
                size += value->data.array_val->capacity * sizeof(tusd_json_value_t*);
                for (size_t i = 0; i < value->data.array_val->count; i++) {
                    size += tusd_json_estimate_memory_usage(value->data.array_val->values[i]);
                }
            }
            break;
            
        case TUSD_JSON_OBJECT:
            if (value->data.object_val) {
                size += sizeof(tusd_json_object_t);
                size += value->data.object_val->capacity * sizeof(tusd_json_pair_t);
                for (size_t i = 0; i < value->data.object_val->count; i++) {
                    if (value->data.object_val->pairs[i].key) {
                        size += strlen(value->data.object_val->pairs[i].key) + 1;
                    }
                    size += tusd_json_estimate_memory_usage(value->data.object_val->pairs[i].value);
                }
            }
            break;
            
        default:
            break;
    }
    
    return size;
}