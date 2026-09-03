#ifndef LIGHTUSD_LAYER_H_
#define LIGHTUSD_LAYER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* C99 USD Layer implementation
 * Provides core USD scene graph structures in pure C99
 */

/* ===== Forward Declarations ===== */
typedef struct lightusd_map_t lightusd_map_t;
typedef struct lightusd_layer_t lightusd_layer_t;
typedef struct lightusd_primspec_t lightusd_primspec_t;
typedef struct lightusd_property_t lightusd_property_t;

/* ===== Core Enums ===== */

typedef enum {
    LIGHTUSD_SPEC_OVER = 0,
    LIGHTUSD_SPEC_DEF = 1,
    LIGHTUSD_SPEC_CLASS = 2
} lightusd_specifier_t;

typedef enum {
    LIGHTUSD_PROP_EMPTY_ATTRIB = 0,
    LIGHTUSD_PROP_ATTRIB = 1,
    LIGHTUSD_PROP_RELATION = 2,
    LIGHTUSD_PROP_NO_TARGETS_RELATION = 3,
    LIGHTUSD_PROP_CONNECTION = 4
} lightusd_property_type_t;

typedef enum {
    LIGHTUSD_VALUE_NONE = 0,
    LIGHTUSD_VALUE_BOOL = 1,
    LIGHTUSD_VALUE_INT = 2,
    LIGHTUSD_VALUE_UINT = 3,
    LIGHTUSD_VALUE_INT64 = 4,
    LIGHTUSD_VALUE_UINT64 = 5,
    LIGHTUSD_VALUE_FLOAT = 6,
    LIGHTUSD_VALUE_DOUBLE = 7,
    LIGHTUSD_VALUE_STRING = 8,
    LIGHTUSD_VALUE_TOKEN = 9,
    LIGHTUSD_VALUE_ARRAY = 10
} lightusd_value_type_t;

typedef enum {
    LIGHTUSD_VARIABILITY_VARYING = 0,
    LIGHTUSD_VARIABILITY_UNIFORM = 1,
    LIGHTUSD_VARIABILITY_CONFIG = 2
} lightusd_variability_t;

/* ===== Pure C99 Map Implementation ===== */

/* Map node for string key -> void* value mapping */
typedef struct lightusd_map_node_t {
    char *key;                     /* String key (owned by node) */
    void *value;                   /* Generic value pointer */
    struct lightusd_map_node_t *left;  /* Left child */
    struct lightusd_map_node_t *right; /* Right child */
    int height;                    /* Height for AVL balancing */
} lightusd_map_node_t;

/* Map structure */
struct lightusd_map_t {
    lightusd_map_node_t *root;
    size_t size;
    void (*value_destructor)(void *value); /* Optional destructor for values */
};

/* Map iterator */
typedef struct {
    lightusd_map_t *map;
    lightusd_map_node_t *current;
    lightusd_map_node_t **stack;
    size_t stack_top;
    size_t stack_capacity;
} lightusd_map_iterator_t;

/* ===== Value System ===== */

/* Generic value container */
typedef struct {
    lightusd_value_type_t type;
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
            lightusd_value_type_t element_type;
        } array;
    } data;
} lightusd_value_t;

/* ===== Layer Meta Information ===== */

typedef struct {
    char *doc;                    /* Documentation string */
    char *comment;               /* Comment string */
    lightusd_value_t up_axis;        /* Up axis (typically "Y" or "Z") */
    double meters_per_unit;      /* Scale factor */
    double time_codes_per_second; /* Frame rate */
    double start_time_code;      /* Animation start time */
    double end_time_code;        /* Animation end time */
    lightusd_map_t *custom_data;     /* Custom metadata (string -> lightusd_value_t*) */
} lightusd_layer_metas_t;

/* ===== Property Implementation ===== */

struct lightusd_property_t {
    char *name;                  /* Property name */
    char *type_name;             /* Type name (e.g., "float", "point3f") */
    lightusd_property_type_t type;   /* Property type */
    lightusd_variability_t variability; /* Variability */

    int is_custom;               /* Custom property flag */
    int has_value;               /* Has actual value */
    lightusd_value_t value;          /* Property value */

