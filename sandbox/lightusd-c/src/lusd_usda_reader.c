/*
 * lusd_usda_reader.c - Pure-C11 USDA ASCII reader
 *
 * Parses a USDA text file into the same flat tables as the USDC reader:
 *   tokens[]      interned string table (owned arena)
 *   strings[]     aliases into tokens[]
 *   paths[]       full path strings (in string_arena)
 *   fields[]      {token_index, LusdValueRep} pairs
 *   fieldsets[]   flat uint32_t[], ~0u separators
 *   specs[]       {path_index, fieldset_index, spec_type}
 *   root_spec_indices[]  top-level prims
 *
 * Value representation — lazy model:
 *   IsInlined=1: bool, int, uint, float, token (value packed in payload)
 *   IsInlined=0, IsArray=0: non-array types stored as text offset into file_data
 *   IsInlined=0, IsArray=1: array text offset (points at '[' in file_data)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal/lusd_layer_internal.h"
#include "internal/lusd_value_rep.h"
#include "lightusd/lusd_result.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ====================================================================
 * Parser cursor
 * ==================================================================== */

typedef struct {
    const char* p;      /* current position */
    const char* end;    /* one past last byte */
    const char* base;   /* start of file_data (for offset calculation) */
    uint32_t    line;   /* 1-based, for error messages */
} UsdaP;

static void usda_init(UsdaP* p, const char* data, uint64_t size) {
    p->p    = data;
    p->end  = data + size;
    p->base = data;
    p->line = 1;
}

static bool usda_at_end(const UsdaP* p) { return p->p >= p->end; }

static char usda_peek(const UsdaP* p) {
    return usda_at_end(p) ? '\0' : *p->p;
}

static char usda_next(UsdaP* p) {
    if (usda_at_end(p)) return '\0';
    char c = *p->p++;
    if (c == '\n') p->line++;
    return c;
}

/* Skip whitespace and # line comments */
static void usda_skip_ws(UsdaP* p) {
    while (!usda_at_end(p)) {
        char c = usda_peek(p);
        if (c == '#') {
            /* skip to end of line */
            while (!usda_at_end(p) && usda_peek(p) != '\n')
                usda_next(p);
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            usda_next(p);
        } else {
            break;
        }
    }
}

static bool usda_expect(UsdaP* p, char ch) {
    usda_skip_ws(p);
    if (usda_at_end(p) || usda_peek(p) != ch) return false;
    usda_next(p);
    return true;
}

/* Read an identifier [a-zA-Z0-9_:.] into buf (NUL-terminated, truncated to max-1) */
static uint32_t usda_read_ident(UsdaP* p, char* buf, size_t max) {
    uint32_t n = 0;
    while (!usda_at_end(p)) {
        char c = usda_peek(p);
        if (!isalnum((unsigned char)c) && c != '_' && c != ':' && c != '.' &&
            c != '-' && c != '+') break;
        if (n + 1 < (uint32_t)max) buf[n] = c;
        n++;
        usda_next(p);
    }
    if (n < (uint32_t)max) buf[n] = '\0';
    else if (max > 0) buf[max-1] = '\0';
    return n;
}

/* Skip a balanced bracket expression: open/close delimiters + strings + @assets@ */
static void usda_skip_balanced(UsdaP* p, char open, char close) {
    int depth = 1;
    while (!usda_at_end(p) && depth > 0) {
        char c = usda_next(p);
        if (c == open)  depth++;
        else if (c == close) depth--;
        else if (c == '"') {
            /* skip quoted string */
            while (!usda_at_end(p)) {
                char q = usda_next(p);
                if (q == '\\') { usda_next(p); continue; }
                if (q == '"')  break;
            }
        } else if (c == '@') {
            /* skip @path@ asset reference */
            while (!usda_at_end(p) && usda_peek(p) != '@')
                usda_next(p);
            if (!usda_at_end(p)) usda_next(p); /* consume closing @ */
        }
    }
}

/* Skip one complete value expression (array, tuple, string, bare word/number) */
static void usda_skip_value(UsdaP* p) {
    usda_skip_ws(p);
    if (usda_at_end(p)) return;
    char c = usda_peek(p);
    if (c == '[') { usda_next(p); usda_skip_balanced(p, '[', ']'); }
    else if (c == '(') { usda_next(p); usda_skip_balanced(p, '(', ')'); }
    else if (c == '{') { usda_next(p); usda_skip_balanced(p, '{', '}'); }
    else if (c == '"') {
        usda_next(p);
        while (!usda_at_end(p)) {
            char q = usda_next(p);
            if (q == '\\') { usda_next(p); continue; }
            if (q == '"')  break;
        }
    } else if (c == '@') {
        usda_next(p);
        while (!usda_at_end(p) && usda_peek(p) != '@') usda_next(p);
        if (!usda_at_end(p)) usda_next(p);
    } else {
        /* bare word / number */
        while (!usda_at_end(p)) {
            char cc = usda_peek(p);
            if (cc == ' ' || cc == '\t' || cc == '\r' || cc == '\n' ||
                cc == ',' || cc == ')' || cc == ']' || cc == '}' ||
                cc == '#') break;
            usda_next(p);
        }
    }
}

/* Parse a decimal integer (no leading whitespace assumed consumed by caller) */
static bool usda_parse_int32(UsdaP* p, int32_t* out) {
    usda_skip_ws(p);
    if (usda_at_end(p)) return false;
    bool neg = false;
    if (usda_peek(p) == '-') { neg = true; usda_next(p); }
    else if (usda_peek(p) == '+') usda_next(p);
    if (usda_at_end(p) || !isdigit((unsigned char)usda_peek(p))) return false;
    int64_t v = 0;
    while (!usda_at_end(p) && isdigit((unsigned char)usda_peek(p))) {
        v = v * 10 + (usda_next(p) - '0');
        if (v > 0x7fffffffLL + 1) return false; /* overflow guard */
    }
    *out = (int32_t)(neg ? -v : v);
    return true;
}

/* Parse a float (scientific notation supported) */
static bool usda_parse_float32(UsdaP* p, float* out) {
    usda_skip_ws(p);
    /* Collect the float token into a small buffer */
    char buf[64];
    uint32_t n = 0;
    if (!usda_at_end(p) && (usda_peek(p) == '-' || usda_peek(p) == '+'))
        buf[n++] = usda_next(p);
    while (!usda_at_end(p) && n < 62) {
        char c = usda_peek(p);
        if (!isdigit((unsigned char)c) && c != '.' && c != 'e' && c != 'E' &&
            c != '-' && c != '+') break;
        buf[n++] = usda_next(p);
    }
    buf[n] = '\0';
    if (n == 0) return false;
    char* end_ptr;
    *out = strtof(buf, &end_ptr);
    return end_ptr != buf;
}

