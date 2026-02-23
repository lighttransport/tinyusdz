// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDC fetch-on-demand API (C-compatible)
//
// This header provides a C-compatible callback API for fetch-on-demand
// reading of USDC files. Enables progressive loading for web/WASM
// environments and large production USD scenes.

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Fetch Tags - Identify which section is being requested
// ============================================================================

typedef enum {
    LIGHTUSD_FETCH_HEADER    = 0,  // 24 bytes at offset 0 (magic + version + TOC offset)
    LIGHTUSD_FETCH_TOC       = 1,  // Table of Contents (section offsets/sizes)
    LIGHTUSD_FETCH_TOKENS    = 2,  // Token strings (LZ4 compressed)
    LIGHTUSD_FETCH_STRINGS   = 3,  // String indices
    LIGHTUSD_FETCH_FIELDS    = 4,  // Field definitions
    LIGHTUSD_FETCH_FIELDSETS = 5,  // Field set groupings (integer compressed)
    LIGHTUSD_FETCH_PATHS     = 6,  // Path table (integer compressed)
    LIGHTUSD_FETCH_SPECS     = 7,  // Spec definitions (integer compressed)
    LIGHTUSD_FETCH_COUNT     = 8   // Sentinel - indicates completion
} lightusd_fetch_tag_t;

// ============================================================================
// Fetch Status - Return status from callback (async-ready)
// ============================================================================

typedef enum {
    LIGHTUSD_FETCH_OK      = 0,   // Data available in response
    LIGHTUSD_FETCH_PENDING = 1,   // Async operation in progress
    LIGHTUSD_FETCH_ERROR   = -1   // Error occurred
} lightusd_fetch_status_t;

// ============================================================================
// Fetch Request - Describes what data is needed
// ============================================================================

typedef struct {
    lightusd_fetch_tag_t tag;     // Which section is requested
    uint64_t offset;              // File offset to read from
    uint64_t size;                // Number of bytes to read
    uint64_t request_id;          // Unique ID for this request (for async correlation)
} lightusd_fetch_request_t;

// ============================================================================
// Fetch Response - Data returned from callback
// ============================================================================

typedef struct {
    lightusd_fetch_status_t status;  // Result status
    const uint8_t* data;             // Pointer to data (caller-owned buffer)
    uint64_t data_size;              // Actual size of data returned
    const char* error_message;       // Error description (if status == ERROR)
    uint64_t async_token;            // Token for completing async requests
} lightusd_fetch_response_t;

// ============================================================================
// Fetch Callback - User-provided function to fetch data
// ============================================================================

// Callback function type
// - request: Describes what data is needed
// - response: Fill in with fetched data or error
// - userdata: User-provided context pointer
typedef void (*lightusd_fetch_callback_t)(
    const lightusd_fetch_request_t* request,
    lightusd_fetch_response_t* response,
    void* userdata
);

// ============================================================================
// Fetch Handler - Configuration for fetch-on-demand reading
// ============================================================================

typedef struct {
    lightusd_fetch_callback_t fetch_callback;  // Required: callback function
    void* userdata;                            // User context passed to callback
    uint64_t max_section_size;                 // 0 = no limit, else max bytes per section
} lightusd_fetch_handler_t;

// ============================================================================
// Helper Functions
// ============================================================================

// Get human-readable name for fetch tag
static inline const char* lightusd_fetch_tag_name(lightusd_fetch_tag_t tag) {
    switch (tag) {
        case LIGHTUSD_FETCH_HEADER:    return "HEADER";
        case LIGHTUSD_FETCH_TOC:       return "TOC";
        case LIGHTUSD_FETCH_TOKENS:    return "TOKENS";
        case LIGHTUSD_FETCH_STRINGS:   return "STRINGS";
        case LIGHTUSD_FETCH_FIELDS:    return "FIELDS";
        case LIGHTUSD_FETCH_FIELDSETS: return "FIELDSETS";
        case LIGHTUSD_FETCH_PATHS:     return "PATHS";
        case LIGHTUSD_FETCH_SPECS:     return "SPECS";
        case LIGHTUSD_FETCH_COUNT:     return "COMPLETE";
        default:                       return "UNKNOWN";
    }
}

// Get human-readable name for fetch status
static inline const char* lightusd_fetch_status_name(lightusd_fetch_status_t status) {
    switch (status) {
        case LIGHTUSD_FETCH_OK:      return "OK";
        case LIGHTUSD_FETCH_PENDING: return "PENDING";
        case LIGHTUSD_FETCH_ERROR:   return "ERROR";
        default:                     return "UNKNOWN";
    }
}

// Initialize request with defaults
static inline void lightusd_fetch_request_init(lightusd_fetch_request_t* req) {
    req->tag = LIGHTUSD_FETCH_HEADER;
    req->offset = 0;
    req->size = 0;
    req->request_id = 0;
}

// Initialize response with defaults
static inline void lightusd_fetch_response_init(lightusd_fetch_response_t* resp) {
    resp->status = LIGHTUSD_FETCH_OK;
    resp->data = NULL;
    resp->data_size = 0;
    resp->error_message = NULL;
    resp->async_token = 0;
}

// Initialize handler with defaults
static inline void lightusd_fetch_handler_init(lightusd_fetch_handler_t* handler) {
    handler->fetch_callback = NULL;
    handler->userdata = NULL;
    handler->max_section_size = 0;
}

// ============================================================================
// Preset Fetch Handlers (C++ only)
// ============================================================================