    /* Relationship data */
    char **target_paths;         /* Array of target paths */
    size_t target_count;         /* Number of targets */

    /* Metadata */
    lightusd_map_t *metadata;        /* Property metadata */
};

/* ===== PrimSpec Implementation ===== */

struct lightusd_primspec_t {
    char *name;                  /* Prim name */
    char *type_name;             /* Prim type (e.g., "Mesh", "Xform") */
    lightusd_specifier_t specifier;  /* Specifier (def/over/class) */

    lightusd_map_t *properties;      /* Properties map (string -> lightusd_property_t*) */
    lightusd_map_t *children;        /* Child PrimSpecs (string -> lightusd_primspec_t*) */

    /* Metadata */
    char *doc;                   /* Documentation */
    char *comment;               /* Comment */
    lightusd_map_t *metadata;        /* Custom metadata */

    /* Composition arcs */
    char **references;           /* Reference asset paths */
    size_t reference_count;
    char **payloads;             /* Payload asset paths */
    size_t payload_count;
    char **inherits;             /* Inherit paths */
    size_t inherit_count;

    /* Variants */
    lightusd_map_t *variant_sets;    /* Variant sets */
};

/* ===== Layer Implementation ===== */

struct lightusd_layer_t {
    char *name;                  /* Layer name/identifier */
    char *file_path;             /* Source file path */

    lightusd_layer_metas_t metas;    /* Layer metadata */
    lightusd_map_t *primspecs;       /* Root PrimSpecs (string -> lightusd_primspec_t*) */

    /* Sublayers */
    char **sublayers;            /* Sublayer asset paths */
    size_t sublayer_count;
};

/* ===== Map API ===== */

/* Create/destroy */
lightusd_map_t *lightusd_map_create(void (*value_destructor)(void *value));
void lightusd_map_destroy(lightusd_map_t *map);

/* Access */
void *lightusd_map_get(lightusd_map_t *map, const char *key);
int lightusd_map_set(lightusd_map_t *map, const char *key, void *value);
int lightusd_map_remove(lightusd_map_t *map, const char *key);
int lightusd_map_has_key(lightusd_map_t *map, const char *key);
size_t lightusd_map_size(lightusd_map_t *map);

/* Iteration */
lightusd_map_iterator_t *lightusd_map_iterator_create(lightusd_map_t *map);
void lightusd_map_iterator_destroy(lightusd_map_iterator_t *iter);
int lightusd_map_iterator_next(lightusd_map_iterator_t *iter, const char **key, void **value);
void lightusd_map_iterator_reset(lightusd_map_iterator_t *iter);

/* ===== Value API ===== */

/* Create/destroy */
lightusd_value_t *lightusd_value_create_bool(int value);
lightusd_value_t *lightusd_value_create_int(int32_t value);
lightusd_value_t *lightusd_value_create_uint(uint32_t value);
lightusd_value_t *lightusd_value_create_int64(int64_t value);
lightusd_value_t *lightusd_value_create_uint64(uint64_t value);
lightusd_value_t *lightusd_value_create_float(float value);
lightusd_value_t *lightusd_value_create_double(double value);
lightusd_value_t *lightusd_value_create_string(const char *value);
lightusd_value_t *lightusd_value_create_token(const char *value);
lightusd_value_t *lightusd_value_create_array(lightusd_value_type_t element_type, size_t count);

void lightusd_value_destroy(lightusd_value_t *value);
void lightusd_value_destructor(void *value); /* For use with maps */

/* Access */
lightusd_value_type_t lightusd_value_get_type(const lightusd_value_t *value);
int lightusd_value_get_bool(const lightusd_value_t *value, int *result);
int lightusd_value_get_int(const lightusd_value_t *value, int32_t *result);
int lightusd_value_get_uint(const lightusd_value_t *value, uint32_t *result);
int lightusd_value_get_int64(const lightusd_value_t *value, int64_t *result);
int lightusd_value_get_uint64(const lightusd_value_t *value, uint64_t *result);
int lightusd_value_get_float(const lightusd_value_t *value, float *result);
int lightusd_value_get_double(const lightusd_value_t *value, double *result);
const char *lightusd_value_get_string(const lightusd_value_t *value);
const char *lightusd_value_get_token(const lightusd_value_t *value);

