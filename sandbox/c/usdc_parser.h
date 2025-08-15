#ifndef USDC_PARSER_H
#define USDC_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USDC File Format Constants */
#define USDC_MAGIC "PXR-USDC"
#define USDC_MAGIC_SIZE 8
#define USDC_VERSION_SIZE 8
#define USDC_TOC_OFFSET_SIZE 8
#define USDC_HEADER_SIZE (USDC_MAGIC_SIZE + USDC_VERSION_SIZE + USDC_TOC_OFFSET_SIZE)

/* Security limits */
#define USDC_MAX_TOC_SECTIONS 32
#define USDC_MAX_TOKENS (1024 * 1024 * 64)        /* 64M tokens */
#define USDC_MAX_STRINGS (1024 * 1024 * 64)       /* 64M strings */
#define USDC_MAX_FIELDS (1024 * 1024 * 256)       /* 256M fields */
#define USDC_MAX_PATHS (1024 * 1024 * 256)        /* 256M paths */
#define USDC_MAX_SPECS (1024 * 1024 * 256)        /* 256M specs */
#define USDC_MAX_FIELDSETS (1024 * 1024 * 64)     /* 64M fieldsets */
#define USDC_MAX_STRING_LENGTH (1024 * 1024 * 64) /* 64MB string */
#define USDC_MAX_MEMORY_BUDGET (2ULL * 1024 * 1024 * 1024) /* 2GB */

/* USDC Data Types (matching crate-format.hh) */
typedef enum {
    USDC_DATA_TYPE_INVALID = 0,
    USDC_DATA_TYPE_BOOL = 1,
    USDC_DATA_TYPE_UCHAR = 2,
    USDC_DATA_TYPE_INT = 3,
    USDC_DATA_TYPE_UINT = 4,
    USDC_DATA_TYPE_INT64 = 5,
    USDC_DATA_TYPE_UINT64 = 6,
    USDC_DATA_TYPE_HALF = 7,
    USDC_DATA_TYPE_FLOAT = 8,
    USDC_DATA_TYPE_DOUBLE = 9,
    USDC_DATA_TYPE_STRING = 10,
    USDC_DATA_TYPE_TOKEN = 11,
    USDC_DATA_TYPE_ASSET_PATH = 12,
    USDC_DATA_TYPE_MATRIX2D = 13,
    USDC_DATA_TYPE_MATRIX3D = 14,
    USDC_DATA_TYPE_MATRIX4D = 15,
    USDC_DATA_TYPE_QUATD = 16,
    USDC_DATA_TYPE_QUATF = 17,
    USDC_DATA_TYPE_QUATH = 18,
    USDC_DATA_TYPE_VEC2D = 19,
    USDC_DATA_TYPE_VEC2F = 20,
    USDC_DATA_TYPE_VEC2H = 21,
    USDC_DATA_TYPE_VEC2I = 22,
    USDC_DATA_TYPE_VEC3D = 23,
    USDC_DATA_TYPE_VEC3F = 24,
    USDC_DATA_TYPE_VEC3H = 25,
    USDC_DATA_TYPE_VEC3I = 26,
    USDC_DATA_TYPE_VEC4D = 27,
    USDC_DATA_TYPE_VEC4F = 28,
    USDC_DATA_TYPE_VEC4H = 29,
    USDC_DATA_TYPE_VEC4I = 30,
    USDC_DATA_TYPE_DICTIONARY = 31,
    USDC_DATA_TYPE_TOKEN_LIST_OP = 32,
    USDC_DATA_TYPE_STRING_LIST_OP = 33,
    USDC_DATA_TYPE_PATH_LIST_OP = 34,
    USDC_DATA_TYPE_REFERENCE_LIST_OP = 35,
    USDC_DATA_TYPE_INT_LIST_OP = 36,
    USDC_DATA_TYPE_INT64_LIST_OP = 37,
    USDC_DATA_TYPE_UINT_LIST_OP = 38,
    USDC_DATA_TYPE_UINT64_LIST_OP = 39,
    USDC_DATA_TYPE_PATH_VECTOR = 40,
    USDC_DATA_TYPE_TOKEN_VECTOR = 41,
    USDC_DATA_TYPE_SPECIFIER = 42,
    USDC_DATA_TYPE_PERMISSION = 43,
    USDC_DATA_TYPE_VARIABILITY = 44,
    USDC_DATA_TYPE_VARIANT_SELECTION_MAP = 45,
    USDC_DATA_TYPE_TIME_SAMPLES = 46,
    USDC_DATA_TYPE_PAYLOAD = 47,
    USDC_DATA_TYPE_DOUBLE_VECTOR = 48,
    USDC_DATA_TYPE_LAYER_OFFSET_VECTOR = 49,
    USDC_DATA_TYPE_STRING_VECTOR = 50,
    USDC_DATA_TYPE_VALUE_BLOCK = 51,
    USDC_DATA_TYPE_VALUE = 52,
    USDC_DATA_TYPE_UNREGISTERED_VALUE = 53,
    USDC_DATA_TYPE_UNREGISTERED_VALUE_LIST_OP = 54,
    USDC_DATA_TYPE_PAYLOAD_LIST_OP = 55,
    USDC_DATA_TYPE_TIME_CODE = 56,
    USDC_NUM_DATA_TYPES
} usdc_data_type_t;

