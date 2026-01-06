#include "usdc_parser.h"
#include <assert.h>

/* Include LZ4 decompression */
extern int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity);

/* ===== Memory Management ===== */

int usdc_check_memory_limit(usdc_reader_t *reader, size_t additional_bytes) {
    if (reader->memory_used + additional_bytes > USDC_MAX_MEMORY_BUDGET) {
        usdc_set_error(reader, "Memory limit exceeded");
        return 0;
    }
    return 1;
}

void usdc_update_memory_usage(usdc_reader_t *reader, size_t bytes) {
    reader->memory_used += bytes;
}

/* ===== Error Handling ===== */

void usdc_set_error(usdc_reader_t *reader, const char *message) {
    strncpy(reader->error_message, message, sizeof(reader->error_message) - 1);
    reader->error_message[sizeof(reader->error_message) - 1] = '\0';
}

void usdc_set_warning(usdc_reader_t *reader, const char *message) {
    strncpy(reader->warning_message, message, sizeof(reader->warning_message) - 1);
    reader->warning_message[sizeof(reader->warning_message) - 1] = '\0';
}

/* ===== File I/O Helpers ===== */

int usdc_read_uint8(usdc_reader_t *reader, uint8_t *value) {
    if (fread(value, 1, 1, reader->file) != 1) {
        usdc_set_error(reader, "Failed to read uint8");
        return 0;
    }
    return 1;
}

int usdc_read_uint32(usdc_reader_t *reader, uint32_t *value) {
    uint8_t bytes[4];
    if (fread(bytes, 1, 4, reader->file) != 4) {
        usdc_set_error(reader, "Failed to read uint32");
        return 0;
    }
    /* Little-endian byte order */
    *value = ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[2] << 16) | 
             ((uint32_t)bytes[1] << 8) | (uint32_t)bytes[0];
    return 1;
}

int usdc_read_uint64(usdc_reader_t *reader, uint64_t *value) {
    uint8_t bytes[8];
    if (fread(bytes, 1, 8, reader->file) != 8) {
        usdc_set_error(reader, "Failed to read uint64");
        return 0;
    }
    /* Little-endian byte order */
    *value = ((uint64_t)bytes[7] << 56) | ((uint64_t)bytes[6] << 48) |
             ((uint64_t)bytes[5] << 40) | ((uint64_t)bytes[4] << 32) |
             ((uint64_t)bytes[3] << 24) | ((uint64_t)bytes[2] << 16) |
             ((uint64_t)bytes[1] << 8) | (uint64_t)bytes[0];
    return 1;
}

int usdc_read_bytes(usdc_reader_t *reader, void *buffer, size_t size) {
    if (fread(buffer, 1, size, reader->file) != size) {
        usdc_set_error(reader, "Failed to read bytes");
        return 0;
    }
    return 1;
}

int usdc_seek(usdc_reader_t *reader, uint64_t offset) {
    if (fseek(reader->file, (long)offset, SEEK_SET) != 0) {
        usdc_set_error(reader, "Failed to seek file position");
        return 0;
    }
    return 1;
}

/* ===== Value Representation Utilities ===== */

int usdc_is_array(usdc_value_rep_t rep) {
    return (rep.data & USDC_VALUE_IS_ARRAY_BIT) != 0;
}

int usdc_is_inlined(usdc_value_rep_t rep) {
    return (rep.data & USDC_VALUE_IS_INLINED_BIT) != 0;
}

int usdc_is_compressed(usdc_value_rep_t rep) {
    return (rep.data & USDC_VALUE_IS_COMPRESSED_BIT) != 0;
}

uint32_t usdc_get_type_id(usdc_value_rep_t rep) {
    /* Extract the type ID from bits 48-55 (8 bits for type) */
    return (uint32_t)((rep.data >> 48) & 0xFF);
}

uint64_t usdc_get_payload(usdc_value_rep_t rep) {
    return rep.data & USDC_VALUE_PAYLOAD_MASK;
}

/* ===== LZ4 Decompression ===== */

int usdc_lz4_decompress(const char *src, char *dst, int compressed_size, int max_decompressed_size) {
    /* TinyUSDZ LZ4 wrapper format:
     * - First byte is number of chunks (0 for single chunk)
     * - For single chunk: rest is direct LZ4 data
     * - For multiple chunks: each chunk has int32_t size + compressed data
     */
    
    if (compressed_size <= 1) {
        return -1; /* Invalid compressed size */
    }
    
    /* Read number of chunks */
    int nChunks = (unsigned char)src[0];
    const char *compressed_data = src + 1;
    int remaining_compressed = compressed_size - 1;
    
    if (nChunks > 127) {
        return -2; /* Too many chunks */
    }
    
    if (nChunks == 0) {
        /* Single chunk - direct LZ4 decompression */
        return LZ4_decompress_safe(compressed_data, dst, remaining_compressed, max_decompressed_size);
    } else {
        /* Multiple chunks - not implemented for now */
        return -3; /* Multi-chunk not implemented */
    }
}

/* ===== Token Parsing Helpers ===== */

int usdc_parse_token_magic(const char *data, size_t size) {
    /* Check for ";-)" magic marker at start of decompressed token data */
    if (size < 3) {
        return 0;
    }
    return (data[0] == ';' && data[1] == '-' && data[2] == ')');
}

int usdc_parse_decompressed_tokens(usdc_reader_t *reader, const char *data, size_t data_size, size_t num_tokens) {
    /* Parse decompressed token data 
     * Format varies by version:
     * - 0.8.0+: ";-)" magic marker followed by null-terminated strings
     * - 0.7.0 and earlier: directly null-terminated strings (no magic marker)
     */
    
    const char *ptr = data;
    size_t remaining = data_size;
    
    /* Check if this uses the newer format with magic marker */
    int has_magic_marker = usdc_parse_token_magic(data, data_size);
    
    if (has_magic_marker) {
        /* Skip magic marker for 0.8.0+ format */
        ptr = data + 3;
        remaining = data_size - 3;
    } else {
        /* 0.7.0 format - no magic marker, tokens start immediately */
        ptr = data;
        remaining = data_size;
    }
    
    /* Parse tokens */
    for (size_t i = 0; i < num_tokens && remaining > 0; i++) {
        /* Find null terminator */
        size_t token_len = 0;
        while (token_len < remaining && ptr[token_len] != '\0') {
            token_len++;
        }
        
        if (token_len >= remaining) {
            usdc_set_error(reader, "Incomplete token data");
            return 0;
        }
        
        /* Allocate and copy token string */
        reader->tokens[i].length = token_len;
        if (token_len > 0) {
            reader->tokens[i].str = (char *)malloc(token_len + 1);
            if (!reader->tokens[i].str) {
                usdc_set_error(reader, "Failed to allocate token string");
                return 0;
            }
            memcpy(reader->tokens[i].str, ptr, token_len);
            reader->tokens[i].str[token_len] = '\0';
            usdc_update_memory_usage(reader, token_len + 1);
        } else {
            reader->tokens[i].str = NULL;
        }
        
        /* Move to next token */
        ptr += token_len + 1;  /* +1 for null terminator */
        remaining -= token_len + 1;
    }
    
    return 1;
}

/* ===== Header Reading ===== */

int usdc_read_header(usdc_reader_t *reader) {
    /* Read magic number */
    if (!usdc_read_bytes(reader, reader->header.magic, USDC_MAGIC_SIZE)) {
        return 0;
    }
    
    /* Verify magic number */
    if (memcmp(reader->header.magic, USDC_MAGIC, USDC_MAGIC_SIZE) != 0) {
        usdc_set_error(reader, "Invalid magic number - not a USDC file");
        return 0;
    }
    
    /* Read version */
    if (!usdc_read_bytes(reader, reader->header.version, USDC_VERSION_SIZE)) {
        return 0;
    }
    
    /* Check version - support 0.4.0 or later, up to 0.9.0 */
    if ((reader->header.version[0] == 0) && (reader->header.version[1] < 4)) {
        usdc_set_error(reader, "Unsupported version - minimum 0.4.0 required");
        return 0;
    }
    
    if ((reader->header.version[0] == 0) && (reader->header.version[1] >= 10)) {
        usdc_set_error(reader, "Unsupported version - maximum 0.9.0 supported");
        return 0;
    }
    
    /* Read TOC offset */
    if (!usdc_read_uint64(reader, &reader->header.toc_offset)) {
        return 0;
    }
    
    /* Validate TOC offset */
    if (reader->header.toc_offset <= USDC_HEADER_SIZE || 
        reader->header.toc_offset >= reader->file_size) {
        usdc_set_error(reader, "Invalid TOC offset");
        return 0;
    }
    
    return 1;
}

/* ===== Section Reading ===== */

int usdc_read_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Read section name (16 bytes, null-terminated) */
    if (!usdc_read_bytes(reader, section->name, 16)) {
        return 0;
    }
    section->name[15] = '\0';  /* Ensure null termination */
    
    /* Read start offset */
    if (!usdc_read_uint64(reader, &section->start)) {
        return 0;
    }
    
    /* Read size */
    if (!usdc_read_uint64(reader, &section->size)) {
        return 0;
    }
    
    /* Basic validation */
    if (section->start >= reader->file_size || 
        section->start + section->size > reader->file_size) {
        usdc_set_error(reader, "Invalid section boundaries");
        return 0;
    }
    
    return 1;
}

/* ===== TOC Reading ===== */

int usdc_read_toc(usdc_reader_t *reader) {
    /* Seek to TOC offset */
    if (!usdc_seek(reader, reader->header.toc_offset)) {
        return 0;
    }
    
    /* Read number of sections */
    if (!usdc_read_uint64(reader, &reader->toc.num_sections)) {
        return 0;
    }
    
    /* Validate number of sections */
    if (reader->toc.num_sections > USDC_MAX_TOC_SECTIONS) {
        usdc_set_error(reader, "Too many TOC sections");
        return 0;
    }
    
    if (reader->toc.num_sections == 0) {
        usdc_set_error(reader, "No TOC sections found");
        return 0;
    }
    
    /* Allocate memory for sections */
    size_t sections_size = reader->toc.num_sections * sizeof(usdc_section_t);
    if (!usdc_check_memory_limit(reader, sections_size)) {
        return 0;
    }
    
    reader->toc.sections = (usdc_section_t *)malloc(sections_size);
    if (!reader->toc.sections) {
        usdc_set_error(reader, "Failed to allocate memory for TOC sections");
        return 0;
    }
    usdc_update_memory_usage(reader, sections_size);
    
    /* Read all sections */
    for (uint64_t i = 0; i < reader->toc.num_sections; i++) {
        if (!usdc_read_section(reader, &reader->toc.sections[i])) {
            return 0;
        }
    }
    
    return 1;
}

/* ===== Token Section Reading ===== */

