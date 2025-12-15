// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// USDC (Crate) file reader implementation

#include "lightusd/usdc_reader.hh"
#include "lightusd/stream_reader.hh"
#include "lightusd/lz4_compression.hh"
#include "lightusd/integer_coding.hh"
#include "lightusd/stage.hh"
#include "lightusd/prim.hh"
#include "lightusd/debug.hh"

#include <fstream>
#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>

namespace lightusd {
namespace v1 {

// Section names in crate file
constexpr const char* kTokensSection = "TOKENS";
constexpr const char* kStringsSection = "STRINGS";
constexpr const char* kFieldsSection = "FIELDS";
constexpr const char* kFieldSetsSection = "FIELDSETS";
constexpr const char* kPathsSection = "PATHS";
constexpr const char* kSpecsSection = "SPECS";

// =============================================================================
// Fetch-on-Demand State Machine
// =============================================================================

enum class FetchReaderState {
    Initial,        // Not started
    NeedHeader,     // Waiting for header data
    HaveHeader,     // Header parsed, need TOC
    NeedTOC,        // Waiting for TOC data
    HaveTOC,        // TOC parsed, need tokens
    NeedTokens,     // Waiting for tokens data
    HaveTokens,     // Tokens parsed, need strings
    NeedStrings,    // Waiting for strings data
    HaveStrings,    // Strings parsed, need fields
    NeedFields,     // Waiting for fields data
    HaveFields,     // Fields parsed, need fieldsets
    NeedFieldsets,  // Waiting for fieldsets data
    HaveFieldsets,  // Fieldsets parsed, need paths
    NeedPaths,      // Waiting for paths data
    HavePaths,      // Paths parsed, need specs
    NeedSpecs,      // Waiting for specs data
    Complete,       // All sections parsed
    Error           // Error occurred
};

// =============================================================================
// UsdcReader Implementation
// =============================================================================

struct UsdcReader::Impl {
    // Raw data (kept for lazy decoding)
    std::vector<uint8_t> data_storage;
    const uint8_t* data = nullptr;
    size_t data_size = 0;

    // File info
    crate::CrateVersion version;
    crate::TableOfContents toc;
    int64_t toc_offset = 0;  // Stored from header parsing

    // Parsed tables
    std::vector<Token> tokens;
    std::vector<std::string> strings;
    std::vector<crate::Field> fields;
    std::vector<std::vector<crate::FieldIndex>> fieldsets;
    std::vector<Path> paths;
    std::vector<crate::Spec> specs;

    // Options
    UsdcReaderOptions options;

    // Error handling
    std::string error;
    std::vector<std::string> warnings;

    // Fetch-on-demand state
    FetchReaderState fetch_state = FetchReaderState::Initial;
    lightusd_fetch_handler_t fetch_handler = {};
    bool fetch_mode_enabled = false;
    uint64_t next_request_id = 1;
    bool has_pending_request = false;
    uint64_t pending_async_token = 0;
    lightusd_fetch_tag_t pending_tag = LIGHTUSD_FETCH_HEADER;

    // Section info (populated after TOC parse)
    struct SectionInfo {
        int64_t offset = 0;
        int64_t size = 0;
        bool valid = false;
    };
    SectionInfo section_info[LIGHTUSD_FETCH_COUNT];

    void add_warning(const std::string& msg) {
        warnings.push_back(msg);
        if (options.warn_handler) {
            options.warn_handler(msg);
        }
    }

    // Parsing methods
    bool parse_header(StreamReader& sr);
    bool parse_toc(StreamReader& sr);
    bool parse_tokens(StreamReader& sr);
    bool parse_strings(StreamReader& sr);
    bool parse_fields(StreamReader& sr);
    bool parse_fieldsets(StreamReader& sr);
    bool parse_paths(StreamReader& sr);
    bool parse_specs(StreamReader& sr);
    bool create_placeholder_specs();

    // Value decoding
    Result<Value> decode_inlined_value(const crate::ValueRep& rep) const;
    Result<Value> decode_external_value(const crate::ValueRep& rep, StreamReader& sr) const;

    // Path building
    bool build_paths_from_compressed(const std::vector<uint32_t>& path_indexes,
                                      const std::vector<int32_t>& element_token_indexes,
                                      const std::vector<int32_t>& jumps);
    bool build_path_recursive(size_t cur_index,
                               const Path& parent_path,
                               const std::vector<uint32_t>& path_indexes,
                               const std::vector<int32_t>& element_token_indexes,
                               const std::vector<int32_t>& jumps,
                               std::vector<bool>& visited);

    // Fetch-on-demand parsing methods (work with provided buffers)
    bool parse_header_from_buffer(const uint8_t* data, size_t size);
    bool parse_toc_from_buffer(const uint8_t* data, size_t size);
    bool parse_tokens_from_buffer(const uint8_t* data, size_t size);
    bool parse_strings_from_buffer(const uint8_t* data, size_t size);
    bool parse_fields_from_buffer(const uint8_t* data, size_t size);
    bool parse_fieldsets_from_buffer(const uint8_t* data, size_t size);
    bool parse_paths_from_buffer(const uint8_t* data, size_t size);
    bool parse_specs_from_buffer(const uint8_t* data, size_t size);

    // Populate section_info from TOC
    void populate_section_info();

    // Get section info by fetch tag
    const crate::Section* find_section_by_tag(lightusd_fetch_tag_t tag) const;
};

// -----------------------------------------------------------------------------
// Header Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_header(StreamReader& sr) {
    PROFILE_FUNCTION();

    // Check magic (8 bytes)
    char magic[crate::kCrateMagicSize + 1] = {0};
    if (!sr.read(magic, crate::kCrateMagicSize)) {
        error = "Failed to read magic number";
        return false;
    }

    if (std::memcmp(magic, crate::kCrateMagic, crate::kCrateMagicSize) != 0) {
        error = "Invalid USDC magic number";
        return false;
    }

    // Read version block (8 bytes: 3 version bytes + 5 padding)
    uint8_t version_block[8];
    if (!sr.read(version_block, 8)) {
        error = "Failed to read version";
        return false;
    }
    version.major = version_block[0];
    version.minor = version_block[1];
    version.patch = version_block[2];

    DCOUT("USDC version: " << version.to_string());

    // Check minimum version (0.4.0 or later)
    if (version.major == 0 && version.minor < 4) {
        error = "Unsupported USDC version: " + version.to_string() + " (need 0.4.0+)";
        return false;
    }

    // Read TOC offset (8 bytes at position 16)
    int64_t toc_offset;
    if (!sr.read_i64(&toc_offset)) {
        error = "Failed to read TOC offset";
        return false;
    }

    DCOUT("TOC offset: " << toc_offset);

    // TOC offset must be past the header (at least 88 bytes) and within file
    if (toc_offset <= 88 || static_cast<size_t>(toc_offset) >= sr.size()) {
        error = "Invalid TOC offset: " + std::to_string(toc_offset) +
                ", file size: " + std::to_string(sr.size());
        return false;
    }

    // Seek to TOC and parse
    if (!sr.seek(static_cast<size_t>(toc_offset))) {
        error = "Failed to seek to TOC";
        return false;
    }

    return parse_toc(sr);
}

// -----------------------------------------------------------------------------
// TOC Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_toc(StreamReader& sr) {
    PROFILE_FUNCTION();

    // Read number of sections
    uint64_t num_sections;
    if (!sr.read_u64(&num_sections)) {
        error = "Failed to read number of sections";
        return false;
    }

    DCOUT("Number of sections: " << num_sections);

    if (num_sections > 1000) {  // Sanity check
        error = "Too many sections";
        return false;
    }

    toc.sections.resize(static_cast<size_t>(num_sections));

    for (size_t i = 0; i < num_sections; ++i) {
        auto& section = toc.sections[i];

        // Read section name (16 bytes, null-padded)
        if (!sr.read(section.name, crate::kSectionNameMaxLength + 1)) {
            error = "Failed to read section name";
            return false;
        }
        section.name[crate::kSectionNameMaxLength] = '\0';

        // Read start and size
        if (!sr.read_i64(&section.start) || !sr.read_i64(&section.size)) {
            error = "Failed to read section info";
            return false;
        }

        DCOUT("Section: " << section.name << " start=" << section.start << " size=" << section.size);
    }

    return true;
}

