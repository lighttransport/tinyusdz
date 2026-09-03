#include "lightusd_layer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===== Internal Utilities ===== */

static char *lightusd_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, str, len + 1);
    return copy;
}

static int lightusd_strcmp_null_safe(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcmp(a, b);
}

/* ===== Pure C99 AVL Tree Map Implementation ===== */

static int lightusd_map_node_height(lightusd_map_node_t *node) {
    return node ? node->height : 0;
}

static int lightusd_map_node_balance_factor(lightusd_map_node_t *node) {
    return node ? lightusd_map_node_height(node->left) - lightusd_map_node_height(node->right) : 0;
}

static void lightusd_map_node_update_height(lightusd_map_node_t *node) {
    if (!node) return;
    int left_height = lightusd_map_node_height(node->left);
    int right_height = lightusd_map_node_height(node->right);
    node->height = 1 + (left_height > right_height ? left_height : right_height);
}

static lightusd_map_node_t *lightusd_map_node_rotate_right(lightusd_map_node_t *y) {
    lightusd_map_node_t *x = y->left;
    lightusd_map_node_t *T2 = x->right;

    /* Perform rotation */
    x->right = y;
    y->left = T2;

    /* Update heights */
    lightusd_map_node_update_height(y);
    lightusd_map_node_update_height(x);

    return x;
}

static lightusd_map_node_t *lightusd_map_node_rotate_left(lightusd_map_node_t *x) {
    lightusd_map_node_t *y = x->right;
    lightusd_map_node_t *T2 = y->left;

    /* Perform rotation */
    y->left = x;
    x->right = T2;

    /* Update heights */
    lightusd_map_node_update_height(x);
    lightusd_map_node_update_height(y);

    return y;
}

static lightusd_map_node_t *lightusd_map_node_create(const char *key, void *value) {
    lightusd_map_node_t *node = (lightusd_map_node_t*)calloc(1, sizeof(lightusd_map_node_t));
    if (!node) return NULL;

    node->key = lightusd_strdup(key);
    if (!node->key) {
        free(node);
        return NULL;
    }

    node->value = value;
    node->height = 1;
    return node;
}

static void lightusd_map_node_destroy(lightusd_map_node_t *node, void (*value_destructor)(void*)) {
    if (!node) return;

    lightusd_map_node_destroy(node->left, value_destructor);
    lightusd_map_node_destroy(node->right, value_destructor);

    free(node->key);
    if (value_destructor && node->value) {
        value_destructor(node->value);
    }
    free(node);
}

static lightusd_map_node_t *lightusd_map_node_insert(lightusd_map_node_t *node, const char *key,
                                             void *value, int *was_inserted) {
    /* 1. Perform normal BST insertion */
    if (!node) {
        *was_inserted = 1;
        return lightusd_map_node_create(key, value);
    }

    int cmp = strcmp(key, node->key);
    if (cmp < 0) {
        node->left = lightusd_map_node_insert(node->left, key, value, was_inserted);
    } else if (cmp > 0) {
        node->right = lightusd_map_node_insert(node->right, key, value, was_inserted);
    } else {
        /* Key already exists, replace value */
        node->value = value;
        *was_inserted = 0;
        return node;
    }

    /* 2. Update height */
    lightusd_map_node_update_height(node);

    /* 3. Get balance factor */
    int balance = lightusd_map_node_balance_factor(node);

    /* 4. Perform rotations if unbalanced */

    /* Left Left Case */
    if (balance > 1 && strcmp(key, node->left->key) < 0) {
        return lightusd_map_node_rotate_right(node);
    }

    /* Right Right Case */
    if (balance < -1 && strcmp(key, node->right->key) > 0) {
        return lightusd_map_node_rotate_left(node);
    }

    /* Left Right Case */
    if (balance > 1 && strcmp(key, node->left->key) > 0) {
        node->left = lightusd_map_node_rotate_left(node->left);
        return lightusd_map_node_rotate_right(node);
    }

    /* Right Left Case */
    if (balance < -1 && strcmp(key, node->right->key) < 0) {
        node->right = lightusd_map_node_rotate_right(node->right);
        return lightusd_map_node_rotate_left(node);
    }

    return node;
}

