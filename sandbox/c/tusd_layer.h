#ifndef TUSD_LAYER_H_
#define TUSD_LAYER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* C99 USD Layer implementation
 * Provides core USD scene graph structures in pure C99
 */

/* ===== Forward Declarations ===== */
typedef struct tusd_map_t tusd_map_t;
typedef struct tusd_layer_t tusd_layer_t;
typedef struct tusd_primspec_t tusd_primspec_t;
typedef struct tusd_property_t tusd_property_t;

/* ===== Core Enums ===== */

typedef enum {
    TUSD_SPEC_OVER = 0,
    TUSD_SPEC_DEF = 1,
    TUSD_SPEC_CLASS = 2
} tusd_specifier_t;

typedef enum {
    TUSD_PROP_EMPTY_ATTRIB = 0,
    TUSD_PROP_ATTRIB = 1,
    TUSD_PROP_RELATION = 2,
    TUSD_PROP_NO_TARGETS_RELATION = 3,
    TUSD_PROP_CONNECTION = 4
} tusd_property_type_t;

typedef enum {
    TUSD_VALUE_NONE = 0,
    TUSD_VALUE_BOOL = 1,
    TUSD_VALUE_INT = 2,
    TUSD_VALUE_UINT = 3,
    TUSD_VALUE_INT64 = 4,
    TUSD_VALUE_UINT64 = 5,
    TUSD_VALUE_FLOAT = 6,
    TUSD_VALUE_DOUBLE = 7,
    TUSD_VALUE_STRING = 8,
    TUSD_VALUE_TOKEN = 9,
    TUSD_VALUE_ARRAY = 10
} tusd_value_type_t;

typedef enum {
    TUSD_VARIABILITY_VARYING = 0,
    TUSD_VARIABILITY_UNIFORM = 1,
    TUSD_VARIABILITY_CONFIG = 2
} tusd_variability_t;

/* ===== Pure C99 Map Implementation ===== */

/* Map node for string key -> void* value mapping */
typedef struct tusd_map_node_t {
    char *key;                     /* String key (owned by node) */
    void *value;                   /* Generic value pointer */
    struct tusd_map_node_t *left;  /* Left child */
    struct tusd_map_node_t *right; /* Right child */
    int height;                    /* Height for AVL balancing */
} tusd_map_node_t;

/* Map structure */
struct tusd_map_t {
    tusd_map_node_t *root;
    size_t size;
    void (*value_destructor)(void *value); /* Optional destructor for values */
};

/* Map iterator */
typedef struct {
    tusd_map_t *map;
    tusd_map_node_t *current;
    tusd_map_node_t **stack;
    size_t stack_top;
    size_t stack_capacity;
} tusd_map_iterator_t;

/* ===== Value System ===== */

/* Generic value container */
typedef struct {
    tusd_value_type_t type;
    union {
        int bool_val;
        int32_t int_val;
        uint32_t uint_val;
        int64_t int64_val;
        uint64_t uint64_val;
        float float_val;
        double double_val;
        char *string_val;  /* Owned string */
        char *token_val;   /* Owned token string */
        struct {           /* Array data */
            void *data;
            size_t count;
            tusd_value_type_t element_type;
        } array;
    } data;
} tusd_value_t;

/* ===== Layer Meta Information ===== */

typedef struct {
    char *doc;                    /* Documentation string */
    char *comment;               /* Comment string */
    tusd_value_t up_axis;        /* Up axis (typically "Y" or "Z") */
    double meters_per_unit;      /* Scale factor */
    double time_codes_per_second; /* Frame rate */
    double start_time_code;      /* Animation start time */
    double end_time_code;        /* Animation end time */
    tusd_map_t *custom_data;     /* Custom metadata (string -> tusd_value_t*) */
} tusd_layer_metas_t;

/* ===== Property Implementation ===== */

struct tusd_property_t {
    char *name;                  /* Property name */
    char *type_name;             /* Type name (e.g., "float", "point3f") */
    tusd_property_type_t type;   /* Property type */
    tusd_variability_t variability; /* Variability */
    