/* USDC File Header */
typedef struct {
    uint8_t magic[8];       /* "PXR-USDC" */
    uint8_t version[8];     /* Version bytes (first 3 are used) */
    uint64_t toc_offset;    /* Offset to Table of Contents */
} usdc_header_t;

/* USDC Section */
typedef struct {
    char name[16];          /* Section name (null-terminated) */
    uint64_t start;         /* Start offset in file */
    uint64_t size;          /* Size in bytes */
} usdc_section_t;

/* USDC Table of Contents */
typedef struct {
    uint64_t num_sections;
    usdc_section_t *sections;
} usdc_toc_t;

/* USDC Index (4-byte index into various tables) */
typedef struct {
    uint32_t value;
} usdc_index_t;

#define USDC_INVALID_INDEX ((uint32_t)~0u)

/* USDC Value Representation (8 bytes: 2 bytes type info + 6 bytes data/offset) */
typedef struct {
    uint64_t data;
} usdc_value_rep_t;

/* Value Rep bit masks and constants */
#define USDC_VALUE_IS_ARRAY_BIT     (1ULL << 63)
#define USDC_VALUE_IS_INLINED_BIT   (1ULL << 62)
#define USDC_VALUE_IS_COMPRESSED_BIT (1ULL << 61)
#define USDC_VALUE_PAYLOAD_MASK     ((1ULL << 48) - 1)

/* USDC Field */
typedef struct {
    usdc_index_t token_index;   /* Index into token table */
    usdc_value_rep_t value_rep; /* Value representation */
} usdc_field_t;

/* USDC Token */
typedef struct {
    char *str;
    size_t length;
} usdc_token_t;

/* USDC Path */
typedef struct {
    char *path_string;
    size_t length;
    int is_absolute;    /* 1 if absolute path, 0 if relative */
} usdc_path_t;

/* USD Spec Types */
typedef enum {
    USDC_SPEC_TYPE_UNKNOWN = 0,
    USDC_SPEC_TYPE_ATTRIBUTE = 1,
    USDC_SPEC_TYPE_CONNECTION = 2,
    USDC_SPEC_TYPE_EXPRESSION = 3,
    USDC_SPEC_TYPE_MAPPER = 4,
    USDC_SPEC_TYPE_MAPPER_ARG = 5,
    USDC_SPEC_TYPE_PRIM = 6,
    USDC_SPEC_TYPE_PSEUDO_ROOT = 7,
    USDC_SPEC_TYPE_RELATIONSHIP = 8,
    USDC_SPEC_TYPE_RELATIONSHIP_TARGET = 9,
    USDC_SPEC_TYPE_VARIANT = 10,
    USDC_SPEC_TYPE_VARIANT_SET = 11
} usdc_spec_type_t;

/* USDC Spec */
typedef struct {
    usdc_index_t path_index;        /* Index into path table */
    usdc_index_t fieldset_index;    /* Index into fieldset table */
    usdc_spec_type_t spec_type;     /* Spec type (32-bit) */
} usdc_spec_t;

/* USDC FieldSet (simplified implementation) */
typedef struct {
    usdc_index_t *field_indices;    /* Array of field indices */
    size_t num_field_indices;       /* Number of field indices in this fieldset */
} usdc_fieldset_t;

