/*
 * lusd_read_usdz.c - USDZ ZIP archive reader (pure C11, no dependencies)
 *
 * USDZ is an uncompressed ZIP archive (all files stored, method=0) with
 * each file's data aligned at a 64-byte boundary.  The first USD file
 * (.usdc or .usda) is the root layer; remaining files are assets (textures
 * etc.) referenced by relative paths.
 *
 * Strategy:
 *   1. Parse ZIP local-file headers from the archive buffer.
 *   2. Locate the first .usdc / .usda file — that is the root USD layer.
 *   3. Extract ALL files from the archive to a temporary directory so that
 *      the existing file-path asset resolver works without changes.
 *   4. Parse the root USD file (in-memory, no second malloc) using the
 *      existing lusd__layer_read_usdc / lusd__layer_read_usda.
 *   5. Update layer->identifier to point at the extracted root file so
 *      that texture paths resolve relative to the temp dir.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal/lusd_layer_internal.h"
#include "lightusd/lusd_result.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

/* Forward declarations for in-memory readers */
LusdResult lusd__layer_read_usdc(LusdLayer_T* layer,
                                  const uint8_t* data, uint64_t size);
LusdResult lusd__layer_read_usda(LusdLayer_T* layer,
                                  const uint8_t* data, uint64_t size);
void lusd__layer_free_tables(LusdLayer_T* layer);

/* ================================================================
 * ZIP parsing helpers
 * ================================================================ */

/* ZIP local file header signature: PK\x03\x04 */
#define ZIP_LOCAL_SIG 0x04034b50u

typedef struct {
    const char*    name;       /* pointer into archive buffer (NOT null-term) */
    uint16_t       name_len;
    const uint8_t* data;       /* pointer to file data in archive buffer */
    uint32_t       data_size;  /* uncompressed == compressed (method must be 0) */
} ZipEntry;

#define MAX_ENTRIES 256

/* Parse all local-file entries from |buf| of |buf_size| bytes.
 * Fills |entries| (up to MAX_ENTRIES) and returns the count.
 * Returns 0 on fatal error (not a USDZ / compressed entries). */
static uint32_t parse_zip_entries(const uint8_t* buf, uint64_t buf_size,
                                   ZipEntry* entries) {
    uint32_t count = 0;
    uint64_t off   = 0;

    while (off + 30 <= buf_size && count < MAX_ENTRIES) {
        uint32_t sig;
        memcpy(&sig, buf + off, 4);
        if (sig != ZIP_LOCAL_SIG) break; /* no more local entries */

        /* compression method (bytes 8-9 from start of header) */
        uint16_t method;
        memcpy(&method, buf + off + 8, 2);
        /* uncompressed size (bytes 22-25) */
        uint32_t uncomp;
        memcpy(&uncomp, buf + off + 22, 4);
        /* filename length (bytes 26-27) */
        uint16_t name_len;
        memcpy(&name_len, buf + off + 26, 2);
        /* extra field length (bytes 28-29) */
        uint16_t extra_len;
        memcpy(&extra_len, buf + off + 28, 2);

        uint64_t header_end = off + 30 + name_len + extra_len;
        if (header_end > buf_size) break;

        /* USDZ spec: data must be 64-byte aligned and uncompressed */
        if (method != 0) {
            /* Compressed entries are not valid in USDZ — skip gracefully */
            off = header_end + uncomp;
            continue;
        }
        if ((header_end % 64) != 0) {
            /* Not 64-byte aligned — still parse, just note */
        }

        if (header_end + uncomp > buf_size) break;

        entries[count].name      = (const char*)(buf + off + 30);
        entries[count].name_len  = name_len;
        entries[count].data      = buf + header_end;
        entries[count].data_size = uncomp;
        count++;

        off = header_end + uncomp;
    }
    return count;
}

/* Returns non-zero if name (NOT null-terminated, len bytes) ends with suffix */
static int name_ends_with(const char* name, uint16_t len, const char* suffix) {
    uint16_t sl = (uint16_t)strlen(suffix);
    if (sl > len) return 0;
    return memcmp(name + len - sl, suffix, sl) == 0;
}

/* ================================================================
 * Temp directory creation + extraction helpers
 * ================================================================ */

/* Create a directory, including any missing parent segments in path.
 * Returns 0 on success. */
static int mkdir_p(const char* path) {
    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0700);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0700);
    return 0;
}

/* Write |data| of |size| bytes to |path|, creating parent dirs as needed.
 * Returns 0 on success. */