static lightusd_map_node_t *lightusd_map_node_find_min(lightusd_map_node_t *node) {
    while (node && node->left) {
        node = node->left;
    }
    return node;
}

static lightusd_map_node_t *lightusd_map_node_remove(lightusd_map_node_t *node, const char *key,
                                             void (*value_destructor)(void*), int *was_removed) {
    if (!node) {
        *was_removed = 0;
        return NULL;
    }

    int cmp = strcmp(key, node->key);
    if (cmp < 0) {
        node->left = lightusd_map_node_remove(node->left, key, value_destructor, was_removed);
    } else if (cmp > 0) {
        node->right = lightusd_map_node_remove(node->right, key, value_destructor, was_removed);
    } else {
        /* Found node to delete */
        *was_removed = 1;

        if (value_destructor && node->value) {
            value_destructor(node->value);
        }

        if (!node->left || !node->right) {
            /* Node with only one child or no child */
            lightusd_map_node_t *temp = node->left ? node->left : node->right;

            if (!temp) {
                /* No child case */
                temp = node;
                node = NULL;
            } else {
                /* One child case */
                *node = *temp; /* Copy contents */
            }

            free(temp->key);
            free(temp);
        } else {
            /* Node with two children */
            lightusd_map_node_t *temp = lightusd_map_node_find_min(node->right);

            /* Copy the inorder successor's data to this node */
            free(node->key);
            node->key = lightusd_strdup(temp->key);
            node->value = temp->value;

            /* Delete the inorder successor */
            int dummy;
            node->right = lightusd_map_node_remove(node->right, temp->key, NULL, &dummy);
        }
    }

    if (!node) return node;

    /* Update height */
    lightusd_map_node_update_height(node);

    /* Get balance factor */
    int balance = lightusd_map_node_balance_factor(node);

    /* Perform rotations if unbalanced */

    /* Left Left Case */
    if (balance > 1 && lightusd_map_node_balance_factor(node->left) >= 0) {
        return lightusd_map_node_rotate_right(node);
    }

    /* Left Right Case */
    if (balance > 1 && lightusd_map_node_balance_factor(node->left) < 0) {
        node->left = lightusd_map_node_rotate_left(node->left);
        return lightusd_map_node_rotate_right(node);
    }

    /* Right Right Case */
    if (balance < -1 && lightusd_map_node_balance_factor(node->right) <= 0) {
        return lightusd_map_node_rotate_left(node);
    }

    /* Right Left Case */
    if (balance < -1 && lightusd_map_node_balance_factor(node->right) > 0) {
        node->right = lightusd_map_node_rotate_right(node->right);
        return lightusd_map_node_rotate_left(node);
    }

    return node;
}

static lightusd_map_node_t *lightusd_map_node_find(lightusd_map_node_t *node, const char *key) {
    if (!node) return NULL;

    int cmp = strcmp(key, node->key);
    if (cmp == 0) {
        return node;
    } else if (cmp < 0) {
        return lightusd_map_node_find(node->left, key);
    } else {
        return lightusd_map_node_find(node->right, key);
    }
}

/* ===== Map API Implementation ===== */

lightusd_map_t *lightusd_map_create(void (*value_destructor)(void *value)) {
    lightusd_map_t *map = (lightusd_map_t*)calloc(1, sizeof(lightusd_map_t));
    if (!map) return NULL;

    map->value_destructor = value_destructor;
    return map;
}

void lightusd_map_destroy(lightusd_map_t *map) {
    if (!map) return;

    lightusd_map_node_destroy(map->root, map->value_destructor);
    free(map);
}

void *lightusd_map_get(lightusd_map_t *map, const char *key) {
    if (!map || !key) return NULL;

    lightusd_map_node_t *node = lightusd_map_node_find(map->root, key);
    return node ? node->value : NULL;
}