/* Parse a quoted string, strip quotes, write into buf (truncated to max-1 chars) */
static bool usda_parse_string(UsdaP* p, char* buf, size_t max) {
    usda_skip_ws(p);
    if (usda_at_end(p) || usda_peek(p) != '"') return false;
    usda_next(p); /* consume opening " */
    size_t n = 0;
    while (!usda_at_end(p)) {
        char c = usda_next(p);
        if (c == '\\') {
            if (!usda_at_end(p)) {
                char esc = usda_next(p);
                if (n + 1 < max) {
                    switch (esc) {
                        case 'n':  buf[n++] = '\n'; break;
                        case 't':  buf[n++] = '\t'; break;
                        case '"':  buf[n++] = '"';  break;
                        case '\\': buf[n++] = '\\'; break;
                        default:   buf[n++] = esc;  break;
                    }
                }
            }
        } else if (c == '"') {
            break;
        } else {
            if (n + 1 < max) buf[n++] = c;
        }
    }
    if (n < max) buf[n] = '\0';
    else if (max > 0) buf[max-1] = '\0';
    return true;
}

/* Parse a bool: "true"/"false" or 1/0 */
static bool usda_parse_bool(UsdaP* p, bool* out) {
    usda_skip_ws(p);
    if (p->p + 4 <= p->end && strncmp(p->p, "true", 4) == 0) {
        p->p += 4; *out = true; return true;
    }
    if (p->p + 5 <= p->end && strncmp(p->p, "false", 5) == 0) {
        p->p += 5; *out = false; return true;
    }
    int32_t v;
    if (usda_parse_int32(p, &v)) { *out = (v != 0); return true; }
    return false;
}

/* ====================================================================
 * Token interning (open-addressing hash table over a string arena)
 * ==================================================================== */

typedef struct {
    char*     arena;
    uint32_t  arena_used;
    uint32_t  arena_cap;
    uint32_t* indices;   /* token_index stored at hash slot */
    uint32_t  cap;       /* power-of-2 */
    uint32_t  count;
} TokenSet;

#define TOKENSET_EMPTY 0xFFFFFFFFu

static uint32_t fnv1a(const char* s, uint32_t len) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++)
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

static bool tokenset_init(TokenSet* ts) {
    ts->arena_cap  = 64 * 1024;
    ts->arena_used = 0;
    ts->arena = (char*)malloc(ts->arena_cap);
    if (!ts->arena) return false;
    ts->cap   = 256;
    ts->count = 0;
    ts->indices = (uint32_t*)malloc(ts->cap * sizeof(uint32_t));
    if (!ts->indices) { free(ts->arena); return false; }
    memset(ts->indices, 0xFF, ts->cap * sizeof(uint32_t));
    return true;
}

static void tokenset_destroy(TokenSet* ts) {
    free(ts->arena);
    free(ts->indices);
    ts->arena = NULL;
    ts->indices = NULL;
}

/* Returns token index (0-based); token 0 is always the empty string "".
   We store tokens as NUL-terminated strings in arena, and keep an index array.
   tokens[idx] == arena + offset_of_token[idx] */

/* Per-token offset table is separate; we need to reconstruct pointers later.
   Strategy: store offsets in a parallel array, grow it together with the hash table. */

static uint32_t  g_offsets_cap = 0;
static uint32_t* g_offsets     = NULL;

static bool tokenset_grow_offsets(uint32_t new_cap) {
    uint32_t* nb = (uint32_t*)realloc(g_offsets, new_cap * sizeof(uint32_t));
    if (!nb) return false;
    g_offsets     = nb;
    g_offsets_cap = new_cap;
    return true;
}

/* Returns existing index or allocates a new slot; ~0u on OOM */
static uint32_t tokenset_intern(TokenSet* ts, const char* s, uint32_t len) {
    uint32_t h   = fnv1a(s, len);
    uint32_t slot = h & (ts->cap - 1);
    for (;;) {
        uint32_t idx = ts->indices[slot];
        if (idx == TOKENSET_EMPTY) break;
        /* check match */
        uint32_t off = g_offsets[idx];
        if (strcmp(ts->arena + off, s) == 0) /* len matches implicitly via NUL */
            return idx;
        slot = (slot + 1) & (ts->cap - 1);
    }

    /* Ensure arena has room */
    uint32_t needed = ts->arena_used + len + 1;
    if (needed > ts->arena_cap) {
        uint32_t ncap = ts->arena_cap * 2;
        while (ncap < needed) ncap *= 2;
        char* na = (char*)realloc(ts->arena, ncap);
        if (!na) return TOKENSET_EMPTY;
        ts->arena     = na;
        ts->arena_cap = ncap;
    }

    /* Ensure offset table has room */
    if (ts->count + 1 > g_offsets_cap) {
        uint32_t ncap = g_offsets_cap == 0 ? 256 : g_offsets_cap * 2;
        if (!tokenset_grow_offsets(ncap)) return TOKENSET_EMPTY;
    }

    /* Store string */
    uint32_t off = ts->arena_used;
    memcpy(ts->arena + off, s, len);
    ts->arena[off + len] = '\0';
    ts->arena_used       = off + len + 1;

    uint32_t new_idx = ts->count++;
    g_offsets[new_idx] = off;
    ts->indices[slot]  = new_idx;

    /* Rehash if load > 0.7 */
    if (ts->count * 10 > ts->cap * 7) {
        uint32_t  ncap     = ts->cap * 2;
        uint32_t* new_idx_arr = (uint32_t*)malloc(ncap * sizeof(uint32_t));
        if (!new_idx_arr) return new_idx; /* skip rehash on OOM */
        memset(new_idx_arr, 0xFF, ncap * sizeof(uint32_t));
        for (uint32_t i = 0; i < ts->cap; i++) {
            uint32_t ii = ts->indices[i];
            if (ii == TOKENSET_EMPTY) continue;
            const char* str = ts->arena + g_offsets[ii];
            uint32_t h2 = fnv1a(str, (uint32_t)strlen(str));
            uint32_t s2 = h2 & (ncap - 1);
            while (new_idx_arr[s2] != TOKENSET_EMPTY)
                s2 = (s2 + 1) & (ncap - 1);
            new_idx_arr[s2] = ii;
        }
        free(ts->indices);
        ts->indices = new_idx_arr;
        ts->cap     = ncap;
    }

    return new_idx;
}

/* ====================================================================
 * Dynamic arrays (realloc-based doubling)
 * ==================================================================== */

typedef struct { uint32_t* data; uint32_t count; uint32_t cap; } DynU32;
typedef struct { LusdFieldEntry* data; uint32_t count; uint32_t cap; } DynField;
typedef struct { LusdSpecEntry*  data; uint32_t count; uint32_t cap; } DynSpec;
typedef struct { LusdTimeSample* data; uint32_t count; uint32_t cap; } DynTimeSample;

static bool dyn_u32_push(DynU32* d, uint32_t v) {
    if (d->count >= d->cap) {
        uint32_t nc = d->cap ? d->cap * 2 : 64;
        uint32_t* nb = (uint32_t*)realloc(d->data, nc * sizeof(uint32_t));
        if (!nb) return false;
        d->data = nb; d->cap = nc;
    }
    d->data[d->count++] = v;
    return true;
}