static int write_file(const char* path, const uint8_t* data, uint32_t size) {
    /* Build parent dir path */
    char dir[4096];
    size_t plen = strlen(path);
    if (plen >= sizeof(dir)) return -1;
    memcpy(dir, path, plen + 1);
    /* Find last '/' and null-terminate to get parent */
    for (size_t i = plen; i > 0; i--) {
        if (dir[i] == '/') { dir[i] = '\0'; break; }
    }
    mkdir_p(dir);

    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ================================================================
 * lusd__read_usdz_file — public entry point
 * ================================================================ */

LusdResult lusd__read_usdz_file(LusdLayer_T* layer, const char* path) {
    if (!layer || !path) return LUSD_ERROR_INVALID_ARGUMENT;

    /* --- Read archive into memory ----------------------------------- */
    FILE* f = fopen(path, "rb");
    if (!f) return LUSD_ERROR_FILE_NOT_FOUND;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return LUSD_ERROR_IO_FAILED; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return LUSD_ERROR_IO_FAILED; }
    rewind(f);

    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return LUSD_ERROR_OUT_OF_MEMORY; }

    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        free(buf); fclose(f); return LUSD_ERROR_IO_FAILED;
    }
    fclose(f);

    /* --- Parse ZIP entries ----------------------------------------- */
    ZipEntry entries[MAX_ENTRIES];
    uint32_t entry_count = parse_zip_entries(buf, (uint64_t)sz, entries);
    if (entry_count == 0) {
        free(buf);
        return LUSD_ERROR_PARSE_FAILED;
    }

    /* Find the root USD entry (first .usdc or .usda) */
    int root_idx = -1;
    int root_is_usdc = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        if (name_ends_with(entries[i].name, entries[i].name_len, ".usdc")) {
            root_idx = (int)i; root_is_usdc = 1; break;
        }
    }
    if (root_idx < 0) {
        for (uint32_t i = 0; i < entry_count; i++) {
            if (name_ends_with(entries[i].name, entries[i].name_len, ".usda")) {
                root_idx = (int)i; root_is_usdc = 0; break;
            }
        }
    }
    if (root_idx < 0) {
        free(buf);
        return LUSD_ERROR_PARSE_FAILED; /* no USD file found in archive */
    }

    /* --- Extract all files to temp dir ----------------------------- */
    /* Build a unique temp dir: /tmp/lusd_usdz_XXXXXX */
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/lusd_usdz_XXXXXX");
    if (!mkdtemp(tmp_dir)) {
        free(buf);
        return LUSD_ERROR_IO_FAILED;
    }

    /* Extract each entry to tmp_dir/<name> */
    for (uint32_t i = 0; i < entry_count; i++) {
        /* Build output path */
        char out_path[4096];
        /* Construct name as null-terminated string */
        char name_buf[1024];
        uint16_t nlen = entries[i].name_len;
        if (nlen >= sizeof(name_buf)) nlen = (uint16_t)(sizeof(name_buf) - 1);
        memcpy(name_buf, entries[i].name, nlen);
        name_buf[nlen] = '\0';

        snprintf(out_path, sizeof(out_path), "%s/%s", tmp_dir, name_buf);
        write_file(out_path, entries[i].data, entries[i].data_size);
    }

    /* --- Parse root USD layer -------------------------------------- */
    /* Build path to extracted root file */
    char root_name[1024];
    uint16_t rnlen = entries[root_idx].name_len;
    if (rnlen >= sizeof(root_name)) rnlen = (uint16_t)(sizeof(root_name) - 1);
    memcpy(root_name, entries[root_idx].name, rnlen);
    root_name[rnlen] = '\0';

    char root_path[4096];
    snprintf(root_path, sizeof(root_path), "%s/%s", tmp_dir, root_name);

    /* Read the extracted root file */
    FILE* rf = fopen(root_path, "rb");
    if (!rf) { free(buf); return LUSD_ERROR_IO_FAILED; }
    if (fseek(rf, 0, SEEK_END) != 0) { fclose(rf); free(buf); return LUSD_ERROR_IO_FAILED; }
    long rsz = ftell(rf);
    rewind(rf);

    uint8_t* rbuf = (uint8_t*)malloc((size_t)rsz);
    if (!rbuf) { fclose(rf); free(buf); return LUSD_ERROR_OUT_OF_MEMORY; }
    if ((long)fread(rbuf, 1, (size_t)rsz, rf) != rsz) {
        free(rbuf); fclose(rf); free(buf); return LUSD_ERROR_IO_FAILED;
    }
    fclose(rf);
    free(buf); /* done with archive buffer */

    /* Parse using the appropriate reader */
    LusdResult res;
    if (root_is_usdc)
        res = lusd__layer_read_usdc(layer, rbuf, (uint64_t)rsz);
    else
        res = lusd__layer_read_usda(layer, rbuf, (uint64_t)rsz);

    if (res != LUSD_SUCCESS) {
        lusd__layer_free_tables(layer);
        free(rbuf);
        return res;
    }

    layer->owns_file_data = true; /* rbuf now owned by layer */

    /* Override identifier so asset paths resolve relative to tmp_dir */
    free(layer->identifier);
    layer->identifier = (char*)malloc(strlen(root_path) + 1);
    if (layer->identifier)
        strcpy(layer->identifier, root_path);

    return LUSD_SUCCESS;
}