int lightusd_map_set(lightusd_map_t *map, const char *key, void *value) {
    if (!map || !key) return 0;

    int was_inserted;
    map->root = lightusd_map_node_insert(map->root, key, value, &was_inserted);

    if (was_inserted) {
        map->size++;
    }

    return 1;
}

int lightusd_map_remove(lightusd_map_t *map, const char *key) {
    if (!map || !key) return 0;

    int was_removed;
    map->root = lightusd_map_node_remove(map->root, key, map->value_destructor, &was_removed);

    if (was_removed) {
        map->size--;
    }

    return was_removed;
}

int lightusd_map_has_key(lightusd_map_t *map, const char *key) {
    return lightusd_map_get(map, key) != NULL;
}

size_t lightusd_map_size(lightusd_map_t *map) {
    return map ? map->size : 0;
}

/* ===== Map Iterator Implementation ===== */

lightusd_map_iterator_t *lightusd_map_iterator_create(lightusd_map_t *map) {
    if (!map) return NULL;

    lightusd_map_iterator_t *iter = (lightusd_map_iterator_t*)calloc(1, sizeof(lightusd_map_iterator_t));
    if (!iter) return NULL;

    iter->map = map;
    iter->stack_capacity = 32; /* Initial capacity */
    iter->stack = (lightusd_map_node_t**)malloc(iter->stack_capacity * sizeof(lightusd_map_node_t*));

    if (!iter->stack) {
        free(iter);
        return NULL;
    }

    lightusd_map_iterator_reset(iter);
    return iter;
}

void lightusd_map_iterator_destroy(lightusd_map_iterator_t *iter) {
    if (!iter) return;

    free(iter->stack);
    free(iter);
}

void lightusd_map_iterator_reset(lightusd_map_iterator_t *iter) {
    if (!iter) return;

    iter->stack_top = 0;
    iter->current = iter->map->root;

    /* Push all left nodes onto stack */
    while (iter->current) {
        if (iter->stack_top >= iter->stack_capacity) {
            /* Expand stack */
            iter->stack_capacity *= 2;
            iter->stack = (lightusd_map_node_t**)realloc(iter->stack,
                          iter->stack_capacity * sizeof(lightusd_map_node_t*));
        }

        iter->stack[iter->stack_top++] = iter->current;
        iter->current = iter->current->left;
    }

    iter->current = (iter->stack_top > 0) ? iter->stack[--iter->stack_top] : NULL;
}

int lightusd_map_iterator_next(lightusd_map_iterator_t *iter, const char **key, void **value) {
    if (!iter || !iter->current) return 0;

    /* Return current node */
    if (key) *key = iter->current->key;
    if (value) *value = iter->current->value;

    /* Move to next node */
    lightusd_map_node_t *node = iter->current->right;

    /* Push all left nodes from right subtree */
    while (node) {
        if (iter->stack_top >= iter->stack_capacity) {
            /* Expand stack */
            iter->stack_capacity *= 2;
            iter->stack = (lightusd_map_node_t**)realloc(iter->stack,
                          iter->stack_capacity * sizeof(lightusd_map_node_t*));
        }

        iter->stack[iter->stack_top++] = node;
        node = node->left;
    }

    iter->current = (iter->stack_top > 0) ? iter->stack[--iter->stack_top] : NULL;

    return 1;
}

/* ===== Value System Implementation ===== */

lightusd_value_t *lightusd_value_create_bool(int value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_BOOL;
    val->data.bool_val = value ? 1 : 0;
    return val;
}

lightusd_value_t *lightusd_value_create_int(int32_t value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_INT;
    val->data.int_val = value;
    return val;
}

lightusd_value_t *lightusd_value_create_uint(uint32_t value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_UINT;
    val->data.uint_val = value;
    return val;
}

lightusd_value_t *lightusd_value_create_int64(int64_t value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_INT64;
    val->data.int64_val = value;
    return val;
}

lightusd_value_t *lightusd_value_create_uint64(uint64_t value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_UINT64;
    val->data.uint64_val = value;
    return val;
}

lightusd_value_t *lightusd_value_create_float(float value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_FLOAT;
    val->data.float_val = value;
    return val;
}