    int is_custom;               /* Custom property flag */
    int has_value;               /* Has actual value */
    tusd_value_t value;          /* Property value */
    
    /* Relationship data */
    char **target_paths;         /* Array of target paths */
    size_t target_count;         /* Number of targets */
    
    /* Metadata */
    tusd_map_t *metadata;        /* Property metadata */
};

/* ===== PrimSpec Implementation ===== */

struct tusd_primspec_t {
    char *name;                  /* Prim name */
    char *type_name;             /* Prim type (e.g., "Mesh", "Xform") */
    tusd_specifier_t specifier;  /* Specifier (def/over/class) */
    
    tusd_map_t *properties;      /* Properties map (string -> tusd_property_t*) */
    tusd_map_t *children;        /* Child PrimSpecs (string -> tusd_primspec_t*) */
    
    /* Metadata */
    char *doc;                   /* Documentation */
    char *comment;               /* Comment */
    tusd_map_t *metadata;        /* Custom metadata */
    
    /* Composition arcs */
    char **references;           /* Reference asset paths */
    size_t reference_count;
    char **payloads;             /* Payload asset paths */
    size_t payload_count;
    char **inherits;             /* Inherit paths */
    size_t inherit_count;
    
    /* Variants */
    tusd_map_t *variant_sets;    /* Variant sets */
};

/* ===== Layer Implementation ===== */

struct tusd_layer_t {
    char *name;                  /* Layer name/identifier */
    char *file_path;             /* Source file path */
    
    tusd_layer_metas_t metas;    /* Layer metadata */
    tusd_map_t *primspecs;       /* Root PrimSpecs (string -> tusd_primspec_t*) */
    
    /* Sublayers */
    char **sublayers;            /* Sublayer asset paths */
    size_t sublayer_count;
};

/* ===== Map API ===== */

/* Create/destroy */
tusd_map_t *tusd_map_create(void (*value_destructor)(void *value));
void tusd_map_destroy(tusd_map_t *map);

/* Access */
void *tusd_map_get(tusd_map_t *map, const char *key);
int tusd_map_set(tusd_map_t *map, const char *key, void *value);
int tusd_map_remove(tusd_map_t *map, const char *key);
int tusd_map_has_key(tusd_map_t *map, const char *key);
size_t tusd_map_size(tusd_map_t *map);

/* Iteration */
tusd_map_iterator_t *tusd_map_iterator_create(tusd_map_t *map);
void tusd_map_iterator_destroy(tusd_map_iterator_t *iter);
int tusd_map_iterator_next(tusd_map_iterator_t *iter, const char **key, void **value);
void tusd_map_iterator_reset(tusd_map_iterator_t *iter);

/* ===== Value API ===== */

/* Create/destroy */
tusd_value_t *tusd_value_create_bool(int value);
tusd_value_t *tusd_value_create_int(int32_t value);
tusd_value_t *tusd_value_create_uint(uint32_t value);
tusd_value_t *tusd_value_create_int64(int64_t value);
tusd_value_t *tusd_value_create_uint64(uint64_t value);
tusd_value_t *tusd_value_create_float(float value);
tusd_value_t *tusd_value_create_double(double value);
tusd_value_t *tusd_value_create_string(const char *value);
tusd_value_t *tusd_value_create_token(const char *value);
tusd_value_t *tusd_value_create_array(tusd_value_type_t element_type, size_t count);

void tusd_value_destroy(tusd_value_t *value);
void tusd_value_destructor(void *value); /* For use with maps */

/* Access */
tusd_value_type_t tusd_value_get_type(const tusd_value_t *value);
int tusd_value_get_bool(const tusd_value_t *value, int *result);
int tusd_value_get_int(const tusd_value_t *value, int32_t *result);
int tusd_value_get_uint(const tusd_value_t *value, uint32_t *result);
int tusd_value_get_int64(const tusd_value_t *value, int64_t *result);
int tusd_value_get_uint64(const tusd_value_t *value, uint64_t *result);
int tusd_value_get_float(const tusd_value_t *value, float *result);
int tusd_value_get_double(const tusd_value_t *value, double *result);
const char *tusd_value_get_string(const tusd_value_t *value);
const char *tusd_value_get_token(const tusd_value_t *value);

