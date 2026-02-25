/*
 * lusd_write_usda.c - USDA writer implementation
 *
 * Exports a write-mode LusdStage to "#usda 1.0" text.
 * All value serialization is done inline (no separate buffer allocation
 * per element).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_write.h"
#include "internal/lusd_internal.h"
#include "internal/lusd_write_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

/* =======================================================================
 * Growing string buffer
 * ======================================================================= */

typedef struct {
    char*  data;
    size_t used;   /* bytes written (not including NUL) */
    size_t cap;    /* allocated bytes */
} StrBuf;

static bool sb_reserve(StrBuf* b, size_t extra) {
    size_t need = b->used + extra + 1;  /* +1 for NUL */
    if (need <= b->cap) return true;
    size_t new_cap = b->cap ? b->cap * 2 : 4096;
    while (new_cap < need) new_cap *= 2;
    char* np = (char*)realloc(b->data, new_cap);
    if (!np) return false;
    b->data = np;
    b->cap  = new_cap;
    return true;
}

static bool sb_append(StrBuf* b, const char* s) {
    size_t len = strlen(s);
    if (!sb_reserve(b, len)) return false;
    memcpy(b->data + b->used, s, len);
    b->used += len;
    b->data[b->used] = '\0';
    return true;
}

static bool sb_appendf(StrBuf* b, const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    if ((size_t)n < sizeof(tmp)) return sb_append(b, tmp);
    /* Large output: allocate */
    char* big = (char*)malloc((size_t)n + 1);
    if (!big) return false;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    bool ok = sb_append(b, big);
    free(big);
    return ok;
}

static void sb_free(StrBuf* b) { free(b->data); b->data = NULL; b->used = b->cap = 0; }

/* =======================================================================
 * Type keyword table
 * ======================================================================= */

/* Returns the USDA type keyword for a base LusdValueType (no ARRAY_BIT). */
static const char* usda_type_keyword(uint32_t base_type) {
    switch (base_type) {
    case LUSD_VALUE_TYPE_BOOL:       return "bool";
    case LUSD_VALUE_TYPE_INT32:      return "int";
    case LUSD_VALUE_TYPE_UINT32:     return "uint";
    case LUSD_VALUE_TYPE_INT64:      return "int64";
    case LUSD_VALUE_TYPE_UINT64:     return "uint64";
    case LUSD_VALUE_TYPE_HALF:       return "half";
    case LUSD_VALUE_TYPE_FLOAT:      return "float";
    case LUSD_VALUE_TYPE_DOUBLE:     return "double";
    case LUSD_VALUE_TYPE_STRING:     return "string";
    case LUSD_VALUE_TYPE_TOKEN:      return "token";
    case LUSD_VALUE_TYPE_ASSET_PATH: return "asset";
    case LUSD_VALUE_TYPE_TIMECODE:   return "timecode";
    case LUSD_VALUE_TYPE_INT2:       return "int2";
    case LUSD_VALUE_TYPE_INT3:       return "int3";
    case LUSD_VALUE_TYPE_INT4:       return "int4";
    case LUSD_VALUE_TYPE_HALF2:      return "half2";
    case LUSD_VALUE_TYPE_HALF3:      return "half3";
    case LUSD_VALUE_TYPE_HALF4:      return "half4";
    case LUSD_VALUE_TYPE_FLOAT2:     return "float2";
    case LUSD_VALUE_TYPE_FLOAT3:     return "float3";
    case LUSD_VALUE_TYPE_FLOAT4:     return "float4";
    case LUSD_VALUE_TYPE_DOUBLE2:    return "double2";
    case LUSD_VALUE_TYPE_DOUBLE3:    return "double3";
    case LUSD_VALUE_TYPE_DOUBLE4:    return "double4";
    case LUSD_VALUE_TYPE_QUATF:      return "quatf";
    case LUSD_VALUE_TYPE_QUATD:      return "quatd";
    case LUSD_VALUE_TYPE_QUATH:      return "quath";
    case LUSD_VALUE_TYPE_MATRIX2F:   return "matrix2f";
    case LUSD_VALUE_TYPE_MATRIX3F:   return "matrix3f";
    case LUSD_VALUE_TYPE_MATRIX4F:   return "matrix4f";
    case LUSD_VALUE_TYPE_MATRIX2D:   return "matrix2d";
    case LUSD_VALUE_TYPE_MATRIX3D:   return "matrix3d";
    case LUSD_VALUE_TYPE_MATRIX4D:   return "matrix4d";
    /* Role types */
    case LUSD_VALUE_TYPE_COLOR3H:    return "color3h";
    case LUSD_VALUE_TYPE_COLOR3F:    return "color3f";
    case LUSD_VALUE_TYPE_COLOR3D:    return "color3d";
    case LUSD_VALUE_TYPE_COLOR4H:    return "color4h";
    case LUSD_VALUE_TYPE_COLOR4F:    return "color4f";
    case LUSD_VALUE_TYPE_COLOR4D:    return "color4d";
    case LUSD_VALUE_TYPE_POINT3H:    return "point3h";
    case LUSD_VALUE_TYPE_POINT3F:    return "point3f";
    case LUSD_VALUE_TYPE_POINT3D:    return "point3d";
    case LUSD_VALUE_TYPE_VECTOR3H:   return "vector3h";
    case LUSD_VALUE_TYPE_VECTOR3F:   return "vector3f";
    case LUSD_VALUE_TYPE_VECTOR3D:   return "vector3d";
    case LUSD_VALUE_TYPE_NORMAL3H:   return "normal3h";
    case LUSD_VALUE_TYPE_NORMAL3F:   return "normal3f";
    case LUSD_VALUE_TYPE_NORMAL3D:   return "normal3d";
    case LUSD_VALUE_TYPE_TEXCOORD2H: return "texCoord2h";
    case LUSD_VALUE_TYPE_TEXCOORD2F: return "texCoord2f";
    case LUSD_VALUE_TYPE_TEXCOORD2D: return "texCoord2d";
    case LUSD_VALUE_TYPE_TEXCOORD3H: return "texCoord3h";
    case LUSD_VALUE_TYPE_TEXCOORD3F: return "texCoord3f";
    case LUSD_VALUE_TYPE_TEXCOORD3D: return "texCoord3d";
    default: return "unknown";
    }
}