/* Hierarchical Path (forward declaration needed for reader structure) */
typedef struct {
    char *path_string;         /* Full hierarchical path */
    char *element_name;        /* Just the element name */
    size_t parent_index;       /* Index of parent path (USDC_INVALID_INDEX for root) */
    int is_property_path;      /* 1 if this is a property path, 0 if prim path */
    int is_absolute;           /* 1 if absolute path, 0 if relative */
    size_t depth;              /* Depth in hierarchy (0 = root) */
} usdc_hierarchical_path_t;

/* Path compression intermediate data */
typedef struct {
    uint32_t *path_indices;
    int32_t *element_token_indices;
    int32_t *jumps;
    size_t num_encoded_paths;
} usdc_compressed_paths_t;

/* USDC Reader State */
typedef struct {
    FILE *file;
    size_t file_size;
    size_t memory_used;
    
    /* Header and TOC */
    usdc_header_t header;
    usdc_toc_t toc;
    
    /* Data tables */
    usdc_token_t *tokens;
    size_t num_tokens;
    
    usdc_index_t *string_indices;
    size_t num_string_indices;
    
    usdc_field_t *fields;
    size_t num_fields;
    
    usdc_path_t *paths;
    size_t num_paths;
    
    usdc_hierarchical_path_t *hierarchical_paths;
    size_t num_hierarchical_paths;
    
    usdc_spec_t *specs;
    size_t num_specs;
    
    usdc_fieldset_t *fieldsets;
    size_t num_fieldsets;
    
    /* Error handling */
    char error_message[256];
    char warning_message[256];
    
} usdc_reader_t;

/* Main API Functions */
int usdc_reader_init(usdc_reader_t *reader, const char *filename);
void usdc_reader_cleanup(usdc_reader_t *reader);
int usdc_reader_read_file(usdc_reader_t *reader);
const char *usdc_reader_get_error(usdc_reader_t *reader);
const char *usdc_reader_get_warning(usdc_reader_t *reader);

/* Header and TOC Functions */
int usdc_read_header(usdc_reader_t *reader);
int usdc_read_toc(usdc_reader_t *reader);
int usdc_read_section(usdc_reader_t *reader, usdc_section_t *section);

/* Data Reading Functions */
int usdc_read_tokens_section(usdc_reader_t *reader, usdc_section_t *section);
int usdc_read_strings_section(usdc_reader_t *reader, usdc_section_t *section);
int usdc_read_fields_section(usdc_reader_t *reader, usdc_section_t *section);
int usdc_read_paths_section(usdc_reader_t *reader, usdc_section_t *section);
int usdc_read_specs_section(usdc_reader_t *reader, usdc_section_t *section);
int usdc_read_fieldsets_section(usdc_reader_t *reader, usdc_section_t *section);

/* Utility Functions */
int usdc_is_array(usdc_value_rep_t rep);
int usdc_is_inlined(usdc_value_rep_t rep);
int usdc_is_compressed(usdc_value_rep_t rep);
uint32_t usdc_get_type_id(usdc_value_rep_t rep);
uint64_t usdc_get_payload(usdc_value_rep_t rep);

/* Memory Management */
int usdc_check_memory_limit(usdc_reader_t *reader, size_t additional_bytes);
void usdc_update_memory_usage(usdc_reader_t *reader, size_t bytes);

/* File I/O Helpers */
int usdc_read_uint8(usdc_reader_t *reader, uint8_t *value);
int usdc_read_uint32(usdc_reader_t *reader, uint32_t *value);
int usdc_read_uint64(usdc_reader_t *reader, uint64_t *value);
int usdc_read_bytes(usdc_reader_t *reader, void *buffer, size_t size);
int usdc_seek(usdc_reader_t *reader, uint64_t offset);

/* LZ4 Decompression */
int usdc_lz4_decompress(const char *src, char *dst, int compressed_size, int max_decompressed_size);

/* Token Parsing Helpers */
int usdc_parse_token_magic(const char *data, size_t size);
int usdc_parse_decompressed_tokens(usdc_reader_t *reader, const char *data, size_t data_size, size_t num_tokens);

/* USD Integer compression/decompression (full implementation) */
typedef struct {
    int32_t common_value;    /* Most common delta value */
    size_t num_codes_bytes;  /* Number of bytes for 2-bit codes */
    const char *codes_ptr;   /* Pointer to 2-bit codes section */
    const char *vints_ptr;   /* Pointer to variable integer section */
} usdc_integer_decode_ctx_t;

