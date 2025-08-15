#include "tusd_layer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===== Internal Utilities ===== */

static char *tusd_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, str, len + 1);
    return copy;
}

static int tusd_strcmp_null_safe(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcmp(a, b);
}

/* ===== Pure C99 AVL Tree Map Implementation ===== */

static int tusd_map_node_height(tusd_map_node_t *node) {
    return node ? node->height : 0;
}

static int tusd_map_node_balance_factor(tusd_map_node_t *node) {
    return node ? tusd_map_node_height(node->left) - tusd_map_node_height(node->right) : 0;
}

static void tusd_map_node_update_height(tusd_map_node_t *node) {
    if (!node) return;
    int left_height = tusd_map_node_height(node->left);
    int right_height = tusd_map_node_height(node->right);
    node->height = 1 + (left_height > right_height ? left_height : right_height);
}

static tusd_map_node_t *tusd_map_node_rotate_right(tusd_map_node_t *y) {
    tusd_map_node_t *x = y->left;
    tusd_map_node_t *T2 = x->right;
    
    /* Perform rotation */
    x->right = y;
    y->left = T2;
    
    /* Update heights */
    tusd_map_node_update_height(y);
    tusd_map_node_update_height(x);
    
    return x;
}

static tusd_map_node_t *tusd_map_node_rotate_left(tusd_map_node_t *x) {
    tusd_map_node_t *y = x->right;
    tusd_map_node_t *T2 = y->left;
    
    /* Perform rotation */
    y->left = x;
    x->right = T2;
    
    /* Update heights */
    tusd_map_node_update_height(x);
    tusd_map_node_update_height(y);
    
    return y;
}

static tusd_map_node_t *tusd_map_node_create(const char *key, void *value) {
    tusd_map_node_t *node = (tusd_map_node_t*)calloc(1, sizeof(tusd_map_node_t));
    if (!node) return NULL;
    
    node->key = tusd_strdup(key);
    if (!node->key) {
        free(node);
        return NULL;
    }
    
    node->value = value;
    node->height = 1;
    return node;
}

static void tusd_map_node_destroy(tusd_map_node_t *node, void (*value_destructor)(void*)) {
    if (!node) return;
    
    tusd_map_node_destroy(node->left, value_destructor);
    tusd_map_node_destroy(node->right, value_destructor);
    
    free(node->key);
    if (value_destructor && node->value) {
        value_destructor(node->value);
    }
    free(node);
}

static tusd_map_node_t *tusd_map_node_insert(tusd_map_node_t *node, const char *key, 
                                             void *value, int *was_inserted) {
    /* 1. Perform normal BST insertion */
    if (!node) {
        *was_inserted = 1;
        return tusd_map_node_create(key, value);
    }
    
    int cmp = strcmp(key, node->key);
    if (cmp < 0) {
        node->left = tusd_map_node_insert(node->left, key, value, was_inserted);
    } else if (cmp > 0) {
        node->right = tusd_map_node_insert(node->right, key, value, was_inserted);
    } else {
        /* Key already exists, replace value */
        node->value = value;
        *was_inserted = 0;
        return node;
    }
    
    /* 2. Update height */
    tusd_map_node_update_height(node);
    
    /* 3. Get balance factor */
    int balance = tusd_map_node_balance_factor(node);
    
    /* 4. Perform rotations if unbalanced */
    
    /* Left Left Case */
    if (balance > 1 && strcmp(key, node->left->key) < 0) {
        return tusd_map_node_rotate_right(node);
    }
    
    /* Right Right Case */
    if (balance < -1 && strcmp(key, node->right->key) > 0) {
        return tusd_map_node_rotate_left(node);
    }
    
    /* Left Right Case */
    if (balance > 1 && strcmp(key, node->left->key) > 0) {
        node->left = tusd_map_node_rotate_left(node->left);
        return tusd_map_node_rotate_right(node);
    }
    
    /* Right Left Case */
    if (balance < -1 && strcmp(key, node->right->key) < 0) {
        node->right = tusd_map_node_rotate_right(node->right);
        return tusd_map_node_rotate_left(node);
    }
    
    return node;
}

static tusd_map_node_t *tusd_map_node_find_min(tusd_map_node_t *node) {
    while (node && node->left) {
        node = node->left;
    }
    return node;
}