static bool dyn_field_push(DynField* d, LusdFieldEntry fe) {
    if (d->count >= d->cap) {
        uint32_t nc = d->cap ? d->cap * 2 : 64;
        LusdFieldEntry* nb = (LusdFieldEntry*)realloc(d->data, nc * sizeof(LusdFieldEntry));
        if (!nb) return false;
        d->data = nb; d->cap = nc;
    }
    d->data[d->count++] = fe;
    return true;
}

static bool dyn_spec_push(DynSpec* d, LusdSpecEntry se) {
    if (d->count >= d->cap) {
        uint32_t nc = d->cap ? d->cap * 2 : 64;
        LusdSpecEntry* nb = (LusdSpecEntry*)realloc(d->data, nc * sizeof(LusdSpecEntry));
        if (!nb) return false;
        d->data = nb; d->cap = nc;
    }
    d->data[d->count++] = se;
    return true;
}

static bool dyn_ts_push(DynTimeSample* d, LusdTimeSample v) {
    if (d->count >= d->cap) {
        uint32_t nc = d->cap ? d->cap * 2 : 16;
        LusdTimeSample* nb = (LusdTimeSample*)realloc(d->data, nc * sizeof(LusdTimeSample));
        if (!nb) return false;
        d->data = nb; d->cap = nc;
    }
    d->data[d->count++] = v;
    return true;
}

/* ====================================================================
 * Type keyword table
 * ==================================================================== */

typedef struct { const char* kw; uint8_t type_id; bool is_array; } UsdaTypeKw;

static const UsdaTypeKw USDA_TYPE_TABLE[] = {
    /* scalars */
    { "bool",          LUSD_CRATE_BOOL,      false },
    { "int",           LUSD_CRATE_INT,       false },
    { "uint",          LUSD_CRATE_UINT,      false },
    { "int64",         LUSD_CRATE_INT64,     false },
    { "uint64",        LUSD_CRATE_UINT64,    false },
    { "half",          LUSD_CRATE_HALF,      false },
    { "float",         LUSD_CRATE_FLOAT,     false },
    { "double",        LUSD_CRATE_DOUBLE,    false },
    { "string",        LUSD_CRATE_STRING,    false },
    { "token",         LUSD_CRATE_TOKEN,     false },
    { "asset",         LUSD_CRATE_ASSET_PATH,false },
    { "matrix2d",      LUSD_CRATE_MATRIX2D,  false },
    { "matrix3d",      LUSD_CRATE_MATRIX3D,  false },
    { "matrix4d",      LUSD_CRATE_MATRIX4D,  false },
    { "quatd",         LUSD_CRATE_QUATD,     false },
    { "quatf",         LUSD_CRATE_QUATF,     false },
    { "quath",         LUSD_CRATE_QUATH,     false },
    { "float2",        LUSD_CRATE_VEC2F,     false },
    { "float3",        LUSD_CRATE_VEC3F,     false },
    { "float4",        LUSD_CRATE_VEC4F,     false },
    { "double2",       LUSD_CRATE_VEC2D,     false },
    { "double3",       LUSD_CRATE_VEC3D,     false },
    { "double4",       LUSD_CRATE_VEC4D,     false },
    { "int2",          LUSD_CRATE_VEC2I,     false },
    { "int3",          LUSD_CRATE_VEC3I,     false },
    { "int4",          LUSD_CRATE_VEC4I,     false },
    { "point3f",       LUSD_CRATE_VEC3F,     false },
    { "normal3f",      LUSD_CRATE_VEC3F,     false },
    { "vector3f",      LUSD_CRATE_VEC3F,     false },
    { "color3f",       LUSD_CRATE_VEC3F,     false },
    { "color4f",       LUSD_CRATE_VEC4F,     false },
    { "texCoord2f",    LUSD_CRATE_VEC2F,     false },
    /* array variants */
    { "bool[]",        LUSD_CRATE_BOOL,      true  },
    { "int[]",         LUSD_CRATE_INT,       true  },
    { "uint[]",        LUSD_CRATE_UINT,      true  },
    { "int64[]",       LUSD_CRATE_INT64,     true  },
    { "uint64[]",      LUSD_CRATE_UINT64,    true  },
    { "half[]",        LUSD_CRATE_HALF,      true  },
    { "float[]",       LUSD_CRATE_FLOAT,     true  },
    { "double[]",      LUSD_CRATE_DOUBLE,    true  },
    { "string[]",      LUSD_CRATE_STRING,    true  },
    { "token[]",       LUSD_CRATE_TOKEN,     true  },
    { "asset[]",       LUSD_CRATE_ASSET_PATH,true  },
    { "matrix2d[]",    LUSD_CRATE_MATRIX2D,  true  },
    { "matrix3d[]",    LUSD_CRATE_MATRIX3D,  true  },
    { "matrix4d[]",    LUSD_CRATE_MATRIX4D,  true  },
    { "quatd[]",       LUSD_CRATE_QUATD,     true  },
    { "quatf[]",       LUSD_CRATE_QUATF,     true  },
    { "quath[]",       LUSD_CRATE_QUATH,     true  },
    { "float2[]",      LUSD_CRATE_VEC2F,     true  },
    { "float3[]",      LUSD_CRATE_VEC3F,     true  },
    { "float4[]",      LUSD_CRATE_VEC4F,     true  },
    { "double2[]",     LUSD_CRATE_VEC2D,     true  },
    { "double3[]",     LUSD_CRATE_VEC3D,     true  },
    { "double4[]",     LUSD_CRATE_VEC4D,     true  },
    { "int2[]",        LUSD_CRATE_VEC2I,     true  },
    { "int3[]",        LUSD_CRATE_VEC3I,     true  },
    { "int4[]",        LUSD_CRATE_VEC4I,     true  },
    { "point3f[]",     LUSD_CRATE_VEC3F,     true  },
    { "normal3f[]",    LUSD_CRATE_VEC3F,     true  },
    { "vector3f[]",    LUSD_CRATE_VEC3F,     true  },
    { "color3f[]",     LUSD_CRATE_VEC3F,     true  },
    { "color4f[]",     LUSD_CRATE_VEC4F,     true  },
    { "texCoord2f[]",  LUSD_CRATE_VEC2F,     true  },
    { "prmvars:normals",     LUSD_CRATE_VEC3F, true }, /* typo variant in Blender exports */
    { "prmvars:normals[]",   LUSD_CRATE_VEC3F, true },
    { NULL, 0, false }
};

static bool type_lookup(const char* kw, uint8_t* type_id_out, bool* is_array_out) {
    for (int i = 0; USDA_TYPE_TABLE[i].kw; i++) {
        if (strcmp(USDA_TYPE_TABLE[i].kw, kw) == 0) {
            *type_id_out  = USDA_TYPE_TABLE[i].type_id;
            *is_array_out = USDA_TYPE_TABLE[i].is_array;
            return true;
        }
    }
    return false;
}

/* ====================================================================
 * Parse context (passed through recursive descent)
 * ==================================================================== */

typedef struct {
    TokenSet*     tokens;
    DynField*     fields;
    DynU32*       fieldsets;
    DynSpec*      specs;
    DynU32*       root_specs;
    DynTimeSample* time_samples;  /* all time-sample entries for this parse */
    LusdLayer_T*  layer;
} ParseCtx;