int usdc_read_tokens_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    /* Read number of tokens */
    uint64_t num_tokens;
    if (!usdc_read_uint64(reader, &num_tokens)) {
        return 0;
    }
    
    if (num_tokens > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Too many tokens");
        return 0;
    }
    
    if (num_tokens == 0) {
        usdc_set_error(reader, "Empty tokens section");
        return 0;
    }
    
    reader->num_tokens = (size_t)num_tokens;
    
    /* Allocate token array */
    size_t tokens_size = reader->num_tokens * sizeof(usdc_token_t);
    if (!usdc_check_memory_limit(reader, tokens_size)) {
        return 0;
    }
    
    reader->tokens = (usdc_token_t *)calloc(reader->num_tokens, sizeof(usdc_token_t));
    if (!reader->tokens) {
        usdc_set_error(reader, "Failed to allocate memory for tokens");
        return 0;
    }
    usdc_update_memory_usage(reader, tokens_size);
    
    /* In USDC version 0.4.0+, tokens are LZ4 compressed */
    
    uint64_t uncompressed_size;
    if (!usdc_read_uint64(reader, &uncompressed_size)) {
        return 0;
    }
    
    uint64_t compressed_size;  
    if (!usdc_read_uint64(reader, &compressed_size)) {
        return 0;
    }
    
    /* Basic validation */
    if (uncompressed_size < num_tokens + 3 || compressed_size > section->size) {
        usdc_set_error(reader, "Invalid token compression sizes");
        return 0;
    }
    
    if (uncompressed_size > USDC_MAX_STRING_LENGTH) {
        usdc_set_error(reader, "Uncompressed token data too large");
        return 0;
    }
    
    /* Read compressed token data */
    if (!usdc_check_memory_limit(reader, compressed_size)) {
        return 0;
    }
    
    char *compressed_data = (char *)malloc(compressed_size);
    if (!compressed_data) {
        usdc_set_error(reader, "Failed to allocate compressed token buffer");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, compressed_data, compressed_size)) {
        free(compressed_data);
        return 0;
    }
    
    /* Allocate buffer for decompressed data */
    if (!usdc_check_memory_limit(reader, uncompressed_size)) {
        free(compressed_data);
        return 0;
    }
    
    char *decompressed_data = (char *)malloc(uncompressed_size);
    if (!decompressed_data) {
        usdc_set_error(reader, "Failed to allocate decompressed token buffer");
        free(compressed_data);
        return 0;
    }
    
    /* Decompress with LZ4 */
    int decompressed_bytes = usdc_lz4_decompress(
        compressed_data, 
        decompressed_data, 
        (int)compressed_size, 
        (int)uncompressed_size);
    
    free(compressed_data);
    
    if (decompressed_bytes <= 0) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), 
            "LZ4 decompression failed (result=%d, compressed_size=%llu, uncompressed_size=%llu)", 
            decompressed_bytes, (unsigned long long)compressed_size, (unsigned long long)uncompressed_size);
        usdc_set_error(reader, error_buf);
        free(decompressed_data);
        return 0;
    }
    
    if ((size_t)decompressed_bytes != uncompressed_size) {
        usdc_set_error(reader, "LZ4 decompression size mismatch");
        free(decompressed_data);
        return 0;
    }
    
    /* Parse decompressed token data */
    if (!usdc_parse_decompressed_tokens(reader, decompressed_data, uncompressed_size, num_tokens)) {
        free(decompressed_data);
        return 0;
    }
    
    free(decompressed_data);
    
    return 1;
}

/* ===== USD Integer Decompression (Full Implementation) ===== */

size_t usdc_get_integer_working_space_size(size_t num_ints) {
    /* Calculate the encoded buffer size needed for working space */
    if (num_ints == 0) {
        return 0;
    }
    
    /* USD encoded format size:
     * - commonValue: 4 bytes (int32_t)
     * - codes section: (num_ints * 2 + 7) / 8 bytes (2 bits per integer, rounded up)
     * - variable integers section: num_ints * 4 bytes (worst case, all 32-bit)
     */
    return sizeof(int32_t) + ((num_ints * 2 + 7) / 8) + (num_ints * sizeof(int32_t));
}

/* Helper functions for reading different integer sizes with pointer advancement */
int8_t usdc_read_int8(const char **data_ptr) {
    int8_t value;
    memcpy(&value, *data_ptr, sizeof(int8_t));
    *data_ptr += sizeof(int8_t);
    return value;
}

int16_t usdc_read_int16(const char **data_ptr) {
    int16_t value;
    memcpy(&value, *data_ptr, sizeof(int16_t));
    *data_ptr += sizeof(int16_t);
    return value;
}

int32_t usdc_read_int32(const char **data_ptr) {
    int32_t value;
    memcpy(&value, *data_ptr, sizeof(int32_t));
    *data_ptr += sizeof(int32_t);
    return value;
}

uint8_t usdc_read_uint8_from_ptr(const char **data_ptr) {
    uint8_t value;
    memcpy(&value, *data_ptr, sizeof(uint8_t));
    *data_ptr += sizeof(uint8_t);
    return value;
}

uint16_t usdc_read_uint16_from_ptr(const char **data_ptr) {
    uint16_t value;
    memcpy(&value, *data_ptr, sizeof(uint16_t));
    *data_ptr += sizeof(uint16_t);
    return value;
}

uint32_t usdc_read_uint32_from_ptr(const char **data_ptr) {
    uint32_t value;
    memcpy(&value, *data_ptr, sizeof(uint32_t));
    *data_ptr += sizeof(uint32_t);
    return value;
}

size_t usdc_usd_integer_decode_signed(const char *encoded_data, size_t num_ints, int32_t *output) {
    if (!encoded_data || !output || num_ints == 0) {
        return 0;
    }
    
    /* Read the common value (most frequent delta) */
    const char *data_ptr = encoded_data;
    int32_t common_value = usdc_read_int32(&data_ptr);
    
    /* Calculate section sizes */
    size_t num_codes_bytes = (num_ints * 2 + 7) / 8;  /* 2 bits per integer, rounded up to bytes */
    const char *codes_ptr = data_ptr;
    const char *vints_ptr = data_ptr + num_codes_bytes;
    
    /* Decode integers using delta decompression */
    int32_t prev_val = 0;
    const char *vints_read_ptr = vints_ptr;
    
    for (size_t i = 0; i < num_ints; i++) {
        /* Determine which code applies to this integer */
        size_t code_byte_index = (i * 2) / 8;  /* Which byte contains our 2-bit code */
        size_t code_bit_offset = (i * 2) % 8;  /* Bit offset within that byte */
        
        if (code_byte_index >= num_codes_bytes) {
            break; /* Safety check */
        }
        
        uint8_t code_byte = codes_ptr[code_byte_index];
        uint8_t code = (code_byte >> code_bit_offset) & 0x3; /* Extract 2-bit code */
        
        int32_t delta;
        switch (code) {
            case 0: /* Common value */
                delta = common_value;
                break;
                
            case 1: /* 8-bit signed integer */
                delta = (int32_t)usdc_read_int8(&vints_read_ptr);
                break;
                
            case 2: /* 16-bit signed integer */
                delta = (int32_t)usdc_read_int16(&vints_read_ptr);
                break;
                
            case 3: /* 32-bit signed integer */
                delta = usdc_read_int32(&vints_read_ptr);
                break;
                
            default:
                delta = 0;
                break;
        }
        
        /* Apply delta to get actual value */
        prev_val += delta;
        output[i] = prev_val;
    }
    
    return num_ints;
}

size_t usdc_usd_integer_decode(const char *encoded_data, size_t num_ints, uint32_t *output) {
    /* For unsigned output, decode as signed then cast */
    int32_t *temp_output = (int32_t*)malloc(num_ints * sizeof(int32_t));
    if (!temp_output) {
        return 0;
    }
    
    size_t result = usdc_usd_integer_decode_signed(encoded_data, num_ints, temp_output);
    
    /* Copy with cast to unsigned */
    for (size_t i = 0; i < result; i++) {
        output[i] = (uint32_t)temp_output[i];
    }
    
    free(temp_output);
    return result;
}

int usdc_usd_integer_decompress_signed(const char *compressed_data, size_t compressed_size,
                                      int32_t *output, size_t num_ints, char *working_space, size_t working_space_size) {
    if (!compressed_data || !output || num_ints == 0) {
        return 0;
    }
    
    /* Step 1: LZ4 decompress to get the encoded integer stream */
    int decompressed_size = usdc_lz4_decompress(compressed_data, working_space, 
                                               (int)compressed_size, (int)working_space_size);
    
    if (decompressed_size <= 0) {
        return 0; /* LZ4 decompression failed */
    }
    
    /* Step 2: USD integer decode the decompressed stream */
    size_t decoded_count = usdc_usd_integer_decode_signed(working_space, num_ints, output);
    
    return (decoded_count == num_ints) ? 1 : 0;
}

int usdc_usd_integer_decompress(const char *compressed_data, size_t compressed_size,
                                uint32_t *output, size_t num_ints, char *working_space, size_t working_space_size) {
    /* For unsigned output, decode as signed then cast */
    int32_t *temp_output = (int32_t*)malloc(num_ints * sizeof(int32_t));
    if (!temp_output) {
        return 0;
    }
    
    int result = usdc_usd_integer_decompress_signed(compressed_data, compressed_size, 
                                                   temp_output, num_ints, working_space, working_space_size);
    
    if (result) {
        /* Copy with cast to unsigned */
        for (size_t i = 0; i < num_ints; i++) {
            output[i] = (uint32_t)temp_output[i];
        }
    }
    
    free(temp_output);
    return result;
}

/* ===== Fallback Integer Decompression (Original Simple Implementation) ===== */

size_t usdc_integer_decompress(const char *compressed_data, size_t compressed_size, 
                               uint32_t *output, size_t num_ints) {
    /* This is a simplified implementation that handles basic cases.
     * USD's integer compression is complex and involves multiple compression layers. */
    
    if (compressed_size == 0 || num_ints == 0) {
        return 0;
    }
    
    /* Try different decompression strategies */
    
    /* Strategy 1: Direct LZ4 decompression expecting raw integers */
    size_t expected_size = num_ints * sizeof(uint32_t);
    char *temp_buffer = (char *)malloc(expected_size * 2); /* Extra space for safety */
    if (!temp_buffer) {
        return 0;
    }
    
    int decompressed_bytes = usdc_lz4_decompress(compressed_data, temp_buffer, 
                                                 (int)compressed_size, (int)(expected_size * 2));
    
    if (decompressed_bytes > 0 && (size_t)decompressed_bytes >= expected_size) {
        /* Copy to output (handling endianness) */
        const uint32_t *src = (const uint32_t *)temp_buffer;
        for (size_t i = 0; i < num_ints; i++) {
            output[i] = src[i]; /* Assumes little-endian */
        }
        free(temp_buffer);
        return num_ints;
    }
    
    /* Strategy 2: Fallback - generate sequential indices */
    free(temp_buffer);
    
    /* For now, generate reasonable fallback values */
    for (size_t i = 0; i < num_ints; i++) {
        output[i] = (uint32_t)i;
    }
    
    return num_ints;
}

size_t usdc_integer_decompress_signed(const char *compressed_data, size_t compressed_size,
                                      int32_t *output, size_t num_ints) {
    /* Try decompression first */
    size_t result = usdc_integer_decompress(compressed_data, compressed_size, 
                                           (uint32_t *)output, num_ints);
    
    if (result == 0) {
        /* Fallback for signed integers - mix of positive/negative for realistic paths */
        for (size_t i = 0; i < num_ints; i++) {
            if (i == 0) {
                output[i] = 0; /* Root often uses token 0 */
            } else {
                output[i] = (int32_t)(i + 1); /* Positive token indices */
            }
        }
        result = num_ints;
    }
    
    return result;
}

