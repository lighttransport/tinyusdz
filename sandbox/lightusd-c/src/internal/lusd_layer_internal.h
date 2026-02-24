/*
 * lusd_layer_internal.h - Internal flat-table Layer and PrimSpec types
 *
 * LusdLayer_T holds the parsed USDC binary as flat C arrays; all data is
 * stored close to the file format so that value materialization can be done
 * lazily by Lydra (C++) without any upfront type conversion.
 *
 * LusdPrim_T is a lightweight view node allocated in a per-layer arena.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_LAYER_INTERNAL_H
#define LUSD_LAYER_INTERNAL_H

#include "lightusd/lusd_platform.h"
#include "lightusd/lusd_result.h"
#include "lightusd/lusd_handles.h"
#include "internal/lusd_internal.h"
#include "internal/lusd_value_rep.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * Forward typedefs so LusdLayer_T and LusdPrim_T can reference each other
 * without requiring the 'struct' keyword everywhere.
 */
typedef struct LusdLayer_T LusdLayer_T;
typedef struct LusdPrim_T  LusdPrim_T;

/* ------------------------------------------------------------------
 * Field entry: one (token_index, value_rep) pair from the FIELDS section.
 * 16 bytes in memory (uint32_t + 4-byte pad + uint64_t due to alignment).
 * On-disk the pair occupies 12 bytes; the parser fills each field separately.
 * ------------------------------------------------------------------ */
typedef struct LusdFieldEntry {
    uint32_t    token_index;   /* index into layer->tokens[] */
    uint32_t    _pad;          /* alignment padding */
    LusdValueRep value_rep;   /* lazy 8-byte representation */
} LusdFieldEntry;

LUSD_STATIC_ASSERT(sizeof(LusdFieldEntry) == 16, "LusdFieldEntry must be 16 bytes");

/* ------------------------------------------------------------------
 * Spec entry: one entry from the SPECS section.
 * 12 bytes total.
 * ------------------------------------------------------------------ */
typedef struct LusdSpecEntry {
    uint32_t path_index;       /* index into layer->paths[] */
    uint32_t fieldset_index;   /* starting offset in layer->fieldsets[] */
    uint32_t spec_type;        /* LusdSpecType */
} LusdSpecEntry;

LUSD_STATIC_ASSERT(sizeof(LusdSpecEntry) == 12, "LusdSpecEntry must be 12 bytes");

/* ------------------------------------------------------------------
 * Layer-level metadata (extracted from root prim fieldset on parse)
 * ------------------------------------------------------------------ */
typedef struct LusdLayerMetas {
    double  meters_per_unit;      /* default 0.01 (cm) */
    double  start_time_code;      /* default 0 */
    double  end_time_code;        /* default 0 */
    double  frames_per_second;    /* default 24 */
    int32_t up_axis;              /* 0=Y, 1=Z, 2=X */
} LusdLayerMetas;

/* ------------------------------------------------------------------
 * LusdLayer_T — the opaque struct behind LusdLayer handles.
 *
 * Flat arrays that mirror the USDC section layout.
 * All char* pointers in tokens/strings/paths point into either
 * file_data (zero-copy) or string_arena (for reconstructed path strings).
 * ------------------------------------------------------------------ */
struct LusdLayer_T {
    LusdInstance    inst;              /* back-ref to owning instance (non-owning) */
    char*           identifier;        /* owned, null-terminated file path */

    /* ---- raw file buffer ----------------------------------------- */
    const uint8_t*  file_data;         /* start of file bytes */
    uint64_t        file_size;
    bool            owns_file_data;    /* if true, free on destroy */

    /* ---- TOKENS: null-terminated strings in file_data (zero-copy) - */
    char**          tokens;            /* tokens[i] → null-term string in decompressed buffer */
    uint32_t        token_count;
    /* Tokens decompression buffer (owns) */
    char*           token_buf;
    uint64_t        token_buf_size;