static tusd_map_node_t *tusd_map_node_remove(tusd_map_node_t *node, const char *key, 
                                             void (*value_destructor)(void*), int *was_removed) {
    if (!node) {
        *was_removed = 0;
        return NULL;
    }
    
    int cmp = strcmp(key, node->key);
    if (cmp < 0) {
        node->left = tusd_map_node_remove(node->left, key, value_destructor, was_removed);
    } else if (cmp > 0) {
        node->right = tusd_map_node_remove(node->right, key, value_destructor, was_removed);
    } else {
        /* Found node to delete */
        *was_removed = 1;
        
        if (value_destructor && node->value) {
            value_destructor(node->value);
        }
        
        if (!node->left || !node->right) {
            /* Node with only one child or no child */
            tusd_map_node_t *temp = node->left ? node->left : node->right;
            
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
            tusd_map_node_t *temp = tusd_map_node_find_min(node->right);
            
            /* Copy the inorder successor's data to this node */
            free(node->key);
            node->key = tusd_strdup(temp->key);
            node->value = temp->value;
            
            /* Delete the inorder successor */
            int dummy;
            node->right = tusd_map_node_remove(node->right, temp->key, NULL, &dummy);
        }
    }
    
    if (!node) return node;
    
    /* Update height */
    tusd_map_node_update_height(node);
    
    /* Get balance factor */
    int balance = tusd_map_node_balance_factor(node);
    
    /* Perform rotations if unbalanced */
    
    /* Left Left Case */
    if (balance > 1 && tusd_map_node_balance_factor(node->left) >= 0) {
        return tusd_map_node_rotate_right(node);
    }
    
    /* Left Right Case */
    if (balance > 1 && tusd_map_node_balance_factor(node->left) < 0) {
        node->left = tusd_map_node_rotate_left(node->left);
        return tusd_map_node_rotate_right(node);
    }
    
    /* Right Right Case */
    if (balance < -1 && tusd_map_node_balance_factor(node->right) <= 0) {
        return tusd_map_node_rotate_left(node);
    }
    
    /* Right Left Case */
    if (balance < -1 && tusd_map_node_balance_factor(node->right) > 0) {
        node->right = tusd_map_node_rotate_right(node->right);
        return tusd_map_node_rotate_left(node);
    }
    
    return node;
}

static tusd_map_node_t *tusd_map_node_find(tusd_map_node_t *node, const char *key) {
    if (!node) return NULL;
    
    int cmp = strcmp(key, node->key);
    if (cmp == 0) {
        return node;
    } else if (cmp < 0) {
        return tusd_map_node_find(node->left, key);
    } else {
        return tusd_map_node_find(node->right, key);
    }
}

/* ===== Map API Implementation ===== */

tusd_map_t *tusd_map_create(void (*value_destructor)(void *value)) {
    tusd_map_t *map = (tusd_map_t*)calloc(1, sizeof(tusd_map_t));
    if (!map) return NULL;
    
    map->value_destructor = value_destructor;
    return map;
}

void tusd_map_destroy(tusd_map_t *map) {
    if (!map) return;
    
    tusd_map_node_destroy(map->root, map->value_destructor);
    free(map);
}

void *tusd_map_get(tusd_map_t *map, const char *key) {
    if (!map || !key) return NULL;
    
    tusd_map_node_t *node = tusd_map_node_find(map->root, key);
    return node ? node->value : NULL;
}

int tusd_map_set(tusd_map_t *map, const char *key, void *value) {
    if (!map || !key) return 0;
    
    int was_inserted;
    map->root = tusd_map_node_insert(map->root, key, value, &was_inserted);
    
    if (was_inserted) {
        map->size++;
    }
    
    return 1;
}

int tusd_map_remove(tusd_map_t *map, const char *key) {
    if (!map || !key) return 0;
    
    int was_removed;
    map->root = tusd_map_node_remove(map->root, key, map->value_destructor, &was_removed);
    
    if (was_removed) {
        map->size--;
    }
    
    return was_removed;
}

int tusd_map_has_key(tusd_map_t *map, const char *key) {
    return tusd_map_get(map, key) != NULL;
}

size_t tusd_map_size(tusd_map_t *map) {
    return map ? map->size : 0;
}

/* ===== Map Iterator Implementation ===== */