lightusd_value_t *lightusd_value_create_double(double value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_DOUBLE;
    val->data.double_val = value;
    return val;
}

lightusd_value_t *lightusd_value_create_string(const char *value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_STRING;
    val->data.string_val = lightusd_strdup(value);

    if (!val->data.string_val) {
        free(val);
        return NULL;
    }

    return val;
}

lightusd_value_t *lightusd_value_create_token(const char *value) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_TOKEN;
    val->data.token_val = lightusd_strdup(value);

    if (!val->data.token_val) {
        free(val);
        return NULL;
    }

    return val;
}

lightusd_value_t *lightusd_value_create_array(lightusd_value_type_t element_type, size_t count) {
    lightusd_value_t *val = (lightusd_value_t*)malloc(sizeof(lightusd_value_t));
    if (!val) return NULL;

    val->type = LIGHTUSD_VALUE_ARRAY;
    val->data.array.element_type = element_type;
    val->data.array.count = count;

    if (count > 0) {
        size_t element_size;
        switch (element_type) {
            case LIGHTUSD_VALUE_BOOL: element_size = sizeof(int); break;
            case LIGHTUSD_VALUE_INT: element_size = sizeof(int32_t); break;
            case LIGHTUSD_VALUE_UINT: element_size = sizeof(uint32_t); break;
            case LIGHTUSD_VALUE_INT64: element_size = sizeof(int64_t); break;
            case LIGHTUSD_VALUE_UINT64: element_size = sizeof(uint64_t); break;
            case LIGHTUSD_VALUE_FLOAT: element_size = sizeof(float); break;
            case LIGHTUSD_VALUE_DOUBLE: element_size = sizeof(double); break;
            case LIGHTUSD_VALUE_STRING:
            case LIGHTUSD_VALUE_TOKEN: element_size = sizeof(char*); break;
            default: element_size = sizeof(void*); break;
        }

        val->data.array.data = calloc(count, element_size);
        if (!val->data.array.data) {
            free(val);
            return NULL;
        }
    } else {
        val->data.array.data = NULL;
    }

    return val;
}

void lightusd_value_destroy(lightusd_value_t *value) {
    if (!value) return;

    switch (value->type) {
        case LIGHTUSD_VALUE_STRING:
            free(value->data.string_val);
            break;
        case LIGHTUSD_VALUE_TOKEN:
            free(value->data.token_val);
            break;
        case LIGHTUSD_VALUE_ARRAY:
            if (value->data.array.data) {
                if (value->data.array.element_type == LIGHTUSD_VALUE_STRING ||
                    value->data.array.element_type == LIGHTUSD_VALUE_TOKEN) {
                    /* Free string array elements */
                    char **strings = (char**)value->data.array.data;
                    for (size_t i = 0; i < value->data.array.count; i++) {
                        free(strings[i]);
                    }
                }
                free(value->data.array.data);
            }
            break;
        default:
            break;
    }

    free(value);
}

void lightusd_value_destructor(void *value) {
    lightusd_value_destroy((lightusd_value_t*)value);
}

lightusd_value_type_t lightusd_value_get_type(const lightusd_value_t *value) {
    return value ? value->type : LIGHTUSD_VALUE_NONE;
}

int lightusd_value_get_bool(const lightusd_value_t *value, int *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_BOOL) return 0;
    *result = value->data.bool_val;
    return 1;
}

int lightusd_value_get_int(const lightusd_value_t *value, int32_t *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_INT) return 0;
    *result = value->data.int_val;
    return 1;
}

int lightusd_value_get_uint(const lightusd_value_t *value, uint32_t *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_UINT) return 0;
    *result = value->data.uint_val;
    return 1;
}

int lightusd_value_get_int64(const lightusd_value_t *value, int64_t *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_INT64) return 0;
    *result = value->data.int64_val;
    return 1;
}

int lightusd_value_get_uint64(const lightusd_value_t *value, uint64_t *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_UINT64) return 0;
    *result = value->data.uint64_val;
    return 1;
}

int lightusd_value_get_float(const lightusd_value_t *value, float *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_FLOAT) return 0;
    *result = value->data.float_val;
    return 1;
}

