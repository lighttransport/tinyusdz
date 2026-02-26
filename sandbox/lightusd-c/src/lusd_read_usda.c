/*
 * lusd_read_usda.c - USDA ASCII file loader (dispatch wrapper)
 *
 * Reads a USDA file from disk into memory and dispatches to
 * lusd__layer_read_usda() which parses the ASCII text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal/lusd_layer_internal.h"
#include "lightusd/lusd_result.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * lusd__read_usda_file
 *
 * Opens `path`, reads the entire file into a heap-allocated buffer, and
 * calls lusd__layer_read_usda() to parse the ASCII text.
 *
 * On success:
 *   layer->file_data      = buffer (owned)
 *   layer->file_size      = file byte count
 *   layer->owns_file_data = true
 *   All table fields are populated.
 *
 * On failure:
 *   Any partially-allocated tables are released via lusd__layer_free_tables().
 *   The buffer is freed; layer->file_data is NULL.
 */
LusdResult lusd__read_usda_file(LusdLayer_T* layer, const char* path) {
    if (!layer || !path) return LUSD_ERROR_INVALID_ARGUMENT;

    FILE* f = fopen(path, "rb");
    if (!f) return LUSD_ERROR_FILE_NOT_FOUND;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return LUSD_ERROR_IO_FAILED; }

    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return LUSD_ERROR_IO_FAILED; }
    rewind(f);

    uint8_t* buf = (uint8_t*)malloc((size_t)sz + 1); /* +1 for NUL sentinel */
    if (!buf) { fclose(f); return LUSD_ERROR_OUT_OF_MEMORY; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        free(buf);
        fclose(f);
        return LUSD_ERROR_IO_FAILED;
    }
    fclose(f);
    buf[sz] = '\0'; /* null-terminate for safer string ops */

    LusdResult res = lusd__layer_read_usda(layer, buf, (uint64_t)sz);
    if (res != LUSD_SUCCESS) {
        lusd__layer_free_tables(layer);
        free(buf);
        return res;
    }

    layer->owns_file_data = true;
    return LUSD_SUCCESS;
}