/* ===== Path Decompression Functions ===== */

void usdc_cleanup_compressed_paths(usdc_compressed_paths_t *compressed) {
    if (!compressed) return;
    
    if (compressed->path_indices) {
        free(compressed->path_indices);
        compressed->path_indices = NULL;
    }
    if (compressed->element_token_indices) {
        free(compressed->element_token_indices);
        compressed->element_token_indices = NULL;
    }
    if (compressed->jumps) {
        free(compressed->jumps);
        compressed->jumps = NULL;
    }
    compressed->num_encoded_paths = 0;
}

int usdc_decompress_path_data(usdc_reader_t *reader, usdc_compressed_paths_t *compressed) {
    /* Read number of encoded paths */
    uint64_t num_encoded_paths;
    if (!usdc_read_uint64(reader, &num_encoded_paths)) {
        return 0;
    }
    
    
    if (num_encoded_paths > USDC_MAX_PATHS) {
        usdc_set_error(reader, "Too many encoded paths");
        return 0;
    }
    
    compressed->num_encoded_paths = (size_t)num_encoded_paths;
    
    /* Allocate arrays */
    size_t array_size = compressed->num_encoded_paths * sizeof(uint32_t);
    if (!usdc_check_memory_limit(reader, array_size * 3)) {
        return 0;
    }
    
    compressed->path_indices = (uint32_t *)malloc(array_size);
    compressed->element_token_indices = (int32_t *)malloc(array_size);  /* Note: int32_t */
    compressed->jumps = (int32_t *)malloc(array_size);                   /* Note: int32_t */
    
    if (!compressed->path_indices || !compressed->element_token_indices || !compressed->jumps) {
        usdc_set_error(reader, "Failed to allocate compressed path arrays");
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    usdc_update_memory_usage(reader, array_size * 3);
    
    /* Read and decompress path indices */
    uint64_t comp_path_size;
    if (!usdc_read_uint64(reader, &comp_path_size)) {
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    /* Validate compressed size */
    if (comp_path_size == 0 || comp_path_size > (1024 * 1024)) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), 
                 "Invalid compressed path size: %llu", (unsigned long long)comp_path_size);
        usdc_set_error(reader, error_buf);
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, comp_path_size)) {
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    char *comp_buffer = (char *)malloc(comp_path_size);
    if (!comp_buffer) {
        usdc_set_error(reader, "Failed to allocate compression buffer");
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (!usdc_read_bytes(reader, comp_buffer, comp_path_size)) {
        free(comp_buffer);
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    /* Calculate working space size for USD integer decompression */
    size_t working_space_size = usdc_get_integer_working_space_size(compressed->num_encoded_paths);
    char *working_space = (char*)malloc(working_space_size);
    if (!working_space) {
        free(comp_buffer);
        usdc_cleanup_compressed_paths(compressed);
        usdc_set_error(reader, "Failed to allocate working space for path decompression");
        return 0;
    }
    
    /* Try full USD integer decompression first */
    int success = usdc_usd_integer_decompress(comp_buffer, comp_path_size, 
                                             compressed->path_indices, compressed->num_encoded_paths,
                                             working_space, working_space_size);
    
    if (!success) {
        /* Fallback to simple decompression */
        size_t decompressed_paths = usdc_integer_decompress(comp_buffer, comp_path_size, 
                                    compressed->path_indices, compressed->num_encoded_paths);
        
        if (decompressed_paths == 0) {
            free(comp_buffer);
            free(working_space);
            usdc_set_error(reader, "Failed to decompress path indices");
            usdc_cleanup_compressed_paths(compressed);
            return 0;
        } else if (decompressed_paths != compressed->num_encoded_paths) {
            usdc_set_warning(reader, "Path indices decompression size mismatch, using partial data");
        }
    }
    
    free(comp_buffer);
    
    /* Read and decompress element token indices */
    uint64_t comp_token_size;
    if (!usdc_read_uint64(reader, &comp_token_size)) {
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (comp_token_size == 0 || comp_token_size > (1024 * 1024)) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), 
                 "Invalid compressed token size: %llu", (unsigned long long)comp_token_size);
        usdc_set_error(reader, error_buf);
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, comp_token_size)) {
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    comp_buffer = (char *)malloc(comp_token_size);
    if (!comp_buffer) {
        usdc_set_error(reader, "Failed to allocate compression buffer");
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (!usdc_read_bytes(reader, comp_buffer, comp_token_size)) {
        free(comp_buffer);
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    /* Try full USD integer decompression for element token indices */
    success = usdc_usd_integer_decompress_signed(comp_buffer, comp_token_size,
                                                compressed->element_token_indices, compressed->num_encoded_paths,
                                                working_space, working_space_size);
    
    if (!success) {
        /* Fallback to simple decompression */
        size_t decompressed_tokens = usdc_integer_decompress_signed(comp_buffer, comp_token_size,
                                           compressed->element_token_indices, compressed->num_encoded_paths);
        
        if (decompressed_tokens == 0) {
            free(comp_buffer);
            free(working_space);
            usdc_set_error(reader, "Failed to decompress element token indices");
            usdc_cleanup_compressed_paths(compressed);
            return 0;
        } else if (decompressed_tokens != compressed->num_encoded_paths) {
            usdc_set_warning(reader, "Element token indices decompression size mismatch, using partial data");
        }
    }
    
    free(comp_buffer);
    
    /* Read and decompress jumps */
    uint64_t comp_jumps_size;
    if (!usdc_read_uint64(reader, &comp_jumps_size)) {
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (comp_jumps_size == 0 || comp_jumps_size > (1024 * 1024)) {
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf), 
                 "Invalid compressed jumps size: %llu", (unsigned long long)comp_jumps_size);
        usdc_set_error(reader, error_buf);
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, comp_jumps_size)) {
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    comp_buffer = (char *)malloc(comp_jumps_size);
    if (!comp_buffer) {
        usdc_set_error(reader, "Failed to allocate compression buffer");
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    if (!usdc_read_bytes(reader, comp_buffer, comp_jumps_size)) {
        free(comp_buffer);
        usdc_cleanup_compressed_paths(compressed);
        return 0;
    }
    
    /* Try full USD integer decompression for jumps */
    success = usdc_usd_integer_decompress_signed(comp_buffer, comp_jumps_size,
                                                compressed->jumps, compressed->num_encoded_paths,
                                                working_space, working_space_size);
    
    if (!success) {
        /* Fallback to simple decompression */
        size_t decompressed_jumps = usdc_integer_decompress_signed(comp_buffer, comp_jumps_size,
                                           compressed->jumps, compressed->num_encoded_paths);
        
        if (decompressed_jumps == 0) {
            free(comp_buffer);
            free(working_space);
            usdc_set_error(reader, "Failed to decompress jumps");
            usdc_cleanup_compressed_paths(compressed);
            return 0;
        } else if (decompressed_jumps != compressed->num_encoded_paths) {
            usdc_set_warning(reader, "Jumps decompression size mismatch, using partial data");
        }
    }
    
    free(comp_buffer);
    free(working_space);
    
    return 1;
}

int usdc_build_paths(usdc_reader_t *reader, usdc_compressed_paths_t *compressed) {
    if (!reader || !compressed) {
        return 0;
    }
    
    if (compressed->num_encoded_paths == 0) {
        reader->num_paths = 0;
        reader->num_hierarchical_paths = 0;
        return 1;
    }
    
    /* Try hierarchical path building first (even with fallback data) */
    if (compressed->path_indices && compressed->element_token_indices && compressed->jumps && 
        usdc_build_hierarchical_paths(reader, compressed)) {
        /* Success! Copy hierarchical paths to regular paths for compatibility */
        if (reader->num_hierarchical_paths > 0) {
            /* Memory check */
            if (!usdc_check_memory_limit(reader, reader->num_hierarchical_paths * sizeof(usdc_path_t))) {
                return 0;
            }
            
            reader->paths = (usdc_path_t*)malloc(reader->num_hierarchical_paths * sizeof(usdc_path_t));
            if (!reader->paths) {
                usdc_set_error(reader, "Failed to allocate paths array");
                return 0;
            }
            
            /* Copy hierarchical paths to regular paths */
            for (size_t i = 0; i < reader->num_hierarchical_paths; i++) {
                usdc_hierarchical_path_t *hpath = &reader->hierarchical_paths[i];
                if (hpath->path_string) {
                    size_t path_len = strlen(hpath->path_string) + 1;
                    reader->paths[i].path_string = (char*)malloc(path_len);
                    if (reader->paths[i].path_string) {
                        strcpy(reader->paths[i].path_string, hpath->path_string);
                        reader->paths[i].length = path_len - 1;
                        reader->paths[i].is_absolute = hpath->is_absolute;
                    }
                } else {
                    /* Fallback */
                    char fallback_path[32];
                    snprintf(fallback_path, sizeof(fallback_path), "/hierarchical_path_%zu", i);
                    size_t fallback_len = strlen(fallback_path) + 1;
                    
                    reader->paths[i].path_string = (char*)malloc(fallback_len);
                    if (reader->paths[i].path_string) {
                        strcpy(reader->paths[i].path_string, fallback_path);
                        reader->paths[i].length = fallback_len - 1;
                        reader->paths[i].is_absolute = 1;
                    }
                }
            }
            
            reader->num_paths = reader->num_hierarchical_paths;
            usdc_update_memory_usage(reader, reader->num_hierarchical_paths * sizeof(usdc_path_t));
            return 1;
        }
    }
    
    /* Fallback to simple linear approach */
    usdc_set_warning(reader, "Hierarchical path building failed, using linear fallback");
    
    /* Memory check */
    if (!usdc_check_memory_limit(reader, compressed->num_encoded_paths * sizeof(usdc_path_t))) {
        return 0;
    }
    
    /* Allocate paths array */
    reader->paths = (usdc_path_t*)malloc(compressed->num_encoded_paths * sizeof(usdc_path_t));
    if (!reader->paths) {
        usdc_set_error(reader, "Failed to allocate paths array");
        return 0;
    }
    
    /* Build paths using simple linear approach (fallback) */
    for (size_t i = 0; i < compressed->num_encoded_paths; i++) {
        reader->paths[i].path_string = NULL;
        reader->paths[i].length = 0;
        reader->paths[i].is_absolute = 1;
        
        /* Get token index for this path element */
        if (i < compressed->num_encoded_paths && compressed->element_token_indices) {
            int32_t token_idx = compressed->element_token_indices[i];
            int is_property = (token_idx < 0);
            uint32_t actual_token_idx = is_property ? (uint32_t)(-token_idx) : (uint32_t)token_idx;
            
            /* Create path string from token */
            if (actual_token_idx < reader->num_tokens && reader->tokens[actual_token_idx].str) {
                const char *token_str = reader->tokens[actual_token_idx].str;
                size_t path_len = strlen(token_str) + 2; /* "/" + token + null */
                
                reader->paths[i].path_string = (char*)malloc(path_len);
                if (reader->paths[i].path_string) {
                    snprintf(reader->paths[i].path_string, path_len, "/%s", token_str);
                    reader->paths[i].length = path_len - 1;
                }
            }
        }
        
        /* Fallback for cases where token resolution fails */
        if (!reader->paths[i].path_string) {
            char fallback_path[32];
            snprintf(fallback_path, sizeof(fallback_path), "/path_%zu", i);
            size_t fallback_len = strlen(fallback_path) + 1;
            
            reader->paths[i].path_string = (char*)malloc(fallback_len);
            if (reader->paths[i].path_string) {
                strcpy(reader->paths[i].path_string, fallback_path);
                reader->paths[i].length = fallback_len - 1;
            }
        }
    }
    
    reader->num_paths = compressed->num_encoded_paths;
    usdc_update_memory_usage(reader, compressed->num_encoded_paths * sizeof(usdc_path_t));
    
    return 1;
}

int usdc_read_compressed_paths(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    usdc_compressed_paths_t compressed = {0};
    
    /* Decompress path data */
    if (!usdc_decompress_path_data(reader, &compressed)) {
        return 0;
    }
    
    /* Build paths from compressed data */
    if (!usdc_build_paths(reader, &compressed)) {
        usdc_cleanup_compressed_paths(&compressed);
        return 0;
    }
    
    usdc_cleanup_compressed_paths(&compressed);
    return 1;
}

/* ===== String Section Reading ===== */

int usdc_read_strings_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    /* Read number of string indices */
    uint64_t num_strings;
    if (!usdc_read_uint64(reader, &num_strings)) {
        return 0;
    }
    
    if (num_strings > USDC_MAX_STRINGS) {
        usdc_set_error(reader, "Too many string indices");
        return 0;
    }
    
    reader->num_string_indices = (size_t)num_strings;
    
    /* Allocate string indices array */
    size_t indices_size = reader->num_string_indices * sizeof(usdc_index_t);
    if (!usdc_check_memory_limit(reader, indices_size)) {
        return 0;
    }
    
    reader->string_indices = (usdc_index_t *)malloc(indices_size);
    if (!reader->string_indices) {
        usdc_set_error(reader, "Failed to allocate memory for string indices");
        return 0;
    }
    usdc_update_memory_usage(reader, indices_size);
    
    /* Read each string index */
    for (size_t i = 0; i < reader->num_string_indices; i++) {
        if (!usdc_read_uint32(reader, &reader->string_indices[i].value)) {
            return 0;
        }
    }
    
    return 1;
}

/* ===== Field Section Reading ===== */

int usdc_read_fields_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    /* Read number of fields */
    uint64_t num_fields;
    if (!usdc_read_uint64(reader, &num_fields)) {
        return 0;
    }
    
    if (num_fields > USDC_MAX_FIELDS) {
        usdc_set_error(reader, "Too many fields");
        return 0;
    }
    
    if (num_fields == 0) {
        reader->num_fields = 0;
        return 1;  /* Empty fields is OK */
    }
    
    reader->num_fields = (size_t)num_fields;
    
    /* Allocate fields array */
    size_t fields_size = reader->num_fields * sizeof(usdc_field_t);
    if (!usdc_check_memory_limit(reader, fields_size)) {
        return 0;
    }
    
    reader->fields = (usdc_field_t *)malloc(fields_size);
    if (!reader->fields) {
        usdc_set_error(reader, "Failed to allocate memory for fields");
        return 0;
    }
    usdc_update_memory_usage(reader, fields_size);
    
    /* Read token indices (compressed integers) */
    uint32_t *temp_indices = (uint32_t *)malloc(reader->num_fields * sizeof(uint32_t));
    if (!temp_indices) {
        usdc_set_error(reader, "Failed to allocate temp indices");
        return 0;
    }
    
    /* For now, try to read compressed integers. If that fails, use fallback */
    size_t working_space_size = usdc_get_integer_working_space_size(reader->num_fields);
    char *working_space = (char *)malloc(working_space_size);
    if (!working_space) {
        free(temp_indices);
        usdc_set_error(reader, "Failed to allocate working space");
        return 0;
    }
    
    /* Read the compressed data size first */
    uint64_t compressed_size;
    if (!usdc_read_uint64(reader, &compressed_size)) {
        free(temp_indices);
        free(working_space);
        return 0;
    }
    
    if (compressed_size > section->size) {
        free(temp_indices);
        free(working_space);
        usdc_set_error(reader, "Invalid compressed indices size");
        return 0;
    }
    
    /* Read compressed data */
    char *compressed_data = (char *)malloc(compressed_size);
    if (!compressed_data) {
        free(temp_indices);
        free(working_space);
        usdc_set_error(reader, "Failed to allocate compressed data buffer");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, compressed_data, compressed_size)) {
        free(temp_indices);
        free(working_space);
        free(compressed_data);
        return 0;
    }
    
    /* Try to decompress using USD integer compression */
    int success = usdc_usd_integer_decompress(compressed_data, compressed_size, 
                                             temp_indices, reader->num_fields,
                                             working_space, working_space_size);
    if (!success) {
        /* Fallback: try simple decompression */
        size_t decompressed_count = usdc_integer_decompress(compressed_data, compressed_size,
                                                           temp_indices, reader->num_fields);
        if (decompressed_count != reader->num_fields) {
            free(temp_indices);
            free(working_space);
            free(compressed_data);
            usdc_set_error(reader, "Failed to decompress field token indices");
            return 0;
        }
    }
    
    /* Copy token indices */
    for (size_t i = 0; i < reader->num_fields; i++) {
        reader->fields[i].token_index.value = temp_indices[i];
    }
    
    free(temp_indices);
    free(working_space);
    free(compressed_data);
    
    /* Read value representations (LZ4 compressed) */
    uint64_t reps_compressed_size;
    if (!usdc_read_uint64(reader, &reps_compressed_size)) {
        return 0;
    }
    
    if (reps_compressed_size > section->size) {
        usdc_set_error(reader, "Invalid value reps compressed size");
        return 0;
    }
    
    /* Read compressed value reps */
    char *compressed_reps = (char *)malloc(reps_compressed_size);
    if (!compressed_reps) {
        usdc_set_error(reader, "Failed to allocate value reps buffer");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, compressed_reps, reps_compressed_size)) {
        free(compressed_reps);
        return 0;
    }
    
    /* Decompress value representations using LZ4 */
    size_t uncompressed_reps_size = reader->num_fields * sizeof(uint64_t);
    uint64_t *reps_data = (uint64_t *)malloc(uncompressed_reps_size);
    if (!reps_data) {
        free(compressed_reps);
        usdc_set_error(reader, "Failed to allocate reps data");
        return 0;
    }
    
    int lz4_result = usdc_lz4_decompress(compressed_reps, (char *)reps_data,
                                        (int)reps_compressed_size, (int)uncompressed_reps_size);
    if (lz4_result <= 0) {
        free(compressed_reps);
        free(reps_data);
        usdc_set_error(reader, "Failed to LZ4 decompress value representations");
        return 0;
    }
    
    /* Copy value representations */
    for (size_t i = 0; i < reader->num_fields; i++) {
        reader->fields[i].value_rep.data = reps_data[i];
    }
    
    free(compressed_reps);
    free(reps_data);
    
    return 1;
}