// -----------------------------------------------------------------------------
// Token Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_tokens(StreamReader& sr) {
    PROFILE_FUNCTION();

    const auto* section = toc.find_section(kTokensSection);
    if (!section) {
        error = "Missing TOKENS section";
        return false;
    }

    if (!sr.seek(static_cast<size_t>(section->start))) {
        error = "Failed to seek to TOKENS section";
        return false;
    }

    // Read number of tokens
    uint64_t num_tokens;
    if (!sr.read_u64(&num_tokens)) {
        error = "Failed to read token count";
        return false;
    }

    DCOUT("Number of tokens: " << num_tokens);

    if (num_tokens == 0) {
        return true;
    }

    // USDC 0.4.0+ uses LZ4 compression for tokens
    // Format: num_tokens (8) + uncompressedSize (8) + compressedSize (8) + compressed data

    uint64_t uncompressed_size;
    if (!sr.read_u64(&uncompressed_size)) {
        error = "Failed to read token uncompressed size";
        return false;
    }

    DCOUT("Tokens uncompressed size: " << uncompressed_size);

    // Minimum: ';-)' (3 bytes) + null terminators for each token
    if ((3 + num_tokens) > uncompressed_size) {
        error = "TOKENS section corrupted (uncompressed size too small)";
        return false;
    }

    uint64_t compressed_size;
    if (!sr.read_u64(&compressed_size)) {
        error = "Failed to read token compressed size";
        return false;
    }

    DCOUT("Tokens compressed size: " << compressed_size);

    if (compressed_size < 1) {
        error = "Invalid compressed size";
        return false;
    }

    // Read compressed data
    std::vector<char> compressed(static_cast<size_t>(compressed_size));
    if (!sr.read(compressed.data(), compressed.size())) {
        error = "Failed to read compressed token data";
        return false;
    }

    // Decompress
    // Add extra safety margin for LZ4 decompression
    std::vector<char> decompressed(static_cast<size_t>(uncompressed_size) + 128);
    std::string lz4_err;
    size_t decompressed_size = LZ4Compression::DecompressFromBuffer(
        compressed.data(), decompressed.data(),
        compressed.size(), uncompressed_size, &lz4_err);

    if (decompressed_size == 0) {
        error = "Failed to decompress tokens: " + lz4_err;
        return false;
    }

    if (decompressed_size != uncompressed_size) {
        error = "Token decompression size mismatch";
        return false;
    }

    // Parse null-terminated strings from decompressed data
    // First 3 bytes should be ';-)'
    if (decompressed_size >= 3 &&
        decompressed[0] == ';' && decompressed[1] == '-' && decompressed[2] == ')') {
        // Skip the marker
        const char* ptr = decompressed.data() + 3;
        const char* end = decompressed.data() + decompressed_size;

        tokens.reserve(static_cast<size_t>(num_tokens));

        while (tokens.size() < num_tokens && ptr < end) {
            // Find null terminator
            const char* str_end = ptr;
            while (str_end < end && *str_end != '\0') {
                ++str_end;
            }

            tokens.emplace_back(std::string(ptr, str_end - ptr));

            // Move past null terminator
            ptr = str_end + 1;
        }
    } else {
        // No marker - treat entire buffer as null-terminated strings
        const char* ptr = decompressed.data();
        const char* end = decompressed.data() + decompressed_size;

        tokens.reserve(static_cast<size_t>(num_tokens));

        while (tokens.size() < num_tokens && ptr < end) {
            const char* str_end = ptr;
            while (str_end < end && *str_end != '\0') {
                ++str_end;
            }

            tokens.emplace_back(std::string(ptr, str_end - ptr));
            ptr = str_end + 1;
        }
    }

    DCOUT("Parsed " << tokens.size() << " tokens");

    if (tokens.size() != num_tokens) {
        add_warning("Expected " + std::to_string(num_tokens) +
                   " tokens but found " + std::to_string(tokens.size()));
    }

    return true;
}

// -----------------------------------------------------------------------------
// String Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_strings(StreamReader& sr) {
    PROFILE_FUNCTION();

    const auto* section = toc.find_section(kStringsSection);
    if (!section) {
        // Strings section is optional
        DCOUT("No STRINGS section");
        return true;
    }

    if (!sr.seek(static_cast<size_t>(section->start))) {
        error = "Failed to seek to STRINGS section";
        return false;
    }

    // First, read indices into token table
    uint64_t num_strings;
    if (!sr.read_u64(&num_strings)) {
        error = "Failed to read string count";
        return false;
    }

    DCOUT("Number of strings: " << num_strings);

    strings.reserve(static_cast<size_t>(num_strings));

    // Strings are stored as indices into the token table
    for (size_t i = 0; i < num_strings; ++i) {
        uint32_t token_idx;
        if (!sr.read_u32(&token_idx)) {
            error = "Failed to read string index " + std::to_string(i);
            return false;
        }

        if (token_idx < tokens.size()) {
            strings.push_back(tokens[token_idx].str());
        } else {
            strings.push_back("");
            add_warning("Invalid string index: " + std::to_string(token_idx));
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Field Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_fields(StreamReader& sr) {
    PROFILE_FUNCTION();

    const auto* section = toc.find_section(kFieldsSection);
    if (!section) {
        error = "Missing FIELDS section";
        return false;
    }

    if (!sr.seek(static_cast<size_t>(section->start))) {
        error = "Failed to seek to FIELDS section";
        return false;
    }

    // Read number of fields
    uint64_t num_fields;
    if (!sr.read_u64(&num_fields)) {
        error = "Failed to read field count";
        return false;
    }

    DCOUT("Number of fields: " << num_fields);

    fields.resize(static_cast<size_t>(num_fields));

    for (size_t i = 0; i < num_fields; ++i) {
        auto& field = fields[i];

        // Read token index
        uint32_t token_idx;
        if (!sr.read_u32(&token_idx)) {
            error = "Failed to read field token index";
            return false;
        }
        field.token_index = crate::TokenIndex(token_idx);

        // Read value rep
        uint64_t rep_data;
        if (!sr.read_u64(&rep_data)) {
            error = "Failed to read field value rep";
            return false;
        }
        field.value_rep = crate::ValueRep(rep_data);
    }

    return true;
}

// -----------------------------------------------------------------------------
// FieldSet Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_fieldsets(StreamReader& sr) {
    PROFILE_FUNCTION();

    const auto* section = toc.find_section(kFieldSetsSection);
    if (!section) {
        error = "Missing FIELDSETS section";
        return false;
    }

    if (!sr.seek(static_cast<size_t>(section->start))) {
        error = "Failed to seek to FIELDSETS section";
        return false;
    }

    // Read total count of field indices (including separators)
    uint64_t num_indices;
    if (!sr.read_u64(&num_indices)) {
        error = "Failed to read fieldset count";
        return false;
    }

    DCOUT("Number of fieldset indices: " << num_indices);

    if (num_indices == 0) {
        return true;
    }

    // USDC 0.4.0+ uses compressed field indices
    // Format: numIndices (uint64) + compressedSize (uint64) + compressed data

    uint64_t comp_size;
    if (!sr.read_u64(&comp_size)) {
        error = "Failed to read fieldsets compressed size";
        return false;
    }

    DCOUT("Fieldsets compressed size: " << comp_size);

    // Allocate buffers for decompression
    size_t comp_buffer_size = IntegerCompression::GetCompressedBufferSize(
        static_cast<size_t>(num_indices));
    size_t workspace_size = IntegerCompression::GetDecompressionWorkingSpaceSize(
        static_cast<size_t>(num_indices));

    std::vector<char> comp_buffer(comp_buffer_size);
    std::vector<char> workspace(workspace_size);
    std::vector<uint32_t> all_indices(static_cast<size_t>(num_indices));

    if (comp_size > comp_buffer_size) {
        comp_size = comp_buffer_size;  // Safety cap
    }

    if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
        error = "Failed to read compressed fieldset data";
        return false;
    }

    std::string decomp_err;
    size_t result = IntegerCompression::DecompressFromBuffer(
        comp_buffer.data(), static_cast<size_t>(comp_size),
        all_indices.data(), all_indices.size(),
        &decomp_err, workspace.data());

    if (result == 0) {
        error = "Failed to decompress fieldsets: " + decomp_err;
        return false;
    }

    // Build fieldsets by splitting at ~0u (Index()) separators
    // The array contains field indices separated by ~0u between different fieldsets
    std::vector<crate::FieldIndex> current_set;

    for (uint32_t idx : all_indices) {
        if (idx == ~0u) {
            // End of current fieldset
            fieldsets.push_back(std::move(current_set));
            current_set.clear();
        } else {
            current_set.push_back(crate::FieldIndex(idx));
        }
    }

    // Don't forget last set if not terminated
    if (!current_set.empty()) {
        fieldsets.push_back(std::move(current_set));
    }

    DCOUT("Parsed " << fieldsets.size() << " fieldsets");

    return true;
}

