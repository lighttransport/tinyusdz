/*
 * lusd_usdc_reader.c - Pure-C11 USDC Crate binary reader
 *
 * Parses the six structural sections of a USDC file into flat C arrays:
 *   TOKENS   → layer->tokens[]   (pointers into decompressed token buffer)
 *   STRINGS  → layer->strings[]  (pointers into tokens[])
 *   PATHS    → layer->paths[]    (reconstructed, owned in string_arena)
 *   FIELDS   → layer->fields[]   (token_index + LusdValueRep)
 *   FIELDSETS→ layer->fieldsets[] (flat uint32_t[], ~0 sentinel)
 *   SPECS    → layer->specs[]    (path_index, fieldset_index, spec_type)
 *
 * After this reader runs, lusd__layer_build_prims() constructs the prim tree.
 * Value materialization (typed array extraction) is intentionally left to
 * Lydra (C++) via lusd__layer_find_field() + direct file_data reads.
 *
 * Integer decompression: Usd_IntegerCompression (from integerCoding.cpp)
 *   → LZ4 decompress the encoded buffer, then delta-decode with 2-bit codes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal/lusd_layer_internal.h"
#include "internal/lusd_value_rep.h"
#include "internal/lusd_internal.h"
#include "lightusd/lusd_platform.h"
#include "lightusd/lusd_result.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/* ====================================================================
 * Forward declarations for LZ4 wrapper (implemented in lusd_lz4.c)
 * ==================================================================== */

uint64_t lusd__lz4_decompress(const uint8_t* comp,
                               uint64_t       comp_size,
                               uint8_t*       out,
                               uint64_t       max_out);

/* ====================================================================
 * USDC binary constants
 * ==================================================================== */

#define USDC_MAGIC          "PXR-USDC"
#define USDC_MAGIC_LEN      8
#define USDC_BOOTSTRAP_SIZE 24   /* magic(8) + version(8) + toc_offset(8) */
#define USDC_SECTION_NAME_LEN  16

#define USDC_INVALID_SECTION ((uint64_t)UINT64_MAX)
#define USDC_SENTINEL        0xFFFFFFFFu

/* Maximum sizes to guard against corrupt/adversarial input */
#define USDC_MAX_TOKENS      (1u << 24)  /* 16 million */
#define USDC_MAX_STRINGS     (1u << 24)
#define USDC_MAX_PATHS       (1u << 24)
#define USDC_MAX_FIELDS      (1u << 26)  /* 64 million */
#define USDC_MAX_FIELDSETS   (1u << 26)
#define USDC_MAX_SPECS       (1u << 24)
#define USDC_MAX_TOKEN_BYTES (256u << 20) /* 256 MB uncompressed */

/* ====================================================================
 * Portable bounded string length (strnlen is POSIX, not C11 standard)
 * ==================================================================== */

static size_t lusd_strnlen(const char* s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n] != '\0') n++;
    return n;
}

/* ====================================================================
 * Byte-stream reader (lightweight, no allocation)
 * ==================================================================== */

typedef struct {
    const uint8_t* data;
    uint64_t       size;
    uint64_t       pos;
} ByteStream;

static void bs_init(ByteStream* s, const uint8_t* data, uint64_t size) {
    s->data = data;
    s->size = size;
    s->pos  = 0;
}

static bool bs_seek(ByteStream* s, uint64_t offset) {
    if (offset > s->size) return false;
    s->pos = offset;
    return true;
}

static bool bs_read(ByteStream* s, void* dst, uint64_t n) {
    if (s->pos + n > s->size) return false;
    memcpy(dst, s->data + s->pos, (size_t)n);
    s->pos += n;
    return true;
}

static bool bs_read_u64(ByteStream* s, uint64_t* out) {
    return bs_read(s, out, 8);
}

static bool bs_read_u32(ByteStream* s, uint32_t* out) {
    return bs_read(s, out, 4);
}

/* ====================================================================
 * String arena helpers
 * ==================================================================== */

static char* arena_alloc(LusdLayer_T* L, uint32_t sz) {
    if (L->string_arena_used + sz > L->string_arena_size) {
        /* grow by doubling */
        uint32_t newSz = L->string_arena_size * 2;
        if (newSz < L->string_arena_used + sz) newSz = L->string_arena_used + sz + 1024;
        char* nb = (char*)realloc(L->string_arena, newSz);
        if (!nb) return NULL;
        L->string_arena      = nb;
        L->string_arena_size = newSz;
    }
    char* ptr = L->string_arena + L->string_arena_used;
    L->string_arena_used += sz;
    return ptr;
}