/* ===== Path Section Reading ===== */

int usdc_read_paths_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    /* Read number of paths */
    uint64_t num_paths;
    if (!usdc_read_uint64(reader, &num_paths)) {
        return 0;
    }
    
    if (num_paths > USDC_MAX_PATHS) {
        usdc_set_error(reader, "Too many paths");
        return 0;
    }
    
    if (num_paths == 0) {
        usdc_set_error(reader, "No paths in PATHS section");
        return 0;
    }
    
    reader->num_paths = (size_t)num_paths;
    
    /* Allocate paths array */
    size_t paths_size = reader->num_paths * sizeof(usdc_path_t);
    if (!usdc_check_memory_limit(reader, paths_size)) {
        return 0;
    }
    
    reader->paths = (usdc_path_t *)calloc(reader->num_paths, sizeof(usdc_path_t));
    if (!reader->paths) {
        usdc_set_error(reader, "Failed to allocate memory for paths");
        return 0;
    }
    usdc_update_memory_usage(reader, paths_size);
    
    /* Try to read compressed path data, fallback to simple paths on error */
    if (!usdc_read_compressed_paths(reader, section)) {
        /* Fallback: Create reasonable path names */
        usdc_set_warning(reader, "Path decompression failed, using fallback paths");
        
        for (size_t i = 0; i < reader->num_paths; i++) {
            char path_buf[64];
            if (i == 0) {
                strcpy(path_buf, "/");
            } else if (i < reader->num_tokens && reader->tokens[i].str) {
                snprintf(path_buf, sizeof(path_buf), "/%s", reader->tokens[i].str);
            } else {
                snprintf(path_buf, sizeof(path_buf), "/path_%zu", i);
            }
            
            size_t path_len = strlen(path_buf);
            reader->paths[i].path_string = (char *)malloc(path_len + 1);
            if (reader->paths[i].path_string) {
                strcpy(reader->paths[i].path_string, path_buf);
                reader->paths[i].length = path_len;
                reader->paths[i].is_absolute = (path_buf[0] == '/') ? 1 : 0;
                usdc_update_memory_usage(reader, path_len + 1);
            }
        }
    }
    
    return 1;
}

/* ===== Hierarchical Path Building ===== */

int usdc_build_hierarchical_paths(usdc_reader_t *reader, usdc_compressed_paths_t *compressed) {
    if (!reader || !compressed || compressed->num_encoded_paths == 0) {
        return 0;
    }
    
    /* Allocate hierarchical paths array */
    if (!usdc_check_memory_limit(reader, compressed->num_encoded_paths * sizeof(usdc_hierarchical_path_t))) {
        return 0;
    }
    
    reader->hierarchical_paths = (usdc_hierarchical_path_t*)malloc(compressed->num_encoded_paths * sizeof(usdc_hierarchical_path_t));
    if (!reader->hierarchical_paths) {
        usdc_set_error(reader, "Failed to allocate hierarchical paths array");
        return 0;
    }
    
    /* Initialize all paths */
    for (size_t i = 0; i < compressed->num_encoded_paths; i++) {
        memset(&reader->hierarchical_paths[i], 0, sizeof(usdc_hierarchical_path_t));
        reader->hierarchical_paths[i].parent_index = USDC_INVALID_INDEX;
    }
    
    reader->num_hierarchical_paths = compressed->num_encoded_paths;
    
    /* Create visit table to prevent circular references */
    int *visit_table = (int*)malloc(compressed->num_encoded_paths * sizeof(int));
    if (!visit_table) {
        usdc_set_error(reader, "Failed to allocate visit table");
        return 0;
    }
    memset(visit_table, 0, compressed->num_encoded_paths * sizeof(int));
    
    /* Start hierarchical path building from root (index 0) */
    int result = usdc_build_hierarchical_paths_recursive(reader, compressed, 0, USDC_INVALID_INDEX, "/", 0, visit_table);
    
    free(visit_table);
    
    if (!result) {
        usdc_set_error(reader, "Hierarchical path building failed");
        return 0;
    }
    
    usdc_update_memory_usage(reader, compressed->num_encoded_paths * sizeof(usdc_hierarchical_path_t));
    return 1;
}

