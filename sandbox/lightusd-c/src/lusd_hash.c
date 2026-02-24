/*
 * lusd_hash.c - Hash functions for token pool and internal tables
 *
 * Uses FNV-1a hash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal/lusd_internal.h"

#define FNV_OFFSET_BASIS 14695981039346656037ULL
#define FNV_PRIME        1099511628211ULL

uint64_t lusd_hash_bytes(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)p[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

uint64_t lusd_hash_string(const char* s) {
    if (!s) return 0;
    uint64_t hash = FNV_OFFSET_BASIS;
    while (*s) {
        hash ^= (uint64_t)(uint8_t)*s;
        hash *= FNV_PRIME;
        s++;
    }
    return hash;
}
