/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-Present Light Transport Entertainment Inc.
 *
 * value.c -- C11 implementation of the lu_value semi-any core.
 *
 * Adopted type descriptor (lu_type_id + lu_type_size):
 *   STRING/ARRAY/LAZY/DICT/BOX kinds are handled by the C++ payload hooks;
 *   POD and BOX storage live entirely here.
 */

#include "lightusd-core-c.h"

#include <stdlib.h>
#include <string.h>

#define LU_VALUE_INLINE_CAP 40

typedef struct lu_value_box {
  size_t size;
  unsigned char data[1];
} lu_value_box;

static const lu_value_payload_ops* s_payload_ops = NULL;

void lu_value_set_payload_ops(const lu_value_payload_ops* ops) {
  s_payload_ops = ops;
}

lu_value_kind lu_value_kind_of(uint16_t type) {
  if (type == LU_TYPE_DICTIONARY) return LU_VALUE_DICT;
  if (lu_value_uses_string_storage(type)) return LU_VALUE_STRING;
  /* arrays share their backing kind but stay array kind at runtime; type kind is POD carrier */
  {
    size_t sz = lu_type_size(type);
    if (sz == 0) return LU_VALUE_EMPTY;
    if (sz <= LU_VALUE_INLINE_CAP) return LU_VALUE_POD;
    return LU_VALUE_BOX;
  }
}

/* Note: Lazy arrays are selected at runtime by C++ (kind LAZY); TypeId alone does not imply LAZY. */

void lu_value_init(lu_value* value, uint16_t type) {
  if (!value) return;
  value->type = type;
  if (type == LU_TYPE_INVALID) value->kind = LU_VALUE_EMPTY;
  else value->kind = (uint8_t)lu_value_kind_of(type);
  value->flags = 0;
  value->size = 0;
  memset(value->store.bytes, 0, LU_VALUE_INLINE_CAP);
}

void lu_value_init_empty(lu_value* value) {
  if (!value) return;
  value->type = LU_TYPE_INVALID;
  value->kind = (uint8_t)LU_VALUE_EMPTY;
  value->flags = 0;
  value->size = 0;
  memset(value->store.bytes, 0, LU_VALUE_INLINE_CAP);
}

void lu_value_init_block(lu_value* value) {
  if (!value) return;
  value->type = LU_TYPE_INVALID;
  value->kind = (uint8_t)LU_VALUE_BLOCK;
  value->flags = 0;
  value->size = 0;
  memset(value->store.bytes, 0, LU_VALUE_INLINE_CAP);
}

static int lu_value_has_payload(const lu_value* v) {
  return v->kind == LU_VALUE_STRING || v->kind == LU_VALUE_ARRAY ||
         v->kind == LU_VALUE_LAZY_ARRAY || v->kind == LU_VALUE_DICT;
}

static size_t lu_value_box_offset(void) { return 0; }

static lu_value_box* lu_value_box_of(lu_value* v) {
  lu_value_box* b = NULL;
  memcpy(&b, v->store.bytes, sizeof(b));
  return b;
}

static const lu_value_box* lu_value_box_c(const lu_value* v) {
  lu_value_box* b = NULL;
  memcpy(&b, v->store.bytes, sizeof(b));
  return b;
}

void lu_value_destroy(lu_value* value) {
  if (!value) return;
  if (lu_value_has_payload(value)) {
    if (s_payload_ops && s_payload_ops->destroy) s_payload_ops->destroy(value);
    lu_value_init_empty(value);
    return;
  }
  if (value->kind == (uint8_t)LU_VALUE_BOX) {
    lu_value_box* b = lu_value_box_of(value);
    free(b);
    lu_value_init_empty(value);
  } else if (value->kind == (uint8_t)LU_VALUE_ARRAY ||
             value->kind == (uint8_t)LU_VALUE_LAZY_ARRAY) {
    if (s_payload_ops && s_payload_ops->destroy) s_payload_ops->destroy(value);
    lu_value_init_empty(value);
  }
}

void lu_value_copy(lu_value* dst, const lu_value* src) {
  if (!dst || !src) return;
  lu_value_destroy(dst);
  dst->type = src->type;
  dst->kind = src->kind;
  dst->flags = src->flags;
  dst->size = src->size;
  if (src->kind == (uint8_t)LU_VALUE_BOX) {
    const lu_value_box* sb = lu_value_box_c(src);
    if (sb) {
      lu_value_box* nb = (lu_value_box*)malloc(sizeof(lu_value_box) - 1 + sb->size);
      if (nb) {
        nb->size = sb->size;
        memcpy(nb->data, sb->data, sb->size);
        memcpy(dst->store.bytes, &nb, sizeof(nb));
      } else {
        dst->kind = (uint8_t)LU_VALUE_EMPTY;
        dst->type = LU_TYPE_INVALID;
        return;
      }
    } else {
      memset(dst->store.bytes, 0, LU_VALUE_INLINE_CAP);
    }
    return;
  }
  if (lu_value_has_payload(src)) {
    memset(dst->store.bytes, 0, LU_VALUE_INLINE_CAP);
    if (s_payload_ops && s_payload_ops->copy) s_payload_ops->copy(dst, src);
    return;
  }
  memcpy(dst->store.bytes, src->store.bytes, LU_VALUE_INLINE_CAP);
  if (dst->kind == (uint8_t)LU_VALUE_POD) {
    size_t sz = lu_type_size(src->type);
    if (sz > 0 && sz <= LU_VALUE_INLINE_CAP) memcpy(dst->store.bytes, src->store.bytes, sz);
  }
}

void lu_value_move(lu_value* dst, lu_value* src) {
  if (!dst || !src) return;
  lu_value_destroy(dst);
  dst->type = src->type;
  dst->kind = src->kind;
  dst->flags = src->flags;
  dst->size = src->size;
  memcpy(dst->store.bytes, src->store.bytes, LU_VALUE_INLINE_CAP);
  src->type = LU_TYPE_INVALID;
  src->kind = (uint8_t)LU_VALUE_EMPTY;
  src->flags = 0;
  src->size = 0;
  memset(src->store.bytes, 0, LU_VALUE_INLINE_CAP);
  if (lu_value_has_payload(dst)) {
    /* non-POD payload moved through hook */
    if (s_payload_ops && s_payload_ops->move) s_payload_ops->move(dst, src);
  }
}