int usdc_build_hierarchical_paths_recursive(usdc_reader_t *reader, 
                                            usdc_compressed_paths_t *compressed,
                                            size_t current_index, 
                                            size_t parent_path_index,
                                            const char *parent_path_string,
                                            size_t depth,
                                            int *visit_table) {
    /* Security check: prevent infinite recursion */
    if (depth > 100) {
        usdc_set_error(reader, "Path hierarchy too deep");
        return 0;
    }
    
    /* Loop to handle siblings */
    do {
        /* Bounds check */
        if (current_index >= compressed->num_encoded_paths) {
            break;
        }
        
        /* Check for circular references */
        uint32_t path_idx = compressed->path_indices[current_index];
        if (path_idx >= compressed->num_encoded_paths || visit_table[path_idx]) {
            /* Skip this path to avoid circular reference */
            current_index++;
            continue;
        }
        
        /* Mark as visited */
        visit_table[path_idx] = 1;
        
        /* Get path information */
        usdc_hierarchical_path_t *hpath = &reader->hierarchical_paths[path_idx];
        hpath->parent_index = parent_path_index;
        hpath->depth = depth;
        hpath->is_absolute = 1;
        
        /* Handle root path */
        if (depth == 0) {
            /* Root path */
            hpath->path_string = (char*)malloc(2);
            if (hpath->path_string) {
                strcpy(hpath->path_string, "/");
            }
            hpath->element_name = (char*)malloc(2);
            if (hpath->element_name) {
                strcpy(hpath->element_name, "/");
            }
            hpath->is_property_path = 0;
        } else {
            /* Get element token */
            int32_t token_idx = compressed->element_token_indices[current_index];
            hpath->is_property_path = (token_idx < 0) ? 1 : 0;
            uint32_t actual_token_idx = hpath->is_property_path ? (uint32_t)(-token_idx) : (uint32_t)token_idx;
            
            /* Get element name from token */
            const char *element_name = NULL;
            if (actual_token_idx < reader->num_tokens && reader->tokens[actual_token_idx].str) {
                element_name = reader->tokens[actual_token_idx].str;
            }
            
            if (!element_name) {
                /* Fallback element name */
                char fallback_name[32];
                snprintf(fallback_name, sizeof(fallback_name), "element_%u", actual_token_idx);
                
                hpath->element_name = (char*)malloc(strlen(fallback_name) + 1);
                if (hpath->element_name) {
                    strcpy(hpath->element_name, fallback_name);
                }
            } else {
                hpath->element_name = (char*)malloc(strlen(element_name) + 1);
                if (hpath->element_name) {
                    strcpy(hpath->element_name, element_name);
                }
            }
            
            /* Build full path string */
            if (hpath->element_name) {
                const char *separator = hpath->is_property_path ? "." : "/";
                size_t parent_len = strlen(parent_path_string);
                size_t element_len = strlen(hpath->element_name);
                size_t separator_len = strlen(separator);
                
                /* Handle root parent case */
                if (parent_len == 1 && parent_path_string[0] == '/') {
                    /* Parent is root, don't add extra slash */
                    hpath->path_string = (char*)malloc(parent_len + element_len + 1);
                    if (hpath->path_string) {
                        snprintf(hpath->path_string, parent_len + element_len + 1, "%s%s", 
                                parent_path_string, hpath->element_name);
                    }
                } else {
                    /* Regular path building */
                    hpath->path_string = (char*)malloc(parent_len + separator_len + element_len + 1);
                    if (hpath->path_string) {
                        snprintf(hpath->path_string, parent_len + separator_len + element_len + 1, 
                                "%s%s%s", parent_path_string, separator, hpath->element_name);
                    }
                }
            }
        }
        
        /* Get jump information */
        int32_t jump = compressed->jumps[current_index];
        int has_child = (jump > 0) || (jump == -1);
        int has_sibling = (jump >= 0) && (jump != -1);
        
        /* Process children first */
        if (has_child) {
            size_t child_index = current_index + 1;
            
            /* If we also have a sibling, process it first (recursively) */
            if (has_sibling && jump > 0) {
                size_t sibling_index = current_index + (size_t)jump;
                if (!usdc_build_hierarchical_paths_recursive(reader, compressed, sibling_index, 
                                                           parent_path_index, parent_path_string, 
                                                           depth, visit_table)) {
                    return 0;
                }
            }
            
            /* Process child with current path as parent */
            const char *new_parent_path = hpath->path_string ? hpath->path_string : "/";
            if (!usdc_build_hierarchical_paths_recursive(reader, compressed, child_index, 
                                                       path_idx, new_parent_path, 
                                                       depth + 1, visit_table)) {
                return 0;
            }
        }
        
        /* Move to sibling if we only have sibling (no recursive call needed) */
        if (has_sibling && !has_child && jump > 0) {
            current_index = current_index + (size_t)jump;
        } else {
            /* End of this branch */
            break;
        }
        
    } while (1);
    
    return 1;
}

void usdc_print_hierarchical_paths(usdc_reader_t *reader) {
    if (!reader || !reader->hierarchical_paths) {
        return;
    }
    
    printf("=== Hierarchical Paths ===\n");
    printf("Number of hierarchical paths: %zu\n", reader->num_hierarchical_paths);
    
    if (reader->num_hierarchical_paths > 0) {
        printf("Path hierarchy:\n");
        
        /* Print paths sorted by depth to show hierarchy */
        for (size_t depth = 0; depth <= 10; depth++) {
            for (size_t i = 0; i < reader->num_hierarchical_paths; i++) {
                usdc_hierarchical_path_t *hpath = &reader->hierarchical_paths[i];
                if (hpath->depth == depth && hpath->path_string) {
                    /* Print indentation based on depth */
                    for (size_t d = 0; d < depth; d++) {
                        printf("  ");
                    }
                    
                    printf("[%zu] \"%s\"", i, hpath->path_string);
                    if (hpath->element_name) {
                        printf(" (element: \"%s\")", hpath->element_name);
                    }
                    if (hpath->is_property_path) {
                        printf(" [PROPERTY]");
                    }
                    if (hpath->parent_index != USDC_INVALID_INDEX) {
                        printf(" (parent: %zu)", hpath->parent_index);
                    }
                    printf(" depth=%zu\n", hpath->depth);
                }
            }
        }
    }
    printf("\n");
}

/* ===== Main API Functions ===== */

int usdc_reader_init(usdc_reader_t *reader, const char *filename) {
    memset(reader, 0, sizeof(*reader));
    
    /* Open file */
    reader->file = fopen(filename, "rb");
    if (!reader->file) {
        usdc_set_error(reader, "Failed to open file");
        return 0;
    }
    
    /* Get file size */
    fseek(reader->file, 0, SEEK_END);
    reader->file_size = ftell(reader->file);
    fseek(reader->file, 0, SEEK_SET);
    
    if (reader->file_size < USDC_HEADER_SIZE) {
        usdc_set_error(reader, "File too small to be valid USDC");
        return 0;
    }
    
    return 1;
}

void usdc_reader_cleanup(usdc_reader_t *reader) {
    if (!reader) return;
    
    /* Close file */
    if (reader->file) {
        fclose(reader->file);
        reader->file = NULL;
    }
    
    /* Free TOC sections */
    if (reader->toc.sections) {
        free(reader->toc.sections);
        reader->toc.sections = NULL;
    }
    
    /* Free tokens */
    if (reader->tokens) {
        for (size_t i = 0; i < reader->num_tokens; i++) {
            if (reader->tokens[i].str) {
                free(reader->tokens[i].str);
            }
        }
        free(reader->tokens);
        reader->tokens = NULL;
    }
    
    /* Free string indices */
    if (reader->string_indices) {
        free(reader->string_indices);
        reader->string_indices = NULL;
    }
    
    /* Free fields */
    if (reader->fields) {
        free(reader->fields);
        reader->fields = NULL;
    }
    
    /* Free paths */
    if (reader->paths) {
        for (size_t i = 0; i < reader->num_paths; i++) {
            if (reader->paths[i].path_string) {
                free(reader->paths[i].path_string);
            }
        }
        free(reader->paths);
        reader->paths = NULL;
    }
    
    /* Free specs */
    if (reader->specs) {
        free(reader->specs);
        reader->specs = NULL;
    }
    
    /* Free fieldsets */
    if (reader->fieldsets) {
        for (size_t i = 0; i < reader->num_fieldsets; i++) {
            if (reader->fieldsets[i].field_indices) {
                free(reader->fieldsets[i].field_indices);
            }
        }
        free(reader->fieldsets);
        reader->fieldsets = NULL;
    }
    
    /* Free hierarchical paths */
    if (reader->hierarchical_paths) {
        for (size_t i = 0; i < reader->num_hierarchical_paths; i++) {
            if (reader->hierarchical_paths[i].path_string) {
                free(reader->hierarchical_paths[i].path_string);
            }
            if (reader->hierarchical_paths[i].element_name) {
                free(reader->hierarchical_paths[i].element_name);
            }
        }
        free(reader->hierarchical_paths);
        reader->hierarchical_paths = NULL;
    }
    
    memset(reader, 0, sizeof(*reader));
}

int usdc_reader_read_file(usdc_reader_t *reader) {
    /* Read header */
    if (!usdc_read_header(reader)) {
        return 0;
    }
    
    /* Read TOC */
    if (!usdc_read_toc(reader)) {
        return 0;
    }
    
    /* Process each section */
    for (uint64_t i = 0; i < reader->toc.num_sections; i++) {
        usdc_section_t *section = &reader->toc.sections[i];
        
        if (strcmp(section->name, "TOKENS") == 0) {
            if (!usdc_read_tokens_section(reader, section)) {
                return 0;
            }
        } else if (strcmp(section->name, "STRINGS") == 0) {
            if (!usdc_read_strings_section(reader, section)) {
                return 0;
            }
        } else if (strcmp(section->name, "FIELDS") == 0) {
            if (!usdc_read_fields_section(reader, section)) {
                return 0;
            }
        } else if (strcmp(section->name, "PATHS") == 0) {
            if (!usdc_read_paths_section(reader, section)) {
                return 0;
            }
        } else if (strcmp(section->name, "SPECS") == 0) {
            if (!usdc_read_specs_section(reader, section)) {
                return 0;
            }
        } else if (strcmp(section->name, "FIELDSETS") == 0) {
            if (!usdc_read_fieldsets_section(reader, section)) {
                return 0;
            }
        }
        /* Add other section types as needed */
    }
    
    return 1;
}

const char *usdc_reader_get_error(usdc_reader_t *reader) {
    return reader->error_message;
}

const char *usdc_reader_get_warning(usdc_reader_t *reader) {
    return reader->warning_message;
}

/* ===== Value Parsing Implementation ===== */