// -----------------------------------------------------------------------------
// Path Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_paths(StreamReader& sr) {
    PROFILE_FUNCTION();

    const auto* section = toc.find_section(kPathsSection);
    if (!section) {
        error = "Missing PATHS section";
        return false;
    }

    if (!sr.seek(static_cast<size_t>(section->start))) {
        error = "Failed to seek to PATHS section";
        return false;
    }

    // Read number of paths (this is maxNumPaths)
    uint64_t num_paths;
    if (!sr.read_u64(&num_paths)) {
        error = "Failed to read path count";
        return false;
    }

    DCOUT("Number of paths: " << num_paths);

    if (num_paths == 0) {
        // Empty stage - just add root path
        paths.push_back(Path("/"));
        return true;
    }

    // Reserve space
    paths.resize(static_cast<size_t>(num_paths));

    // USDC 0.4.0+ uses compressed paths
    // Format: numEncodedPaths + compressed arrays (pathIndexes, elementTokenIndexes, jumps)

    uint64_t num_encoded_paths;
    if (!sr.read_u64(&num_encoded_paths)) {
        error = "Failed to read encoded path count";
        return false;
    }

    DCOUT("Number of encoded paths: " << num_encoded_paths);

    if (num_encoded_paths > num_paths) {
        error = "Encoded path count exceeds max path count";
        return false;
    }

    // Allocate buffers for decompression
    std::vector<uint32_t> path_indexes(static_cast<size_t>(num_encoded_paths));
    std::vector<int32_t> element_token_indexes(static_cast<size_t>(num_encoded_paths));
    std::vector<int32_t> jumps(static_cast<size_t>(num_encoded_paths));

    size_t comp_buffer_size = IntegerCompression::GetCompressedBufferSize(
        static_cast<size_t>(num_encoded_paths));
    size_t workspace_size = IntegerCompression::GetDecompressionWorkingSpaceSize(
        static_cast<size_t>(num_encoded_paths));

    std::vector<char> comp_buffer(comp_buffer_size);
    std::vector<char> workspace(workspace_size);

    // Read and decompress pathIndexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read pathIndexes compressed size";
            return false;
        }

        DCOUT("PathIndexes compressed size: " << comp_size);

        if (comp_size > comp_buffer_size) {
            error = "PathIndexes compressed size too large";
            return false;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed pathIndexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            path_indexes.data(), path_indexes.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress pathIndexes: " + decomp_err;
            return false;
        }
    }

    // Read and decompress elementTokenIndexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read elementTokenIndexes compressed size";
            return false;
        }

        DCOUT("ElementTokenIndexes compressed size: " << comp_size);

        if (comp_size > comp_buffer_size) {
            error = "ElementTokenIndexes compressed size too large";
            return false;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed elementTokenIndexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            element_token_indexes.data(), element_token_indexes.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress elementTokenIndexes: " + decomp_err;
            return false;
        }
    }

    // Read and decompress jumps
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read jumps compressed size";
            return false;
        }

        DCOUT("Jumps compressed size: " << comp_size);

        if (comp_size > comp_buffer_size) {
            error = "Jumps compressed size too large";
            return false;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed jumps";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            jumps.data(), jumps.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress jumps: " + decomp_err;
            return false;
        }
    }

    // Build paths from decompressed arrays
    // Algorithm: Use recursive DFS with jumps for sibling navigation
    // - pathIndexes[i] = index where to store the path
    // - elementTokenIndexes[i] = token index (negative = property path)
    // - jumps[i] = offset to next sibling (0 = no sibling, -1 = end)

    if (!build_paths_from_compressed(path_indexes, element_token_indexes, jumps)) {
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Path Building
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::build_paths_from_compressed(
    const std::vector<uint32_t>& path_indexes,
    const std::vector<int32_t>& element_token_indexes,
    const std::vector<int32_t>& jumps)
{
    if (path_indexes.empty()) {
        // Just set root path
        if (!paths.empty()) {
            paths[0] = Path("/");
        }
        return true;
    }

    // Track visited path indices to prevent infinite loops
    std::vector<bool> visited(paths.size(), false);

    // Start recursive build from index 0 with empty parent (root will be set)
    if (!build_path_recursive(0, Path(), path_indexes, element_token_indexes, jumps, visited)) {
        return false;
    }

    return true;
}

bool UsdcReader::Impl::build_path_recursive(
    size_t cur_index,
    const Path& parent_path,
    const std::vector<uint32_t>& path_indexes,
    const std::vector<int32_t>& element_token_indexes,
    const std::vector<int32_t>& jumps,
    std::vector<bool>& visited)
{
    Path current_parent = parent_path;
    bool has_child = false, has_sibling = false;

    do {
        size_t this_index = cur_index++;

        if (this_index >= path_indexes.size()) {
            error = "Index exceeds pathIndexes size";
            return false;
        }

        size_t path_idx = path_indexes[this_index];
        if (path_idx >= paths.size()) {
            error = "Path index out of range";
            return false;
        }

        // Check for circular reference
        if (path_idx < visited.size() && visited[path_idx]) {
            add_warning("Circular path reference at index " + std::to_string(path_idx));
            return true;
        }

        if (current_parent.full_path().empty()) {
            // Root node
            paths[path_idx] = Path("/");
            visited[path_idx] = true;
            current_parent = Path("/");
            DCOUT("Path[" << path_idx << "] = / (root)");
        } else {
            // Non-root node
            if (this_index >= element_token_indexes.size()) {
                error = "Index exceeds elementTokenIndexes size";
                return false;
            }

            int32_t token_idx_signed = element_token_indexes[this_index];
            bool is_property = token_idx_signed < 0;
            uint32_t token_idx = static_cast<uint32_t>(is_property ? -token_idx_signed : token_idx_signed);

            if (token_idx >= tokens.size()) {
                error = "Invalid token index: " + std::to_string(token_idx);
                return false;
            }

            const std::string& elem_name = tokens[token_idx].str();

            // Build path
            if (is_property) {
                paths[path_idx] = Path(current_parent.full_path() + "." + elem_name);
            } else {
                if (current_parent.full_path() == "/") {
                    paths[path_idx] = Path("/" + elem_name);
                } else {
                    paths[path_idx] = Path(current_parent.full_path() + "/" + elem_name);
                }
            }
            visited[path_idx] = true;

            DCOUT("Path[" << path_idx << "] = " << paths[path_idx].full_path());
        }

        // Determine if we have child and/or sibling
        // jump == -1: has child only (leaf)
        // jump == 0: has sibling only (continue in loop)
        // jump > 0: has both child and sibling at (this_index + jump)
        if (this_index >= jumps.size()) {
            error = "Index exceeds jumps size";
            return false;
        }

        int32_t jump = jumps[this_index];
        has_child = (jump > 0) || (jump == -1);
        has_sibling = (jump >= 0);

        DCOUT("  jump=" << jump << " hasChild=" << has_child << " hasSibling=" << has_sibling);

        if (has_child) {
            if (has_sibling) {
                // Process sibling recursively (sibling uses same parent)
                size_t sibling_index = this_index + static_cast<size_t>(jump);
                if (!build_path_recursive(sibling_index, current_parent,
                                          path_indexes, element_token_indexes, jumps, visited)) {
                    return false;
                }
            }

            // Update parent to current path for child processing
            current_parent = paths[path_idx];
        }
        // If we only have sibling, continue loop (parent unchanged, next item is sibling)

    } while (has_child || has_sibling);

    return true;
}

// -----------------------------------------------------------------------------
// Spec Parsing
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_specs(StreamReader& sr) {
    PROFILE_FUNCTION();

    const auto* section = toc.find_section(kSpecsSection);
    if (!section) {
        error = "Missing SPECS section";
        return false;
    }

    if (!sr.seek(static_cast<size_t>(section->start))) {
        error = "Failed to seek to SPECS section";
        return false;
    }

    // Read number of specs
    uint64_t num_specs;
    if (!sr.read_u64(&num_specs)) {
        error = "Failed to read spec count";
        return false;
    }

    DCOUT("Number of specs: " << num_specs);

    if (num_specs == 0) {
        add_warning("Empty SPECS section");
        return true;
    }

    // Reserve space
    specs.resize(static_cast<size_t>(num_specs));

    // Allocate buffers for decompression
    size_t comp_buffer_size = IntegerCompression::GetCompressedBufferSize(
        static_cast<size_t>(num_specs));
    size_t workspace_size = IntegerCompression::GetDecompressionWorkingSpaceSize(
        static_cast<size_t>(num_specs));

    std::vector<char> comp_buffer(comp_buffer_size);
    std::vector<char> workspace(workspace_size);
    std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));

    // Read and decompress path indexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read path indexes size";
            return false;
        }

        DCOUT("Path indexes compressed size: " << comp_size);

        if (comp_size > comp_buffer_size) {
            comp_size = comp_buffer_size;  // Safety cap
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed path indexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            tmp.data(), tmp.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress path indexes: " + decomp_err;
            return false;
        }

        for (size_t i = 0; i < num_specs; ++i) {
            specs[i].path_index = crate::PathIndex(tmp[i]);
        }
    }

    // Read and decompress fieldset indexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read fieldset indexes size";
            return false;
        }

        DCOUT("Fieldset indexes compressed size: " << comp_size);

        if (comp_size > comp_buffer_size) {
            comp_size = comp_buffer_size;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed fieldset indexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            tmp.data(), tmp.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress fieldset indexes: " + decomp_err;
            return false;
        }

        for (size_t i = 0; i < num_specs; ++i) {
            specs[i].fieldset_index = crate::FieldSetIndex(tmp[i]);
        }
    }

    // Read and decompress spec types
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read spec types size";
            return false;
        }

        DCOUT("Spec types compressed size: " << comp_size);

        if (comp_size > comp_buffer_size) {
            comp_size = comp_buffer_size;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed spec types";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            tmp.data(), tmp.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress spec types: " + decomp_err;
            return false;
        }

        for (size_t i = 0; i < num_specs; ++i) {
            specs[i].spec_type = static_cast<crate::SpecType>(tmp[i]);
            DCOUT("Spec[" << i << "] path=" << specs[i].path_index.value
                  << " fieldset=" << specs[i].fieldset_index.value
                  << " type=" << static_cast<int>(specs[i].spec_type));
        }
    }

    return true;
}

bool UsdcReader::Impl::create_placeholder_specs() {
    // Create minimal placeholder specs based on paths
    // This allows the reader to still provide path/token information
    specs.clear();

    for (size_t i = 0; i < paths.size(); ++i) {
        crate::Spec spec;
        spec.path_index = crate::PathIndex(static_cast<uint32_t>(i));
        spec.fieldset_index = crate::FieldSetIndex(0);

        // Try to guess spec type from path
        const std::string& path_str = paths[i].full_path();
        if (path_str == "/") {
            spec.spec_type = crate::SpecType::PseudoRoot;
        } else if (path_str.find('.') != std::string::npos) {
            // Contains property separator
            spec.spec_type = crate::SpecType::Attribute;
        } else {
            spec.spec_type = crate::SpecType::Prim;
        }

        specs.push_back(spec);
    }

    return true;
}

