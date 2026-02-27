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

LUSD_EXTERN_C_BEGIN

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
 * LusdTimeSample — one entry in a .timeSamples attribute dictionary.
 *
 * For USDA layers the value lives as ASCII text at [text_offset, text_offset+text_len)
 * in L->file_data.  canonical_idx gives the index of the dedup-canonical entry so
 * that Lydra can share materialized buffers across identical samples.
 * ------------------------------------------------------------------ */
typedef struct LusdTimeSample {
    double   time;           /* time code (from the "{t: ...}" key)             */
    uint64_t text_offset;    /* byte offset into L->file_data for value text    */
    uint32_t text_len;       /* byte length of the value text (used for dedup)  */
    uint32_t canonical_idx;  /* index of the dedup-canonical entry (self if unique) */
    uint32_t text_hash;      /* FNV-32 hash of value text (for fast dedup check) */
    uint8_t  type_id;        /* LusdCrateTypeId of element type (VEC3F, QUATF…) */
    uint8_t  is_array;       /* non-zero when each sample is itself an array    */
    uint8_t  _pad[2];
} LusdTimeSample;            /* 32 bytes total */

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
    uint8_t         format;            /* LUSD_FORMAT_USDC=0, LUSD_FORMAT_USDA=1 */

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

    /* ---- Time samples table (USDA only) --------------------------- */
    /* All .timeSamples attribute entries stored in arrival order.     */
    /* Each attribute's samples occupy a contiguous [start, start+n)   */
    /* range; the corresponding field's ValueRep payload encodes       */
    /*   bits 47-24 = start index   bits 23-0 = count                 */
    LusdTimeSample* time_samples;
    uint32_t        time_sample_count;
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

/* Format tag values */
#define LUSD_FORMAT_USDC 0
#define LUSD_FORMAT_USDA 1

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
 * lusd__layer_read_usda - Parse a USDA ASCII buffer into layer->flat tables.
 * Called from lusd_read_usda.c after the file is loaded into memory.
 */
LusdResult lusd__layer_read_usda(LusdLayer_T* layer,
                                  const uint8_t* data,
                                  uint64_t size);

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

/*
 * lusd__resolve_path_target - Decode a PathListOp ValueRep and return
 * the first target path string.  Used for material:binding relationships.
 * Returns NULL if the ValueRep is not a PathListOp or has no targets.
 */
const char* lusd__resolve_path_target(const LusdLayer_T* layer,
                                       LusdValueRep rep);

/*
 * lusd__find_relationship_target - Find a RELATIONSHIP spec for the given
 * prim and relationship name, and return its first target path.
 * E.g. lusd__find_relationship_target(L, P, "material:binding") returns
 * "/Root/Material" if the prim has that binding.
 * Returns NULL if not found.
 */
const char* lusd__find_relationship_target(const LusdLayer_T* layer,
                                            const LusdPrim_T* prim,
                                            const char* rel_name);

LUSD_EXTERN_C_END

#endif /* LUSD_LAYER_INTERNAL_H */