/* ===== Property API ===== */

lightusd_property_t *lightusd_property_create(const char *name, const char *type_name,
                                      lightusd_property_type_t type);
void lightusd_property_destroy(lightusd_property_t *property);
void lightusd_property_destructor(void *property); /* For use with maps */

int lightusd_property_set_value(lightusd_property_t *property, const lightusd_value_t *value);
const lightusd_value_t *lightusd_property_get_value(const lightusd_property_t *property);

int lightusd_property_set_custom(lightusd_property_t *property, int is_custom);
int lightusd_property_is_custom(const lightusd_property_t *property);

int lightusd_property_set_variability(lightusd_property_t *property, lightusd_variability_t variability);
lightusd_variability_t lightusd_property_get_variability(const lightusd_property_t *property);

/* Relationship targets */
int lightusd_property_add_target(lightusd_property_t *property, const char *target_path);
size_t lightusd_property_get_target_count(const lightusd_property_t *property);
const char *lightusd_property_get_target(const lightusd_property_t *property, size_t index);

/* ===== PrimSpec API ===== */

lightusd_primspec_t *lightusd_primspec_create(const char *name, const char *type_name,
                                      lightusd_specifier_t specifier);
void lightusd_primspec_destroy(lightusd_primspec_t *primspec);
void lightusd_primspec_destructor(void *primspec); /* For use with maps */

/* Properties */
int lightusd_primspec_add_property(lightusd_primspec_t *primspec, lightusd_property_t *property);
lightusd_property_t *lightusd_primspec_get_property(lightusd_primspec_t *primspec, const char *name);
lightusd_map_t *lightusd_primspec_get_properties(lightusd_primspec_t *primspec);

/* Children */
int lightusd_primspec_add_child(lightusd_primspec_t *primspec, lightusd_primspec_t *child);
lightusd_primspec_t *lightusd_primspec_get_child(lightusd_primspec_t *primspec, const char *name);
lightusd_map_t *lightusd_primspec_get_children(lightusd_primspec_t *primspec);

/* Metadata */
int lightusd_primspec_set_doc(lightusd_primspec_t *primspec, const char *doc);
const char *lightusd_primspec_get_doc(const lightusd_primspec_t *primspec);
int lightusd_primspec_set_comment(lightusd_primspec_t *primspec, const char *comment);
const char *lightusd_primspec_get_comment(const lightusd_primspec_t *primspec);

/* ===== Layer API ===== */

lightusd_layer_t *lightusd_layer_create(const char *name);
void lightusd_layer_destroy(lightusd_layer_t *layer);

/* File operations */
int lightusd_layer_set_file_path(lightusd_layer_t *layer, const char *file_path);
const char *lightusd_layer_get_file_path(const lightusd_layer_t *layer);

/* PrimSpecs */
int lightusd_layer_add_primspec(lightusd_layer_t *layer, lightusd_primspec_t *primspec);
lightusd_primspec_t *lightusd_layer_get_primspec(lightusd_layer_t *layer, const char *name);
lightusd_map_t *lightusd_layer_get_primspecs(lightusd_layer_t *layer);

/* Layer metadata */
int lightusd_layer_set_doc(lightusd_layer_t *layer, const char *doc);
const char *lightusd_layer_get_doc(const lightusd_layer_t *layer);
int lightusd_layer_set_up_axis(lightusd_layer_t *layer, const char *axis);
const char *lightusd_layer_get_up_axis(const lightusd_layer_t *layer);
int lightusd_layer_set_meters_per_unit(lightusd_layer_t *layer, double meters_per_unit);
double lightusd_layer_get_meters_per_unit(const lightusd_layer_t *layer);

/* Utility functions */
const char *lightusd_specifier_to_string(lightusd_specifier_t spec);
const char *lightusd_property_type_to_string(lightusd_property_type_t type);
const char *lightusd_variability_to_string(lightusd_variability_t variability);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUSD_LAYER_H_ */