tusd_map_iterator_t *tusd_map_iterator_create(tusd_map_t *map) {
    if (!map) return NULL;
    
    tusd_map_iterator_t *iter = (tusd_map_iterator_t*)calloc(1, sizeof(tusd_map_iterator_t));
    if (!iter) return NULL;
    
    iter->map = map;
    iter->stack_capacity = 32; /* Initial capacity */
    iter->stack = (tusd_map_node_t**)malloc(iter->stack_capacity * sizeof(tusd_map_node_t*));
    
    if (!iter->stack) {
        free(iter);
        return NULL;
    }
    
    tusd_map_iterator_reset(iter);
    return iter;
}

void tusd_map_iterator_destroy(tusd_map_iterator_t *iter) {
    if (!iter) return;
    
    free(iter->stack);
    free(iter);
}

void tusd_map_iterator_reset(tusd_map_iterator_t *iter) {
    if (!iter) return;
    
    iter->stack_top = 0;
    iter->current = iter->map->root;
    
    /* Push all left nodes onto stack */
    while (iter->current) {
        if (iter->stack_top >= iter->stack_capacity) {
            /* Expand stack */
            iter->stack_capacity *= 2;
            iter->stack = (tusd_map_node_t**)realloc(iter->stack, 
                          iter->stack_capacity * sizeof(tusd_map_node_t*));
        }
        
        iter->stack[iter->stack_top++] = iter->current;
        iter->current = iter->current->left;
    }
    
    iter->current = (iter->stack_top > 0) ? iter->stack[--iter->stack_top] : NULL;
}

int tusd_map_iterator_next(tusd_map_iterator_t *iter, const char **key, void **value) {
    if (!iter || !iter->current) return 0;
    
    /* Return current node */
    if (key) *key = iter->current->key;
    if (value) *value = iter->current->value;
    
    /* Move to next node */
    tusd_map_node_t *node = iter->current->right;
    
    /* Push all left nodes from right subtree */
    while (node) {
        if (iter->stack_top >= iter->stack_capacity) {
            /* Expand stack */
            iter->stack_capacity *= 2;
            iter->stack = (tusd_map_node_t**)realloc(iter->stack, 
                          iter->stack_capacity * sizeof(tusd_map_node_t*));
        }
        
        iter->stack[iter->stack_top++] = node;
        node = node->left;
    }
    
    iter->current = (iter->stack_top > 0) ? iter->stack[--iter->stack_top] : NULL;
    
    return 1;
}

/* ===== Value System Implementation ===== */

tusd_value_t *tusd_value_create_bool(int value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_BOOL;
    val->data.bool_val = value ? 1 : 0;
    return val;
}

tusd_value_t *tusd_value_create_int(int32_t value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_INT;
    val->data.int_val = value;
    return val;
}

tusd_value_t *tusd_value_create_uint(uint32_t value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_UINT;
    val->data.uint_val = value;
    return val;
}

tusd_value_t *tusd_value_create_int64(int64_t value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_INT64;
    val->data.int64_val = value;
    return val;
}

tusd_value_t *tusd_value_create_uint64(uint64_t value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_UINT64;
    val->data.uint64_val = value;
    return val;
}

tusd_value_t *tusd_value_create_float(float value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_FLOAT;
    val->data.float_val = value;
    return val;
}

tusd_value_t *tusd_value_create_double(double value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_DOUBLE;
    val->data.double_val = value;
    return val;
}

tusd_value_t *tusd_value_create_string(const char *value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_STRING;
    val->data.string_val = tusd_strdup(value);
    
    if (!val->data.string_val) {
        free(val);
        return NULL;
    }
    
    return val;
}

tusd_value_t *tusd_value_create_token(const char *value) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_TOKEN;
    val->data.token_val = tusd_strdup(value);
    
    if (!val->data.token_val) {
        free(val);
        return NULL;
    }
    
    return val;
}