/* Returns element size in bytes for a base type (0 if not supported). */
static size_t elem_byte_size(uint32_t base_type) {
    switch (base_type) {
    case LUSD_VALUE_TYPE_BOOL:   return sizeof(bool);
    case LUSD_VALUE_TYPE_INT32:
    case LUSD_VALUE_TYPE_UINT32:
    case LUSD_VALUE_TYPE_FLOAT:  return 4;
    case LUSD_VALUE_TYPE_INT64:
    case LUSD_VALUE_TYPE_UINT64:
    case LUSD_VALUE_TYPE_DOUBLE: return 8;
    case LUSD_VALUE_TYPE_HALF:   return 2;
    case LUSD_VALUE_TYPE_INT2:   return 2*4;
    case LUSD_VALUE_TYPE_INT3:   return 3*4;
    case LUSD_VALUE_TYPE_INT4:   return 4*4;
    case LUSD_VALUE_TYPE_FLOAT2:
    case LUSD_VALUE_TYPE_TEXCOORD2F:
    case LUSD_VALUE_TYPE_TEXCOORD2H: return 2*4;
    case LUSD_VALUE_TYPE_FLOAT3:
    case LUSD_VALUE_TYPE_POINT3F:
    case LUSD_VALUE_TYPE_NORMAL3F:
    case LUSD_VALUE_TYPE_VECTOR3F:
    case LUSD_VALUE_TYPE_COLOR3F: return 3*4;
    case LUSD_VALUE_TYPE_FLOAT4:
    case LUSD_VALUE_TYPE_QUATF:
    case LUSD_VALUE_TYPE_COLOR4F: return 4*4;
    case LUSD_VALUE_TYPE_DOUBLE2:
    case LUSD_VALUE_TYPE_TEXCOORD2D: return 2*8;
    case LUSD_VALUE_TYPE_DOUBLE3:
    case LUSD_VALUE_TYPE_POINT3D:
    case LUSD_VALUE_TYPE_NORMAL3D:
    case LUSD_VALUE_TYPE_VECTOR3D:
    case LUSD_VALUE_TYPE_COLOR3D: return 3*8;
    case LUSD_VALUE_TYPE_DOUBLE4:
    case LUSD_VALUE_TYPE_QUATD:
    case LUSD_VALUE_TYPE_COLOR4D: return 4*8;
    case LUSD_VALUE_TYPE_MATRIX2F: return 4*4;
    case LUSD_VALUE_TYPE_MATRIX3F: return 9*4;
    case LUSD_VALUE_TYPE_MATRIX4F: return 16*4;
    case LUSD_VALUE_TYPE_MATRIX2D: return 4*8;
    case LUSD_VALUE_TYPE_MATRIX3D: return 9*8;
    case LUSD_VALUE_TYPE_MATRIX4D: return 16*8;
    default: return 0;
    }
}