// -----------------------------------------------------------------------------
// Fetch-on-Demand Parsing Methods
// -----------------------------------------------------------------------------

bool UsdcReader::Impl::parse_header_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    // Header is 24 bytes: magic(8) + version(8) + toc_offset(8)
    if (size < 24) {
        error = "Header data too small (need 24 bytes)";
        return false;
    }

    StreamReader sr(buf, size, false);

    // Check magic (8 bytes)
    char magic[crate::kCrateMagicSize + 1] = {0};
    if (!sr.read(magic, crate::kCrateMagicSize)) {
        error = "Failed to read magic number";
        return false;
    }

    if (std::memcmp(magic, crate::kCrateMagic, crate::kCrateMagicSize) != 0) {
        error = "Invalid USDC magic number";
        return false;
    }

    // Read version block (8 bytes: 3 version bytes + 5 padding)
    uint8_t version_block[8];
    if (!sr.read(version_block, 8)) {
        error = "Failed to read version";
        return false;
    }
    version.major = version_block[0];
    version.minor = version_block[1];
    version.patch = version_block[2];

    DCOUT("USDC version: " << version.to_string());

    // Check minimum version (0.4.0 or later)
    if (version.major == 0 && version.minor < 4) {
        error = "Unsupported USDC version: " + version.to_string() + " (need 0.4.0+)";
        return false;
    }

    // Read TOC offset (8 bytes at position 16)
    if (!sr.read_i64(&toc_offset)) {
        error = "Failed to read TOC offset";
        return false;
    }

    DCOUT("TOC offset: " << toc_offset);

    // TOC offset must be past the header (at least 88 bytes)
    if (toc_offset <= 88) {
        error = "Invalid TOC offset: " + std::to_string(toc_offset);
        return false;
    }

    return true;
}

bool UsdcReader::Impl::parse_toc_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    if (size < 8) {
        error = "TOC data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    // Read number of sections
    uint64_t num_sections;
    if (!sr.read_u64(&num_sections)) {
        error = "Failed to read number of sections";
        return false;
    }

    DCOUT("Number of sections: " << num_sections);

    if (num_sections > 1000) {  // Sanity check
        error = "Too many sections";
        return false;
    }

    // Each section entry is 32 bytes: name(16) + start(8) + size(8)
    size_t expected_size = 8 + num_sections * 32;
    if (size < expected_size) {
        error = "TOC data incomplete";
        return false;
    }

    toc.sections.resize(static_cast<size_t>(num_sections));

    for (size_t i = 0; i < num_sections; ++i) {
        auto& section = toc.sections[i];

        // Read section name (16 bytes, null-padded)
        if (!sr.read(section.name, crate::kSectionNameMaxLength + 1)) {
            error = "Failed to read section name";
            return false;
        }
        section.name[crate::kSectionNameMaxLength] = '\0';

        // Read start and size
        if (!sr.read_i64(&section.start) || !sr.read_i64(&section.size)) {
            error = "Failed to read section info";
            return false;
        }

        DCOUT("Section: " << section.name << " start=" << section.start << " size=" << section.size);
    }

    // Populate section_info for fetch-on-demand
    populate_section_info();

    return true;
}

void UsdcReader::Impl::populate_section_info() {
    // Initialize all sections as invalid
    for (int i = 0; i < LIGHTUSD_FETCH_COUNT; ++i) {
        section_info[i].valid = false;
    }

    // Header is always at offset 0, size 24
    section_info[LIGHTUSD_FETCH_HEADER].offset = 0;
    section_info[LIGHTUSD_FETCH_HEADER].size = 24;
    section_info[LIGHTUSD_FETCH_HEADER].valid = true;

    // TOC info - we know offset from header, calculate size
    // TOC size = 8 (num_sections) + num_sections * 32 (each section entry)
    section_info[LIGHTUSD_FETCH_TOC].offset = toc_offset;
    section_info[LIGHTUSD_FETCH_TOC].size = 8 + toc.sections.size() * 32;
    section_info[LIGHTUSD_FETCH_TOC].valid = true;

    // Build a list of section starts sorted by offset for calculating actual sizes
    // The TOC section sizes are sometimes smaller than actual data needed
    // (the regular reader just reads past section boundaries)
    std::vector<int64_t> section_starts;
    for (const auto& section : toc.sections) {
        section_starts.push_back(section.start);
    }
    section_starts.push_back(toc_offset);  // TOC is the end marker
    std::sort(section_starts.begin(), section_starts.end());

    // Helper to find actual section size
    // IMPORTANT: USDC section sizes in TOC are not reliable - sections may read
    // past their declared boundaries (e.g., FIELDS reads into FIELDSETS area).
    // For safety, we fetch from section start to TOC start for these sections.
    auto find_actual_size = [&](const char* name, int64_t start, int64_t declared_size) -> int64_t {
        // For FIELDS, FIELDSETS, PATHS, SPECS - these sections may read past boundaries
        // Fetch all data from section start up to TOC
        if (std::strcmp(name, kFieldsSection) == 0 ||
            std::strcmp(name, kFieldSetsSection) == 0 ||
            std::strcmp(name, kPathsSection) == 0 ||
            std::strcmp(name, kSpecsSection) == 0) {
            return toc_offset - start;
        }
        // For other sections, use declared size
        return declared_size;
    };

    // Map section names to fetch tags with actual sizes
    for (const auto& section : toc.sections) {
        int64_t actual_size = find_actual_size(section.name, section.start, section.size);

        if (std::strcmp(section.name, kTokensSection) == 0) {
            section_info[LIGHTUSD_FETCH_TOKENS].offset = section.start;
            section_info[LIGHTUSD_FETCH_TOKENS].size = actual_size;
            section_info[LIGHTUSD_FETCH_TOKENS].valid = true;
        } else if (std::strcmp(section.name, kStringsSection) == 0) {
            section_info[LIGHTUSD_FETCH_STRINGS].offset = section.start;
            section_info[LIGHTUSD_FETCH_STRINGS].size = actual_size;
            section_info[LIGHTUSD_FETCH_STRINGS].valid = true;
        } else if (std::strcmp(section.name, kFieldsSection) == 0) {
            section_info[LIGHTUSD_FETCH_FIELDS].offset = section.start;
            section_info[LIGHTUSD_FETCH_FIELDS].size = actual_size;
            section_info[LIGHTUSD_FETCH_FIELDS].valid = true;
        } else if (std::strcmp(section.name, kFieldSetsSection) == 0) {
            section_info[LIGHTUSD_FETCH_FIELDSETS].offset = section.start;
            section_info[LIGHTUSD_FETCH_FIELDSETS].size = actual_size;
            section_info[LIGHTUSD_FETCH_FIELDSETS].valid = true;
        } else if (std::strcmp(section.name, kPathsSection) == 0) {
            section_info[LIGHTUSD_FETCH_PATHS].offset = section.start;
            section_info[LIGHTUSD_FETCH_PATHS].size = actual_size;
            section_info[LIGHTUSD_FETCH_PATHS].valid = true;
        } else if (std::strcmp(section.name, kSpecsSection) == 0) {
            section_info[LIGHTUSD_FETCH_SPECS].offset = section.start;
            section_info[LIGHTUSD_FETCH_SPECS].size = actual_size;
            section_info[LIGHTUSD_FETCH_SPECS].valid = true;
        }
    }
}

const crate::Section* UsdcReader::Impl::find_section_by_tag(lightusd_fetch_tag_t tag) const {
    const char* name = nullptr;
    switch (tag) {
        case LIGHTUSD_FETCH_TOKENS:    name = kTokensSection; break;
        case LIGHTUSD_FETCH_STRINGS:   name = kStringsSection; break;
        case LIGHTUSD_FETCH_FIELDS:    name = kFieldsSection; break;
        case LIGHTUSD_FETCH_FIELDSETS: name = kFieldSetsSection; break;
        case LIGHTUSD_FETCH_PATHS:     name = kPathsSection; break;
        case LIGHTUSD_FETCH_SPECS:     name = kSpecsSection; break;
        default: return nullptr;
    }
    return toc.find_section(name);
}