tusd_value_t *tusd_value_create_array(tusd_value_type_t element_type, size_t count) {
    tusd_value_t *val = (tusd_value_t*)malloc(sizeof(tusd_value_t));
    if (!val) return NULL;
    
    val->type = TUSD_VALUE_ARRAY;
    val->data.array.element_type = element_type;
    val->data.array.count = count;
    
    if (count > 0) {
        size_t element_size;
        switch (element_type) {
            case TUSD_VALUE_BOOL: element_size = sizeof(int); break;
            case TUSD_VALUE_INT: element_size = sizeof(int32_t); break;
            case TUSD_VALUE_UINT: element_size = sizeof(uint32_t); break;
            case TUSD_VALUE_INT64: element_size = sizeof(int64_t); break;
            case TUSD_VALUE_UINT64: element_size = sizeof(uint64_t); break;
            case TUSD_VALUE_FLOAT: element_size = sizeof(float); break;
            case TUSD_VALUE_DOUBLE: element_size = sizeof(double); break;
            case TUSD_VALUE_STRING:
            case TUSD_VALUE_TOKEN: element_size = sizeof(char*); break;
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

void tusd_value_destroy(tusd_value_t *value) {
    if (!value) return;
    
    switch (value->type) {
        case TUSD_VALUE_STRING:
            free(value->data.string_val);
            break;
        case TUSD_VALUE_TOKEN:
            free(value->data.token_val);
            break;
        case TUSD_VALUE_ARRAY:
            if (value->data.array.data) {
                if (value->data.array.element_type == TUSD_VALUE_STRING ||
                    value->data.array.element_type == TUSD_VALUE_TOKEN) {
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

void tusd_value_destructor(void *value) {
    tusd_value_destroy((tusd_value_t*)value);
}

tusd_value_type_t tusd_value_get_type(const tusd_value_t *value) {
    return value ? value->type : TUSD_VALUE_NONE;
}

int tusd_value_get_bool(const tusd_value_t *value, int *result) {
    if (!value || !result || value->type != TUSD_VALUE_BOOL) return 0;
    *result = value->data.bool_val;
    return 1;
}

int tusd_value_get_int(const tusd_value_t *value, int32_t *result) {
    if (!value || !result || value->type != TUSD_VALUE_INT) return 0;
    *result = value->data.int_val;
    return 1;
}

int tusd_value_get_uint(const tusd_value_t *value, uint32_t *result) {
    if (!value || !result || value->type != TUSD_VALUE_UINT) return 0;
    *result = value->data.uint_val;
    return 1;
}

int tusd_value_get_int64(const tusd_value_t *value, int64_t *result) {
    if (!value || !result || value->type != TUSD_VALUE_INT64) return 0;
    *result = value->data.int64_val;
    return 1;
}

int tusd_value_get_uint64(const tusd_value_t *value, uint64_t *result) {
    if (!value || !result || value->type != TUSD_VALUE_UINT64) return 0;
    *result = value->data.uint64_val;
    return 1;
}

int tusd_value_get_float(const tusd_value_t *value, float *result) {
    if (!value || !result || value->type != TUSD_VALUE_FLOAT) return 0;
    *result = value->data.float_val;
    return 1;
}

int tusd_value_get_double(const tusd_value_t *value, double *result) {
    if (!value || !result || value->type != TUSD_VALUE_DOUBLE) return 0;
    *result = value->data.double_val;
    return 1;
}

const char *tusd_value_get_string(const tusd_value_t *value) {
    if (!value || value->type != TUSD_VALUE_STRING) return NULL;
    return value->data.string_val;
}

const char *tusd_value_get_token(const tusd_value_t *value) {
    if (!value || value->type != TUSD_VALUE_TOKEN) return NULL;
    return value->data.token_val;
}

/* ===== Property Implementation ===== */

tusd_property_t *tusd_property_create(const char *name, const char *type_name, 
                                      tusd_property_type_t type) {
    if (!name || !type_name) return NULL;
    
    tusd_property_t *prop = (tusd_property_t*)calloc(1, sizeof(tusd_property_t));
    if (!prop) return NULL;
    
    prop->name = tusd_strdup(name);
    prop->type_name = tusd_strdup(type_name);
    
    if (!prop->name || !prop->type_name) {
        tusd_property_destroy(prop);
        return NULL;
    }
    
    prop->type = type;
    prop->variability = TUSD_VARIABILITY_VARYING;
    prop->metadata = tusd_map_create(tusd_value_destructor);
    
    if (!prop->metadata) {
        tusd_property_destroy(prop);
        return NULL;
    }
    
    return prop;
}

void tusd_property_destroy(tusd_property_t *property) {
    if (!property) return;
    
    free(property->name);
    free(property->type_name);
    
    if (property->has_value) {
        /* Clean up embedded value content */
        if (property->value.type == TUSD_VALUE_STRING && property->value.data.string_val) {
            free(property->value.data.string_val);
        } else if (property->value.type == TUSD_VALUE_TOKEN && property->value.data.token_val) {
            free(property->value.data.token_val);
        } else if (property->value.type == TUSD_VALUE_ARRAY && property->value.data.array.data) {
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
    
    tusd_map_destroy(property->metadata);
    free(property);
}

void tusd_property_destructor(void *property) {
    tusd_property_destroy((tusd_property_t*)property);
}

int tusd_property_set_value(tusd_property_t *property, const tusd_value_t *value) {
    if (!property || !value) return 0;
    
    if (property->has_value) {
        /* Clean up existing value content without freeing the struct itself */
        if (property->value.type == TUSD_VALUE_STRING && property->value.data.string_val) {
            free(property->value.data.string_val);
        } else if (property->value.type == TUSD_VALUE_TOKEN && property->value.data.token_val) {
            free(property->value.data.token_val);
        } else if (property->value.type == TUSD_VALUE_ARRAY && property->value.data.array.data) {
            free(property->value.data.array.data);
        }
    }
    
    /* Deep copy the value */
    memcpy(&property->value, value, sizeof(tusd_value_t));
    
    /* Handle string/token copying */
    switch (value->type) {
        case TUSD_VALUE_STRING:
            property->value.data.string_val = tusd_strdup(value->data.string_val);
            if (!property->value.data.string_val) return 0;
            break;
        case TUSD_VALUE_TOKEN:
            property->value.data.token_val = tusd_strdup(value->data.token_val);
            if (!property->value.data.token_val) return 0;
            break;
        case TUSD_VALUE_ARRAY:
            /* TODO: Implement array copying if needed */
            break;
        default:
            break;
    }
    
    property->has_value = 1;
    return 1;
}

const tusd_value_t *tusd_property_get_value(const tusd_property_t *property) {
    if (!property || !property->has_value) return NULL;
    return &property->value;
}

int tusd_property_set_custom(tusd_property_t *property, int is_custom) {
    if (!property) return 0;
    property->is_custom = is_custom ? 1 : 0;
    return 1;
}

int tusd_property_is_custom(const tusd_property_t *property) {
    return property ? property->is_custom : 0;
}

int tusd_property_set_variability(tusd_property_t *property, tusd_variability_t variability) {
    if (!property) return 0;
    property->variability = variability;
    return 1;
}

tusd_variability_t tusd_property_get_variability(const tusd_property_t *property) {
    return property ? property->variability : TUSD_VARIABILITY_VARYING;
}

int tusd_property_add_target(tusd_property_t *property, const char *target_path) {
    if (!property || !target_path) return 0;
    
    /* Reallocate targets array */
    char **new_targets = (char**)realloc(property->target_paths, 
                                        (property->target_count + 1) * sizeof(char*));
    if (!new_targets) return 0;
    
    property->target_paths = new_targets;
    property->target_paths[property->target_count] = tusd_strdup(target_path);
    
    if (!property->target_paths[property->target_count]) return 0;
    
    property->target_count++;
    return 1;
}

size_t tusd_property_get_target_count(const tusd_property_t *property) {
    return property ? property->target_count : 0;
}

const char *tusd_property_get_target(const tusd_property_t *property, size_t index) {
    if (!property || index >= property->target_count) return NULL;
    return property->target_paths[index];
}

/* ===== PrimSpec Implementation ===== */

tusd_primspec_t *tusd_primspec_create(const char *name, const char *type_name, 
                                      tusd_specifier_t specifier) {
    if (!name) return NULL;
    
    tusd_primspec_t *primspec = (tusd_primspec_t*)calloc(1, sizeof(tusd_primspec_t));
    if (!primspec) return NULL;
    
    primspec->name = tusd_strdup(name);
    if (!primspec->name) {
        tusd_primspec_destroy(primspec);
        return NULL;
    }
    
    if (type_name) {
        primspec->type_name = tusd_strdup(type_name);
        if (!primspec->type_name) {
            tusd_primspec_destroy(primspec);
            return NULL;
        }
    }
    
    primspec->specifier = specifier;
    
    /* Create maps */
    primspec->properties = tusd_map_create(tusd_property_destructor);
    primspec->children = tusd_map_create(tusd_primspec_destructor);
    primspec->metadata = tusd_map_create(tusd_value_destructor);
    primspec->variant_sets = tusd_map_create(tusd_value_destructor); /* TODO: Better destructor */
    
    if (!primspec->properties || !primspec->children || 
        !primspec->metadata || !primspec->variant_sets) {
        tusd_primspec_destroy(primspec);
        return NULL;
    }
    
    return primspec;
}

void tusd_primspec_destroy(tusd_primspec_t *primspec) {
    if (!primspec) return;
    
    free(primspec->name);
    free(primspec->type_name);
    free(primspec->doc);
    free(primspec->comment);
    
    tusd_map_destroy(primspec->properties);
    tusd_map_destroy(primspec->children);
    tusd_map_destroy(primspec->metadata);
    tusd_map_destroy(primspec->variant_sets);
    
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

void tusd_primspec_destructor(void *primspec) {
    tusd_primspec_destroy((tusd_primspec_t*)primspec);
}

int tusd_primspec_add_property(tusd_primspec_t *primspec, tusd_property_t *property) {
    if (!primspec || !property || !property->name) return 0;
    
    return tusd_map_set(primspec->properties, property->name, property);
}

tusd_property_t *tusd_primspec_get_property(tusd_primspec_t *primspec, const char *name) {
    if (!primspec || !name) return NULL;
    
    return (tusd_property_t*)tusd_map_get(primspec->properties, name);
}

tusd_map_t *tusd_primspec_get_properties(tusd_primspec_t *primspec) {
    return primspec ? primspec->properties : NULL;
}

int tusd_primspec_add_child(tusd_primspec_t *primspec, tusd_primspec_t *child) {
    if (!primspec || !child || !child->name) return 0;
    
    return tusd_map_set(primspec->children, child->name, child);
}

tusd_primspec_t *tusd_primspec_get_child(tusd_primspec_t *primspec, const char *name) {
    if (!primspec || !name) return NULL;
    
    return (tusd_primspec_t*)tusd_map_get(primspec->children, name);
}

tusd_map_t *tusd_primspec_get_children(tusd_primspec_t *primspec) {
    return primspec ? primspec->children : NULL;
}

int tusd_primspec_set_doc(tusd_primspec_t *primspec, const char *doc) {
    if (!primspec) return 0;
    
    free(primspec->doc);
    primspec->doc = doc ? tusd_strdup(doc) : NULL;
    return 1;
}

const char *tusd_primspec_get_doc(const tusd_primspec_t *primspec) {
    return primspec ? primspec->doc : NULL;
}

int tusd_primspec_set_comment(tusd_primspec_t *primspec, const char *comment) {
    if (!primspec) return 0;
    
    free(primspec->comment);
    primspec->comment = comment ? tusd_strdup(comment) : NULL;
    return 1;
}

const char *tusd_primspec_get_comment(const tusd_primspec_t *primspec) {
    return primspec ? primspec->comment : NULL;
}

/* ===== Layer Implementation ===== */

tusd_layer_t *tusd_layer_create(const char *name) {
    tusd_layer_t *layer = (tusd_layer_t*)calloc(1, sizeof(tusd_layer_t));
    if (!layer) return NULL;
    
    if (name) {
        layer->name = tusd_strdup(name);
        if (!layer->name) {
            tusd_layer_destroy(layer);
            return NULL;
        }
    }
    
    /* Initialize metadata */
    layer->metas.meters_per_unit = 1.0;
    layer->metas.time_codes_per_second = 24.0;
    layer->metas.start_time_code = 1.0;
    layer->metas.end_time_code = 1.0;
    layer->metas.custom_data = tusd_map_create(tusd_value_destructor);
    
    /* Create maps */
    layer->primspecs = tusd_map_create(tusd_primspec_destructor);
    
    if (!layer->metas.custom_data || !layer->primspecs) {
        tusd_layer_destroy(layer);
        return NULL;
    }
    
    return layer;
}

void tusd_layer_destroy(tusd_layer_t *layer) {
    if (!layer) return;
    
    free(layer->name);
    free(layer->file_path);
    free(layer->metas.doc);
    free(layer->metas.comment);
    
    if (layer->metas.up_axis.type == TUSD_VALUE_STRING) {
        free(layer->metas.up_axis.data.string_val);
    } else if (layer->metas.up_axis.type == TUSD_VALUE_TOKEN) {
        free(layer->metas.up_axis.data.token_val);
    }
    
    tusd_map_destroy(layer->metas.custom_data);
    tusd_map_destroy(layer->primspecs);
    
    /* Free sublayers array */
    if (layer->sublayers) {
        for (size_t i = 0; i < layer->sublayer_count; i++) {
            free(layer->sublayers[i]);
        }
        free(layer->sublayers);
    }
    
    free(layer);
}

int tusd_layer_set_file_path(tusd_layer_t *layer, const char *file_path) {
    if (!layer) return 0;
    
    free(layer->file_path);
    layer->file_path = file_path ? tusd_strdup(file_path) : NULL;
    return 1;
}

const char *tusd_layer_get_file_path(const tusd_layer_t *layer) {
    return layer ? layer->file_path : NULL;
}

int tusd_layer_add_primspec(tusd_layer_t *layer, tusd_primspec_t *primspec) {
    if (!layer || !primspec || !primspec->name) return 0;
    
    return tusd_map_set(layer->primspecs, primspec->name, primspec);
}

tusd_primspec_t *tusd_layer_get_primspec(tusd_layer_t *layer, const char *name) {
    if (!layer || !name) return NULL;
    
    return (tusd_primspec_t*)tusd_map_get(layer->primspecs, name);
}

tusd_map_t *tusd_layer_get_primspecs(tusd_layer_t *layer) {
    return layer ? layer->primspecs : NULL;
}

int tusd_layer_set_doc(tusd_layer_t *layer, const char *doc) {
    if (!layer) return 0;
    
    free(layer->metas.doc);
    layer->metas.doc = doc ? tusd_strdup(doc) : NULL;
    return 1;
}

const char *tusd_layer_get_doc(const tusd_layer_t *layer) {
    return layer ? layer->metas.doc : NULL;
}

int tusd_layer_set_up_axis(tusd_layer_t *layer, const char *axis) {
    if (!layer) return 0;
    
    /* Clean up previous value */
    if (layer->metas.up_axis.type == TUSD_VALUE_STRING) {
        free(layer->metas.up_axis.data.string_val);
    } else if (layer->metas.up_axis.type == TUSD_VALUE_TOKEN) {
        free(layer->metas.up_axis.data.token_val);
    }
    
    if (axis) {
        layer->metas.up_axis.type = TUSD_VALUE_TOKEN;
        layer->metas.up_axis.data.token_val = tusd_strdup(axis);
        return layer->metas.up_axis.data.token_val != NULL;
    } else {
        layer->metas.up_axis.type = TUSD_VALUE_NONE;
        return 1;
    }
}

const char *tusd_layer_get_up_axis(const tusd_layer_t *layer) {
    if (!layer) return NULL;
    
    if (layer->metas.up_axis.type == TUSD_VALUE_TOKEN) {
        return layer->metas.up_axis.data.token_val;
    } else if (layer->metas.up_axis.type == TUSD_VALUE_STRING) {
        return layer->metas.up_axis.data.string_val;
    }
    
    return NULL;
}

int tusd_layer_set_meters_per_unit(tusd_layer_t *layer, double meters_per_unit) {
    if (!layer) return 0;
    
    layer->metas.meters_per_unit = meters_per_unit;
    return 1;
}

double tusd_layer_get_meters_per_unit(const tusd_layer_t *layer) {
    return layer ? layer->metas.meters_per_unit : 1.0;
}

/* ===== Utility Functions ===== */

const char *tusd_specifier_to_string(tusd_specifier_t spec) {
    switch (spec) {
        case TUSD_SPEC_OVER: return "over";
        case TUSD_SPEC_DEF: return "def";
        case TUSD_SPEC_CLASS: return "class";
        default: return "unknown";
    }
}

const char *tusd_property_type_to_string(tusd_property_type_t type) {
    switch (type) {
        case TUSD_PROP_EMPTY_ATTRIB: return "empty_attrib";
        case TUSD_PROP_ATTRIB: return "attrib";
        case TUSD_PROP_RELATION: return "relation";
        case TUSD_PROP_NO_TARGETS_RELATION: return "no_targets_relation";
        case TUSD_PROP_CONNECTION: return "connection";
        default: return "unknown";
    }
}

const char *tusd_variability_to_string(tusd_variability_t variability) {
    switch (variability) {
        case TUSD_VARIABILITY_VARYING: return "varying";
        case TUSD_VARIABILITY_UNIFORM: return "uniform";
        case TUSD_VARIABILITY_CONFIG: return "config";
        default: return "unknown";
    }
}