int lightusd_value_get_double(const lightusd_value_t *value, double *result) {
    if (!value || !result || value->type != LIGHTUSD_VALUE_DOUBLE) return 0;
    *result = value->data.double_val;
    return 1;
}

const char *lightusd_value_get_string(const lightusd_value_t *value) {
    if (!value || value->type != LIGHTUSD_VALUE_STRING) return NULL;
    return value->data.string_val;
}

const char *lightusd_value_get_token(const lightusd_value_t *value) {
    if (!value || value->type != LIGHTUSD_VALUE_TOKEN) return NULL;
    return value->data.token_val;
}

/* ===== Property Implementation ===== */

lightusd_property_t *lightusd_property_create(const char *name, const char *type_name,
                                      lightusd_property_type_t type) {
    if (!name || !type_name) return NULL;

    lightusd_property_t *prop = (lightusd_property_t*)calloc(1, sizeof(lightusd_property_t));
    if (!prop) return NULL;

    prop->name = lightusd_strdup(name);
    prop->type_name = lightusd_strdup(type_name);

    if (!prop->name || !prop->type_name) {
        lightusd_property_destroy(prop);
        return NULL;
    }

    prop->type = type;
    prop->variability = LIGHTUSD_VARIABILITY_VARYING;
    prop->metadata = lightusd_map_create(lightusd_value_destructor);

    if (!prop->metadata) {
        lightusd_property_destroy(prop);
        return NULL;
    }

    return prop;
}

void lightusd_property_destroy(lightusd_property_t *property) {
    if (!property) return;

    free(property->name);
    free(property->type_name);

    if (property->has_value) {
        /* Clean up embedded value content */
        if (property->value.type == LIGHTUSD_VALUE_STRING && property->value.data.string_val) {
            free(property->value.data.string_val);
        } else if (property->value.type == LIGHTUSD_VALUE_TOKEN && property->value.data.token_val) {
            free(property->value.data.token_val);
        } else if (property->value.type == LIGHTUSD_VALUE_ARRAY && property->value.data.array.data) {
            free(property->value.data.array.data);
        }
    }

    /* Free target paths array */
    if (property->target_paths) {
        for (size_t i = 0; i < property->target_count; i++) {
            free(property->target_paths[i]);
        }
        free(property->target_paths);
    }

    lightusd_map_destroy(property->metadata);
    free(property);
}

void lightusd_property_destructor(void *property) {
    lightusd_property_destroy((lightusd_property_t*)property);
}

int lightusd_property_set_value(lightusd_property_t *property, const lightusd_value_t *value) {
    if (!property || !value) return 0;

    if (property->has_value) {
        /* Clean up existing value content without freeing the struct itself */
        if (property->value.type == LIGHTUSD_VALUE_STRING && property->value.data.string_val) {
            free(property->value.data.string_val);
        } else if (property->value.type == LIGHTUSD_VALUE_TOKEN && property->value.data.token_val) {
            free(property->value.data.token_val);
        } else if (property->value.type == LIGHTUSD_VALUE_ARRAY && property->value.data.array.data) {
            free(property->value.data.array.data);
        }
    }

    /* Deep copy the value */
    memcpy(&property->value, value, sizeof(lightusd_value_t));

    /* Handle string/token copying */
    switch (value->type) {
        case LIGHTUSD_VALUE_STRING:
            property->value.data.string_val = lightusd_strdup(value->data.string_val);
            if (!property->value.data.string_val) return 0;
            break;
        case LIGHTUSD_VALUE_TOKEN:
            property->value.data.token_val = lightusd_strdup(value->data.token_val);
            if (!property->value.data.token_val) return 0;
            break;
        case LIGHTUSD_VALUE_ARRAY:
            /* TODO: Implement array copying if needed */
            break;
        default:
            break;
    }

    property->has_value = 1;
    return 1;
}

const lightusd_value_t *lightusd_property_get_value(const lightusd_property_t *property) {
    if (!property || !property->has_value) return NULL;
    return &property->value;
}