int usdc_usd_integer_decompress(const char *compressed_data, size_t compressed_size,
                                uint32_t *output, size_t num_ints, char *working_space, size_t working_space_size);
int usdc_usd_integer_decompress_signed(const char *compressed_data, size_t compressed_size,
                                      int32_t *output, size_t num_ints, char *working_space, size_t working_space_size);

size_t usdc_usd_integer_decode(const char *encoded_data, size_t num_ints, uint32_t *output);
size_t usdc_usd_integer_decode_signed(const char *encoded_data, size_t num_ints, int32_t *output);

/* Helper functions for reading different integer sizes */
int8_t usdc_read_int8(const char **data_ptr);
int16_t usdc_read_int16(const char **data_ptr);
int32_t usdc_read_int32(const char **data_ptr);
uint8_t usdc_read_uint8_from_ptr(const char **data_ptr);
uint16_t usdc_read_uint16_from_ptr(const char **data_ptr);
uint32_t usdc_read_uint32_from_ptr(const char **data_ptr);

/* Working space size calculation */
size_t usdc_get_integer_working_space_size(size_t num_ints);

/* Fallback simple decompression (original functions, renamed for compatibility) */
size_t usdc_integer_decompress(const char *compressed_data, size_t compressed_size,
                               uint32_t *output, size_t num_ints);
size_t usdc_integer_decompress_signed(const char *compressed_data, size_t compressed_size,
                                      int32_t *output, size_t num_ints);

/* Path Decompression */
int usdc_read_compressed_paths(usdc_reader_t *reader, usdc_section_t *section);
int usdc_decompress_path_data(usdc_reader_t *reader, usdc_compressed_paths_t *compressed);
int usdc_build_paths(usdc_reader_t *reader, usdc_compressed_paths_t *compressed);
void usdc_cleanup_compressed_paths(usdc_compressed_paths_t *compressed);

/* Hierarchical Path Building */
int usdc_build_hierarchical_paths(usdc_reader_t *reader, usdc_compressed_paths_t *compressed);
int usdc_build_hierarchical_paths_recursive(usdc_reader_t *reader, 
                                            usdc_compressed_paths_t *compressed,
                                            size_t current_index, 
                                            size_t parent_path_index,
                                            const char *parent_path_string,
                                            size_t depth,
                                            int *visit_table);
void usdc_print_hierarchical_paths(usdc_reader_t *reader);

/* Value Parsing */
typedef struct {
    usdc_data_type_t type;
    int is_array;
    int is_inlined;
    int is_compressed;
    uint64_t payload;
    
    union {
        /* Inlined values */
        int bool_val;
        uint8_t uchar_val;
        int32_t int_val;
        uint32_t uint_val;
        int64_t int64_val;
        uint64_t uint64_val;
        float float_val;
        double double_val;
        uint32_t token_index;
        uint32_t string_index;
        
        /* Non-inlined data pointer */
        void *data_ptr;
    } value;
    
    /* Array size for array types */
    size_t array_size;
} usdc_parsed_value_t;

/* Value parsing functions */
int usdc_parse_value_rep(usdc_reader_t *reader, usdc_value_rep_t rep, usdc_parsed_value_t *parsed_value);
int usdc_parse_inlined_value(usdc_reader_t *reader, usdc_value_rep_t rep, usdc_parsed_value_t *parsed_value);
int usdc_parse_non_inlined_value(usdc_reader_t *reader, usdc_value_rep_t rep, usdc_parsed_value_t *parsed_value);

/* Array parsing functions */
int usdc_parse_bool_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_int_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_uint_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_int64_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_uint64_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_float_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_double_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_token_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);
int usdc_parse_string_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value);

/* Value cleanup */
void usdc_cleanup_parsed_value(usdc_parsed_value_t *parsed_value);

/* Value display helpers */
void usdc_print_parsed_value(usdc_reader_t *reader, usdc_parsed_value_t *parsed_value);
const char *usdc_get_data_type_name(usdc_data_type_t type);
const char *usdc_get_spec_type_name(usdc_spec_type_t type);

/* String Utilities */
void usdc_set_error(usdc_reader_t *reader, const char *message);
void usdc_set_warning(usdc_reader_t *reader, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* USDC_PARSER_H */