/* =======================================================================
 * Single-element value serialization
 * ======================================================================= */

/* Helper: format a float with no trailing zeros, but keep at least one
 * decimal place so the value is clearly floating-point. */
static bool append_float(StrBuf* b, float v) {
    /* Use %g for compact output; check if it contains '.' or 'e' already */
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", (double)v);
    /* If no '.' and no 'e', append ".0" */
    bool has_dot = (strchr(tmp, '.') != NULL) || (strchr(tmp, 'e') != NULL);
    if (!has_dot) {
        size_t len = strlen(tmp);
        if (len + 2 < sizeof(tmp)) { tmp[len] = '.'; tmp[len+1] = '0'; tmp[len+2] = '\0'; }
    }
    return sb_append(b, tmp);
}

static bool append_double(StrBuf* b, double v) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", v);
    bool has_dot = (strchr(tmp, '.') != NULL) || (strchr(tmp, 'e') != NULL);
    if (!has_dot) {
        size_t len = strlen(tmp);
        if (len + 2 < sizeof(tmp)) { tmp[len] = '.'; tmp[len+1] = '0'; tmp[len+2] = '\0'; }
    }
    return sb_append(b, tmp);
}

/* Write one element of base_type starting at ptr. */
static bool write_element(StrBuf* b, uint32_t base_type, const void* ptr) {
    const float*   fp  = (const float*)ptr;
    const double*  dp  = (const double*)ptr;
    const int32_t* ip  = (const int32_t*)ptr;
    const int64_t* i64 = (const int64_t*)ptr;

    switch (base_type) {
    case LUSD_VALUE_TYPE_BOOL: {
        bool v; memcpy(&v, ptr, sizeof(bool));
        return sb_appendf(b, "%d", (int)v);
    }
    case LUSD_VALUE_TYPE_INT32:
        return sb_appendf(b, "%d", ip[0]);
    case LUSD_VALUE_TYPE_UINT32: {
        uint32_t v; memcpy(&v, ptr, 4);
        return sb_appendf(b, "%u", (unsigned)v);
    }
    case LUSD_VALUE_TYPE_INT64:
        return sb_appendf(b, "%lld", (long long)i64[0]);
    case LUSD_VALUE_TYPE_UINT64: {
        uint64_t v; memcpy(&v, ptr, 8);
        return sb_appendf(b, "%llu", (unsigned long long)v);
    }
    case LUSD_VALUE_TYPE_FLOAT:
        return append_float(b, fp[0]);
    case LUSD_VALUE_TYPE_DOUBLE:
        return append_double(b, dp[0]);

    /* 2-component float */
    case LUSD_VALUE_TYPE_FLOAT2:
    case LUSD_VALUE_TYPE_TEXCOORD2F:
    case LUSD_VALUE_TYPE_TEXCOORD2H:
        if (!sb_append(b, "(")) return false;
        if (!append_float(b, fp[0])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_float(b, fp[1])) return false;
        return sb_append(b, ")");

    /* 3-component float */
    case LUSD_VALUE_TYPE_FLOAT3:
    case LUSD_VALUE_TYPE_POINT3F:
    case LUSD_VALUE_TYPE_NORMAL3F:
    case LUSD_VALUE_TYPE_VECTOR3F:
    case LUSD_VALUE_TYPE_COLOR3F:
        if (!sb_append(b, "(")) return false;
        if (!append_float(b, fp[0])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_float(b, fp[1])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_float(b, fp[2])) return false;
        return sb_append(b, ")");

    /* 4-component float */
    case LUSD_VALUE_TYPE_FLOAT4:
    case LUSD_VALUE_TYPE_QUATF:
    case LUSD_VALUE_TYPE_COLOR4F:
        if (!sb_append(b, "(")) return false;
        if (!append_float(b, fp[0])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_float(b, fp[1])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_float(b, fp[2])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_float(b, fp[3])) return false;
        return sb_append(b, ")");

    /* 2-component double */
    case LUSD_VALUE_TYPE_DOUBLE2:
    case LUSD_VALUE_TYPE_TEXCOORD2D:
        if (!sb_append(b, "(")) return false;
        if (!append_double(b, dp[0])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_double(b, dp[1])) return false;
        return sb_append(b, ")");

    /* 3-component double */
    case LUSD_VALUE_TYPE_DOUBLE3:
    case LUSD_VALUE_TYPE_POINT3D:
    case LUSD_VALUE_TYPE_NORMAL3D:
    case LUSD_VALUE_TYPE_VECTOR3D:
    case LUSD_VALUE_TYPE_COLOR3D:
        if (!sb_append(b, "(")) return false;
        if (!append_double(b, dp[0])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_double(b, dp[1])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_double(b, dp[2])) return false;
        return sb_append(b, ")");

    /* 4-component double */
    case LUSD_VALUE_TYPE_DOUBLE4:
    case LUSD_VALUE_TYPE_QUATD:
    case LUSD_VALUE_TYPE_COLOR4D:
        if (!sb_append(b, "(")) return false;
        if (!append_double(b, dp[0])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_double(b, dp[1])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_double(b, dp[2])) return false;
        if (!sb_append(b, ", ")) return false;
        if (!append_double(b, dp[3])) return false;
        return sb_append(b, ")");

    /* int vectors */
    case LUSD_VALUE_TYPE_INT2:
        return sb_appendf(b, "(%d, %d)", ip[0], ip[1]);
    case LUSD_VALUE_TYPE_INT3:
        return sb_appendf(b, "(%d, %d, %d)", ip[0], ip[1], ip[2]);
    case LUSD_VALUE_TYPE_INT4:
        return sb_appendf(b, "(%d, %d, %d, %d)", ip[0], ip[1], ip[2], ip[3]);

    /* matrix4f: row-major, 4 rows of 4 floats each */
    case LUSD_VALUE_TYPE_MATRIX4F: {
        if (!sb_append(b, "( ")) return false;
        for (int r = 0; r < 4; r++) {
            if (!sb_append(b, "(")) return false;
            for (int c = 0; c < 4; c++) {
                if (!append_float(b, fp[r*4+c])) return false;
                if (c < 3 && !sb_append(b, ", ")) return false;
            }
            if (!sb_append(b, r < 3 ? "), " : ")")) return false;
        }
        return sb_append(b, " )");
    }

    /* matrix4d */
    case LUSD_VALUE_TYPE_MATRIX4D: {
        if (!sb_append(b, "( ")) return false;
        for (int r = 0; r < 4; r++) {
            if (!sb_append(b, "(")) return false;
            for (int c = 0; c < 4; c++) {
                if (!append_double(b, dp[r*4+c])) return false;
                if (c < 3 && !sb_append(b, ", ")) return false;
            }
            if (!sb_append(b, r < 3 ? "), " : ")")) return false;
        }
        return sb_append(b, " )");
    }

    case LUSD_VALUE_TYPE_STRING: {
        /* Stored as a char* in inline storage */
        char* s;
        memcpy(&s, ptr, sizeof(char*));
        if (!sb_append(b, "\"")) return false;
        if (s && !sb_append(b, s)) return false;
        return sb_append(b, "\"");
    }

    case LUSD_VALUE_TYPE_TOKEN: {
        /* Stored as a LusdToken (pointer-sized) in inline storage —
         * we can't resolve the token text here without the instance.
         * Emit as empty string; real token writing needs instance context. */
        char* s;
        memcpy(&s, ptr, sizeof(char*));
        if (!sb_append(b, "\"")) return false;
        if (s && !sb_append(b, s)) return false;
        return sb_append(b, "\"");
    }

    default:
        return sb_appendf(b, "/* unsupported type %u */", base_type);
    }
}