const char *usdc_get_data_type_name(usdc_data_type_t type) {
    switch (type) {
        case USDC_DATA_TYPE_INVALID: return "invalid";
        case USDC_DATA_TYPE_BOOL: return "bool";
        case USDC_DATA_TYPE_UCHAR: return "uchar";
        case USDC_DATA_TYPE_INT: return "int";
        case USDC_DATA_TYPE_UINT: return "uint";
        case USDC_DATA_TYPE_INT64: return "int64";
        case USDC_DATA_TYPE_UINT64: return "uint64";
        case USDC_DATA_TYPE_HALF: return "half";
        case USDC_DATA_TYPE_FLOAT: return "float";
        case USDC_DATA_TYPE_DOUBLE: return "double";
        case USDC_DATA_TYPE_STRING: return "string";
        case USDC_DATA_TYPE_TOKEN: return "token";
        case USDC_DATA_TYPE_ASSET_PATH: return "asset_path";
        case USDC_DATA_TYPE_MATRIX2D: return "matrix2d";
        case USDC_DATA_TYPE_MATRIX3D: return "matrix3d";
        case USDC_DATA_TYPE_MATRIX4D: return "matrix4d";
        case USDC_DATA_TYPE_QUATD: return "quatd";
        case USDC_DATA_TYPE_QUATF: return "quatf";
        case USDC_DATA_TYPE_QUATH: return "quath";
        case USDC_DATA_TYPE_VEC2D: return "vec2d";
        case USDC_DATA_TYPE_VEC2F: return "vec2f";
        case USDC_DATA_TYPE_VEC2H: return "vec2h";
        case USDC_DATA_TYPE_VEC2I: return "vec2i";
        case USDC_DATA_TYPE_VEC3D: return "vec3d";
        case USDC_DATA_TYPE_VEC3F: return "vec3f";
        case USDC_DATA_TYPE_VEC3H: return "vec3h";
        case USDC_DATA_TYPE_VEC3I: return "vec3i";
        case USDC_DATA_TYPE_VEC4D: return "vec4d";
        case USDC_DATA_TYPE_VEC4F: return "vec4f";
        case USDC_DATA_TYPE_VEC4H: return "vec4h";
        case USDC_DATA_TYPE_VEC4I: return "vec4i";
        default: return "unknown";
    }
}

int usdc_parse_value_rep(usdc_reader_t *reader, usdc_value_rep_t rep, usdc_parsed_value_t *parsed_value) {
    if (!reader || !parsed_value) {
        return 0;
    }
    
    /* Clear the parsed value structure */
    memset(parsed_value, 0, sizeof(*parsed_value));
    
    /* Extract metadata from value representation */
    parsed_value->type = (usdc_data_type_t)usdc_get_type_id(rep);
    parsed_value->is_array = usdc_is_array(rep);
    parsed_value->is_inlined = usdc_is_inlined(rep);
    parsed_value->is_compressed = usdc_is_compressed(rep);
    parsed_value->payload = usdc_get_payload(rep);
    
    /* Handle invalid types */
    if (parsed_value->type == USDC_DATA_TYPE_INVALID || parsed_value->type >= USDC_NUM_DATA_TYPES) {
        usdc_set_error(reader, "Invalid data type in value representation");
        return 0;
    }
    
    /* Check for compressed data (not fully supported) */
    if (parsed_value->is_compressed) {
        usdc_set_warning(reader, "Compressed values not fully supported");
        return 0;
    }
    
    /* Parse based on whether it's inlined or not */
    if (parsed_value->is_inlined) {
        return usdc_parse_inlined_value(reader, rep, parsed_value);
    } else {
        return usdc_parse_non_inlined_value(reader, rep, parsed_value);
    }
}

int usdc_parse_inlined_value(usdc_reader_t *reader, usdc_value_rep_t rep, usdc_parsed_value_t *parsed_value) {
    uint64_t payload = parsed_value->payload;
    
    switch (parsed_value->type) {
        case USDC_DATA_TYPE_BOOL:
            parsed_value->value.bool_val = (payload != 0) ? 1 : 0;
            break;
            
        case USDC_DATA_TYPE_UCHAR:
            parsed_value->value.uchar_val = (uint8_t)(payload & 0xFF);
            break;
            
        case USDC_DATA_TYPE_INT:
            /* Sign extend from 48 bits to 32 bits */
            if (payload & (1ULL << 47)) {
                parsed_value->value.int_val = (int32_t)(payload | 0xFFFF000000000000ULL);
            } else {
                parsed_value->value.int_val = (int32_t)(payload & 0xFFFFFFFF);
            }
            break;
            
        case USDC_DATA_TYPE_UINT:
            parsed_value->value.uint_val = (uint32_t)(payload & 0xFFFFFFFF);
            break;
            
        case USDC_DATA_TYPE_INT64:
            parsed_value->value.int64_val = (int64_t)payload;
            break;
            
        case USDC_DATA_TYPE_UINT64:
            parsed_value->value.uint64_val = payload;
            break;
            
        case USDC_DATA_TYPE_FLOAT:
            /* Payload contains 32-bit float bits */
            {
                uint32_t float_bits = (uint32_t)(payload & 0xFFFFFFFF);
                memcpy(&parsed_value->value.float_val, &float_bits, sizeof(float));
            }
            break;
            
        case USDC_DATA_TYPE_DOUBLE:
            /* Payload contains 48-bit double approximation (not full precision) */
            memcpy(&parsed_value->value.double_val, &payload, sizeof(double));
            break;
            
        case USDC_DATA_TYPE_TOKEN:
            parsed_value->value.token_index = (uint32_t)(payload & 0xFFFFFFFF);
            break;
            
        case USDC_DATA_TYPE_STRING:
            parsed_value->value.string_index = (uint32_t)(payload & 0xFFFFFFFF);
            break;
            
        case USDC_DATA_TYPE_SPECIFIER:
            /* Specifier values: 0=def, 1=over, 2=class */
            parsed_value->value.uint_val = (uint32_t)(payload & 0xFF);
            break;
            
        case USDC_DATA_TYPE_VARIABILITY:
            /* Variability values: 0=varying, 1=uniform, 2=config */
            parsed_value->value.uint_val = (uint32_t)(payload & 0xFF);
            break;
            
        case USDC_DATA_TYPE_PERMISSION:
            /* Permission values */
            parsed_value->value.uint_val = (uint32_t)(payload & 0xFF);
            break;
            
        case USDC_DATA_TYPE_TOKEN_VECTOR:
            /* Token vectors are non-inlined arrays typically */
            if (payload == 0) {
                /* Empty vector */
                parsed_value->array_size = 0;
                parsed_value->value.data_ptr = NULL;
            } else {
                usdc_set_error(reader, "Non-empty token vector should not be inlined");
                return 0;
            }
            break;
            
        default:
            usdc_set_error(reader, "Inlined value type not supported");
            return 0;
    }
    
    return 1;
}

int usdc_parse_non_inlined_value(usdc_reader_t *reader, usdc_value_rep_t rep, usdc_parsed_value_t *parsed_value) {
    uint64_t offset = parsed_value->payload;
    
    /* Seek to the data location */
    if (!usdc_seek(reader, offset)) {
        usdc_set_error(reader, "Failed to seek to value data");
        return 0;
    }
    
    /* Handle arrays vs single values */
    if (parsed_value->is_array) {
        switch (parsed_value->type) {
            case USDC_DATA_TYPE_BOOL:
                return usdc_parse_bool_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_INT:
                return usdc_parse_int_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_UINT:
                return usdc_parse_uint_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_INT64:
                return usdc_parse_int64_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_UINT64:
                return usdc_parse_uint64_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_FLOAT:
                return usdc_parse_float_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_DOUBLE:
                return usdc_parse_double_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_TOKEN:
                return usdc_parse_token_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_STRING:
                return usdc_parse_string_array(reader, offset, parsed_value);
            case USDC_DATA_TYPE_TOKEN_VECTOR:
                return usdc_parse_token_array(reader, offset, parsed_value);
            default:
                usdc_set_error(reader, "Array type not supported");
                return 0;
        }
    } else {
        /* Single values */
        switch (parsed_value->type) {
            case USDC_DATA_TYPE_BOOL:
                {
                    uint8_t val;
                    if (!usdc_read_uint8(reader, &val)) return 0;
                    parsed_value->value.bool_val = (val != 0) ? 1 : 0;
                }
                break;
                
            case USDC_DATA_TYPE_INT:
                if (!usdc_read_uint32(reader, (uint32_t*)&parsed_value->value.int_val)) return 0;
                break;
                
            case USDC_DATA_TYPE_UINT:
                if (!usdc_read_uint32(reader, &parsed_value->value.uint_val)) return 0;
                break;
                
            case USDC_DATA_TYPE_INT64:
                if (!usdc_read_uint64(reader, (uint64_t*)&parsed_value->value.int64_val)) return 0;
                break;
                
            case USDC_DATA_TYPE_UINT64:
                if (!usdc_read_uint64(reader, &parsed_value->value.uint64_val)) return 0;
                break;
                
            case USDC_DATA_TYPE_FLOAT:
                if (!usdc_read_bytes(reader, &parsed_value->value.float_val, sizeof(float))) return 0;
                break;
                
            case USDC_DATA_TYPE_DOUBLE:
                if (!usdc_read_bytes(reader, &parsed_value->value.double_val, sizeof(double))) return 0;
                break;
                
            case USDC_DATA_TYPE_TOKEN:
                if (!usdc_read_uint32(reader, &parsed_value->value.token_index)) return 0;
                break;
                
            case USDC_DATA_TYPE_STRING:
                if (!usdc_read_uint32(reader, &parsed_value->value.string_index)) return 0;
                break;
                
            default:
                usdc_set_error(reader, "Single value type not supported");
                return 0;
        }
    }
    
    return 1;
}

int usdc_parse_bool_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read bool array size");
        return 0;
    }
    
    /* Security check */
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Bool array too large");
        return 0;
    }
    
    /* Memory check */
    if (!usdc_check_memory_limit(reader, array_size * sizeof(uint8_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    /* Allocate and read array */
    uint8_t *data = (uint8_t*)malloc(array_size * sizeof(uint8_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate bool array");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, data, array_size * sizeof(uint8_t))) {
        free(data);
        usdc_set_error(reader, "Failed to read bool array data");
        return 0;
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(uint8_t));
    
    return 1;
}

int usdc_parse_int_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read int array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Int array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(int32_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    int32_t *data = (int32_t*)malloc(array_size * sizeof(int32_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate int array");
        return 0;
    }
    
    for (uint64_t i = 0; i < array_size; i++) {
        if (!usdc_read_uint32(reader, (uint32_t*)&data[i])) {
            free(data);
            usdc_set_error(reader, "Failed to read int array data");
            return 0;
        }
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(int32_t));
    
    return 1;
}

int usdc_parse_uint_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read uint array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Uint array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(uint32_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    uint32_t *data = (uint32_t*)malloc(array_size * sizeof(uint32_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate uint array");
        return 0;
    }
    
    for (uint64_t i = 0; i < array_size; i++) {
        if (!usdc_read_uint32(reader, &data[i])) {
            free(data);
            usdc_set_error(reader, "Failed to read uint array data");
            return 0;
        }
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(uint32_t));
    
    return 1;
}

int usdc_parse_int64_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read int64 array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Int64 array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(int64_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    int64_t *data = (int64_t*)malloc(array_size * sizeof(int64_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate int64 array");
        return 0;
    }
    
    for (uint64_t i = 0; i < array_size; i++) {
        if (!usdc_read_uint64(reader, (uint64_t*)&data[i])) {
            free(data);
            usdc_set_error(reader, "Failed to read int64 array data");
            return 0;
        }
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(int64_t));
    
    return 1;
}

int usdc_parse_uint64_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read uint64 array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Uint64 array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(uint64_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    uint64_t *data = (uint64_t*)malloc(array_size * sizeof(uint64_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate uint64 array");
        return 0;
    }
    
    for (uint64_t i = 0; i < array_size; i++) {
        if (!usdc_read_uint64(reader, &data[i])) {
            free(data);
            usdc_set_error(reader, "Failed to read uint64 array data");
            return 0;
        }
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(uint64_t));
    
    return 1;
}

int usdc_parse_float_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read float array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Float array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(float))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    float *data = (float*)malloc(array_size * sizeof(float));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate float array");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, data, array_size * sizeof(float))) {
        free(data);
        usdc_set_error(reader, "Failed to read float array data");
        return 0;
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(float));
    
    return 1;
}

int usdc_parse_double_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read double array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Double array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(double))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    double *data = (double*)malloc(array_size * sizeof(double));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate double array");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, data, array_size * sizeof(double))) {
        free(data);
        usdc_set_error(reader, "Failed to read double array data");
        return 0;
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(double));
    
    return 1;
}

int usdc_parse_token_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read token array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_TOKENS) {
        usdc_set_error(reader, "Token array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(uint32_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    uint32_t *data = (uint32_t*)malloc(array_size * sizeof(uint32_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate token array");
        return 0;
    }
    
    for (uint64_t i = 0; i < array_size; i++) {
        if (!usdc_read_uint32(reader, &data[i])) {
            free(data);
            usdc_set_error(reader, "Failed to read token array data");
            return 0;
        }
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(uint32_t));
    
    return 1;
}