bool UsdcReader::Impl::parse_tokens_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    if (size < 8) {
        error = "Tokens data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    // Read number of tokens
    uint64_t num_tokens;
    if (!sr.read_u64(&num_tokens)) {
        error = "Failed to read token count";
        return false;
    }

    DCOUT("Number of tokens: " << num_tokens);

    if (num_tokens == 0) {
        return true;
    }

    // USDC 0.4.0+ uses LZ4 compression for tokens
    uint64_t uncompressed_size;
    if (!sr.read_u64(&uncompressed_size)) {
        error = "Failed to read token uncompressed size";
        return false;
    }

    if ((3 + num_tokens) > uncompressed_size) {
        error = "TOKENS section corrupted (uncompressed size too small)";
        return false;
    }

    uint64_t compressed_size;
    if (!sr.read_u64(&compressed_size)) {
        error = "Failed to read token compressed size";
        return false;
    }

    if (compressed_size < 1) {
        error = "Invalid compressed size";
        return false;
    }

    // Read compressed data
    std::vector<char> compressed(static_cast<size_t>(compressed_size));
    if (!sr.read(compressed.data(), compressed.size())) {
        error = "Failed to read compressed token data";
        return false;
    }

    // Decompress
    std::vector<char> decompressed(static_cast<size_t>(uncompressed_size) + 128);
    std::string lz4_err;
    size_t decompressed_size = LZ4Compression::DecompressFromBuffer(
        compressed.data(), decompressed.data(),
        compressed.size(), uncompressed_size, &lz4_err);

    if (decompressed_size == 0) {
        error = "Failed to decompress tokens: " + lz4_err;
        return false;
    }

    if (decompressed_size != uncompressed_size) {
        error = "Token decompression size mismatch";
        return false;
    }

    // Parse null-terminated strings from decompressed data
    if (decompressed_size >= 3 &&
        decompressed[0] == ';' && decompressed[1] == '-' && decompressed[2] == ')') {
        const char* ptr = decompressed.data() + 3;
        const char* end = decompressed.data() + decompressed_size;

        tokens.reserve(static_cast<size_t>(num_tokens));

        while (tokens.size() < num_tokens && ptr < end) {
            const char* str_end = ptr;
            while (str_end < end && *str_end != '\0') {
                ++str_end;
            }
            tokens.emplace_back(std::string(ptr, str_end - ptr));
            ptr = str_end + 1;
        }
    } else {
        const char* ptr = decompressed.data();
        const char* end = decompressed.data() + decompressed_size;

        tokens.reserve(static_cast<size_t>(num_tokens));

        while (tokens.size() < num_tokens && ptr < end) {
            const char* str_end = ptr;
            while (str_end < end && *str_end != '\0') {
                ++str_end;
            }
            tokens.emplace_back(std::string(ptr, str_end - ptr));
            ptr = str_end + 1;
        }
    }

    DCOUT("Parsed " << tokens.size() << " tokens");

    if (tokens.size() != num_tokens) {
        add_warning("Expected " + std::to_string(num_tokens) +
                   " tokens but found " + std::to_string(tokens.size()));
    }

    return true;
}

bool UsdcReader::Impl::parse_strings_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    // Strings section is optional
    if (buf == nullptr || size == 0) {
        DCOUT("No STRINGS section data");
        return true;
    }

    if (size < 8) {
        error = "Strings data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    uint64_t num_strings;
    if (!sr.read_u64(&num_strings)) {
        error = "Failed to read string count";
        return false;
    }

    DCOUT("Number of strings: " << num_strings);

    strings.reserve(static_cast<size_t>(num_strings));

    for (size_t i = 0; i < num_strings; ++i) {
        uint32_t token_idx;
        if (!sr.read_u32(&token_idx)) {
            error = "Failed to read string index " + std::to_string(i);
            return false;
        }

        if (token_idx < tokens.size()) {
            strings.push_back(tokens[token_idx].str());
        } else {
            strings.push_back("");
            add_warning("Invalid string index: " + std::to_string(token_idx));
        }
    }

    return true;
}

bool UsdcReader::Impl::parse_fields_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    if (size < 8) {
        error = "Fields data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    uint64_t num_fields;
    if (!sr.read_u64(&num_fields)) {
        error = "Failed to read field count";
        return false;
    }

    DCOUT("Number of fields: " << num_fields);

    fields.resize(static_cast<size_t>(num_fields));

    for (size_t i = 0; i < num_fields; ++i) {
        auto& field = fields[i];

        uint32_t token_idx;
        if (!sr.read_u32(&token_idx)) {
            error = "Failed to read field token index";
            return false;
        }
        field.token_index = crate::TokenIndex(token_idx);

        uint64_t rep_data;
        if (!sr.read_u64(&rep_data)) {
            error = "Failed to read field value rep";
            return false;
        }
        field.value_rep = crate::ValueRep(rep_data);
    }

    return true;
}

bool UsdcReader::Impl::parse_fieldsets_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    if (size < 8) {
        error = "Fieldsets data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    uint64_t num_indices;
    if (!sr.read_u64(&num_indices)) {
        error = "Failed to read fieldset count";
        return false;
    }

    DCOUT("Number of fieldset indices: " << num_indices);

    if (num_indices == 0) {
        return true;
    }

    uint64_t comp_size;
    if (!sr.read_u64(&comp_size)) {
        error = "Failed to read fieldsets compressed size";
        return false;
    }

    DCOUT("Fieldsets compressed size: " << comp_size);

    size_t comp_buffer_size = IntegerCompression::GetCompressedBufferSize(
        static_cast<size_t>(num_indices));
    size_t workspace_size = IntegerCompression::GetDecompressionWorkingSpaceSize(
        static_cast<size_t>(num_indices));

    std::vector<char> comp_buffer(comp_buffer_size);
    std::vector<char> workspace(workspace_size);
    std::vector<uint32_t> all_indices(static_cast<size_t>(num_indices));

    if (comp_size > comp_buffer_size) {
        comp_size = comp_buffer_size;
    }

    if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
        error = "Failed to read compressed fieldset data";
        return false;
    }

    std::string decomp_err;
    size_t result = IntegerCompression::DecompressFromBuffer(
        comp_buffer.data(), static_cast<size_t>(comp_size),
        all_indices.data(), all_indices.size(),
        &decomp_err, workspace.data());

    if (result == 0) {
        error = "Failed to decompress fieldsets: " + decomp_err;
        return false;
    }

    // Build fieldsets by splitting at ~0u separators
    std::vector<crate::FieldIndex> current_set;

    for (uint32_t idx : all_indices) {
        if (idx == ~0u) {
            fieldsets.push_back(std::move(current_set));
            current_set.clear();
        } else {
            current_set.push_back(crate::FieldIndex(idx));
        }
    }

    if (!current_set.empty()) {
        fieldsets.push_back(std::move(current_set));
    }

    DCOUT("Parsed " << fieldsets.size() << " fieldsets");

    return true;
}

bool UsdcReader::Impl::parse_paths_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    if (size < 8) {
        error = "Paths data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    uint64_t num_paths;
    if (!sr.read_u64(&num_paths)) {
        error = "Failed to read path count";
        return false;
    }

    DCOUT("Number of paths: " << num_paths);

    if (num_paths == 0) {
        paths.push_back(Path("/"));
        return true;
    }

    paths.resize(static_cast<size_t>(num_paths));

    uint64_t num_encoded_paths;
    if (!sr.read_u64(&num_encoded_paths)) {
        error = "Failed to read encoded path count";
        return false;
    }

    DCOUT("Number of encoded paths: " << num_encoded_paths);

    if (num_encoded_paths > num_paths) {
        error = "Encoded path count exceeds max path count";
        return false;
    }

    std::vector<uint32_t> path_indexes(static_cast<size_t>(num_encoded_paths));
    std::vector<int32_t> element_token_indexes(static_cast<size_t>(num_encoded_paths));
    std::vector<int32_t> jumps(static_cast<size_t>(num_encoded_paths));

    size_t comp_buffer_size = IntegerCompression::GetCompressedBufferSize(
        static_cast<size_t>(num_encoded_paths));
    size_t workspace_size = IntegerCompression::GetDecompressionWorkingSpaceSize(
        static_cast<size_t>(num_encoded_paths));

    std::vector<char> comp_buffer(comp_buffer_size);
    std::vector<char> workspace(workspace_size);

    // Read and decompress pathIndexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read pathIndexes compressed size";
            return false;
        }

        if (comp_size > comp_buffer_size) {
            error = "PathIndexes compressed size too large";
            return false;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed pathIndexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            path_indexes.data(), path_indexes.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress pathIndexes: " + decomp_err;
            return false;
        }
    }

    // Read and decompress elementTokenIndexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read elementTokenIndexes compressed size";
            return false;
        }

        if (comp_size > comp_buffer_size) {
            error = "ElementTokenIndexes compressed size too large";
            return false;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed elementTokenIndexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            element_token_indexes.data(), element_token_indexes.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress elementTokenIndexes: " + decomp_err;
            return false;
        }
    }

    // Read and decompress jumps
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read jumps compressed size";
            return false;
        }

        if (comp_size > comp_buffer_size) {
            error = "Jumps compressed size too large";
            return false;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed jumps";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            jumps.data(), jumps.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress jumps: " + decomp_err;
            return false;
        }
    }

    // Build paths from decompressed arrays
    if (!build_paths_from_compressed(path_indexes, element_token_indexes, jumps)) {
        return false;
    }

    return true;
}