/* Intern a string of given length as a token; return index */
static uint32_t intern(ParseCtx* ctx, const char* s, uint32_t len) {
    return tokenset_intern(ctx->tokens, s, len);
}

static uint32_t intern_cstr(ParseCtx* ctx, const char* s) {
    return intern(ctx, s, (uint32_t)strlen(s));
}

/* ====================================================================
 * String arena helper (for paths)
 * ==================================================================== */

/* Append a string to the layer's string_arena, return offset.
   Returns ~0u on OOM. */
static uint32_t arena_store(LusdLayer_T* L, const char* s, uint32_t len) {
    uint32_t needed = L->string_arena_used + len + 1;
    if (needed > L->string_arena_size) {
        uint32_t ncap = L->string_arena_size ? L->string_arena_size * 2 : 4096;
        while (ncap < needed) ncap *= 2;
        char* na = (char*)realloc(L->string_arena, ncap);
        if (!na) return 0xFFFFFFFFu;
        L->string_arena      = na;
        L->string_arena_size = ncap;
    }
    uint32_t off = L->string_arena_used;
    memcpy(L->string_arena + off, s, len);
    L->string_arena[off + len] = '\0';
    L->string_arena_used       = off + len + 1;
    return off;
}

/* Growing path table */
static uint32_t* g_path_offsets     = NULL; /* arena offsets per path */
static uint32_t  g_path_offsets_cap = 0;

static bool path_table_push(LusdLayer_T* L, const char* full_path, uint32_t* idx_out) {
    uint32_t off = arena_store(L, full_path, (uint32_t)strlen(full_path));
    if (off == 0xFFFFFFFFu) return false;

    /* Grow paths array */
    if (L->path_count + 1 > g_path_offsets_cap) {
        uint32_t nc = g_path_offsets_cap ? g_path_offsets_cap * 2 : 64;
        uint32_t* nb = (uint32_t*)realloc(g_path_offsets, nc * sizeof(uint32_t));
        if (!nb) return false;
        g_path_offsets     = nb;
        g_path_offsets_cap = nc;
    }

    uint32_t idx = L->path_count++;
    g_path_offsets[idx] = off;
    *idx_out = idx;
    return true;
}


/* ====================================================================
 * ValueRep construction helpers
 * ==================================================================== */

static LusdValueRep make_vrep_inlined(uint8_t type_id, uint64_t payload) {
    LusdValueRep r;
    r.data = LUSD_VREP_IS_INLINED_BIT
           | ((uint64_t)type_id << LUSD_VREP_TYPE_SHIFT)
           | (payload & LUSD_VREP_PAYLOAD_MASK);
    return r;
}

static LusdValueRep make_vrep_array(uint8_t type_id, uint64_t text_offset) {
    LusdValueRep r;
    r.data = LUSD_VREP_IS_ARRAY_BIT
           | ((uint64_t)type_id << LUSD_VREP_TYPE_SHIFT)
           | (text_offset & LUSD_VREP_PAYLOAD_MASK);
    return r;
}

static LusdValueRep make_vrep_nonarray(uint8_t type_id, uint64_t text_offset) {
    LusdValueRep r;
    r.data = ((uint64_t)type_id << LUSD_VREP_TYPE_SHIFT)
           | (text_offset & LUSD_VREP_PAYLOAD_MASK);
    return r;
}

/* ====================================================================
 * Try to pack a scalar value as IsInlined (fast path)
 * Returns a valid vrep (IsInlined=1) or LUSD_NULL_VREP (caller stores as text)
 * ==================================================================== */
static LusdValueRep try_inline_scalar(UsdaP* p_saved, UsdaP* p,
                                       uint8_t type_id, bool is_array,
                                       ParseCtx* ctx) {
    (void)p_saved;
    if (is_array) return LUSD_NULL_VREP; /* arrays are never inlined */

    usda_skip_ws(p);

    switch (type_id) {
        case LUSD_CRATE_BOOL: {
            bool v;
            UsdaP tmp = *p;
            if (usda_parse_bool(&tmp, &v)) {
                *p = tmp;
                return make_vrep_inlined(LUSD_CRATE_BOOL, v ? 1u : 0u);
            }
            break;
        }
        case LUSD_CRATE_INT: {
            int32_t v;
            UsdaP tmp = *p;
            if (usda_parse_int32(&tmp, &v)) {
                *p = tmp;
                return make_vrep_inlined(LUSD_CRATE_INT, (uint64_t)(uint32_t)v);
            }
            break;
        }
        case LUSD_CRATE_UINT: {
            int32_t v;
            UsdaP tmp = *p;
            if (usda_parse_int32(&tmp, &v)) {
                *p = tmp;
                return make_vrep_inlined(LUSD_CRATE_UINT, (uint64_t)(uint32_t)v);
            }
            break;
        }
        case LUSD_CRATE_FLOAT: {
            float v;
            UsdaP tmp = *p;
            if (usda_parse_float32(&tmp, &v)) {
                *p = tmp;
                uint32_t bits;
                memcpy(&bits, &v, 4);
                return make_vrep_inlined(LUSD_CRATE_FLOAT, bits);
            }
            break;
        }
        case LUSD_CRATE_TOKEN: {
            usda_skip_ws(p);
            char tbuf[512];
            bool got = false;
            if (usda_peek(p) == '"') {
                got = usda_parse_string(p, tbuf, sizeof(tbuf));
            } else {
                uint32_t n = usda_read_ident(p, tbuf, sizeof(tbuf));
                got = (n > 0);
            }
            if (got) {
                uint32_t idx = intern_cstr(ctx, tbuf);
                if (idx != TOKENSET_EMPTY)
                    return make_vrep_inlined(LUSD_CRATE_TOKEN, idx);
            }
            break;
        }
        default:
            break;
    }
    return LUSD_NULL_VREP;
}

/* ====================================================================
 * Emit a field + fieldset index
 * ==================================================================== */

static bool emit_field(ParseCtx* ctx,
                        uint32_t token_idx,
                        LusdValueRep vrep) {
    LusdFieldEntry fe;
    memset(&fe, 0, sizeof(fe));
    fe.token_index = token_idx;
    fe.value_rep   = vrep;
    if (!dyn_field_push(ctx->fields, fe)) return false;

    uint32_t field_idx = ctx->fields->count - 1;
    if (!dyn_u32_push(ctx->fieldsets, field_idx)) return false;
    return true;
}

/* ====================================================================
 * parse_timesample_dict
 *
 * Parses a USDA time-samples dictionary:  { t0: value, t1: value, ... }
 * Each value text range is recorded in ctx->time_samples.  Identical value
 * texts (same bytes) are deduplicated: canonical_idx points to the first
 * occurrence so Lydra can share materialized buffers.
 *
 * Returns true on success; *start_out / *count_out receive the slice into
 * ctx->time_samples that belongs to this attribute.
 * ==================================================================== */