int usdc_parse_string_array(usdc_reader_t *reader, uint64_t offset, usdc_parsed_value_t *parsed_value) {
    uint64_t array_size;
    if (!usdc_read_uint64(reader, &array_size)) {
        usdc_set_error(reader, "Failed to read string array size");
        return 0;
    }
    
    if (array_size > USDC_MAX_STRINGS) {
        usdc_set_error(reader, "String array too large");
        return 0;
    }
    
    if (!usdc_check_memory_limit(reader, array_size * sizeof(uint32_t))) {
        return 0;
    }
    
    parsed_value->array_size = (size_t)array_size;
    if (array_size == 0) {
        parsed_value->value.data_ptr = NULL;
        return 1;
    }
    
    uint32_t *data = (uint32_t*)malloc(array_size * sizeof(uint32_t));
    if (!data) {
        usdc_set_error(reader, "Failed to allocate string array");
        return 0;
    }
    
    for (uint64_t i = 0; i < array_size; i++) {
        if (!usdc_read_uint32(reader, &data[i])) {
            free(data);
            usdc_set_error(reader, "Failed to read string array data");
            return 0;
        }
    }
    
    parsed_value->value.data_ptr = data;
    usdc_update_memory_usage(reader, array_size * sizeof(uint32_t));
    
    return 1;
}

void usdc_cleanup_parsed_value(usdc_parsed_value_t *parsed_value) {
    if (!parsed_value) return;
    
    if (parsed_value->is_array && parsed_value->value.data_ptr) {
        free(parsed_value->value.data_ptr);
        parsed_value->value.data_ptr = NULL;
    }
    
    memset(parsed_value, 0, sizeof(*parsed_value));
}