/* =======================================================================
 * Full value serialization (scalar or array)
 * ======================================================================= */

static bool write_value(StrBuf* b, const LusdValueData* vd) {
    uint32_t base = (uint32_t)vd->type & ~LUSD_VALUE_TYPE_ARRAY_BIT;
    bool is_array = ((uint32_t)vd->type & LUSD_VALUE_TYPE_ARRAY_BIT) != 0;
    size_t esz = elem_byte_size(base);

    if (!is_array) {
        /* Scalar */
        const void* ptr = vd->useHeap ? vd->storage.heap.ptr
                                       : (const void*)vd->storage.inlineData;
        return write_element(b, base, ptr);
    }

    /* Array */
    if (!sb_append(b, "[")) return false;
    if (esz > 0 && vd->arrayCount > 0 && vd->storage.heap.ptr) {
        const uint8_t* data = (const uint8_t*)vd->storage.heap.ptr;
        for (uint64_t i = 0; i < vd->arrayCount; i++) {
            if (i > 0 && !sb_append(b, ", ")) return false;
            if (!write_element(b, base, data + i * esz)) return false;
        }
    }
    return sb_append(b, "]");
}

/* =======================================================================
 * Indent helper
 * ======================================================================= */

static bool write_indent(StrBuf* b, int depth) {
    for (int i = 0; i < depth; i++)
        if (!sb_append(b, "    ")) return false;
    return true;
}