static bool parse_timesample_dict(UsdaP* p, ParseCtx* ctx,
                                   uint8_t type_id, bool is_array,
                                   uint32_t* start_out,
                                   uint32_t* count_out) {
    usda_skip_ws(p);
    if (usda_at_end(p) || usda_peek(p) != '{') return false;
    usda_next(p); /* consume '{' */

    uint32_t start = ctx->time_samples->count;

    for (;;) {
        usda_skip_ws(p);
        if (usda_at_end(p) || usda_peek(p) == '}') break;

        /* Parse time code (strtod advances past the number) */
        char* endp;
        double tc = strtod(p->p, &endp);
        if (endp == p->p) {
            /* not a number — skip to end of entry */
            while (!usda_at_end(p) && usda_peek(p) != ',' && usda_peek(p) != '}')
                usda_next(p);
            if (!usda_at_end(p) && usda_peek(p) == ',') usda_next(p);
            continue;
        }
        /* Advance UsdaP past the number (tracking newlines) */
        while (p->p < endp) usda_next(p);

        usda_skip_ws(p);
        if (usda_at_end(p) || usda_peek(p) != ':') {
            while (!usda_at_end(p) && usda_peek(p) != ',' && usda_peek(p) != '}')
                usda_next(p);
            if (!usda_at_end(p) && usda_peek(p) == ',') usda_next(p);
            continue;
        }
        usda_next(p); /* consume ':' */
        usda_skip_ws(p);

        /* Record where the value text starts */
        uint64_t text_start = (uint64_t)(p->p - p->base);
        usda_skip_value(p);
        uint64_t text_end = (uint64_t)(p->p - p->base);
        uint32_t text_len = (uint32_t)(text_end - text_start);

        /* FNV-32 hash of value text for dedup */
        uint32_t hash = fnv1a(p->base + text_start, text_len);

        /* Dedup: compare against previous entries for this attribute */
        uint32_t own_idx = ctx->time_samples->count;
        uint32_t canonical = own_idx; /* default: self is canonical */
        for (uint32_t k = start; k < own_idx; k++) {
            LusdTimeSample* prev = &ctx->time_samples->data[k];
            if (prev->text_len == text_len && prev->text_hash == hash) {
                if (memcmp(p->base + prev->text_offset,
                           p->base + text_start, text_len) == 0) {
                    canonical = prev->canonical_idx; /* transitively canonical */
                    break;
                }
            }
        }

        LusdTimeSample e;
        e.time         = tc;
        e.text_offset  = text_start;
        e.text_len     = text_len;
        e.canonical_idx= canonical;
        e.text_hash    = hash;
        e.type_id      = type_id;
        e.is_array     = is_array ? 1 : 0;
        e._pad[0]      = 0;
        e._pad[1]      = 0;
        if (!dyn_ts_push(ctx->time_samples, e)) return false;

        /* skip optional comma between entries */
        usda_skip_ws(p);
        if (!usda_at_end(p) && usda_peek(p) == ',') usda_next(p);
    }

    if (!usda_at_end(p)) usda_next(p); /* consume '}' */

    *start_out = start;
    *count_out = ctx->time_samples->count - start;
    return true;
}

/* ====================================================================
 * parse_attribute
 *
 * Parses one attribute line from a prim body.
 * On entry p is positioned just before the type keyword (qualifier already
 * consumed by caller if any).
 *
 * Syntax:
 *   [qualifier] TypeName["] AttrName [= value] [(metadata)]
 * ==================================================================== */