bool UsdcReader::Impl::parse_specs_from_buffer(const uint8_t* buf, size_t size) {
    PROFILE_FUNCTION();

    if (size < 8) {
        error = "Specs data too small";
        return false;
    }

    StreamReader sr(buf, size, false);

    uint64_t num_specs;
    if (!sr.read_u64(&num_specs)) {
        error = "Failed to read spec count";
        return false;
    }

    DCOUT("Number of specs: " << num_specs);

    if (num_specs == 0) {
        add_warning("Empty SPECS section");
        return true;
    }

    specs.resize(static_cast<size_t>(num_specs));

    size_t comp_buffer_size = IntegerCompression::GetCompressedBufferSize(
        static_cast<size_t>(num_specs));
    size_t workspace_size = IntegerCompression::GetDecompressionWorkingSpaceSize(
        static_cast<size_t>(num_specs));

    std::vector<char> comp_buffer(comp_buffer_size);
    std::vector<char> workspace(workspace_size);
    std::vector<uint32_t> tmp(static_cast<size_t>(num_specs));

    // Read and decompress path indexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read path indexes size";
            return false;
        }

        if (comp_size > comp_buffer_size) {
            comp_size = comp_buffer_size;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed path indexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            tmp.data(), tmp.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress path indexes: " + decomp_err;
            return false;
        }

        for (size_t i = 0; i < num_specs; ++i) {
            specs[i].path_index = crate::PathIndex(tmp[i]);
        }
    }

    // Read and decompress fieldset indexes
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read fieldset indexes size";
            return false;
        }

        if (comp_size > comp_buffer_size) {
            comp_size = comp_buffer_size;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed fieldset indexes";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            tmp.data(), tmp.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress fieldset indexes: " + decomp_err;
            return false;
        }

        for (size_t i = 0; i < num_specs; ++i) {
            specs[i].fieldset_index = crate::FieldSetIndex(tmp[i]);
        }
    }

    // Read and decompress spec types
    {
        uint64_t comp_size;
        if (!sr.read_u64(&comp_size)) {
            error = "Failed to read spec types size";
            return false;
        }

        if (comp_size > comp_buffer_size) {
            comp_size = comp_buffer_size;
        }

        if (!sr.read(comp_buffer.data(), static_cast<size_t>(comp_size))) {
            error = "Failed to read compressed spec types";
            return false;
        }

        std::string decomp_err;
        size_t result = IntegerCompression::DecompressFromBuffer(
            comp_buffer.data(), static_cast<size_t>(comp_size),
            tmp.data(), tmp.size(),
            &decomp_err, workspace.data());

        if (result == 0) {
            error = "Failed to decompress spec types: " + decomp_err;
            return false;
        }

        for (size_t i = 0; i < num_specs; ++i) {
            specs[i].spec_type = static_cast<crate::SpecType>(tmp[i]);
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Value Decoding
// -----------------------------------------------------------------------------

Result<Value> UsdcReader::Impl::decode_inlined_value(const crate::ValueRep& rep) const {
    using namespace crate;

    uint64_t payload = rep.payload();
    CrateDataTypeId tid = rep.type_id();

    switch (tid) {
        case CrateDataTypeId::Bool:
            return Value::from_bool(payload != 0);

        case CrateDataTypeId::Int:
            return Value::from_int32(static_cast<int32_t>(payload));

        case CrateDataTypeId::UInt:
            return Value::from_uint32(static_cast<uint32_t>(payload));

        case CrateDataTypeId::Float: {
            uint32_t bits = static_cast<uint32_t>(payload);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return Value::from_float(f);
        }

        case CrateDataTypeId::Double: {
            double d;
            std::memcpy(&d, &payload, sizeof(d));
            return Value::from_double(d);
        }

        case CrateDataTypeId::Token:
            if (payload < tokens.size()) {
                return Value::from_string(tokens[payload].str());
            }
            return make_error("Invalid token index");

        case CrateDataTypeId::String:
            if (payload < strings.size()) {
                return Value::from_string(strings[payload]);
            }
            return make_error("Invalid string index");

        case CrateDataTypeId::Specifier:
            return Value::from_int32(static_cast<int32_t>(payload));

        case CrateDataTypeId::Variability:
            return Value::from_int32(static_cast<int32_t>(payload));

        case CrateDataTypeId::Permission:
            return Value::from_int32(static_cast<int32_t>(payload));

        default:
            return make_error("Unsupported inlined type: " +
                             std::string(crate_data_type_name(tid)));
    }
}

Result<Value> UsdcReader::Impl::decode_external_value(const crate::ValueRep& rep,
                                                       StreamReader& sr) const {
    using namespace crate;

    uint64_t offset = rep.payload();
    CrateDataTypeId tid = rep.type_id();

    if (!sr.seek(static_cast<size_t>(offset))) {
        return make_error("Invalid value offset");
    }

    // Handle arrays
    if (rep.is_array()) {
        // Read array size
        uint64_t array_size;
        if (!sr.read_u64(&array_size)) {
            return make_error("Failed to read array size");
        }

        // For now, handle basic numeric arrays
        switch (tid) {
            case CrateDataTypeId::Float: {
                std::vector<float> arr(static_cast<size_t>(array_size));
                for (size_t i = 0; i < array_size; ++i) {
                    if (!sr.read_f32(&arr[i])) {
                        return make_error("Failed to read float array element");
                    }
                }
                return Value::from_float_array(arr.data(), arr.size());
            }

            case CrateDataTypeId::Int: {
                std::vector<int32_t> arr(static_cast<size_t>(array_size));
                for (size_t i = 0; i < array_size; ++i) {
                    if (!sr.read_i32(&arr[i])) {
                        return make_error("Failed to read int array element");
                    }
                }
                return Value::from_int32_array(arr.data(), arr.size());
            }

            case CrateDataTypeId::Double: {
                std::vector<double> arr(static_cast<size_t>(array_size));
                for (size_t i = 0; i < array_size; ++i) {
                    if (!sr.read_f64(&arr[i])) {
                        return make_error("Failed to read double array element");
                    }
                }
                return Value::from_double_array(arr.data(), arr.size());
            }

            default:
                return make_error("Unsupported array type: " +
                                 std::string(crate_data_type_name(tid)));
        }
    }

    // Non-array external values
    switch (tid) {
        case CrateDataTypeId::String: {
            std::string str;
            if (!sr.read_length_string(&str)) {
                return make_error("Failed to read string");
            }
            return Value::from_string(str);
        }

        case CrateDataTypeId::Token: {
            uint32_t token_idx;
            if (!sr.read_u32(&token_idx)) {
                return make_error("Failed to read token index");
            }
            if (token_idx < tokens.size()) {
                return Value::from_string(tokens[token_idx].str());
            }
            return make_error("Invalid token index");
        }

        case CrateDataTypeId::AssetPath: {
            std::string path_str;
            if (!sr.read_length_string(&path_str)) {
                return make_error("Failed to read asset path");
            }
            return Value::from_asset_path("@" + path_str + "@");
        }

        default:
            return make_error("Unsupported external value type: " +
                             std::string(crate_data_type_name(tid)));
    }
}

// =============================================================================
// UsdcReader Public Interface
// =============================================================================

UsdcReader::UsdcReader() : impl_(new Impl()) {}
UsdcReader::~UsdcReader() = default;
UsdcReader::UsdcReader(UsdcReader&&) noexcept = default;
UsdcReader& UsdcReader::operator=(UsdcReader&&) noexcept = default;

Result<void> UsdcReader::read(const uint8_t* data, size_t size,
                               const UsdcReaderOptions& options) {
    PROFILE_FUNCTION();

    impl_->data = data;
    impl_->data_size = size;
    impl_->options = options;
    impl_->error.clear();
    impl_->warnings.clear();

    if (options.max_file_size > 0 && size > options.max_file_size) {
        impl_->error = "File size exceeds limit";
        return make_error(impl_->error);
    }

    // Create stream reader (little-endian, no swap on x86)
    StreamReader sr(data, size, false);

    // Parse header and TOC
    if (!impl_->parse_header(sr)) {
        return make_error(impl_->error);
    }

    // Parse sections
    if (!impl_->parse_tokens(sr)) {
        return make_error(impl_->error);
    }

    if (!impl_->parse_strings(sr)) {
        return make_error(impl_->error);
    }

    if (!impl_->parse_fields(sr)) {
        return make_error(impl_->error);
    }

    if (!impl_->parse_fieldsets(sr)) {
        return make_error(impl_->error);
    }

    if (!impl_->parse_paths(sr)) {
        return make_error(impl_->error);
    }

    if (!impl_->parse_specs(sr)) {
        return make_error(impl_->error);
    }

    return {};
}

Result<void> UsdcReader::read_file(const std::string& filepath,
                                    const UsdcReaderOptions& options) {
    PROFILE_FUNCTION();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        return make_error("Failed to open file: " + filepath);
    }

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    impl_->data_storage.resize(size);
    if (!file.read(reinterpret_cast<char*>(impl_->data_storage.data()), size)) {
        return make_error("Failed to read file: " + filepath);
    }

    return read(impl_->data_storage.data(), size, options);
}

bool UsdcReader::is_usdc(const uint8_t* data, size_t size) {
    if (size < crate::kCrateMagicSize) {
        return false;
    }
    return std::memcmp(data, crate::kCrateMagic, crate::kCrateMagicSize) == 0;
}

crate::CrateVersion UsdcReader::version() const {
    return impl_->version;
}

size_t UsdcReader::token_count() const { return impl_->tokens.size(); }
size_t UsdcReader::string_count() const { return impl_->strings.size(); }
size_t UsdcReader::path_count() const { return impl_->paths.size(); }
size_t UsdcReader::spec_count() const { return impl_->specs.size(); }

Result<Token> UsdcReader::get_token(crate::TokenIndex index) const {
    if (index.value >= impl_->tokens.size()) {
        return make_error("Invalid token index");
    }
    return impl_->tokens[index.value];
}

Result<std::string> UsdcReader::get_string(crate::StringIndex index) const {
    if (index.value >= impl_->strings.size()) {
        return make_error("Invalid string index");
    }
    return impl_->strings[index.value];
}

Result<Path> UsdcReader::get_path(crate::PathIndex index) const {
    if (index.value >= impl_->paths.size()) {
        return make_error("Invalid path index");
    }
    return impl_->paths[index.value];
}

std::vector<Path> UsdcReader::get_all_paths() const {
    return impl_->paths;
}

Result<crate::Spec> UsdcReader::get_spec(size_t index) const {
    if (index >= impl_->specs.size()) {
        return make_error("Invalid spec index");
    }
    return impl_->specs[index];
}

Result<std::vector<crate::Field>> UsdcReader::get_spec_fields(size_t spec_index) const {
    if (spec_index >= impl_->specs.size()) {
        return make_error("Invalid spec index");
    }

    const auto& spec = impl_->specs[spec_index];
    if (spec.fieldset_index.value >= impl_->fieldsets.size()) {
        return make_error("Invalid fieldset index");
    }

    const auto& fieldset = impl_->fieldsets[spec.fieldset_index.value];
    std::vector<crate::Field> result;
    result.reserve(fieldset.size());

    for (const auto& field_idx : fieldset) {
        if (field_idx.value < impl_->fields.size()) {
            result.push_back(impl_->fields[field_idx.value]);
        }
    }

    return result;
}

crate::LazyValue UsdcReader::get_lazy_value(const crate::Field& field) const {
    return crate::LazyValue(field.value_rep);
}

Result<Value> UsdcReader::decode_value(const crate::LazyValue& lazy) const {
    return decode_value_rep(lazy.rep());
}

Result<Value> UsdcReader::decode_value_rep(const crate::ValueRep& rep) const {
    PROFILE_FUNCTION();

    if (rep.is_inlined()) {
        return impl_->decode_inlined_value(rep);
    } else {
        StreamReader sr(impl_->data, impl_->data_size, false);
        return impl_->decode_external_value(rep, sr);
    }
}

Result<Stage> UsdcReader::reconstruct_stage() const {
    PROFILE_FUNCTION();

    Stage stage;

    // Build path-to-spec mapping
    std::unordered_map<std::string, size_t> path_to_spec;
    for (size_t i = 0; i < impl_->specs.size(); ++i) {
        const auto& spec = impl_->specs[i];
        if (spec.path_index.value < impl_->paths.size()) {
            path_to_spec[impl_->paths[spec.path_index.value].full_path()] = i;
        }
    }

    // Process specs to create prims
    for (size_t i = 0; i < impl_->specs.size(); ++i) {
        const auto& spec = impl_->specs[i];

        // Only create prims for Prim specs
        if (spec.spec_type != crate::SpecType::Prim &&
            spec.spec_type != crate::SpecType::PseudoRoot) {
            continue;
        }

        if (spec.path_index.value >= impl_->paths.size()) {
            continue;
        }

        const Path& prim_path = impl_->paths[spec.path_index.value];
        std::string path_str = prim_path.full_path();

        // Skip root for now
        if (path_str == "/") {
            continue;
        }

        // Get fields for this spec
        auto fields_result = get_spec_fields(i);
        if (!fields_result) {
            continue;
        }

        // Create prim
        Prim prim;
        prim.set_name(prim_path.element_name());
        // Note: Path is set automatically when added to stage hierarchy

        // Process fields
        for (const auto& field : fields_result.value()) {
            if (field.token_index.value >= impl_->tokens.size()) {
                continue;
            }

            const Token& field_name = impl_->tokens[field.token_index.value];
            std::string name = field_name.str();

            // Handle known metadata fields
            if (name == "typeName") {
                auto val = decode_value_rep(field.value_rep);
                if (val) {
                    const std::string* str = val.value().as_string();
                    if (str) {
                        prim.set_type_name(*str);
                    }
                }
            } else if (name == "specifier") {
                // Skip - handled by spec_type
            } else if (name == "kind") {
                auto val = decode_value_rep(field.value_rep);
                if (val) {
                    const std::string* str = val.value().as_string();
                    if (str) {
                        prim.set_kind(*str);
                    }
                }
            }
            // Other fields would be attributes/properties - store as lazy
        }

        // Add prim to stage (only root-level prims for now)
        // Check if this is a direct child of root
        if (prim_path.prim_part().find('/', 1) == std::string::npos) {
            stage.add_root_prim(std::move(prim));
        }
    }

    return stage;
}

const std::string& UsdcReader::error() const {
    return impl_->error;
}

const std::vector<std::string>& UsdcReader::warnings() const {
    return impl_->warnings;
}

// =============================================================================
// Fetch-on-Demand Public Interface
// =============================================================================

void UsdcReader::set_fetch_handler(const lightusd_fetch_handler_t& handler) {
    impl_->fetch_handler = handler;
    impl_->fetch_mode_enabled = (handler.fetch_callback != nullptr);
    impl_->fetch_state = FetchReaderState::Initial;
    impl_->has_pending_request = false;
}

bool UsdcReader::is_fetch_mode() const {
    return impl_->fetch_mode_enabled;
}

lightusd_fetch_tag_t UsdcReader::step(lightusd_fetch_request_t* out_request) {
    if (!impl_->fetch_mode_enabled) {
        impl_->error = "Fetch mode not enabled";
        return LIGHTUSD_FETCH_COUNT;
    }

    if (out_request) {
        lightusd_fetch_request_init(out_request);
        out_request->request_id = impl_->next_request_id++;
    }

    switch (impl_->fetch_state) {
        case FetchReaderState::Initial:
        case FetchReaderState::NeedHeader:
            // Need to fetch header (24 bytes at offset 0)
            impl_->fetch_state = FetchReaderState::NeedHeader;
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_HEADER;
                out_request->offset = 0;
                out_request->size = 24;
            }
            return LIGHTUSD_FETCH_HEADER;

        case FetchReaderState::HaveHeader:
        case FetchReaderState::NeedTOC: {
            // Need to fetch TOC (variable size at toc_offset)
            impl_->fetch_state = FetchReaderState::NeedTOC;
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_TOC;
                out_request->offset = static_cast<uint64_t>(impl_->toc_offset);
                // We don't know TOC size yet, request a reasonable amount
                // Caller may need to provide partial data first
                out_request->size = 8 + 6 * 32;  // Assume up to 6 sections initially
            }
            return LIGHTUSD_FETCH_TOC;
        }

        case FetchReaderState::HaveTOC:
        case FetchReaderState::NeedTokens: {
            impl_->fetch_state = FetchReaderState::NeedTokens;
            if (!impl_->section_info[LIGHTUSD_FETCH_TOKENS].valid) {
                impl_->error = "TOKENS section not found in TOC";
                impl_->fetch_state = FetchReaderState::Error;
                return LIGHTUSD_FETCH_COUNT;
            }
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_TOKENS;
                out_request->offset = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_TOKENS].offset);
                out_request->size = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_TOKENS].size);
            }
            return LIGHTUSD_FETCH_TOKENS;
        }

        case FetchReaderState::HaveTokens:
        case FetchReaderState::NeedStrings: {
            impl_->fetch_state = FetchReaderState::NeedStrings;
            // Strings section is optional
            if (!impl_->section_info[LIGHTUSD_FETCH_STRINGS].valid) {
                // Skip to fields
                impl_->fetch_state = FetchReaderState::HaveStrings;
                return step(out_request);
            }
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_STRINGS;
                out_request->offset = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_STRINGS].offset);
                out_request->size = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_STRINGS].size);
            }
            return LIGHTUSD_FETCH_STRINGS;
        }

        case FetchReaderState::HaveStrings:
        case FetchReaderState::NeedFields: {
            impl_->fetch_state = FetchReaderState::NeedFields;
            if (!impl_->section_info[LIGHTUSD_FETCH_FIELDS].valid) {
                impl_->error = "FIELDS section not found in TOC";
                impl_->fetch_state = FetchReaderState::Error;
                return LIGHTUSD_FETCH_COUNT;
            }
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_FIELDS;
                out_request->offset = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_FIELDS].offset);
                out_request->size = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_FIELDS].size);
            }
            return LIGHTUSD_FETCH_FIELDS;
        }

        case FetchReaderState::HaveFields:
        case FetchReaderState::NeedFieldsets: {
            impl_->fetch_state = FetchReaderState::NeedFieldsets;
            if (!impl_->section_info[LIGHTUSD_FETCH_FIELDSETS].valid) {
                impl_->error = "FIELDSETS section not found in TOC";
                impl_->fetch_state = FetchReaderState::Error;
                return LIGHTUSD_FETCH_COUNT;
            }
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_FIELDSETS;
                out_request->offset = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_FIELDSETS].offset);
                out_request->size = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_FIELDSETS].size);
            }
            return LIGHTUSD_FETCH_FIELDSETS;
        }

        case FetchReaderState::HaveFieldsets:
        case FetchReaderState::NeedPaths: {
            impl_->fetch_state = FetchReaderState::NeedPaths;
            if (!impl_->section_info[LIGHTUSD_FETCH_PATHS].valid) {
                impl_->error = "PATHS section not found in TOC";
                impl_->fetch_state = FetchReaderState::Error;
                return LIGHTUSD_FETCH_COUNT;
            }
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_PATHS;
                out_request->offset = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_PATHS].offset);
                out_request->size = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_PATHS].size);
            }
            return LIGHTUSD_FETCH_PATHS;
        }

        case FetchReaderState::HavePaths:
        case FetchReaderState::NeedSpecs: {
            impl_->fetch_state = FetchReaderState::NeedSpecs;
            if (!impl_->section_info[LIGHTUSD_FETCH_SPECS].valid) {
                impl_->error = "SPECS section not found in TOC";
                impl_->fetch_state = FetchReaderState::Error;
                return LIGHTUSD_FETCH_COUNT;
            }
            if (out_request) {
                out_request->tag = LIGHTUSD_FETCH_SPECS;
                out_request->offset = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_SPECS].offset);
                out_request->size = static_cast<uint64_t>(impl_->section_info[LIGHTUSD_FETCH_SPECS].size);
            }
            return LIGHTUSD_FETCH_SPECS;
        }

        case FetchReaderState::Complete:
            // All done
            return LIGHTUSD_FETCH_COUNT;

        case FetchReaderState::Error:
            return LIGHTUSD_FETCH_COUNT;
    }

    return LIGHTUSD_FETCH_COUNT;
}