/* =======================================================================
 * Attribute serialization
 * ======================================================================= */

static bool write_attr(StrBuf* b, const LusdWriteAttr_T* a, int depth) {
    if (!write_indent(b, depth)) return false;

    /* variability prefix */
    if (a->variability == LUSD_VARIABILITY_UNIFORM)
        if (!sb_append(b, "uniform ")) return false;

    /* type keyword + optional [] */
    uint32_t base = (uint32_t)a->type & ~LUSD_VALUE_TYPE_ARRAY_BIT;
    bool is_array = ((uint32_t)a->type & LUSD_VALUE_TYPE_ARRAY_BIT) != 0;

    if (!sb_append(b, usda_type_keyword(base))) return false;
    if (is_array && !sb_append(b, "[]")) return false;
    if (!sb_append(b, " ")) return false;
    if (!sb_append(b, a->name)) return false;

    if (a->has_default) {
        if (!sb_append(b, " = ")) return false;
        if (!write_value(b, &a->default_value)) return false;
    }
    return sb_append(b, "\n");
}

/* =======================================================================
 * Prim serialization (recursive)
 * ======================================================================= */

static bool write_prim(StrBuf* b, const LusdWritePrim_T* p, int depth) {
    if (!write_indent(b, depth)) return false;

    /* specifier keyword */
    const char* spec_kw =
        p->specifier == LUSD_SPECIFIER_OVER  ? "over"  :
        p->specifier == LUSD_SPECIFIER_CLASS ? "class" : "def";
    if (!sb_append(b, spec_kw)) return false;
    if (!sb_append(b, " ")) return false;

    /* type name (may be empty for typeless prims) */
    if (p->type_name && p->type_name[0]) {
        if (!sb_append(b, p->type_name)) return false;
        if (!sb_append(b, " ")) return false;
    }

    /* quoted name */
    if (!sb_appendf(b, "\"%s\"", p->name)) return false;
    if (!sb_append(b, " {\n")) return false;

    /* attributes */
    for (uint32_t i = 0; i < p->attr_count; i++)
        if (!write_attr(b, &p->attrs[i], depth + 1)) return false;

    /* child prims */
    for (uint32_t i = 0; i < p->child_count; i++) {
        if (p->attr_count > 0 && i == 0)
            if (!sb_append(b, "\n")) return false; /* blank line before children */
        if (!write_prim(b, p->children[i], depth + 1)) return false;
    }

    if (!write_indent(b, depth)) return false;
    return sb_append(b, "}\n");
}

/* =======================================================================
 * Layer metadata block
 * ======================================================================= */