static bool parse_attribute(UsdaP* p, ParseCtx* ctx,
                              const char* first_word) {
    /* first_word may be a qualifier ("uniform","varying","custom","rel") or type */
    char type_kw[128];
    const char* kw = first_word;

    /* Skip known qualifiers, then read type keyword */
    if (strcmp(first_word, "uniform")  == 0 ||
        strcmp(first_word, "varying")  == 0 ||
        strcmp(first_word, "custom")   == 0 ||
        strcmp(first_word, "prepend")  == 0 ||
        strcmp(first_word, "append")   == 0 ||
        strcmp(first_word, "delete")   == 0) {
        usda_skip_ws(p);
        usda_read_ident(p, type_kw, sizeof(type_kw));
        kw = type_kw;
    }

    /* Handle relationships */
    if (strcmp(kw, "rel") == 0) {
        /* rel attrname [= <target_path>] [(metadata)] */
        usda_skip_ws(p);
        char rbuf[256]; usda_read_ident(p, rbuf, sizeof(rbuf));

        /* Also read colon-separated parts (e.g. "material:binding") */
        while (!usda_at_end(p) && usda_peek(p) == ':') {
            size_t len = strlen(rbuf);
            if (len + 1 < sizeof(rbuf)) { rbuf[len] = ':'; rbuf[len+1] = '\0'; }
            usda_next(p); /* consume ':' */
            char tmp_id[128];
            usda_read_ident(p, tmp_id, sizeof(tmp_id));
            size_t tlen = strlen(tmp_id);
            len = strlen(rbuf);
            if (len + tlen < sizeof(rbuf)) {
                memcpy(rbuf + len, tmp_id, tlen + 1);
            }
        }

        usda_skip_ws(p);
        if (usda_peek(p) == '=') {
            usda_next(p);
            usda_skip_ws(p);

            /* Try to parse <path> target for material:binding */
            if (usda_peek(p) == '<') {
                usda_next(p); /* consume '<' */
                char path_buf[512];
                size_t pi = 0;
                while (!usda_at_end(p) && usda_peek(p) != '>') {
                    if (pi + 1 < sizeof(path_buf))
                        path_buf[pi++] = usda_next(p);
                    else
                        usda_next(p);
                }
                path_buf[pi] = '\0';
                if (!usda_at_end(p)) usda_next(p); /* consume '>' */

                /* Store as a token field named after the relationship */
                if (pi > 0) {
                    uint32_t rel_tok = intern_cstr(ctx, rbuf);
                    uint32_t path_tok = intern_cstr(ctx, path_buf);
                    if (rel_tok != TOKENSET_EMPTY && path_tok != TOKENSET_EMPTY) {
                        LusdValueRep vrep = make_vrep_inlined(LUSD_CRATE_TOKEN, path_tok);
                        emit_field(ctx, rel_tok, vrep);
                    }
                }
            } else {
                usda_skip_value(p);
            }
        }
        if (usda_peek(p) == '(') {
            usda_next(p);
            usda_skip_balanced(p, '(', ')');
        }
        return true;
    }

    /* Check for [] suffix on type keyword */
    char full_kw[192];
    size_t kw_len = strlen(kw);
    memcpy(full_kw, kw, kw_len + 1);
    usda_skip_ws(p);
    if (usda_peek(p) == '[') {
        /* check next char for ']' */
        UsdaP tmp = *p;
        usda_next(&tmp); /* consume '[' */
        if (!usda_at_end(&tmp) && usda_peek(&tmp) == ']') {
            usda_next(&tmp); /* consume ']' */
            if (kw_len + 2 < sizeof(full_kw)) {
                full_kw[kw_len]   = '[';
                full_kw[kw_len+1] = ']';
                full_kw[kw_len+2] = '\0';
                *p = tmp;
            }
        }
    }

    uint8_t type_id;
    bool    is_array;
    if (!type_lookup(full_kw, &type_id, &is_array)) {
        /* Unknown type — skip the whole line/value */
        usda_skip_ws(p);
        char namebuf[256]; usda_read_ident(p, namebuf, sizeof(namebuf));
        usda_skip_ws(p);
        if (usda_peek(p) == '=') { usda_next(p); usda_skip_value(p); }
        if (usda_peek(p) == '(') { usda_next(p); usda_skip_balanced(p, '(', ')'); }
        return true;
    }

    /* Read attribute name (may include ".timeSamples" suffix) */
    usda_skip_ws(p);
    char attr_name[256];
    usda_read_ident(p, attr_name, sizeof(attr_name));
    if (attr_name[0] == '\0') return true; /* malformed, skip */

    /* Detect ".timeSamples" suffix: e.g. "translations.timeSamples" */
    bool is_ts = false;
    {
        char* dot = strrchr(attr_name, '.');
        if (dot && strcmp(dot + 1, "timeSamples") == 0) {
            is_ts = true;
            *dot = '\0'; /* trim suffix — field name becomes "translations" */
        }
    }

    /* Detect ".connect" suffix: e.g. "inputs:diffuseColor.connect = <path>"
     * Store the connection target as a TOKEN field named "inputs:X.connect". */
    {
        char* dot = strrchr(attr_name, '.');
        if (dot && strcmp(dot + 1, "connect") == 0) {
            usda_skip_ws(p);
            if (usda_peek(p) != '=') {
                /* declared without value — skip metadata */
                if (usda_peek(p) == '(') { usda_next(p); usda_skip_balanced(p, '(', ')'); }
                return true;
            }
            usda_next(p); /* consume '=' */
            usda_skip_ws(p);
            if (usda_peek(p) == '<') {
                usda_next(p); /* consume '<' */
                char path_buf[512];
                size_t pi2 = 0;
                while (!usda_at_end(p) && usda_peek(p) != '>') {
                    if (pi2 + 1 < sizeof(path_buf))
                        path_buf[pi2++] = usda_next(p);
                    else
                        usda_next(p);
                }
                path_buf[pi2] = '\0';
                if (!usda_at_end(p)) usda_next(p); /* consume '>' */
                if (pi2 > 0) {
                    uint32_t conn_tok  = intern_cstr(ctx, attr_name);
                    uint32_t path_tok2 = intern_cstr(ctx, path_buf);
                    if (conn_tok != TOKENSET_EMPTY && path_tok2 != TOKENSET_EMPTY) {
                        LusdValueRep vrep2 = make_vrep_inlined(LUSD_CRATE_TOKEN, path_tok2);
                        emit_field(ctx, conn_tok, vrep2);
                    }
                }
            } else {
                usda_skip_value(p);
            }
            if (usda_peek(p) == '(') { usda_next(p); usda_skip_balanced(p, '(', ')'); }
            return true;
        }
    }

    uint32_t attr_tok = intern_cstr(ctx, attr_name);
    if (attr_tok == TOKENSET_EMPTY) return false;

    /* Check for '=' */
    usda_skip_ws(p);
    if (usda_peek(p) != '=') {
        /* Attribute declared without value */
        if (usda_peek(p) == '(') { usda_next(p); usda_skip_balanced(p, '(', ')'); }
        return true;
    }
    usda_next(p); /* consume '=' */
    usda_skip_ws(p);

    LusdValueRep vrep = LUSD_NULL_VREP;

    if (is_ts) {
        /* Time-sampled attribute: parse "{ t0: value, t1: value, ... }" */
        uint32_t ts_start = 0, ts_count = 0;
        if (!parse_timesample_dict(p, ctx, type_id, is_array, &ts_start, &ts_count))
            return false;
        /* Encode start (bits 47-24) and count (bits 23-0) in payload */
        uint64_t ts_payload = ((uint64_t)ts_start << 24) | (uint64_t)(ts_count & 0xFFFFFFu);
        vrep.data = ((uint64_t)LUSD_CRATE_TIME_SAMPLES << LUSD_VREP_TYPE_SHIFT) | ts_payload;
    } else if (!is_array) {
        /* Record text offset for lazy decode */
        uint64_t val_offset = (uint64_t)(p->p - p->base);
        /* Try inlining small scalars */
        UsdaP tmp = *p;
        vrep = try_inline_scalar(p, &tmp, type_id, false, ctx);
        if (!lusd_vrep_is_null(vrep)) {
            *p = tmp; /* advance past consumed value */
        } else {
            /* Store as text offset */
            vrep = make_vrep_nonarray(type_id, val_offset);
            usda_skip_value(p);
        }
    } else {
        /* Array: store text offset pointing at '[' */
        uint64_t val_offset = (uint64_t)(p->p - p->base);
        vrep = make_vrep_array(type_id, val_offset);
        usda_skip_value(p);
    }

    /* Skip optional metadata block */
    usda_skip_ws(p);
    if (usda_peek(p) == '(') {
        usda_next(p);
        usda_skip_balanced(p, '(', ')');
    }

    if (!emit_field(ctx, attr_tok, vrep)) return false;
    return true;
}

/* ====================================================================
 * Forward declaration for mutual recursion
 * ==================================================================== */
static bool parse_prim_def(UsdaP* p, ParseCtx* ctx, const char* parent_path);

/* ====================================================================
 * parse_prim_body_item
 * ==================================================================== */
static bool parse_prim_body_item(UsdaP* p, ParseCtx* ctx,
                                  const char* prim_path) {
    usda_skip_ws(p);
    if (usda_at_end(p)) return true;

    char word[256];
    usda_read_ident(p, word, sizeof(word));
    if (word[0] == '\0') {
        /* Could be '}' or something unexpected */
        return true;
    }

    /* Nested prim definition */
    if (strcmp(word, "def")   == 0 ||
        strcmp(word, "over")  == 0 ||
        strcmp(word, "class") == 0) {
        return parse_prim_def(p, ctx, prim_path);
    }

    /* Otherwise it's an attribute */
    return parse_attribute(p, ctx, word);
}

/* ====================================================================
 * parse_prim_def
 *
 * Consumes: ("def"|"over"|"class") [TypeName] "Name" [(meta)] { body }
 * ==================================================================== */
