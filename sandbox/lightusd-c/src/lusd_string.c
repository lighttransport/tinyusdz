/*
 * lusd_string.c - Internal string utilities
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal/lusd_internal.h"
#include <string.h>

int lusd_strcmp(const char* a, const char* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcmp(a, b);
}