static bool write_layer_metas(StrBuf* b, const LusdStage_T* S) {
    /* Emit if any non-default metadata is set */
    bool has_metas = (S->meters_per_unit != 0.0 ||
                      S->start_time_code != 0.0  ||
                      S->end_time_code   != 0.0  ||
                      S->frames_per_second != 0.0);
    if (!has_metas) return true;

    if (!sb_append(b, "(\n")) return false;

    if (S->up_axis == LUSD_UP_AXIS_Z) {
        if (!sb_append(b, "    upAxis = \"Z\"\n")) return false;
    } else if (S->up_axis == LUSD_UP_AXIS_X) {
        if (!sb_append(b, "    upAxis = \"X\"\n")) return false;
    } else {
        if (!sb_append(b, "    upAxis = \"Y\"\n")) return false;
    }

    if (S->meters_per_unit != 0.0)
        if (!sb_appendf(b, "    metersPerUnit = %g\n", S->meters_per_unit)) return false;

    if (S->start_time_code != 0.0 || S->end_time_code != 0.0) {
        if (!sb_appendf(b, "    startTimeCode = %g\n", S->start_time_code)) return false;
        if (!sb_appendf(b, "    endTimeCode = %g\n",   S->end_time_code))   return false;
    }

    if (S->frames_per_second != 0.0)
        if (!sb_appendf(b, "    framesPerSecond = %g\n", S->frames_per_second)) return false;

    return sb_append(b, ")\n");
}

/* =======================================================================
 * Public API
 * ======================================================================= */

LusdResult lusdCreateWriter(LusdInstance inst, const LusdWriterCreateInfo* pCI, LusdWriter* pWriter) {
    LUSD_UNUSED(inst);
    if (!pCI || !pWriter) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdWriter_T* w = (LusdWriter_T*)calloc(1, sizeof(LusdWriter_T));
    if (!w) return LUSD_ERROR_OUT_OF_MEMORY;

    w->format = pCI->format;
    if (pCI->pFilePath && pCI->pFilePath[0]) {
        w->file_path = (char*)malloc(strlen(pCI->pFilePath) + 1);
        if (!w->file_path) { free(w); return LUSD_ERROR_OUT_OF_MEMORY; }
        strcpy(w->file_path, pCI->pFilePath);
    }
    *pWriter = (LusdWriter)w;
    return LUSD_SUCCESS;
}

void lusdDestroyWriter(LusdInstance inst, LusdWriter writer) {
    LUSD_UNUSED(inst);
    if (!writer) return;
    LusdWriter_T* w = (LusdWriter_T*)writer;
    free(w->file_path);
    free(w);
}

LusdResult lusdStageExportToString(LusdInstance inst, LusdStage stage,
                                    char** ppOutput, uint64_t* pLength) {
    LUSD_UNUSED(inst);
    if (!stage || !ppOutput || !pLength) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdStage_T* S = (LusdStage_T*)stage;
    StrBuf buf = {NULL, 0, 0};

    /* Header */
    if (!sb_append(&buf, "#usda 1.0\n")) goto oom;

    /* Layer metadata block */
    if (!write_layer_metas(&buf, S)) goto oom;
    if (!sb_append(&buf, "\n")) goto oom;

    /* Root prims */
    for (uint32_t i = 0; i < S->root_prim_count; i++) {
        if (!write_prim(&buf, S->root_prims[i], 0)) goto oom;
        if (i + 1 < S->root_prim_count)
            if (!sb_append(&buf, "\n")) goto oom;
    }

    *ppOutput = buf.data;
    *pLength  = (uint64_t)buf.used;
    return LUSD_SUCCESS;

oom:
    sb_free(&buf);
    return LUSD_ERROR_OUT_OF_MEMORY;
}

LusdResult lusdWriterWriteStage(LusdWriter writer, LusdStage stage) {
    if (!writer || !stage) return LUSD_ERROR_INVALID_ARGUMENT;
    LusdWriter_T* w = (LusdWriter_T*)writer;
    if (!w->file_path) return LUSD_ERROR_INVALID_ARGUMENT;

    char* text = NULL;
    uint64_t length = 0;
    LusdResult r = lusdStageExportToString(NULL, stage, &text, &length);
    if (r != LUSD_SUCCESS) return r;

    FILE* f = fopen(w->file_path, "wb");
    if (!f) { free(text); return LUSD_ERROR_FILE_NOT_FOUND; }
    fwrite(text, 1, (size_t)length, f);
    fclose(f);
    free(text);
    return LUSD_SUCCESS;
}