Result<void> UsdcReader::provide_data(lightusd_fetch_tag_t tag,
                                       const uint8_t* data, size_t size) {
    if (!impl_->fetch_mode_enabled) {
        return make_error("Fetch mode not enabled");
    }

    bool success = false;

    switch (tag) {
        case LIGHTUSD_FETCH_HEADER:
            if (impl_->fetch_state != FetchReaderState::NeedHeader) {
                return make_error("Unexpected HEADER data");
            }
            success = impl_->parse_header_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HaveHeader;
            }
            break;

        case LIGHTUSD_FETCH_TOC:
            if (impl_->fetch_state != FetchReaderState::NeedTOC) {
                return make_error("Unexpected TOC data");
            }
            success = impl_->parse_toc_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HaveTOC;
            }
            break;

        case LIGHTUSD_FETCH_TOKENS:
            if (impl_->fetch_state != FetchReaderState::NeedTokens) {
                return make_error("Unexpected TOKENS data");
            }
            success = impl_->parse_tokens_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HaveTokens;
            }
            break;

        case LIGHTUSD_FETCH_STRINGS:
            if (impl_->fetch_state != FetchReaderState::NeedStrings) {
                return make_error("Unexpected STRINGS data");
            }
            success = impl_->parse_strings_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HaveStrings;
            }
            break;

        case LIGHTUSD_FETCH_FIELDS:
            if (impl_->fetch_state != FetchReaderState::NeedFields) {
                return make_error("Unexpected FIELDS data");
            }
            success = impl_->parse_fields_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HaveFields;
            }
            break;

        case LIGHTUSD_FETCH_FIELDSETS:
            if (impl_->fetch_state != FetchReaderState::NeedFieldsets) {
                return make_error("Unexpected FIELDSETS data");
            }
            success = impl_->parse_fieldsets_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HaveFieldsets;
            }
            break;

        case LIGHTUSD_FETCH_PATHS:
            if (impl_->fetch_state != FetchReaderState::NeedPaths) {
                return make_error("Unexpected PATHS data");
            }
            success = impl_->parse_paths_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::HavePaths;
            }
            break;

        case LIGHTUSD_FETCH_SPECS:
            if (impl_->fetch_state != FetchReaderState::NeedSpecs) {
                return make_error("Unexpected SPECS data");
            }
            success = impl_->parse_specs_from_buffer(data, size);
            if (success) {
                impl_->fetch_state = FetchReaderState::Complete;
            }
            break;

        default:
            return make_error("Unknown fetch tag");
    }

    if (!success) {
        impl_->fetch_state = FetchReaderState::Error;
        return make_error(impl_->error);
    }

    return {};
}