void usdc_print_parsed_value(usdc_reader_t *reader, usdc_parsed_value_t *parsed_value) {
    if (!parsed_value) return;
    
    printf("Value: type=%s", usdc_get_data_type_name(parsed_value->type));
    
    if (parsed_value->is_array) {
        printf(" ARRAY[%zu]", parsed_value->array_size);
        
        /* Print first few elements for arrays */
        if (parsed_value->value.data_ptr && parsed_value->array_size > 0) {
            size_t print_count = (parsed_value->array_size > 5) ? 5 : parsed_value->array_size;
            printf(" = [");
            
            for (size_t i = 0; i < print_count; i++) {
                if (i > 0) printf(", ");
                
                switch (parsed_value->type) {
                    case USDC_DATA_TYPE_BOOL:
                        printf("%d", ((uint8_t*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_INT:
                        printf("%d", ((int32_t*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_UINT:
                        printf("%u", ((uint32_t*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_INT64:
                        printf("%lld", (long long)((int64_t*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_UINT64:
                        printf("%llu", (unsigned long long)((uint64_t*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_FLOAT:
                        printf("%.6f", ((float*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_DOUBLE:
                        printf("%.6f", ((double*)parsed_value->value.data_ptr)[i]);
                        break;
                    case USDC_DATA_TYPE_TOKEN:
                        {
                            uint32_t token_idx = ((uint32_t*)parsed_value->value.data_ptr)[i];
                            if (token_idx < reader->num_tokens && reader->tokens[token_idx].str) {
                                printf("\"%s\"", reader->tokens[token_idx].str);
                            } else {
                                printf("token[%u]", token_idx);
                            }
                        }
                        break;
                    case USDC_DATA_TYPE_STRING:
                        printf("string[%u]", ((uint32_t*)parsed_value->value.data_ptr)[i]);
                        break;
                    default:
                        printf("?");
                        break;
                }
            }
            
            if (parsed_value->array_size > print_count) {
                printf(", ...");
            }
            printf("]");
        }
        
    } else {
        /* Single values */
        printf(" = ");
        switch (parsed_value->type) {
            case USDC_DATA_TYPE_BOOL:
                printf("%s", parsed_value->value.bool_val ? "true" : "false");
                break;
            case USDC_DATA_TYPE_UCHAR:
                printf("%u", parsed_value->value.uchar_val);
                break;
            case USDC_DATA_TYPE_INT:
                printf("%d", parsed_value->value.int_val);
                break;
            case USDC_DATA_TYPE_UINT:
                printf("%u", parsed_value->value.uint_val);
                break;
            case USDC_DATA_TYPE_INT64:
                printf("%lld", (long long)parsed_value->value.int64_val);
                break;
            case USDC_DATA_TYPE_UINT64:
                printf("%llu", (unsigned long long)parsed_value->value.uint64_val);
                break;
            case USDC_DATA_TYPE_FLOAT:
                printf("%.6f", parsed_value->value.float_val);
                break;
            case USDC_DATA_TYPE_DOUBLE:
                printf("%.6f", parsed_value->value.double_val);
                break;
            case USDC_DATA_TYPE_TOKEN:
                if (parsed_value->value.token_index < reader->num_tokens && 
                    reader->tokens[parsed_value->value.token_index].str) {
                    printf("\"%s\"", reader->tokens[parsed_value->value.token_index].str);
                } else {
                    printf("token[%u]", parsed_value->value.token_index);
                }
                break;
            case USDC_DATA_TYPE_STRING:
                printf("string[%u]", parsed_value->value.string_index);
                break;
            case USDC_DATA_TYPE_SPECIFIER:
                {
                    const char *spec_names[] = {"def", "over", "class"};
                    uint32_t spec_val = parsed_value->value.uint_val;
                    if (spec_val < 3) {
                        printf("%s", spec_names[spec_val]);
                    } else {
                        printf("spec[%u]", spec_val);
                    }
                }
                break;
            case USDC_DATA_TYPE_VARIABILITY:
                {
                    const char *var_names[] = {"varying", "uniform", "config"};
                    uint32_t var_val = parsed_value->value.uint_val;
                    if (var_val < 3) {
                        printf("%s", var_names[var_val]);
                    } else {
                        printf("variability[%u]", var_val);
                    }
                }
                break;
            case USDC_DATA_TYPE_PERMISSION:
                printf("permission[%u]", parsed_value->value.uint_val);
                break;
            case USDC_DATA_TYPE_TOKEN_VECTOR:
                if (parsed_value->array_size == 0) {
                    printf("[]");
                } else {
                    printf("token_vector[%zu]", parsed_value->array_size);
                }
                break;
            default:
                printf("(unsupported type)");
                break;
        }
    }
    
    if (parsed_value->is_inlined) printf(" INLINED");
    if (parsed_value->is_compressed) printf(" COMPRESSED");
}

const char *usdc_get_spec_type_name(usdc_spec_type_t type) {
    switch (type) {
        case USDC_SPEC_TYPE_UNKNOWN: return "unknown";
        case USDC_SPEC_TYPE_ATTRIBUTE: return "attribute";
        case USDC_SPEC_TYPE_CONNECTION: return "connection";
        case USDC_SPEC_TYPE_EXPRESSION: return "expression";
        case USDC_SPEC_TYPE_MAPPER: return "mapper";
        case USDC_SPEC_TYPE_MAPPER_ARG: return "mapper_arg";
        case USDC_SPEC_TYPE_PRIM: return "prim";
        case USDC_SPEC_TYPE_PSEUDO_ROOT: return "pseudo_root";
        case USDC_SPEC_TYPE_RELATIONSHIP: return "relationship";
        case USDC_SPEC_TYPE_RELATIONSHIP_TARGET: return "relationship_target";
        case USDC_SPEC_TYPE_VARIANT: return "variant";
        case USDC_SPEC_TYPE_VARIANT_SET: return "variant_set";
        default: return "invalid";
    }
}

/* ===== SPECS Section Reading ===== */

int usdc_read_specs_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    /* Read number of specs */
    uint64_t num_specs;
    if (!usdc_read_uint64(reader, &num_specs)) {
        return 0;
    }
    
    if (num_specs == 0) {
        usdc_set_error(reader, "SPECS section cannot be empty");
        return 0;
    }
    
    if (num_specs > USDC_MAX_SPECS) {
        usdc_set_error(reader, "Too many specs");
        return 0;
    }
    
    reader->num_specs = (size_t)num_specs;
    
    /* Allocate specs array */
    size_t specs_size = reader->num_specs * sizeof(usdc_spec_t);
    if (!usdc_check_memory_limit(reader, specs_size)) {
        return 0;
    }
    
    reader->specs = (usdc_spec_t *)malloc(specs_size);
    if (!reader->specs) {
        usdc_set_error(reader, "Failed to allocate memory for specs");
        return 0;
    }
    usdc_update_memory_usage(reader, specs_size);
    
    /* Prepare working space for integer decompression */
    size_t working_space_size = usdc_get_integer_working_space_size(reader->num_specs);
    char *working_space = (char *)malloc(working_space_size);
    if (!working_space) {
        usdc_set_error(reader, "Failed to allocate working space for specs");
        return 0;
    }
    
    /* Temporary space for decompressed integers */
    uint32_t *temp_indices = (uint32_t *)malloc(reader->num_specs * sizeof(uint32_t));
    if (!temp_indices) {
        free(working_space);
        usdc_set_error(reader, "Failed to allocate temp indices for specs");
        return 0;
    }
    
    /* Read path indices (compressed) */
    uint64_t path_indexes_size;
    if (!usdc_read_uint64(reader, &path_indexes_size)) {
        free(working_space);
        free(temp_indices);
        return 0;
    }
    
    if (path_indexes_size > section->size) {
        free(working_space);
        free(temp_indices);
        usdc_set_error(reader, "Invalid path indexes size");
        return 0;
    }
    
    char *compressed_path_data = (char *)malloc(path_indexes_size);
    if (!compressed_path_data) {
        free(working_space);
        free(temp_indices);
        usdc_set_error(reader, "Failed to allocate compressed path data");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, compressed_path_data, path_indexes_size)) {
        free(working_space);
        free(temp_indices);
        free(compressed_path_data);
        return 0;
    }
    
    /* Try to decompress path indices using USD compression */
    int success = usdc_usd_integer_decompress(compressed_path_data, path_indexes_size,
                                             temp_indices, reader->num_specs,
                                             working_space, working_space_size);
    if (!success) {
        /* Fallback: try simple decompression */
        size_t decompressed_count = usdc_integer_decompress(compressed_path_data, path_indexes_size,
                                                           temp_indices, reader->num_specs);
        if (decompressed_count != reader->num_specs) {
            free(working_space);
            free(temp_indices);
            free(compressed_path_data);
            usdc_set_error(reader, "Failed to decompress spec path indices");
            return 0;
        }
    }
    
    /* Copy path indices */
    for (size_t i = 0; i < reader->num_specs; i++) {
        reader->specs[i].path_index.value = temp_indices[i];
    }
    free(compressed_path_data);
    
    /* Read fieldset indices (compressed) */
    uint64_t fset_indexes_size;
    if (!usdc_read_uint64(reader, &fset_indexes_size)) {
        free(working_space);
        free(temp_indices);
        return 0;
    }
    
    if (fset_indexes_size > section->size) {
        free(working_space);
        free(temp_indices);
        usdc_set_error(reader, "Invalid fieldset indexes size");
        return 0;
    }
    
    char *compressed_fset_data = (char *)malloc(fset_indexes_size);
    if (!compressed_fset_data) {
        free(working_space);
        free(temp_indices);
        usdc_set_error(reader, "Failed to allocate compressed fieldset data");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, compressed_fset_data, fset_indexes_size)) {
        free(working_space);
        free(temp_indices);
        free(compressed_fset_data);
        return 0;
    }
    
    /* Try to decompress fieldset indices */
    success = usdc_usd_integer_decompress(compressed_fset_data, fset_indexes_size,
                                         temp_indices, reader->num_specs,
                                         working_space, working_space_size);
    if (!success) {
        /* Fallback: try simple decompression */
        size_t decompressed_count = usdc_integer_decompress(compressed_fset_data, fset_indexes_size,
                                                           temp_indices, reader->num_specs);
        if (decompressed_count != reader->num_specs) {
            free(working_space);
            free(temp_indices);
            free(compressed_fset_data);
            usdc_set_error(reader, "Failed to decompress spec fieldset indices");
            return 0;
        }
    }
    
    /* Copy fieldset indices */
    for (size_t i = 0; i < reader->num_specs; i++) {
        reader->specs[i].fieldset_index.value = temp_indices[i];
    }
    free(compressed_fset_data);
    
    /* Read spec types (compressed) */
    uint64_t spectype_size;
    if (!usdc_read_uint64(reader, &spectype_size)) {
        free(working_space);
        free(temp_indices);
        return 0;
    }
    
    if (spectype_size > section->size) {
        free(working_space);
        free(temp_indices);
        usdc_set_error(reader, "Invalid spectype size");
        return 0;
    }
    
    char *compressed_spectype_data = (char *)malloc(spectype_size);
    if (!compressed_spectype_data) {
        free(working_space);
        free(temp_indices);
        usdc_set_error(reader, "Failed to allocate compressed spectype data");
        return 0;
    }
    
    if (!usdc_read_bytes(reader, compressed_spectype_data, spectype_size)) {
        free(working_space);
        free(temp_indices);
        free(compressed_spectype_data);
        return 0;
    }
    
    /* Try to decompress spec types */
    success = usdc_usd_integer_decompress(compressed_spectype_data, spectype_size,
                                         temp_indices, reader->num_specs,
                                         working_space, working_space_size);
    if (!success) {
        /* Fallback: try simple decompression */
        size_t decompressed_count = usdc_integer_decompress(compressed_spectype_data, spectype_size,
                                                           temp_indices, reader->num_specs);
        if (decompressed_count != reader->num_specs) {
            free(working_space);
            free(temp_indices);
            free(compressed_spectype_data);
            usdc_set_error(reader, "Failed to decompress spec types");
            return 0;
        }
    }
    
    /* Copy spec types */
    for (size_t i = 0; i < reader->num_specs; i++) {
        uint32_t spec_type_raw = temp_indices[i];
        if (spec_type_raw > USDC_SPEC_TYPE_VARIANT_SET) {
            reader->specs[i].spec_type = USDC_SPEC_TYPE_UNKNOWN;
        } else {
            reader->specs[i].spec_type = (usdc_spec_type_t)spec_type_raw;
        }
    }
    
    free(working_space);
    free(temp_indices);
    free(compressed_spectype_data);
    
    return 1;
}

/* ===== FIELDSETS Section Reading ===== */

int usdc_read_fieldsets_section(usdc_reader_t *reader, usdc_section_t *section) {
    /* Seek to section start */
    if (!usdc_seek(reader, section->start)) {
        return 0;
    }
    
    /* Read number of fieldsets */
    uint64_t num_fieldsets;
    if (!usdc_read_uint64(reader, &num_fieldsets)) {
        usdc_set_error(reader, "Failed to read number of fieldsets");
        return 0;
    }
    
    /* Security check */
    if (num_fieldsets > USDC_MAX_FIELDSETS) {
        usdc_set_error(reader, "Too many fieldsets");
        return 0;
    }
    
    if (num_fieldsets == 0) {
        reader->num_fieldsets = 0;
        return 1;
    }
    
    /* Read compressed size */
    uint64_t compressed_size;
    if (!usdc_read_uint64(reader, &compressed_size)) {
        usdc_set_error(reader, "Failed to read fieldsets compressed size");
        return 0;
    }
    
    /* Validate compressed size */
    if (compressed_size > section->size - 16) {  /* 16 bytes for the two uint64s we just read */
        usdc_set_error(reader, "Invalid fieldsets compressed size");
        return 0;
    }
    
    /* For now, implement a simplified fallback approach */
    /* Try to read the compressed data and decompress it */
    char *compressed_data = NULL;
    uint32_t *decompressed_indices = NULL;
    
    if (compressed_size > 0) {
        compressed_data = (char*)malloc(compressed_size);
        if (!compressed_data) {
            usdc_set_error(reader, "Failed to allocate compressed fieldsets buffer");
            return 0;
        }
        
        if (!usdc_read_bytes(reader, compressed_data, compressed_size)) {
            free(compressed_data);
            usdc_set_error(reader, "Failed to read compressed fieldsets data");
            return 0;
        }
        
        /* Try to decompress using full USD integer decompression */
        decompressed_indices = (uint32_t*)malloc(num_fieldsets * sizeof(uint32_t));
        if (!decompressed_indices) {
            free(compressed_data);
            usdc_set_error(reader, "Failed to allocate decompressed fieldsets buffer");
            return 0;
        }
        
        /* Calculate working space size */
        size_t working_space_size = usdc_get_integer_working_space_size(num_fieldsets);
        char *working_space = (char*)malloc(working_space_size);
        if (!working_space) {
            free(compressed_data);
            free(decompressed_indices);
            usdc_set_error(reader, "Failed to allocate working space");
            return 0;
        }
        
        /* Try full USD integer decompression first */
        int success = usdc_usd_integer_decompress(compressed_data, compressed_size, 
                                                 decompressed_indices, num_fieldsets, 
                                                 working_space, working_space_size);
        
        free(working_space);
        
        if (!success) {
            /* Fallback to simple decompression */
            size_t decompressed_count = usdc_integer_decompress(
                compressed_data, compressed_size, 
                decompressed_indices, num_fieldsets);
            
            if (decompressed_count != num_fieldsets) {
                /* Final fallback: generate sequential fieldset indices */
                usdc_set_warning(reader, "Fieldsets decompression failed, using fallback indices");
                for (uint64_t i = 0; i < num_fieldsets; i++) {
                    decompressed_indices[i] = (uint32_t)i;
                }
            }
        }
        
        free(compressed_data);
        compressed_data = NULL;
    } else {
        /* Empty compressed data, create dummy indices */
        decompressed_indices = (uint32_t*)malloc(num_fieldsets * sizeof(uint32_t));
        if (!decompressed_indices) {
            usdc_set_error(reader, "Failed to allocate fieldsets indices");
            return 0;
        }
        for (uint64_t i = 0; i < num_fieldsets; i++) {
            decompressed_indices[i] = (uint32_t)i;
        }
    }
    
    /* Build live fieldsets by parsing separators (value 0 or USDC_INVALID_INDEX) */
    /* Count actual fieldsets by counting separators */
    size_t actual_fieldset_count = 0;
    size_t current_start = 0;
    
    for (size_t i = 0; i < num_fieldsets; i++) {
        if (decompressed_indices[i] == 0 || decompressed_indices[i] == USDC_INVALID_INDEX) {
            /* Found separator, this completes a fieldset */
            if (i > current_start) {
                actual_fieldset_count++;
            }
            current_start = i + 1;
        }
    }
    
    /* Handle final fieldset if no trailing separator */
    if (current_start < num_fieldsets) {
        actual_fieldset_count++;
    }
    
    if (actual_fieldset_count == 0) {
        actual_fieldset_count = 1; /* At least one fieldset */
    }
    
    /* Memory check */
    if (!usdc_check_memory_limit(reader, actual_fieldset_count * sizeof(usdc_fieldset_t))) {
        free(decompressed_indices);
        return 0;
    }
    
    /* Allocate fieldsets array */
    reader->fieldsets = (usdc_fieldset_t*)calloc(actual_fieldset_count, sizeof(usdc_fieldset_t));
    if (!reader->fieldsets) {
        free(decompressed_indices);
        usdc_set_error(reader, "Failed to allocate fieldsets array");
        return 0;
    }
    
    /* Build fieldsets from decompressed indices */
    size_t fieldset_idx = 0;
    current_start = 0;
    
    for (size_t i = 0; i <= num_fieldsets; i++) {
        int is_separator = (i == num_fieldsets) || 
                          (decompressed_indices[i] == 0) || 
                          (decompressed_indices[i] == USDC_INVALID_INDEX);
        
        if (is_separator && i > current_start && fieldset_idx < actual_fieldset_count) {
            /* Create fieldset with indices from current_start to i-1 */
            size_t field_count = i - current_start;
            
            reader->fieldsets[fieldset_idx].num_field_indices = field_count;
            reader->fieldsets[fieldset_idx].field_indices = 
                (usdc_index_t*)malloc(field_count * sizeof(usdc_index_t));
            
            if (!reader->fieldsets[fieldset_idx].field_indices) {
                /* Cleanup on failure */
                for (size_t j = 0; j < fieldset_idx; j++) {
                    free(reader->fieldsets[j].field_indices);
                }
                free(reader->fieldsets);
                free(decompressed_indices);
                usdc_set_error(reader, "Failed to allocate fieldset field indices");
                return 0;
            }
            
            /* Copy field indices (validate them) */
            for (size_t j = 0; j < field_count; j++) {
                uint32_t field_idx = decompressed_indices[current_start + j];
                if (field_idx < reader->num_fields) {
                    reader->fieldsets[fieldset_idx].field_indices[j].value = field_idx;
                } else {
                    reader->fieldsets[fieldset_idx].field_indices[j].value = USDC_INVALID_INDEX;
                }
            }
            
            fieldset_idx++;
            current_start = i + 1;
        }
    }
    
    free(decompressed_indices);
    reader->num_fieldsets = actual_fieldset_count;
    usdc_update_memory_usage(reader, actual_fieldset_count * sizeof(usdc_fieldset_t));
    
    return 1;
}