int lightusd_property_set_custom(lightusd_property_t *property, int is_custom) {
    if (!property) return 0;
    property->is_custom = is_custom ? 1 : 0;
    return 1;
}

int lightusd_property_is_custom(const lightusd_property_t *property) {
    return property ? property->is_custom : 0;
}

int lightusd_property_set_variability(lightusd_property_t *property, lightusd_variability_t variability) {
    if (!property) return 0;
    property->variability = variability;
    return 1;
}

lightusd_variability_t lightusd_property_get_variability(const lightusd_property_t *property) {
    return property ? property->variability : LIGHTUSD_VARIABILITY_VARYING;
}

int lightusd_property_add_target(lightusd_property_t *property, const char *target_path) {
    if (!property || !target_path) return 0;

    /* Reallocate targets array */
    char **new_targets = (char**)realloc(property->target_paths,
                                        (property->target_count + 1) * sizeof(char*));
    if (!new_targets) return 0;

    property->target_paths = new_targets;
    property->target_paths[property->target_count] = lightusd_strdup(target_path);

    if (!property->target_paths[property->target_count]) return 0;

    property->target_count++;
    return 1;
}

size_t lightusd_property_get_target_count(const lightusd_property_t *property) {
    return property ? property->target_count : 0;
}

const char *lightusd_property_get_target(const lightusd_property_t *property, size_t index) {
    if (!property || index >= property->target_count) return NULL;
    return property->target_paths[index];
}

/* ===== PrimSpec Implementation ===== */

lightusd_primspec_t *lightusd_primspec_create(const char *name, const char *type_name,
                                      lightusd_specifier_t specifier) {
    if (!name) return NULL;

    lightusd_primspec_t *primspec = (lightusd_primspec_t*)calloc(1, sizeof(lightusd_primspec_t));
    if (!primspec) return NULL;

    primspec->name = lightusd_strdup(name);
    if (!primspec->name) {
        lightusd_primspec_destroy(primspec);
        return NULL;
    }

    if (type_name) {
        primspec->type_name = lightusd_strdup(type_name);
        if (!primspec->type_name) {
            lightusd_primspec_destroy(primspec);
            return NULL;
        }
    }

    primspec->specifier = specifier;

    /* Create maps */
    primspec->properties = lightusd_map_create(lightusd_property_destructor);
    primspec->children = lightusd_map_create(lightusd_primspec_destructor);
    primspec->metadata = lightusd_map_create(lightusd_value_destructor);
    primspec->variant_sets = lightusd_map_create(lightusd_value_destructor); /* TODO: Better destructor */

    if (!primspec->properties || !primspec->children ||
        !primspec->metadata || !primspec->variant_sets) {
        lightusd_primspec_destroy(primspec);
        return NULL;
    }

    return primspec;
}

void lightusd_primspec_destroy(lightusd_primspec_t *primspec) {
    if (!primspec) return;

    free(primspec->name);
    free(primspec->type_name);
    free(primspec->doc);
    free(primspec->comment);

    lightusd_map_destroy(primspec->properties);
    lightusd_map_destroy(primspec->children);
    lightusd_map_destroy(primspec->metadata);
    lightusd_map_destroy(primspec->variant_sets);

    /* Free composition arrays */
    if (primspec->references) {
        for (size_t i = 0; i < primspec->reference_count; i++) {
            free(primspec->references[i]);
        }
        free(primspec->references);
    }

    if (primspec->payloads) {
        for (size_t i = 0; i < primspec->payload_count; i++) {
            free(primspec->payloads[i]);
        }
        free(primspec->payloads);
    }

    if (primspec->inherits) {
        for (size_t i = 0; i < primspec->inherit_count; i++) {
            free(primspec->inherits[i]);
        }
        free(primspec->inherits);
    }

    free(primspec);
}

void lightusd_primspec_destructor(void *primspec) {
    lightusd_primspec_destroy((lightusd_primspec_t*)primspec);
}

int lightusd_primspec_add_property(lightusd_primspec_t *primspec, lightusd_property_t *property) {
    if (!primspec || !property || !property->name) return 0;

    return lightusd_map_set(primspec->properties, property->name, property);
}