Result<void> UsdcReader::complete_async(uint64_t async_token,
                                         lightusd_fetch_status_t status,
                                         const uint8_t* data, size_t size,
                                         const char* error_message) {
    if (!impl_->has_pending_request) {
        return make_error("No pending async request");
    }

    if (async_token != impl_->pending_async_token) {
        return make_error("Async token mismatch");
    }

    impl_->has_pending_request = false;

    if (status == LIGHTUSD_FETCH_ERROR) {
        impl_->fetch_state = FetchReaderState::Error;
        impl_->error = error_message ? error_message : "Async fetch failed";
        return make_error(impl_->error);
    }

    if (status == LIGHTUSD_FETCH_PENDING) {
        // Still pending - shouldn't happen
        return make_error("Cannot complete with PENDING status");
    }

    // Process the data
    return provide_data(impl_->pending_tag, data, size);
}

bool UsdcReader::is_pending() const {
    return impl_->has_pending_request;
}

lightusd_fetch_tag_t UsdcReader::current_fetch_state() const {
    switch (impl_->fetch_state) {
        case FetchReaderState::Initial:
        case FetchReaderState::NeedHeader:
            return LIGHTUSD_FETCH_HEADER;
        case FetchReaderState::HaveHeader:
        case FetchReaderState::NeedTOC:
            return LIGHTUSD_FETCH_TOC;
        case FetchReaderState::HaveTOC:
        case FetchReaderState::NeedTokens:
            return LIGHTUSD_FETCH_TOKENS;
        case FetchReaderState::HaveTokens:
        case FetchReaderState::NeedStrings:
            return LIGHTUSD_FETCH_STRINGS;
        case FetchReaderState::HaveStrings:
        case FetchReaderState::NeedFields:
            return LIGHTUSD_FETCH_FIELDS;
        case FetchReaderState::HaveFields:
        case FetchReaderState::NeedFieldsets:
            return LIGHTUSD_FETCH_FIELDSETS;
        case FetchReaderState::HaveFieldsets:
        case FetchReaderState::NeedPaths:
            return LIGHTUSD_FETCH_PATHS;
        case FetchReaderState::HavePaths:
        case FetchReaderState::NeedSpecs:
            return LIGHTUSD_FETCH_SPECS;
        case FetchReaderState::Complete:
        case FetchReaderState::Error:
            return LIGHTUSD_FETCH_COUNT;
    }
    return LIGHTUSD_FETCH_COUNT;
}

Result<void> UsdcReader::read_with_fetch(const UsdcReaderOptions& options) {
    PROFILE_FUNCTION();

    if (!impl_->fetch_mode_enabled) {
        return make_error("Fetch mode not enabled - call set_fetch_handler first");
    }

    if (!impl_->fetch_handler.fetch_callback) {
        return make_error("No fetch callback set");
    }

    impl_->options = options;
    impl_->error.clear();
    impl_->warnings.clear();
    impl_->fetch_state = FetchReaderState::Initial;

    lightusd_fetch_request_t request;
    lightusd_fetch_response_t response;

    while (true) {
        lightusd_fetch_request_init(&request);
        lightusd_fetch_response_init(&response);

        lightusd_fetch_tag_t tag = step(&request);

        if (tag == LIGHTUSD_FETCH_COUNT) {
            // Done or error
            if (impl_->fetch_state == FetchReaderState::Error) {
                return make_error(impl_->error);
            }
            break;
        }

        DCOUT("Fetch request: " << lightusd_fetch_tag_name(tag)
              << " offset=" << request.offset << " size=" << request.size);

        // Check max section size limit
        if (impl_->fetch_handler.max_section_size > 0 &&
            request.size > impl_->fetch_handler.max_section_size) {
            impl_->error = "Section size exceeds max_section_size limit";
            impl_->fetch_state = FetchReaderState::Error;
            return make_error(impl_->error);
        }

        // Call the fetch callback
        impl_->fetch_handler.fetch_callback(&request, &response,
                                            impl_->fetch_handler.userdata);

        if (response.status == LIGHTUSD_FETCH_PENDING) {
            // Async mode - store state and return
            impl_->has_pending_request = true;
            impl_->pending_async_token = response.async_token;
            impl_->pending_tag = tag;
            return make_error("PENDING");  // Special return for async
        }

        if (response.status == LIGHTUSD_FETCH_ERROR) {
            impl_->fetch_state = FetchReaderState::Error;
            impl_->error = response.error_message ? response.error_message : "Fetch failed";
            return make_error(impl_->error);
        }

        // Provide the data
        auto result = provide_data(tag, response.data, response.data_size);
        if (!result) {
            return result;
        }
    }

    return {};
}

// =============================================================================
// Free Functions
// =============================================================================

bool is_usdc_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    uint8_t magic[crate::kCrateMagicSize];
    if (!file.read(reinterpret_cast<char*>(magic), crate::kCrateMagicSize)) {
        return false;
    }

    return UsdcReader::is_usdc(magic, crate::kCrateMagicSize);
}

Result<Stage> load_usdc(const std::string& filepath, const UsdcReaderOptions& options) {
    UsdcReader reader;
    auto result = reader.read_file(filepath, options);
    if (!result) {
        return make_error(reader.error());
    }
    return reader.reconstruct_stage();
}

Result<Stage> load_usdc(const uint8_t* data, size_t size, const UsdcReaderOptions& options) {
    UsdcReader reader;
    auto result = reader.read(data, size, options);
    if (!result) {
        return make_error(reader.error());
    }
    return reader.reconstruct_stage();
}

} // namespace v1
} // namespace lightusd