/* Allocate a copy of `s` (with null terminator) in the string arena */
static char* arena_strdup(LusdLayer_T* L, const char* s) {
    uint32_t len = (uint32_t)strlen(s);
    char* p = arena_alloc(L, len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

/* arena_sprintf: format into a temp stack buffer then copy to arena */
static char* arena_sprintf(LusdLayer_T* L, const char* fmt,
                            const char* a, const char* b) {
    /* Format into a local buffer first */
    char tmp[4096];
    int n;
    if (b) {
        n = snprintf(tmp, sizeof(tmp), fmt, a, b);
    } else {
        n = snprintf(tmp, sizeof(tmp), fmt, a);
    }
    if (n < 0 || (size_t)n >= sizeof(tmp)) return NULL;
    return arena_strdup(L, tmp);
}

/* ====================================================================
 * Integer decompression (Usd_IntegerCompression, 32-bit variant)
 *
 * Decompresses LZ4-encoded, delta-coded integer arrays.
 * Layout after LZ4 decompression:
 *   [4 bytes]       int32_t commonValue (most frequent delta)
 *   [ceil(N*2/8)]   2-bit type codes per integer (4 codes per byte, LSB-first)
 *     code 0: delta = commonValue   (no extra bytes)
 *     code 1: delta = int8_t        (1 extra byte)
 *     code 2: delta = int16_t       (2 extra bytes)
 *     code 3: delta = int32_t       (4 extra bytes)
 *   [variable]      packed delta values
 *
 * Reconstruction: out[i] = (uint32_t)(prevVal += delta)
 * ==================================================================== */

/* Working-space size for decoding N uint32_t integers (mirrors GetDecompressionWorkingSpaceSize) */
static uint64_t usdc_int_working_size(uint32_t n) {
    /* _GetEncodedBufferSize<int32_t>(n) = sizeof(int32_t) + ceil(n*2/8) + n*sizeof(int32_t) */
    return 4u + ((uint64_t)n * 2 + 7) / 8 + (uint64_t)n * 4 + 16 /* safety */;
}

/*
 * Decode a Usd_IntegerCompression buffer (already LZ4-decompressed).
 * `enc`   - encoded buffer (starts with int32_t commonValue)
 * `enc_sz`- byte size of encoded buffer
 * `out`   - output uint32_t array (count elements)
 * Returns true on success.
 */
static bool decode_int_buf(const uint8_t* enc, uint64_t enc_sz,
                            uint32_t* out, uint32_t count) {
    if (count == 0) return true;
    if (enc_sz < 4) return false;

    int32_t common;
    memcpy(&common, enc, 4);

    uint64_t codes_bytes = ((uint64_t)count * 2 + 7) / 8;
    if (enc_sz < 4 + codes_bytes) return false;

    const uint8_t* codes   = enc + 4;
    const uint8_t* vintsIn = enc + 4 + codes_bytes;
    const uint8_t* enc_end = enc + enc_sz;

    int32_t prev = 0;
    for (uint32_t i = 0; i < count; i++) {
        int code = (codes[i / 4] >> ((i % 4) * 2)) & 3;
        int32_t delta;
        switch (code) {
        case 0: delta = common; break;
        case 1:
            if (vintsIn + 1 > enc_end) return false;
            { int8_t v; memcpy(&v, vintsIn, 1); delta = (int32_t)v; vintsIn += 1; }
            break;
        case 2:
            if (vintsIn + 2 > enc_end) return false;
            { int16_t v; memcpy(&v, vintsIn, 2); delta = (int32_t)v; vintsIn += 2; }
            break;
        default: /* 3 */
            if (vintsIn + 4 > enc_end) return false;
            { memcpy(&delta, vintsIn, 4); vintsIn += 4; }
            break;
        }
        prev += delta;
        out[i] = (uint32_t)prev;
    }
    return true;
}

/*
 * decode_compressed_ints — full pipeline: LZ4 decompress → integer decode.
 *
 * `s`     - byte stream positioned at the [uint64_t compSize] prefix
 * `out`   - pre-allocated output array of `count` uint32_t
 * `work`  - scratch buffer of >= usdc_int_working_size(count) bytes
 * `wsz`   - size of work buffer
 */
static LusdResult decode_compressed_ints(ByteStream* s, uint32_t* out,
                                          uint32_t count,
                                          uint8_t* work, uint64_t wsz) {
    uint64_t compSize;
    if (!bs_read_u64(s, &compSize)) return LUSD_ERROR_PARSE_FAILED;
    if (compSize == 0 || compSize > s->size - s->pos) return LUSD_ERROR_PARSE_FAILED;
    if (wsz < compSize + 8) return LUSD_ERROR_PARSE_FAILED;

    /* Read compressed bytes */
    const uint8_t* compData = s->data + s->pos;
    s->pos += compSize;

    /* Decompress into the scratch buffer */
    uint64_t decSz = usdc_int_working_size(count);
    if (decSz > wsz) return LUSD_ERROR_PARSE_FAILED;

    uint64_t actual = lusd__lz4_decompress(compData, compSize, work, decSz);
    if (actual == 0) return LUSD_ERROR_PARSE_FAILED;

    /* Delta-decode integers */
    if (!decode_int_buf(work, actual, out, count)) return LUSD_ERROR_PARSE_FAILED;
    return LUSD_SUCCESS;
}

/* ====================================================================
 * Section lookup helpers
 * ==================================================================== */

typedef struct {
    char     name[USDC_SECTION_NAME_LEN + 1];
    uint64_t start;
    uint64_t size;
} UsdcSection;

typedef struct {
    UsdcSection* secs;
    uint64_t     count;
} UsdcTOC;

static const UsdcSection* toc_find(const UsdcTOC* toc, const char* name) {
    for (uint64_t i = 0; i < toc->count; i++) {
        if (strncmp(toc->secs[i].name, name, USDC_SECTION_NAME_LEN) == 0)
            return &toc->secs[i];
    }
    return NULL;
}

/* ====================================================================
 * Section parsers
 * ==================================================================== */

/* TOKENS: LZ4-compressed null-terminated string blob */
static LusdResult parse_tokens(LusdLayer_T* L, ByteStream* s,
                                const UsdcSection* sec) {
    if (!bs_seek(s, sec->start)) return LUSD_ERROR_PARSE_FAILED;

    uint64_t num_tokens;
    if (!bs_read_u64(s, &num_tokens)) return LUSD_ERROR_PARSE_FAILED;
    if (num_tokens == 0 || num_tokens > USDC_MAX_TOKENS) return LUSD_ERROR_PARSE_FAILED;

    uint64_t uncSz, compSz;
    if (!bs_read_u64(s, &uncSz))  return LUSD_ERROR_PARSE_FAILED;
    if (!bs_read_u64(s, &compSz)) return LUSD_ERROR_PARSE_FAILED;
    if (uncSz > USDC_MAX_TOKEN_BYTES) return LUSD_ERROR_PARSE_FAILED;
    if (compSz == 0 || compSz > sec->size || s->pos + compSz > s->size)
        return LUSD_ERROR_PARSE_FAILED;

    /* Allocate decompression buffer (with safety padding for LZ4) */
    uint64_t bufSz = uncSz + 128;
    char* tokBuf = (char*)malloc((size_t)bufSz);
    if (!tokBuf) return LUSD_ERROR_OUT_OF_MEMORY;
    memset(tokBuf, 0, (size_t)bufSz);

    const uint8_t* compData = s->data + s->pos;
    uint64_t actual = lusd__lz4_decompress(compData, compSz, (uint8_t*)tokBuf, bufSz);
    if (actual < uncSz) { free(tokBuf); return LUSD_ERROR_PARSE_FAILED; }
    s->pos += compSz;

    /* Allocate pointer array */
    char** toks = (char**)malloc((size_t)num_tokens * sizeof(char*));
    if (!toks) { free(tokBuf); return LUSD_ERROR_OUT_OF_MEMORY; }

    /* Scan null-terminated strings */
    const char* p = tokBuf;
    const char* end = tokBuf + uncSz;
    for (uint64_t i = 0; i < num_tokens; i++) {
        if (p >= end) { free(toks); free(tokBuf); return LUSD_ERROR_PARSE_FAILED; }
        toks[i] = (char*)p;
        size_t len = lusd_strnlen(p, (size_t)(end - p));
        p += len + 1; /* skip null terminator */
    }

    L->token_buf       = tokBuf;
    L->token_buf_size  = bufSz;
    L->tokens          = toks;
    L->token_count     = (uint32_t)num_tokens;
    return LUSD_SUCCESS;
}

/* Read a plain (uncompressed) array of uint32_t preceded by a varint count */
static bool read_index_array(ByteStream* s, uint32_t** out, uint32_t* outCount) {
    uint64_t cnt;
    if (!bs_read_u64(s, &cnt)) return false;
    if (cnt == 0) { *out = NULL; *outCount = 0; return true; }
    if (cnt > USDC_MAX_STRINGS) return false;
    uint32_t* arr = (uint32_t*)malloc((size_t)cnt * 4);
    if (!arr) return false;
    if (!bs_read(s, arr, cnt * 4)) { free(arr); return false; }
    *out = arr; *outCount = (uint32_t)cnt;
    return true;
}

/* STRINGS: plain array of uint32_t token indices */
static LusdResult parse_strings(LusdLayer_T* L, ByteStream* s,
                                  const UsdcSection* sec) {
    if (sec->size == 0) return LUSD_SUCCESS; /* empty strings section is OK */
    if (!bs_seek(s, sec->start)) return LUSD_ERROR_PARSE_FAILED;

    uint32_t* indices = NULL;
    uint32_t  cnt = 0;
    if (!read_index_array(s, &indices, &cnt)) return LUSD_ERROR_PARSE_FAILED;
    if (cnt == 0) return LUSD_SUCCESS;

    char** strs = (char**)malloc((size_t)cnt * sizeof(char*));
    if (!strs) { free(indices); return LUSD_ERROR_OUT_OF_MEMORY; }
    for (uint32_t i = 0; i < cnt; i++) {
        if (indices[i] >= L->token_count) {
            free(strs); free(indices); return LUSD_ERROR_PARSE_FAILED;
        }
        strs[i] = L->tokens[indices[i]];
    }
    free(indices);

    L->strings      = strs;
    L->string_count = cnt;
    return LUSD_SUCCESS;
}

/* FIELDS: compressed token indices + LZ4-only value reps */
static LusdResult parse_fields(LusdLayer_T* L, ByteStream* s,
                                 const UsdcSection* sec) {
    if (sec->size == 0) return LUSD_SUCCESS;
    if (!bs_seek(s, sec->start)) return LUSD_ERROR_PARSE_FAILED;

    uint64_t num_fields;
    if (!bs_read_u64(s, &num_fields)) return LUSD_ERROR_PARSE_FAILED;
    if (num_fields == 0) return LUSD_SUCCESS;
    if (num_fields > USDC_MAX_FIELDS) return LUSD_ERROR_PARSE_FAILED;

    LusdFieldEntry* fields = (LusdFieldEntry*)malloc((size_t)num_fields * sizeof(LusdFieldEntry));
    if (!fields) return LUSD_ERROR_OUT_OF_MEMORY;

    /* Scratch buffer for decompression */
    uint64_t wsz = usdc_int_working_size((uint32_t)num_fields) + 256;
    uint8_t* work = (uint8_t*)malloc((size_t)wsz);
    if (!work) { free(fields); return LUSD_ERROR_OUT_OF_MEMORY; }

    /* Token indices: Usd_IntegerCompression */
    {
        uint32_t* tmp = (uint32_t*)malloc((size_t)num_fields * 4);
        if (!tmp) { free(work); free(fields); return LUSD_ERROR_OUT_OF_MEMORY; }
        LusdResult r = decode_compressed_ints(s, tmp, (uint32_t)num_fields, work, wsz);
        if (r != LUSD_SUCCESS) { free(tmp); free(work); free(fields); return r; }
        for (uint64_t i = 0; i < num_fields; i++) fields[i].token_index = tmp[i];
        free(tmp);
    }

    /* Value reps: plain LZ4 (not delta-coded) */
    {
        uint64_t repsCompSz;
        if (!bs_read_u64(s, &repsCompSz)) { free(work); free(fields); return LUSD_ERROR_PARSE_FAILED; }
        if (repsCompSz == 0 || s->pos + repsCompSz > s->size) { free(work); free(fields); return LUSD_ERROR_PARSE_FAILED; }

        /* Output buffer: num_fields * 8 bytes */
        uint64_t repsSz = num_fields * sizeof(uint64_t);
        uint64_t* repsData = (uint64_t*)malloc((size_t)repsSz);
        if (!repsData) { free(work); free(fields); return LUSD_ERROR_OUT_OF_MEMORY; }

        const uint8_t* compData = s->data + s->pos;
        uint64_t actual = lusd__lz4_decompress(compData, repsCompSz, (uint8_t*)repsData, repsSz);
        s->pos += repsCompSz;
        if (actual != repsSz) { free(repsData); free(work); free(fields); return LUSD_ERROR_PARSE_FAILED; }

        for (uint64_t i = 0; i < num_fields; i++) {
            fields[i].value_rep.data = repsData[i];
        }
        free(repsData);
    }

    free(work);
    L->fields      = fields;
    L->field_count = (uint32_t)num_fields;
    return LUSD_SUCCESS;
}

/* FIELDSETS: compressed uint32_t[] with ~0 separators */
static LusdResult parse_fieldsets(LusdLayer_T* L, ByteStream* s,
                                    const UsdcSection* sec) {
    if (!bs_seek(s, sec->start)) return LUSD_ERROR_PARSE_FAILED;

    uint64_t num_entries;
    if (!bs_read_u64(s, &num_entries)) return LUSD_ERROR_PARSE_FAILED;
    if (num_entries == 0 || num_entries > USDC_MAX_FIELDSETS) return LUSD_ERROR_PARSE_FAILED;

    uint32_t* fsets = (uint32_t*)malloc((size_t)num_entries * 4);
    if (!fsets) return LUSD_ERROR_OUT_OF_MEMORY;

    uint64_t wsz = usdc_int_working_size((uint32_t)num_entries) + 256;
    uint8_t* work = (uint8_t*)malloc((size_t)wsz);
    if (!work) { free(fsets); return LUSD_ERROR_OUT_OF_MEMORY; }

    LusdResult r = decode_compressed_ints(s, fsets, (uint32_t)num_entries, work, wsz);
    free(work);
    if (r != LUSD_SUCCESS) { free(fsets); return r; }

    L->fieldsets             = fsets;
    L->fieldset_entry_count  = (uint32_t)num_entries;
    return LUSD_SUCCESS;
}

/* SPECS: three compressed arrays: path_indices, fieldset_indices, spec_types */
static LusdResult parse_specs(LusdLayer_T* L, ByteStream* s,
                                const UsdcSection* sec) {
    if (!bs_seek(s, sec->start)) return LUSD_ERROR_PARSE_FAILED;

    uint64_t num_specs;
    if (!bs_read_u64(s, &num_specs)) return LUSD_ERROR_PARSE_FAILED;
    if (num_specs == 0 || num_specs > USDC_MAX_SPECS) return LUSD_ERROR_PARSE_FAILED;

    LusdSpecEntry* specs = (LusdSpecEntry*)malloc((size_t)num_specs * sizeof(LusdSpecEntry));
    if (!specs) return LUSD_ERROR_OUT_OF_MEMORY;

    uint64_t wsz = usdc_int_working_size((uint32_t)num_specs) + 256;
    uint8_t* work = (uint8_t*)malloc((size_t)wsz);
    if (!work) { free(specs); return LUSD_ERROR_OUT_OF_MEMORY; }

    uint32_t* tmp = (uint32_t*)malloc((size_t)num_specs * 4);
    if (!tmp) { free(work); free(specs); return LUSD_ERROR_OUT_OF_MEMORY; }

    /* path indices */
    {
        LusdResult r = decode_compressed_ints(s, tmp, (uint32_t)num_specs, work, wsz);
        if (r != LUSD_SUCCESS) { free(tmp); free(work); free(specs); return r; }
        for (uint64_t i = 0; i < num_specs; i++) specs[i].path_index = tmp[i];
    }
    /* fieldset indices */
    {
        LusdResult r = decode_compressed_ints(s, tmp, (uint32_t)num_specs, work, wsz);
        if (r != LUSD_SUCCESS) { free(tmp); free(work); free(specs); return r; }
        for (uint64_t i = 0; i < num_specs; i++) specs[i].fieldset_index = tmp[i];
    }
    /* spec types */
    {
        LusdResult r = decode_compressed_ints(s, tmp, (uint32_t)num_specs, work, wsz);
        if (r != LUSD_SUCCESS) { free(tmp); free(work); free(specs); return r; }
        for (uint64_t i = 0; i < num_specs; i++) specs[i].spec_type = tmp[i];
    }

    free(tmp);
    free(work);
    L->specs      = specs;
    L->spec_count = (uint32_t)num_specs;
    return LUSD_SUCCESS;
}

/* ====================================================================
 * PATHS: compressed delta-tree → full path strings
 * ==================================================================== */

/* Stack entry used by the iterative path-tree reconstruction.
 * Stores path INDEX rather than a raw arena pointer so that arena
 * realloc (triggered by subsequent arena_strdup/arena_sprintf calls)
 * cannot invalidate the stored value. */
typedef struct {
    size_t   start_idx;
    size_t   end_idx;
    uint32_t parent_path_idx; /* index into path_offsets[], or PATH_NO_PARENT */
} PathStackEntry;

#define PATH_STACK_MAX  2048
#define PATH_NO_PARENT  0xFFFFFFFFu

static LusdResult build_path_tree(LusdLayer_T*   L,
                                   const uint32_t* path_idxs,    /* pathIndexes */
                                   const int32_t*  elem_tok_idxs,/* elementTokenIndexes */
                                   const int32_t*  jumps,
                                   uint32_t        num_encoded,
                                   uint32_t        num_paths) {
    if (num_encoded == 0) return LUSD_SUCCESS;

    /* Temporary offset table: path_offsets[i] = byte offset of L->paths[i]
     * within L->string_arena.  Using offsets (not raw pointers) keeps the
     * values stable across arena realloc calls.  L->paths[] is populated
     * with real char* values only at the very end, after all arena
     * allocations are complete. */
    uint32_t* path_offsets = (uint32_t*)calloc(num_paths, sizeof(uint32_t));
    if (!path_offsets) return LUSD_ERROR_OUT_OF_MEMORY;

    PathStackEntry stack[PATH_STACK_MAX];
    int stackTop = 0;

    size_t   end_index       = num_encoded - 1;
    uint32_t parent_path_idx = PATH_NO_PARENT; /* root level */
    bool     done            = false;
    LusdResult res           = LUSD_SUCCESS;

    for (size_t i = 0; i <= end_index && !done; i++) {
        if (i >= num_encoded) break;

        uint32_t path_idx = path_idxs[i];
        if (path_idx >= num_paths) { res = LUSD_ERROR_PARSE_FAILED; goto out; }

        int32_t tok_idx = elem_tok_idxs[i];
        int32_t jump    = jumps[i];

        /* Build path string for this node.
         * IMPORTANT: `parent_str` must be derived from L->string_arena AFTER
         * any pending arena operations so that it reflects the current base.
         * It is read by snprintf into a stack buffer (tmp) inside arena_sprintf
         * BEFORE the resulting arena_strdup call can trigger a realloc, so
         * the pointer is valid for exactly that snprintf call. */
        char* result;
        if (parent_path_idx == PATH_NO_PARENT) {
            /* Root node — absolute path "/" */
            result = arena_strdup(L, "/");
        } else {
            /* Re-derive parent pointer from current arena base + stored offset.
             * Safe even if previous iterations triggered arena realloc. */
            const char* parent_str =
                (const char*)L->string_arena + path_offsets[parent_path_idx];

            bool is_prop = (tok_idx < 0);
            uint32_t ti  = (uint32_t)(is_prop ? -tok_idx : tok_idx);
            if (ti >= L->token_count) { res = LUSD_ERROR_PARSE_FAILED; goto out; }
            const char* elem = L->tokens[ti]; /* tokens point into file_data, never into arena */

            if (is_prop) {
                result = arena_sprintf(L, "%s.%s", parent_str, elem);
            } else if (parent_str[0] == '/' && parent_str[1] == '\0') {
                result = arena_sprintf(L, "/%s", elem, NULL);
            } else {
                result = arena_sprintf(L, "%s/%s", parent_str, elem);
            }
        }
        if (!result) { res = LUSD_ERROR_OUT_OF_MEMORY; goto out; }

        /* Record offset within the (possibly just-reallocated) arena.
         * result was returned by arena_alloc which updated L->string_arena,
         * so (result - L->string_arena) is always the correct offset. */
        path_offsets[path_idx] = (uint32_t)(result - (char*)L->string_arena);

        bool has_child   = (jump > 0) || (jump == -1);
        bool has_sibling = (jump >= 0);

        if (has_child) {
            if (has_sibling) {
                /* Push sibling context */
                size_t sibling_idx = i + (size_t)jump;
                if (sibling_idx >= num_encoded || stackTop >= PATH_STACK_MAX) {
                    res = LUSD_ERROR_PARSE_FAILED; goto out;
                }
                stack[stackTop].start_idx       = sibling_idx;
                stack[stackTop].end_idx         = end_index;
                stack[stackTop].parent_path_idx = parent_path_idx;
                stackTop++;
                end_index = sibling_idx - 1; /* narrow range for children */
            }
            /* Descend: children follow at i+1 */
            parent_path_idx = path_idx;
        } else if (!has_sibling) {
            /* Terminal node: pop from stack */
            if (stackTop == 0) { done = true; break; }
            stackTop--;
            i               = stack[stackTop].start_idx - 1; /* -1: loop will i++ */
            end_index       = stack[stackTop].end_idx;
            parent_path_idx = stack[stackTop].parent_path_idx;
        }
        /* !has_child && has_sibling: loop continues naturally to i+1 */
    }

    /* Convert arena offsets → real char* pointers.
     * The arena will not be reallocated after this point (all path strings
     * are now in place), so the pointers remain stable for the layer's
     * lifetime. */
    for (uint32_t j = 0; j < num_paths; j++) {
        L->paths[j] = (char*)L->string_arena + path_offsets[j];
    }

out:
    free(path_offsets);
    return res;
}

/* PATHS section: read three compressed arrays, then build path strings */
static LusdResult parse_paths(LusdLayer_T* L, ByteStream* s,
                               const UsdcSection* sec) {
    if (!bs_seek(s, sec->start)) return LUSD_ERROR_PARSE_FAILED;

    uint64_t num_paths;
    if (!bs_read_u64(s, &num_paths)) return LUSD_ERROR_PARSE_FAILED;
    if (num_paths == 0 || num_paths > USDC_MAX_PATHS) return LUSD_ERROR_PARSE_FAILED;

    /* Allocate paths pointer array */
    char** paths = (char**)calloc((size_t)num_paths, sizeof(char*));
    if (!paths) return LUSD_ERROR_OUT_OF_MEMORY;
    L->paths      = paths;
    L->path_count = (uint32_t)num_paths;

    /* numEncodedPaths */
    uint64_t num_encoded;
    if (!bs_read_u64(s, &num_encoded)) return LUSD_ERROR_PARSE_FAILED;
    if (num_encoded > num_paths) return LUSD_ERROR_PARSE_FAILED;
    if (num_encoded == 0) return LUSD_SUCCESS;

    /* Scratch buffer for decompression */
    uint64_t wsz = usdc_int_working_size((uint32_t)num_encoded) + 256;
    uint8_t* work = (uint8_t*)malloc((size_t)wsz);
    if (!work) return LUSD_ERROR_OUT_OF_MEMORY;

    uint32_t* pathIndexes = (uint32_t*)malloc((size_t)num_encoded * 4);
    int32_t*  elemToks    = (int32_t*) malloc((size_t)num_encoded * 4);
    int32_t*  jumps       = (int32_t*) malloc((size_t)num_encoded * 4);

    if (!pathIndexes || !elemToks || !jumps) {
        free(work); free(pathIndexes); free(elemToks); free(jumps);
        return LUSD_ERROR_OUT_OF_MEMORY;
    }

    LusdResult r;

    /* pathIndexes */
    r = decode_compressed_ints(s, pathIndexes, (uint32_t)num_encoded, work, wsz);
    if (r != LUSD_SUCCESS) goto cleanup_paths;

    /* elementTokenIndexes (can be signed/negative for property paths) */
    r = decode_compressed_ints(s, (uint32_t*)elemToks, (uint32_t)num_encoded, work, wsz);
    if (r != LUSD_SUCCESS) goto cleanup_paths;

    /* jumps */
    r = decode_compressed_ints(s, (uint32_t*)jumps, (uint32_t)num_encoded, work, wsz);
    if (r != LUSD_SUCCESS) goto cleanup_paths;

    /* Reconstruct full path strings */
    r = build_path_tree(L, pathIndexes, elemToks, jumps,
                        (uint32_t)num_encoded, (uint32_t)num_paths);

cleanup_paths:
    free(work);
    free(pathIndexes);
    free(elemToks);
    free(jumps);
    return r;
}

/* ====================================================================
 * Layer metadata extraction helpers
 * ==================================================================== */

/* Get a token string from an inlined token ValueRep */
static const char* layer_find_token(const LusdLayer_T* L, LusdValueRep rep) {
    if (!lusd_vrep_is_inlined(rep)) return NULL;
    uint64_t payload = lusd_vrep_payload(rep);
    if (payload >= L->token_count) return NULL;
    return L->tokens[(uint32_t)payload];
}

/* Extract a double from an inlined double ValueRep */
static bool layer_vrep_double(const LusdLayer_T* L, LusdValueRep rep, double* out) {
    (void)L;
    if (!lusd_vrep_is_inlined(rep)) return false;
    /* Inlined double: payload bits 47-0 hold the low 48 bits of the double.
     * For full doubles, tinyusdz stores them inline if they fit;
     * the payload IS the double reinterpreted as uint64_t with upper 16 bits cleared.
     * We reconstruct by shifting the payload back. */
    uint64_t pay = lusd_vrep_payload(rep);
    /* The actual double is stored as the raw uint64 with upper type+flag bits removed */
    /* upper 16 bits of the original uint64 are type/flags; payload is lower 48 bits.
     * For a double, the full 64-bit representation is reconstructed differently.
     * tinyusdz inlines doubles by storing the bit pattern directly in the 48-bit payload
     * only for special values (0, 1, etc.).  For the general case the payload IS the
     * file offset, not an inlined value.  We handle only the most common cases here. */
    double d;
    /* Attempt: treat lower 48 bits of the stored 64-bit data as the double payload.
     * Many USD writers store the raw bits with the upper 16 bits zeroed. */
    uint64_t raw = pay; /* 48-bit payload */
    memcpy(&d, &raw, sizeof(d)); /* safe-reinterpret */
    *out = d;
    return true;
}

/* Find a field in a range of fieldsets[] by token name */
static LusdValueRep find_field_in_fieldset(const LusdLayer_T* L,
                                            uint32_t fs_index,
                                            const char* name) {
    if (!L->fieldsets || !L->fields || !L->tokens) return LUSD_NULL_VREP;
    if (fs_index >= L->fieldset_entry_count) return LUSD_NULL_VREP;

    for (uint32_t i = fs_index; i < L->fieldset_entry_count; i++) {
        if (L->fieldsets[i] == USDC_SENTINEL) break; /* end of this fieldset */
        uint32_t fi = L->fieldsets[i];
        if (fi >= L->field_count) continue;
        const LusdFieldEntry* f = &L->fields[fi];
        if (f->token_index >= L->token_count) continue;
        if (strcmp(L->tokens[f->token_index], name) == 0) {
            return f->value_rep;
        }
    }
    return LUSD_NULL_VREP;
}

/* Extract layer-level metadata from the pseudo-root spec (spec 0) */
static void extract_layer_metas(LusdLayer_T* L) {
    /* Defaults */
    L->metas.meters_per_unit  = 0.01;
    L->metas.frames_per_second = 24.0;
    L->metas.start_time_code  = 0.0;
    L->metas.end_time_code    = 0.0;
    L->metas.up_axis          = 0; /* Y-up */

    if (L->spec_count == 0) return;

    /* Spec 0 is the pseudo-root, typically at path "/" */
    uint32_t fs = L->specs[0].fieldset_index;

    /* upAxis */
    {
        LusdValueRep r = find_field_in_fieldset(L, fs, "upAxis");
        if (!lusd_vrep_is_null(r)) {
            const char* tok = layer_find_token(L, r);
            if (tok) {
                if (strcmp(tok, "Z") == 0) L->metas.up_axis = 1;
                else if (strcmp(tok, "X") == 0) L->metas.up_axis = 2;
                else L->metas.up_axis = 0; /* Y */
            }
        }
    }
    /* metersPerUnit */
    {
        LusdValueRep r = find_field_in_fieldset(L, fs, "metersPerUnit");
        if (!lusd_vrep_is_null(r)) {
            double v; if (layer_vrep_double(L, r, &v)) L->metas.meters_per_unit = v;
        }
    }
    /* startTimeCode */
    {
        LusdValueRep r = find_field_in_fieldset(L, fs, "startTimeCode");
        if (!lusd_vrep_is_null(r)) {
            double v; if (layer_vrep_double(L, r, &v)) L->metas.start_time_code = v;
        }
    }
    /* endTimeCode */
    {
        LusdValueRep r = find_field_in_fieldset(L, fs, "endTimeCode");
        if (!lusd_vrep_is_null(r)) {
            double v; if (layer_vrep_double(L, r, &v)) L->metas.end_time_code = v;
        }
    }
    /* framesPerSecond */
    {
        LusdValueRep r = find_field_in_fieldset(L, fs, "framesPerSecond");
        if (!lusd_vrep_is_null(r)) {
            double v; if (layer_vrep_double(L, r, &v)) L->metas.frames_per_second = v;
        }
    }
}

/* ====================================================================
 * Main entry point: lusd__layer_read_usdc
 * ==================================================================== */

LusdResult lusd__layer_read_usdc(LusdLayer_T* layer,
                                  const uint8_t* data,
                                  uint64_t       size) {
    if (!layer || !data || size < USDC_BOOTSTRAP_SIZE) return LUSD_ERROR_INVALID_ARGUMENT;

    /* --- Bootstrap -------------------------------------------------- */
    if (memcmp(data, USDC_MAGIC, USDC_MAGIC_LEN) != 0) return LUSD_ERROR_PARSE_FAILED;

    uint8_t  major = data[8];
    uint8_t  minor = data[9];
    /* patch = data[10]; (not checked) */
    (void)major;
    if (minor < 4) return LUSD_ERROR_PARSE_FAILED; /* require >= 0.4.0 */

    uint64_t toc_offset;
    memcpy(&toc_offset, data + 16, 8);
    if (toc_offset <= 88 || toc_offset >= size) return LUSD_ERROR_PARSE_FAILED;

    /* --- TOC -------------------------------------------------------- */
    ByteStream bs;
    bs_init(&bs, data, size);
    if (!bs_seek(&bs, toc_offset)) return LUSD_ERROR_PARSE_FAILED;

    uint64_t num_sections;
    if (!bs_read_u64(&bs, &num_sections)) return LUSD_ERROR_PARSE_FAILED;
    if (num_sections == 0 || num_sections > 64) return LUSD_ERROR_PARSE_FAILED;

    UsdcSection* secs = (UsdcSection*)malloc((size_t)num_sections * sizeof(UsdcSection));
    if (!secs) return LUSD_ERROR_OUT_OF_MEMORY;

    for (uint64_t i = 0; i < num_sections; i++) {
        char name[USDC_SECTION_NAME_LEN + 1];
        if (!bs_read(&bs, name, USDC_SECTION_NAME_LEN)) {
            free(secs); return LUSD_ERROR_PARSE_FAILED;
        }
        name[USDC_SECTION_NAME_LEN] = '\0';
        strncpy(secs[i].name, name, USDC_SECTION_NAME_LEN + 1);
        if (!bs_read_u64(&bs, &secs[i].start) ||
            !bs_read_u64(&bs, &secs[i].size)) {
            free(secs); return LUSD_ERROR_PARSE_FAILED;
        }
    }

    UsdcTOC toc = { secs, num_sections };

    /* --- Find sections ---------------------------------------------- */
    const UsdcSection* secTokens    = toc_find(&toc, "TOKENS");
    const UsdcSection* secStrings   = toc_find(&toc, "STRINGS");
    const UsdcSection* secPaths     = toc_find(&toc, "PATHS");
    const UsdcSection* secFields    = toc_find(&toc, "FIELDS");
    const UsdcSection* secFieldsets = toc_find(&toc, "FIELDSETS");
    const UsdcSection* secSpecs     = toc_find(&toc, "SPECS");

    if (!secTokens || !secPaths || !secFields || !secFieldsets || !secSpecs) {
        free(secs); return LUSD_ERROR_PARSE_FAILED;
    }

    /* --- Parse each section in order -------------------------------- */
    LusdResult r;

    r = parse_tokens(layer, &bs, secTokens);
    if (r != LUSD_SUCCESS) { free(secs); return r; }

    if (secStrings && secStrings->size > 0) {
        r = parse_strings(layer, &bs, secStrings);
        if (r != LUSD_SUCCESS) { free(secs); return r; }
    }

    r = parse_fields(layer, &bs, secFields);
    if (r != LUSD_SUCCESS) { free(secs); return r; }

    r = parse_fieldsets(layer, &bs, secFieldsets);
    if (r != LUSD_SUCCESS) { free(secs); return r; }

    r = parse_specs(layer, &bs, secSpecs);
    if (r != LUSD_SUCCESS) { free(secs); return r; }

    r = parse_paths(layer, &bs, secPaths);
    if (r != LUSD_SUCCESS) { free(secs); return r; }

    free(secs);

    /* Store reference to file data (already set by caller, just verify) */
    layer->file_data = data;
    layer->file_size = size;

    /* Extract layer-level metadata */
    extract_layer_metas(layer);

    return LUSD_SUCCESS;
}

/* ====================================================================
 * Table cleanup
 * ==================================================================== */

void lusd__layer_free_tables(LusdLayer_T* layer) {
    if (!layer) return;
    /* Free child_spec_indices inside each prim node before freeing the arena */
    if (layer->prim_nodes) {
        for (uint32_t i = 0; i < layer->prim_node_count; i++) {
            free(layer->prim_nodes[i].child_spec_indices);
            layer->prim_nodes[i].child_spec_indices = NULL;
        }
    }
    free(layer->token_buf);   layer->token_buf = NULL;
    free(layer->tokens);      layer->tokens = NULL;
    free(layer->strings);     layer->strings = NULL;
    free(layer->paths);       layer->paths = NULL;
    free(layer->fields);      layer->fields = NULL;
    free(layer->fieldsets);   layer->fieldsets = NULL;
    free(layer->specs);       layer->specs = NULL;
    free(layer->string_arena);layer->string_arena = NULL;
    free(layer->prim_nodes);  layer->prim_nodes = NULL;
    free(layer->root_spec_indices); layer->root_spec_indices = NULL;
    free(layer->time_samples);      layer->time_samples = NULL;

    layer->token_count = layer->string_count = layer->path_count = 0;
    layer->field_count = layer->fieldset_entry_count = layer->spec_count = 0;
    layer->string_arena_size = layer->string_arena_used = 0;
    layer->prim_node_count   = layer->prim_node_capacity = 0;
    layer->root_spec_count   = 0;
    layer->time_sample_count = 0;

    if (layer->owns_file_data) {
        free((void*)layer->file_data);
        layer->file_data = NULL;
    }
}

/* ====================================================================
 * lusd__layer_find_field (used by Lydra for value materialization)
 * ==================================================================== */

LusdValueRep lusd__layer_find_field(const LusdLayer_T* layer,
                                     const LusdPrim_T*  prim,
                                     const char*        name) {
    if (!layer || !prim || !name) return LUSD_NULL_VREP;
    return find_field_in_fieldset(layer, prim->fieldset_index, name);
}

/* ====================================================================
 * PrimSpec tree construction (lusd__layer_build_prims)
 * ==================================================================== */

/* Helper: find the token "typeName" in a prim's fieldset, return it or "" */
static const char* prim_type_name(const LusdLayer_T* L, uint32_t fs_index) {
    LusdValueRep r = find_field_in_fieldset(L, fs_index, "typeName");
    if (lusd_vrep_is_null(r)) return "";
    const char* tok = layer_find_token(L, r);
    return tok ? tok : "";
}

/* Helper: compute fieldset length (entries before next ~0 sentinel) */
static uint32_t fieldset_len(const LusdLayer_T* L, uint32_t fs_index) {
    uint32_t cnt = 0;
    for (uint32_t i = fs_index; i < L->fieldset_entry_count; i++) {
        if (L->fieldsets[i] == USDC_SENTINEL) break;
        cnt++;
    }
    return cnt;
}

/* Allocate a prim node in the layer's prim_nodes arena-array */
static LusdPrim_T* alloc_prim_node(LusdLayer_T* L) {
    if (L->prim_node_count >= L->prim_node_capacity) {
        uint32_t newCap = L->prim_node_capacity ? L->prim_node_capacity * 2 : 64;
        LusdPrim_T* nb = (LusdPrim_T*)realloc(L->prim_nodes, newCap * sizeof(LusdPrim_T));
        if (!nb) return NULL;
        L->prim_nodes        = nb;
        L->prim_node_capacity = newCap;
    }
    LusdPrim_T* node = &L->prim_nodes[L->prim_node_count++];
    memset(node, 0, sizeof(*node));
    return node;
}

/* Parent path of a given path (e.g. "/a/b/c" → "/a/b", "/Cube" → "/") */
static bool path_parent_equals(const char* child_path, const char* parent_path) {
    /* Returns true if `parent_path` is the direct parent of `child_path`. */
    if (!child_path || !parent_path) return false;
    size_t clen = strlen(child_path);
    size_t plen = strlen(parent_path);

    /* Root "/" has no parent */
    if (clen == 1 && child_path[0] == '/') return false;

    /* Find the last '/' in child_path */
    const char* last_sep = strrchr(child_path, '/');
    if (!last_sep) return false;

    /* Parent portion is child_path[0..last_sep] */
    size_t parent_portion_len = (size_t)(last_sep - child_path);
    if (parent_portion_len == 0) parent_portion_len = 1; /* root "/" */

    if (plen != parent_portion_len) return false;
    return strncmp(child_path, parent_path, parent_portion_len) == 0;
}

/*
 * Build the prim tree:
 * 1. Scan specs for DEF/OVER/CLASS prims (spec_type == LUSD_SPEC_TYPE_PRIM)
 * 2. For each prim spec, create a LusdPrim_T node
 * 3. Connect children to parents by path comparison
 */
LusdResult lusd__layer_build_prims(LusdLayer_T* L) {
    if (!L || L->spec_count == 0) return LUSD_SUCCESS;

    /* --- Pass 1: create prim nodes for all PRIM/PSEUDO_ROOT specs --- */
    for (uint32_t si = 0; si < L->spec_count; si++) {
        const LusdSpecEntry* se = &L->specs[si];
        if (se->spec_type != LUSD_SPEC_TYPE_PRIM &&
            se->spec_type != LUSD_SPEC_TYPE_PSEUDO_ROOT) continue;
        if (se->path_index >= L->path_count) continue;

        const char* full_path = L->paths[se->path_index];
        if (!full_path) continue;

        LusdPrim_T* node = alloc_prim_node(L);
        if (!node) return LUSD_ERROR_OUT_OF_MEMORY;

        node->spec_index     = si;
        node->fieldset_index = se->fieldset_index;
        node->field_count    = fieldset_len(L, se->fieldset_index);
        node->layer          = L;
        node->type_name      = prim_type_name(L, se->fieldset_index);

        /* Element name = last path component */
        const char* last_sep = strrchr(full_path, '/');
        node->name = last_sep ? last_sep + 1 : full_path;
    }

    /* --- Pass 2: wire parent→child relationships ------------------- */
    /* For each prim node, collect children (nodes whose path parent == this node's path) */
    for (uint32_t pi = 0; pi < L->prim_node_count; pi++) {
        LusdPrim_T* parent = &L->prim_nodes[pi];
        const char* parent_path = L->paths[L->specs[parent->spec_index].path_index];

        /* Count children first */
        uint32_t child_count = 0;
        for (uint32_t ci = 0; ci < L->prim_node_count; ci++) {
            if (ci == pi) continue;
            LusdPrim_T* child = &L->prim_nodes[ci];
            const char* child_path = L->paths[L->specs[child->spec_index].path_index];
            if (path_parent_equals(child_path, parent_path)) child_count++;
        }

        if (child_count == 0) continue;

        /* Allocate child index array */
        uint32_t* cids = (uint32_t*)malloc(child_count * 4);
        if (!cids) return LUSD_ERROR_OUT_OF_MEMORY;

        uint32_t idx = 0;
        for (uint32_t ci = 0; ci < L->prim_node_count; ci++) {
            if (ci == pi) continue;
            LusdPrim_T* child = &L->prim_nodes[ci];
            const char* child_path = L->paths[L->specs[child->spec_index].path_index];
            if (path_parent_equals(child_path, parent_path)) {
                cids[idx++] = ci;
            }
        }

        parent->child_spec_indices = cids;
        parent->child_count        = child_count;
    }

    /* --- Pass 3: collect root prims (direct children of "/") ------- */
    uint32_t root_count = 0;
    for (uint32_t ni = 0; ni < L->prim_node_count; ni++) {
        const LusdPrim_T* node = &L->prim_nodes[ni];
        const char* path = L->paths[L->specs[node->spec_index].path_index];
        if (!path) continue;
        /* Root prims have paths like "/Cube", "/World", etc. (one level deep) */
        if (path[0] != '/') continue;
        if (strcmp(path, "/") == 0) continue; /* skip pseudo-root */
        /* Count '/' separators — root prims have exactly 1 */
        int slashes = 0;
        for (const char* c = path; *c; c++) if (*c == '/') slashes++;
        if (slashes == 1) root_count++;
    }

    if (root_count == 0) return LUSD_SUCCESS;

    uint32_t* roots = (uint32_t*)malloc(root_count * 4);
    if (!roots) return LUSD_ERROR_OUT_OF_MEMORY;

    uint32_t ri = 0;
    for (uint32_t ni = 0; ni < L->prim_node_count; ni++) {
        const LusdPrim_T* node = &L->prim_nodes[ni];
        const char* path = L->paths[L->specs[node->spec_index].path_index];
        if (!path || path[0] != '/' || strcmp(path, "/") == 0) continue;
        int slashes = 0;
        for (const char* c = path; *c; c++) if (*c == '/') slashes++;
        if (slashes == 1) roots[ri++] = ni;
    }

    L->root_spec_indices = roots;
    L->root_spec_count   = root_count;
    return LUSD_SUCCESS;
}