lightusd_property_t *lightusd_primspec_get_property(lightusd_primspec_t *primspec, const char *name) {
    if (!primspec || !name) return NULL;

    return (lightusd_property_t*)lightusd_map_get(primspec->properties, name);
}

lightusd_map_t *lightusd_primspec_get_properties(lightusd_primspec_t *primspec) {
    return primspec ? primspec->properties : NULL;
}

int lightusd_primspec_add_child(lightusd_primspec_t *primspec, lightusd_primspec_t *child) {
    if (!primspec || !child || !child->name) return 0;

    return lightusd_map_set(primspec->children, child->name, child);
}

lightusd_primspec_t *lightusd_primspec_get_child(lightusd_primspec_t *primspec, const char *name) {
    if (!primspec || !name) return NULL;

    return (lightusd_primspec_t*)lightusd_map_get(primspec->children, name);
}

lightusd_map_t *lightusd_primspec_get_children(lightusd_primspec_t *primspec) {
    return primspec ? primspec->children : NULL;
}

int lightusd_primspec_set_doc(lightusd_primspec_t *primspec, const char *doc) {
    if (!primspec) return 0;

    free(primspec->doc);
    primspec->doc = doc ? lightusd_strdup(doc) : NULL;
    return 1;
}

const char *lightusd_primspec_get_doc(const lightusd_primspec_t *primspec) {
    return primspec ? primspec->doc : NULL;
}

int lightusd_primspec_set_comment(lightusd_primspec_t *primspec, const char *comment) {
    if (!primspec) return 0;

    free(primspec->comment);
    primspec->comment = comment ? lightusd_strdup(comment) : NULL;
    return 1;
}

const char *lightusd_primspec_get_comment(const lightusd_primspec_t *primspec) {
    return primspec ? primspec->comment : NULL;
}

/* ===== Layer Implementation ===== */

lightusd_layer_t *lightusd_layer_create(const char *name) {
    lightusd_layer_t *layer = (lightusd_layer_t*)calloc(1, sizeof(lightusd_layer_t));
    if (!layer) return NULL;

    if (name) {
        layer->name = lightusd_strdup(name);
        if (!layer->name) {
            lightusd_layer_destroy(layer);
            return NULL;
        }
    }

    /* Initialize metadata */
    layer->metas.meters_per_unit = 1.0;
    layer->metas.time_codes_per_second = 24.0;
    layer->metas.start_time_code = 1.0;
    layer->metas.end_time_code = 1.0;
    layer->metas.custom_data = lightusd_map_create(lightusd_value_destructor);

    /* Create maps */
    layer->primspecs = lightusd_map_create(lightusd_primspec_destructor);

    if (!layer->metas.custom_data || !layer->primspecs) {
        lightusd_layer_destroy(layer);
        return NULL;
    }

    return layer;
}

void lightusd_layer_destroy(lightusd_layer_t *layer) {
    if (!layer) return;

    free(layer->name);
    free(layer->file_path);
    free(layer->metas.doc);
    free(layer->metas.comment);

    if (layer->metas.up_axis.type == LIGHTUSD_VALUE_STRING) {
        free(layer->metas.up_axis.data.string_val);
    } else if (layer->metas.up_axis.type == LIGHTUSD_VALUE_TOKEN) {
        free(layer->metas.up_axis.data.token_val);
    }

    lightusd_map_destroy(layer->metas.custom_data);
    lightusd_map_destroy(layer->primspecs);

    /* Free sublayers array */
    if (layer->sublayers) {
        for (size_t i = 0; i < layer->sublayer_count; i++) {
            free(layer->sublayers[i]);
        }
        free(layer->sublayers);
    }

    free(layer);
}

int lightusd_layer_set_file_path(lightusd_layer_t *layer, const char *file_path) {
    if (!layer) return 0;

    free(layer->file_path);
    layer->file_path = file_path ? lightusd_strdup(file_path) : NULL;
    return 1;
}