static bool parse_prim_def(UsdaP* p, ParseCtx* ctx, const char* parent_path) {
    /* optional type name + required prim name (both may or may not be present) */
    usda_skip_ws(p);

    /* Peek: if the next token is a quoted string it's the prim name directly.
       Otherwise it's a type name followed by the prim name. */
    char type_name[256] = "";
    char prim_name[256] = "";

    char first[256];
    usda_read_ident(p, first, sizeof(first));

    usda_skip_ws(p);
    if (usda_peek(p) == '"') {
        /* first == type name, next token is quoted prim name */
        memcpy(type_name, first, sizeof(type_name));
        usda_parse_string(p, prim_name, sizeof(prim_name));
    } else {
        /* first == prim name (unquoted) — treat it as the name */
        memcpy(prim_name, first, sizeof(prim_name));
    }

    if (prim_name[0] == '\0') return false;

    /* Build full path */
    char full_path[1024];
    size_t parent_len = strlen(parent_path);
    if (parent_len == 1 && parent_path[0] == '/') {
        /* root */
        snprintf(full_path, sizeof(full_path), "/%s", prim_name);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", parent_path, prim_name);
    }

    /* Skip optional prim metadata block */
    usda_skip_ws(p);
    if (usda_peek(p) == '(') {
        usda_next(p);
        usda_skip_balanced(p, '(', ')');
        usda_skip_ws(p);
    }

    /* Expect '{' */
    if (!usda_expect(p, '{')) return false;

    /* Add path to path table */
    uint32_t path_idx;
    if (!path_table_push(ctx->layer, full_path, &path_idx)) return false;

    /* Intern type name token */
    uint32_t type_tok = intern_cstr(ctx, type_name);
    if (type_tok == TOKENSET_EMPTY) return false;
    uint32_t name_tok = intern_cstr(ctx, prim_name);
    if (name_tok == TOKENSET_EMPTY) return false;

    /* Create spec entry — fieldset starts at current fieldsets count */
    uint32_t fieldset_start = ctx->fieldsets->count;

    LusdSpecEntry se;
    se.path_index     = path_idx;
    se.fieldset_index = fieldset_start;
    se.spec_type      = LUSD_SPEC_TYPE_PRIM;
    if (!dyn_spec_push(ctx->specs, se)) return false;

    uint32_t spec_idx = ctx->specs->count - 1;

    /* Emit "typeName" field if we have one */
    if (type_name[0] != '\0') {
        LusdValueRep vrep = make_vrep_inlined(LUSD_CRATE_TOKEN, type_tok);
        if (!emit_field(ctx, intern_cstr(ctx, "typeName"), vrep)) return false;
    }

    /* Is this a root prim? */
    bool is_root = (strlen(parent_path) == 1 && parent_path[0] == '/');
    if (is_root) {
        if (!dyn_u32_push(ctx->root_specs, spec_idx)) return false;
    }

    /* Parse body — two-pass approach to keep the fieldset layout correct.
     *
     * Problem: if child prims are parsed inside the body loop, their fields
     * get appended to the fieldsets array before the parent's sentinel, making
     * the parent's fieldset inadvertently include child fields (e.g. typeName).
     *
     * Solution: defer child prim positions, process all attributes first,
     * emit the parent sentinel, then recurse into child prims.
     */

    /* Deferred child prim positions: up to 64 children */
#define MAX_DEFERRED_CHILDREN 64
    const char* deferred_pos[MAX_DEFERRED_CHILDREN];
    uint32_t    deferred_line[MAX_DEFERRED_CHILDREN];
    uint32_t    deferred_count = 0;

    /* --- Single pass: process attributes inline, defer child prims -- */
    for (;;) {
        usda_skip_ws(p);
        if (usda_at_end(p) || usda_peek(p) == '}') {
            if (!usda_at_end(p)) usda_next(p); /* consume '}' */
            break;
        }
        /* Peek at the next keyword */
        const char* saved_p    = p->p;
        uint32_t    saved_line = p->line;
        char kw[16];
        usda_read_ident(p, kw, sizeof(kw));

        if (strcmp(kw, "def") == 0 || strcmp(kw, "over") == 0 ||
            strcmp(kw, "class") == 0) {
            /* Defer child prim — record position BEFORE keyword */
            if (deferred_count < MAX_DEFERRED_CHILDREN) {
                deferred_pos[deferred_count]  = saved_p;
                deferred_line[deferred_count] = saved_line;
                deferred_count++;
            }
            /* Skip the child prim block: find '{' then skip balanced */
            bool in_str = false, in_asset = false;
            while (!usda_at_end(p)) {
                char c = usda_peek(p);
                if (in_str) {
                    usda_next(p);
                    if (c == '"') in_str = false;
                } else if (in_asset) {
                    usda_next(p);
                    if (c == '@') in_asset = false;
                } else {
                    if (c == '"') { in_str = true; usda_next(p); }
                    else if (c == '@') { in_asset = true; usda_next(p); }
                    else if (c == '(') { usda_next(p); usda_skip_balanced(p, '(', ')'); }
                    else if (c == '{') {
                        usda_next(p); /* consume '{' */
                        usda_skip_balanced(p, '{', '}');
                        break; /* done skipping this child */
                    } else usda_next(p);
                }
            }
        } else {
            /* Attribute: restore pos and parse normally */
            p->p    = saved_p;
            p->line = saved_line;
            if (!parse_prim_body_item(p, ctx, full_path)) return false;
        }
    }

    /* Emit parent sentinel — closes this prim's own fieldset */
    if (!dyn_u32_push(ctx->fieldsets, 0xFFFFFFFFu)) return false;

    /* --- Process deferred child prims ----------------------------- */
    for (uint32_t di = 0; di < deferred_count; di++) {
        /* Reconstruct a local parser cursor at the saved position */
        UsdaP cp;
        cp.p    = deferred_pos[di];
        cp.end  = p->end;
        cp.base = p->base;
        cp.line = deferred_line[di];
        /* Read the keyword (def/over/class) again and dispatch */
        char kw2[16];
        usda_read_ident(&cp, kw2, sizeof(kw2));
        if (!parse_prim_def(&cp, ctx, full_path)) return false;
        /* Advance main parser cursor past this child (already skipped above) */
    }
#undef MAX_DEFERRED_CHILDREN

    return true;
}

/* ====================================================================
 * parse_layer_metas  —  ( key = value ... )
 * ==================================================================== */
static void parse_layer_metas(UsdaP* p, LusdLayer_T* L) {
    /* consume '(' already done by caller */
    for (;;) {
        usda_skip_ws(p);
        if (usda_at_end(p) || usda_peek(p) == ')') break;

        char key[128];
        usda_read_ident(p, key, sizeof(key));
        if (key[0] == '\0') { usda_next(p); continue; } /* skip bad char */

        usda_skip_ws(p);
        if (!usda_expect(p, '=')) {
            /* no value — skip */
            usda_skip_value(p);
            continue;
        }
        usda_skip_ws(p);

        if (strcmp(key, "metersPerUnit") == 0) {
            float v; if (usda_parse_float32(p, &v)) L->metas.meters_per_unit = (double)v;
            else usda_skip_value(p);
        } else if (strcmp(key, "upAxis") == 0) {
            char sv[16]; usda_parse_string(p, sv, sizeof(sv));
            if (sv[0] == 'Y' || sv[0] == 'y') L->metas.up_axis = 0;
            else if (sv[0] == 'Z' || sv[0] == 'z') L->metas.up_axis = 1;
            else if (sv[0] == 'X' || sv[0] == 'x') L->metas.up_axis = 2;
        } else if (strcmp(key, "doc") == 0) {
            /* read and discard */
            usda_skip_value(p);
        } else {
            usda_skip_value(p);
        }
    }
    usda_expect(p, ')');
}

/* ====================================================================
 * lusd__layer_read_usda  —  entry point
 * ==================================================================== */