#ifdef __cplusplus
} // extern "C"

#include <string>

namespace lightusd {
namespace v1 {

// Forward declarations for preset handlers (opaque types)
struct MmapFetchHandler;
struct CachedFetchHandler;

// Custom deleters for unique_ptr-like usage
struct MmapFetchHandlerDeleter {
    void operator()(MmapFetchHandler* p) const;
};

struct CachedFetchHandlerDeleter {
    void operator()(CachedFetchHandler* p) const;
};

/// RAII wrapper for MmapFetchHandler
class MmapFetchContext {
public:
    MmapFetchContext() : ctx_(nullptr) {}
    explicit MmapFetchContext(MmapFetchHandler* p) : ctx_(p) {}
    ~MmapFetchContext() { if (ctx_) MmapFetchHandlerDeleter()(ctx_); }

    // Move only
    MmapFetchContext(MmapFetchContext&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }
    MmapFetchContext& operator=(MmapFetchContext&& other) noexcept {
        if (this != &other) {
            if (ctx_) MmapFetchHandlerDeleter()(ctx_);
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    // Non-copyable
    MmapFetchContext(const MmapFetchContext&) = delete;
    MmapFetchContext& operator=(const MmapFetchContext&) = delete;

    MmapFetchHandler* get() const { return ctx_; }
    MmapFetchHandler* release() { auto* p = ctx_; ctx_ = nullptr; return p; }
    explicit operator bool() const { return ctx_ != nullptr; }

private:
    MmapFetchHandler* ctx_;
};

/// RAII wrapper for CachedFetchHandler
class CachedFetchContext {
public:
    CachedFetchContext() : ctx_(nullptr) {}
    explicit CachedFetchContext(CachedFetchHandler* p) : ctx_(p) {}
    ~CachedFetchContext() { if (ctx_) CachedFetchHandlerDeleter()(ctx_); }

    // Move only
    CachedFetchContext(CachedFetchContext&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }
    CachedFetchContext& operator=(CachedFetchContext&& other) noexcept {
        if (this != &other) {
            if (ctx_) CachedFetchHandlerDeleter()(ctx_);
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    // Non-copyable
    CachedFetchContext(const CachedFetchContext&) = delete;
    CachedFetchContext& operator=(const CachedFetchContext&) = delete;

    CachedFetchHandler* get() const { return ctx_; }
    CachedFetchHandler* release() { auto* p = ctx_; ctx_ = nullptr; return p; }
    explicit operator bool() const { return ctx_ != nullptr; }

private:
    CachedFetchHandler* ctx_;
};

/// Create a memory-mapped fetch handler (PC/desktop use)
/// Maps the entire file into memory for zero-copy access.
/// Efficient for local files, minimal memory overhead from OS paging.
/// @param filepath Path to the USDC file
/// @return Handler ready for use with UsdcReader::set_fetch_handler()
///         Returns handler with null callback on failure
/// @note Context is auto-managed globally; handler remains valid until program exit
lightusd_fetch_handler_t create_mmap_fetch_handler(const std::string& filepath);

/// Create a memory-mapped fetch handler with explicit context management
/// @param filepath Path to the USDC file
/// @param out_context Receives the RAII wrapper for the handler context
/// @return Handler ready for use
lightusd_fetch_handler_t create_mmap_fetch_handler(const std::string& filepath,
                                                    MmapFetchContext* out_context);

/// Destroy mmap handler context (frees mapped memory)
void destroy_mmap_fetch_handler(MmapFetchHandler* ctx);

/// Create a cached fetch handler (web/WASM use)
/// Caches fetched sections with LRU eviction.
/// Good for environments without mmap support.
/// @param filepath Path to the USDC file
/// @param cache_size_bytes Maximum cache size (0 = unlimited)
/// @return Handler ready for use
/// @note Context is auto-managed globally; handler remains valid until program exit
lightusd_fetch_handler_t create_cached_fetch_handler(const std::string& filepath,
                                                      size_t cache_size_bytes = 0);

/// Create a cached fetch handler with explicit context management
/// @param filepath Path to the USDC file
/// @param cache_size_bytes Maximum cache size (0 = unlimited)
/// @param out_context Receives the RAII wrapper for the handler context
/// @return Handler ready for use
lightusd_fetch_handler_t create_cached_fetch_handler(const std::string& filepath,
                                                      size_t cache_size_bytes,
                                                      CachedFetchContext* out_context);

/// Destroy cached handler context (frees cache memory)
void destroy_cached_fetch_handler(CachedFetchHandler* ctx);

/// Cache statistics structure
struct CacheStats {
    size_t cache_size_bytes;      // Current cache usage
    size_t cache_limit_bytes;     // Maximum cache size (0 = unlimited)
    size_t total_fetches;         // Number of fetch requests
    size_t cache_hits;            // Fetches served from cache
    size_t cache_misses;          // Fetches that required file read
    size_t bytes_read_from_file;  // Total bytes read from file
    size_t evictions;             // Number of cache evictions
};

/// Get cache statistics from a cached handler
/// @param ctx The cached handler context
/// @return Cache statistics
CacheStats get_cache_stats(const CachedFetchHandler* ctx);

/// Clear the cache in a cached handler
/// @param ctx The cached handler context
void clear_cache(CachedFetchHandler* ctx);

} // namespace v1
} // namespace lightusd

#else
// C-only: extern "C" already closed above
#endif