const char *lightusd_layer_get_file_path(const lightusd_layer_t *layer) {
    return layer ? layer->file_path : NULL;
}

int lightusd_layer_add_primspec(lightusd_layer_t *layer, lightusd_primspec_t *primspec) {
    if (!layer || !primspec || !primspec->name) return 0;

    return lightusd_map_set(layer->primspecs, primspec->name, primspec);
}

lightusd_primspec_t *lightusd_layer_get_primspec(lightusd_layer_t *layer, const char *name) {
    if (!layer || !name) return NULL;

    return (lightusd_primspec_t*)lightusd_map_get(layer->primspecs, name);
}

lightusd_map_t *lightusd_layer_get_primspecs(lightusd_layer_t *layer) {
    return layer ? layer->primspecs : NULL;
}

int lightusd_layer_set_doc(lightusd_layer_t *layer, const char *doc) {
    if (!layer) return 0;

    free(layer->metas.doc);
    layer->metas.doc = doc ? lightusd_strdup(doc) : NULL;
    return 1;
}

const char *lightusd_layer_get_doc(const lightusd_layer_t *layer) {
    return layer ? layer->metas.doc : NULL;
}

int lightusd_layer_set_up_axis(lightusd_layer_t *layer, const char *axis) {
    if (!layer) return 0;

    /* Clean up previous value */
    if (layer->metas.up_axis.type == LIGHTUSD_VALUE_STRING) {
        free(layer->metas.up_axis.data.string_val);
    } else if (layer->metas.up_axis.type == LIGHTUSD_VALUE_TOKEN) {
        free(layer->metas.up_axis.data.token_val);
    }

    if (axis) {
        layer->metas.up_axis.type = LIGHTUSD_VALUE_TOKEN;
        layer->metas.up_axis.data.token_val = lightusd_strdup(axis);
        return layer->metas.up_axis.data.token_val != NULL;
    } else {
        layer->metas.up_axis.type = LIGHTUSD_VALUE_NONE;
        return 1;
    }
}

const char *lightusd_layer_get_up_axis(const lightusd_layer_t *layer) {
    if (!layer) return NULL;

    if (layer->metas.up_axis.type == LIGHTUSD_VALUE_TOKEN) {
        return layer->metas.up_axis.data.token_val;
    } else if (layer->metas.up_axis.type == LIGHTUSD_VALUE_STRING) {
        return layer->metas.up_axis.data.string_val;
    }

    return NULL;
}

int lightusd_layer_set_meters_per_unit(lightusd_layer_t *layer, double meters_per_unit) {
    if (!layer) return 0;

    layer->metas.meters_per_unit = meters_per_unit;
    return 1;
}

double lightusd_layer_get_meters_per_unit(const lightusd_layer_t *layer) {
    return layer ? layer->metas.meters_per_unit : 1.0;
}

/* ===== Utility Functions ===== */

const char *lightusd_specifier_to_string(lightusd_specifier_t spec) {
    switch (spec) {
        case LIGHTUSD_SPEC_OVER: return "over";
        case LIGHTUSD_SPEC_DEF: return "def";
        case LIGHTUSD_SPEC_CLASS: return "class";
        default: return "unknown";
    }
}

const char *lightusd_property_type_to_string(lightusd_property_type_t type) {
    switch (type) {
        case LIGHTUSD_PROP_EMPTY_ATTRIB: return "empty_attrib";
        case LIGHTUSD_PROP_ATTRIB: return "attrib";
        case LIGHTUSD_PROP_RELATION: return "relation";
        case LIGHTUSD_PROP_NO_TARGETS_RELATION: return "no_targets_relation";
        case LIGHTUSD_PROP_CONNECTION: return "connection";
        default: return "unknown";
    }
}

const char *lightusd_variability_to_string(lightusd_variability_t variability) {
    switch (variability) {
        case LIGHTUSD_VARIABILITY_VARYING: return "varying";
        case LIGHTUSD_VARIABILITY_UNIFORM: return "uniform";
        case LIGHTUSD_VARIABILITY_CONFIG: return "config";
        default: return "unknown";
    }
}