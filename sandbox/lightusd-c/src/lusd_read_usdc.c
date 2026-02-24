/*
 * lusd_read_usdc.c - USDC binary file loader (dispatch wrapper)
 *
 * Reads a USDC file from disk into memory and dispatches to
 * lusd__layer_read_usdc() which parses the binary sections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal/lusd_layer_internal.h"
#include "lightusd/lusd_result.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * lusd__read_usdc_file
 *
 * Opens `path`, reads the entire file into a heap-allocated buffer, and
 * calls lusd__layer_read_usdc() to parse the USDC binary sections.
 *
 * On success:
 *   layer->file_data      = buffer (owned)
 *   layer->file_size      = file byte count
 *   layer->owns_file_data = true
 *   All table fields (tokens, strings, paths, fields, fieldsets, specs)
 *   are populated.
 *
 * On failure:
 *   Any partially-allocated tables are released via lusd__layer_free_tables().
 *   The buffer is freed; layer->file_data is NULL.
 *   Returns an appropriate LusdResult error code.
 */
LusdResult lusd__read_usdc_file(LusdLayer_T* layer, const char* path) {
    if (!layer || !path) return LUSD_ERROR_INVALID_ARGUMENT;

    FILE* f = fopen(path, "rb");
    if (!f) return LUSD_ERROR_FILE_NOT_FOUND;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return LUSD_ERROR_IO_FAILED; }

    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return LUSD_ERROR_IO_FAILED; }
    rewind(f);

    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return LUSD_ERROR_OUT_OF_MEMORY; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        free(buf);
        fclose(f);
        return LUSD_ERROR_IO_FAILED;
    }
    fclose(f);

    /* Parse USDC binary; the reader stores data pointer in layer->file_data */
    LusdResult res = lusd__layer_read_usdc(layer, buf, (uint64_t)sz);
    if (res != LUSD_SUCCESS) {
        /* Clean up any tables the reader may have partially populated */
        lusd__layer_free_tables(layer);
        free(buf);
        return res;
    }

    /* Reader set layer->file_data = buf; mark ownership here */
    layer->owns_file_data = true;
    return LUSD_SUCCESS;
}