    /* ---- STRINGS: indices → tokens[] ----------------------------- */
    char**          strings;           /* strings[i] = tokens[string_token_indices[i]] */
    uint32_t        string_count;

    /* ---- PATHS: reconstructed full path strings ------------------- */
    char**          paths;             /* paths[i] → null-term string in string_arena */
    uint32_t        path_count;

    /* ---- FIELDS: {token_index, value_rep} pairs ------------------- */
    LusdFieldEntry* fields;
    uint32_t        field_count;

    /* ---- FIELDSETS: flat uint32_t[], ~0u separates sets ----------- */
    uint32_t*       fieldsets;
    uint32_t        fieldset_entry_count;

    /* ---- SPECS: {path_index, fieldset_index, spec_type} ----------- */
    LusdSpecEntry*  specs;
    uint32_t        spec_count;

    /* ---- Layer metadata ------------------------------------------ */
    LusdLayerMetas  metas;

    /* ---- String arena for reconstructed path strings -------------- */
    char*           string_arena;
    uint32_t        string_arena_size;
    uint32_t        string_arena_used;

    /* ---- PrimSpec nodes (arena-allocated flat array) -------------- */
    struct LusdPrim_T* prim_nodes;
    uint32_t           prim_node_count;
    uint32_t           prim_node_capacity;

    /* ---- Root prims (spec indices of DEF/OVER/CLASS at root path) - */
    uint32_t*       root_spec_indices;
    uint32_t        root_spec_count;
};

/* ------------------------------------------------------------------
 * LusdPrim_T — the opaque struct behind LusdPrim handles.
 *
 * Lightweight view into the layer's flat arrays.
 * All pointers are non-owning (point into layer or layer->tokens[]).
 * ------------------------------------------------------------------ */
struct LusdPrim_T {
    const char*      name;           /* element name (last path component) */
    const char*      type_name;      /* USD type name e.g. "Mesh", or "" */
    uint32_t         spec_index;     /* index into layer->specs[] */
    uint32_t         fieldset_index; /* starting offset in layer->fieldsets[] */
    uint32_t         field_count;    /* number of fields in this prim's fieldset */
    uint32_t*        child_spec_indices; /* spec indices of child prims (owned by prim_nodes arena) */
    uint32_t         child_count;
    LusdLayer_T*     layer;          /* back-ref, non-owning */
};

/* ------------------------------------------------------------------
 * Internal functions (called between lusd_layer.c and lusd_usdc_reader.c)
 * ------------------------------------------------------------------ */

/*
 * lusd__layer_read_usdc - Parse a USDC binary buffer into layer->flat tables.
 * Called from lusd_read_usdc.c after the file is loaded into memory.
 * On success all layer fields are populated.
 * On failure the layer is left in a partially-initialised state;
 * lusd__layer_free_tables() should be called before returning the error.
 */
LusdResult lusd__layer_read_usdc(LusdLayer_T* layer,
                                  const uint8_t* data,
                                  uint64_t size);

/*
 * lusd__layer_build_prims - Walk the specs[] table and construct the
 * PrimSpec hierarchy (prim_nodes[], root_spec_indices[]).
 * Must be called after lusd__layer_read_usdc() succeeds.
 */
LusdResult lusd__layer_build_prims(LusdLayer_T* layer);

/*
 * lusd__layer_free_tables - Release all dynamic allocations inside the layer
 * (tokens/strings/paths/fields/fieldsets/specs arrays, string arena, etc.).
 * Does NOT free the layer struct itself; that is done by lusdDestroyLayer.
 */
void lusd__layer_free_tables(LusdLayer_T* layer);

/*
 * lusd__layer_find_field - Find a named field in a prim's fieldset.
 * Returns LUSD_NULL_VREP if not found.
 */
LusdValueRep lusd__layer_find_field(const LusdLayer_T* layer,
                                     const LusdPrim_T* prim,
                                     const char* field_name);

#endif /* LUSD_LAYER_INTERNAL_H */