LusdResult lusd__layer_read_usda(LusdLayer_T* L,
                                  const uint8_t* data,
                                  uint64_t       size) {
    if (!L || !data || size == 0) return LUSD_ERROR_INVALID_ARGUMENT;

    /* --- Set up file buffer ---------------------------------------- */
    L->file_data   = data;
    L->file_size   = size;
    L->format      = LUSD_FORMAT_USDA;

    /* Reset layer tables */
    L->metas.meters_per_unit  = 0.01;
    L->metas.up_axis          = 0; /* Y */
    L->metas.frames_per_second = 24.0;

    /* Reset global static arrays (single-threaded) */
    g_offsets         = NULL;
    g_offsets_cap     = 0;
    g_path_offsets    = NULL;
    g_path_offsets_cap = 0;

    UsdaP parser;
    usda_init(&parser, (const char*)data, size);

    /* --- Check header "#usda 1.0" ---------------------------------- */
    /* Skip only whitespace (spaces, tabs, CR, LF) before the header,
       NOT '#' comments, since the header itself starts with '#'. */
    while (!usda_at_end(&parser)) {
        char c = usda_peek(&parser);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') usda_next(&parser);
        else break;
    }
    if (parser.p + 9 <= parser.end && strncmp(parser.p, "#usda 1.0", 9) == 0) {
        parser.p += 9;
        /* skip to end of line */
        while (!usda_at_end(&parser) && usda_peek(&parser) != '\n')
            usda_next(&parser);
    } else {
        return LUSD_ERROR_PARSE_FAILED;
    }

    /* --- Prepare dynamic arrays ------------------------------------ */
    TokenSet ts;
    if (!tokenset_init(&ts)) return LUSD_ERROR_OUT_OF_MEMORY;

    /* Pre-intern the empty string at index 0 */
    tokenset_intern(&ts, "", 0);

    DynField      fields     = {NULL, 0, 0};
    DynU32        fieldsets  = {NULL, 0, 0};
    DynSpec       specs      = {NULL, 0, 0};
    DynU32        root_specs = {NULL, 0, 0};
    DynTimeSample time_samps = {NULL, 0, 0};

    ParseCtx ctx;
    ctx.tokens       = &ts;
    ctx.fields       = &fields;
    ctx.fieldsets    = &fieldsets;
    ctx.specs        = &specs;
    ctx.root_specs   = &root_specs;
    ctx.time_samples = &time_samps;
    ctx.layer        = L;

    LusdResult result = LUSD_SUCCESS;

    /* --- Add pseudo-root spec "/" ---------------------------------- */
    {
        uint32_t path_idx;
        if (!path_table_push(L, "/", &path_idx)) {
            result = LUSD_ERROR_OUT_OF_MEMORY; goto cleanup;
        }
        /* pseudo-root has an empty fieldset */
        uint32_t fieldset_start = fieldsets.count;
        if (!dyn_u32_push(&fieldsets, 0xFFFFFFFFu)) {
            result = LUSD_ERROR_OUT_OF_MEMORY; goto cleanup;
        }
        LusdSpecEntry pse;
        pse.path_index     = path_idx;
        pse.fieldset_index = fieldset_start;
        pse.spec_type      = LUSD_SPEC_TYPE_PSEUDO_ROOT;
        if (!dyn_spec_push(&specs, pse)) {
            result = LUSD_ERROR_OUT_OF_MEMORY; goto cleanup;
        }
    }

    /* --- Layer metadata block ------------------------------------- */
    usda_skip_ws(&parser);
    if (!usda_at_end(&parser) && usda_peek(&parser) == '(') {
        usda_next(&parser);
        parse_layer_metas(&parser, L);
    }

    /* --- Parse top-level prim defs -------------------------------- */
    for (;;) {
        usda_skip_ws(&parser);
        if (usda_at_end(&parser)) break;

        char word[64];
        usda_read_ident(&parser, word, sizeof(word));
        if (word[0] == '\0') {
            /* skip unknown character */
            usda_next(&parser);
            continue;
        }

        if (strcmp(word, "def")   == 0 ||
            strcmp(word, "over")  == 0 ||
            strcmp(word, "class") == 0) {
            if (!parse_prim_def(&parser, &ctx, "/")) {
                result = LUSD_ERROR_PARSE_FAILED;
                goto cleanup;
            }
        } else {
            /* Unknown top-level keyword — skip line */
            while (!usda_at_end(&parser) && usda_peek(&parser) != '\n')
                usda_next(&parser);
        }
    }

    /* --- Commit all tables to L ----------------------------------- */
    {
        /* tokens[]: build char** array from arena + offsets */
        uint32_t ntok = ts.count;
        char** tokptrs = (char**)malloc(ntok * sizeof(char*));
        if (!tokptrs && ntok > 0) { result = LUSD_ERROR_OUT_OF_MEMORY; goto cleanup; }
        for (uint32_t i = 0; i < ntok; i++)
            tokptrs[i] = ts.arena + g_offsets[i];
        L->tokens      = tokptrs;
        L->token_count = ntok;
        L->token_buf   = ts.arena; /* owns the arena; freed by free_tables */
        L->token_buf_size = ts.arena_cap;
        ts.arena = NULL; /* transferred ownership */

        /* strings[]: copy of tokens[] pointers (USDA has no separate string table;
           using the same token table for simplicity) */
        L->strings      = NULL; /* not used by Lydra */
        L->string_count = 0;

        /* paths[]: build char** from string_arena + path_offsets */
        uint32_t npath = L->path_count;
        char** pathptrs = (char**)malloc(npath * sizeof(char*));
        if (!pathptrs && npath > 0) {
            free(tokptrs); L->tokens = NULL;
            result = LUSD_ERROR_OUT_OF_MEMORY; goto cleanup;
        }
        for (uint32_t i = 0; i < npath; i++)
            pathptrs[i] = L->string_arena + g_path_offsets[i];
        L->paths = pathptrs;
        /* L->path_count already set */

        /* fields[] */
        L->fields      = fields.data;
        L->field_count = fields.count;
        fields.data    = NULL; /* transferred */

        /* fieldsets[] */
        L->fieldsets             = fieldsets.data;
        L->fieldset_entry_count  = fieldsets.count;
        fieldsets.data           = NULL;

        /* specs[] */
        L->specs      = specs.data;
        L->spec_count = specs.count;
        specs.data    = NULL;

        /* root_spec_indices[] */
        L->root_spec_indices = root_specs.data;
        L->root_spec_count   = root_specs.count;
        root_specs.data      = NULL;

        /* time_samples[] */
        L->time_samples      = time_samps.data;
        L->time_sample_count = time_samps.count;
        time_samps.data      = NULL; /* transferred */
    }

    /* Fall through to cleanup — on success we just free the local arrays
       that were NOT transferred (indices table of TokenSet, etc.) */

cleanup:
    tokenset_destroy(&ts); /* frees indices; arena was transferred (or freed if ts.arena != NULL) */
    if (fields.data)     free(fields.data);
    if (fieldsets.data)  free(fieldsets.data);
    if (specs.data)      free(specs.data);
    if (root_specs.data) free(root_specs.data);
    if (time_samps.data) free(time_samps.data);
    free(g_offsets);      g_offsets = NULL;     g_offsets_cap = 0;
    free(g_path_offsets); g_path_offsets = NULL; g_path_offsets_cap = 0;

    return result;
}