/* ===== Property API ===== */

tusd_property_t *tusd_property_create(const char *name, const char *type_name, 
                                      tusd_property_type_t type);
void tusd_property_destroy(tusd_property_t *property);
void tusd_property_destructor(void *property); /* For use with maps */

int tusd_property_set_value(tusd_property_t *property, const tusd_value_t *value);
const tusd_value_t *tusd_property_get_value(const tusd_property_t *property);

int tusd_property_set_custom(tusd_property_t *property, int is_custom);
int tusd_property_is_custom(const tusd_property_t *property);

int tusd_property_set_variability(tusd_property_t *property, tusd_variability_t variability);
tusd_variability_t tusd_property_get_variability(const tusd_property_t *property);

/* Relationship targets */
int tusd_property_add_target(tusd_property_t *property, const char *target_path);
size_t tusd_property_get_target_count(const tusd_property_t *property);
const char *tusd_property_get_target(const tusd_property_t *property, size_t index);

/* ===== PrimSpec API ===== */

tusd_primspec_t *tusd_primspec_create(const char *name, const char *type_name, 
                                      tusd_specifier_t specifier);
void tusd_primspec_destroy(tusd_primspec_t *primspec);
void tusd_primspec_destructor(void *primspec); /* For use with maps */

/* Properties */
int tusd_primspec_add_property(tusd_primspec_t *primspec, tusd_property_t *property);
tusd_property_t *tusd_primspec_get_property(tusd_primspec_t *primspec, const char *name);
tusd_map_t *tusd_primspec_get_properties(tusd_primspec_t *primspec);

/* Children */
int tusd_primspec_add_child(tusd_primspec_t *primspec, tusd_primspec_t *child);
tusd_primspec_t *tusd_primspec_get_child(tusd_primspec_t *primspec, const char *name);
tusd_map_t *tusd_primspec_get_children(tusd_primspec_t *primspec);

/* Metadata */
int tusd_primspec_set_doc(tusd_primspec_t *primspec, const char *doc);
const char *tusd_primspec_get_doc(const tusd_primspec_t *primspec);
int tusd_primspec_set_comment(tusd_primspec_t *primspec, const char *comment);
const char *tusd_primspec_get_comment(const tusd_primspec_t *primspec);

/* ===== Layer API ===== */

tusd_layer_t *tusd_layer_create(const char *name);
void tusd_layer_destroy(tusd_layer_t *layer);

/* File operations */
int tusd_layer_set_file_path(tusd_layer_t *layer, const char *file_path);
const char *tusd_layer_get_file_path(const tusd_layer_t *layer);

/* PrimSpecs */
int tusd_layer_add_primspec(tusd_layer_t *layer, tusd_primspec_t *primspec);
tusd_primspec_t *tusd_layer_get_primspec(tusd_layer_t *layer, const char *name);
tusd_map_t *tusd_layer_get_primspecs(tusd_layer_t *layer);

/* Layer metadata */
int tusd_layer_set_doc(tusd_layer_t *layer, const char *doc);
const char *tusd_layer_get_doc(const tusd_layer_t *layer);
int tusd_layer_set_up_axis(tusd_layer_t *layer, const char *axis);
const char *tusd_layer_get_up_axis(const tusd_layer_t *layer);
int tusd_layer_set_meters_per_unit(tusd_layer_t *layer, double meters_per_unit);
double tusd_layer_get_meters_per_unit(const tusd_layer_t *layer);

/* Utility functions */
const char *tusd_specifier_to_string(tusd_specifier_t spec);
const char *tusd_property_type_to_string(tusd_property_type_t type);
const char *tusd_variability_to_string(tusd_variability_t variability);

#ifdef __cplusplus
}
#endif

#endif /* TUSD_LAYER_H_ */