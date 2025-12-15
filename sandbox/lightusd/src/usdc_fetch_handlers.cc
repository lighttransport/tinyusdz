// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// USDC preset fetch handlers implementation

#include "lightusd/usdc_fetch.hh"
#include "lightusd/debug.hh"

#include <fstream>
#include <vector>
#include <list>
#include <unordered_map>
#include <cstring>
#include <mutex>
#include <memory>

// Platform-specific mmap includes
#if defined(_WIN32) || defined(_WIN64)
#define LIGHTUSD_WINDOWS 1
#include <windows.h>
#else
#define LIGHTUSD_POSIX 1
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lightusd {
namespace v1 {

// ============================================================================
// Memory-Mapped Fetch Handler (PC/Desktop)
// ============================================================================

struct MmapFetchHandler {
#ifdef LIGHTUSD_WINDOWS
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = NULL;
#else
    int fd = -1;
#endif
    uint8_t* mapped_data = nullptr;
    size_t file_size = 0;
    bool valid = false;
    std::string error_message;

    MmapFetchHandler() = default;
    ~MmapFetchHandler() { close(); }

    bool open(const std::string& filepath) {
#ifdef LIGHTUSD_WINDOWS
        // Windows memory-mapped file
        file_handle = CreateFileA(
            filepath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (file_handle == INVALID_HANDLE_VALUE) {
            error_message = "Failed to open file";
            return false;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(file_handle, &size)) {
            error_message = "Failed to get file size";
            CloseHandle(file_handle);
            file_handle = INVALID_HANDLE_VALUE;
            return false;
        }
        file_size = static_cast<size_t>(size.QuadPart);

        mapping_handle = CreateFileMappingA(
            file_handle,
            NULL,
            PAGE_READONLY,
            0, 0,
            NULL
        );

        if (mapping_handle == NULL) {
            error_message = "Failed to create file mapping";
            CloseHandle(file_handle);
            file_handle = INVALID_HANDLE_VALUE;
            return false;
        }

        mapped_data = static_cast<uint8_t*>(MapViewOfFile(
            mapping_handle,
            FILE_MAP_READ,
            0, 0, 0
        ));

        if (mapped_data == nullptr) {
            error_message = "Failed to map view of file";
            CloseHandle(mapping_handle);
            CloseHandle(file_handle);
            mapping_handle = NULL;
            file_handle = INVALID_HANDLE_VALUE;
            return false;
        }

#else
        // POSIX mmap
        fd = ::open(filepath.c_str(), O_RDONLY);
        if (fd < 0) {
            error_message = "Failed to open file";
            return false;
        }

        struct stat st;
        if (fstat(fd, &st) < 0) {
            error_message = "Failed to get file size";
            ::close(fd);
            fd = -1;
            return false;
        }
        file_size = static_cast<size_t>(st.st_size);

        mapped_data = static_cast<uint8_t*>(
            mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0)
        );

        if (mapped_data == MAP_FAILED) {
            error_message = "Failed to mmap file";
            mapped_data = nullptr;
            ::close(fd);
            fd = -1;
            return false;
        }
#endif

        valid = true;
        DCOUT("Mmap handler: mapped " << file_size << " bytes");
        return true;
    }

    void close() {
        if (!valid) return;

#ifdef LIGHTUSD_WINDOWS
        if (mapped_data) {
            UnmapViewOfFile(mapped_data);
            mapped_data = nullptr;
        }
        if (mapping_handle) {
            CloseHandle(mapping_handle);
            mapping_handle = NULL;
        }
        if (file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle);
            file_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (mapped_data && file_size > 0) {
            munmap(mapped_data, file_size);
            mapped_data = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
#endif

        valid = false;
        file_size = 0;
    }

    // Non-copyable
    MmapFetchHandler(const MmapFetchHandler&) = delete;
    MmapFetchHandler& operator=(const MmapFetchHandler&) = delete;
};

// Mmap callback function
static void mmap_fetch_callback(const lightusd_fetch_request_t* req,
                                 lightusd_fetch_response_t* resp,
                                 void* userdata) {
    auto* ctx = static_cast<MmapFetchHandler*>(userdata);

    if (!ctx || !ctx->valid) {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = ctx ? ctx->error_message.c_str() : "Invalid mmap context";
        return;
    }

    // Bounds check
    if (req->offset + req->size > ctx->file_size) {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = "Request exceeds file size";
        return;
    }

    // Return pointer directly into mapped memory (zero-copy)
    resp->status = LIGHTUSD_FETCH_OK;
    resp->data = ctx->mapped_data + req->offset;
    resp->data_size = req->size;
}

// Global storage for auto-managed mmap handlers
static std::mutex g_mmap_handlers_mutex;
static std::list<std::unique_ptr<MmapFetchHandler>> g_mmap_handlers;

lightusd_fetch_handler_t create_mmap_fetch_handler(const std::string& filepath) {
    auto ctx = std::make_unique<MmapFetchHandler>();
    if (!ctx->open(filepath)) {
        lightusd_fetch_handler_t handler;
        lightusd_fetch_handler_init(&handler);
        return handler;  // Returns handler with null callback
    }

    lightusd_fetch_handler_t handler;
    handler.fetch_callback = mmap_fetch_callback;
    handler.userdata = ctx.get();
    handler.max_section_size = 0;

    // Store in global list for lifetime management
    std::lock_guard<std::mutex> lock(g_mmap_handlers_mutex);
    g_mmap_handlers.push_back(std::move(ctx));

    return handler;
}

lightusd_fetch_handler_t create_mmap_fetch_handler(const std::string& filepath,
                                                    MmapFetchContext* out_context) {
    auto* ctx = new MmapFetchHandler();
    if (!ctx->open(filepath)) {
        lightusd_fetch_handler_t handler;
        lightusd_fetch_handler_init(&handler);
        delete ctx;
        if (out_context) *out_context = MmapFetchContext();
        return handler;
    }

    lightusd_fetch_handler_t handler;
    handler.fetch_callback = mmap_fetch_callback;
    handler.userdata = ctx;
    handler.max_section_size = 0;

    if (out_context) {
        *out_context = MmapFetchContext(ctx);
    } else {
        // If no out_context, store in global list
        std::lock_guard<std::mutex> lock(g_mmap_handlers_mutex);
        g_mmap_handlers.push_back(std::unique_ptr<MmapFetchHandler>(ctx));
    }

    return handler;
}

// Deleter implementation
void MmapFetchHandlerDeleter::operator()(MmapFetchHandler* p) const {
    if (p) {
        p->close();
        delete p;
    }
}

void destroy_mmap_fetch_handler(MmapFetchHandler* ctx) {
    if (!ctx) return;

    // Check if it's in global list
    std::lock_guard<std::mutex> lock(g_mmap_handlers_mutex);
    for (auto it = g_mmap_handlers.begin(); it != g_mmap_handlers.end(); ++it) {
        if (it->get() == ctx) {
            g_mmap_handlers.erase(it);
            return;
        }
    }

    // If not in global list, assume caller manages it
    ctx->close();
}

// ============================================================================
// Cached Fetch Handler (Web/WASM)
// ============================================================================

struct CacheEntry {
    uint64_t offset;
    std::vector<uint8_t> data;
    lightusd_fetch_tag_t tag;
};

struct CachedFetchHandler {
    std::string filepath;
    std::ifstream file;
    size_t file_size = 0;
    bool valid = false;
    std::string error_message;

    // Cache configuration
    size_t cache_limit = 0;  // 0 = unlimited

    // Cache storage (LRU: front = most recently used)
    std::list<CacheEntry> cache_list;
    std::unordered_map<uint64_t, std::list<CacheEntry>::iterator> cache_map;
    size_t cache_size = 0;

    // Statistics
    size_t total_fetches = 0;
    size_t cache_hits = 0;
    size_t cache_misses = 0;
    size_t bytes_read = 0;
    size_t evictions = 0;

    CachedFetchHandler() = default;
    ~CachedFetchHandler() { close(); }

    bool open(const std::string& path, size_t limit) {
        filepath = path;
        cache_limit = limit;

        file.open(filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            error_message = "Failed to open file";
            return false;
        }

        file_size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        valid = true;
        DCOUT("Cached handler: opened " << file_size << " bytes, cache limit "
              << (cache_limit > 0 ? std::to_string(cache_limit) : "unlimited"));
        return true;
    }

    void close() {
        if (file.is_open()) {
            file.close();
        }
        cache_list.clear();
        cache_map.clear();
        cache_size = 0;
        valid = false;
    }

    // Find in cache (returns nullptr if not found)
    const CacheEntry* find_in_cache(uint64_t offset) {
        auto it = cache_map.find(offset);
        if (it != cache_map.end()) {
            // Move to front (most recently used)
            cache_list.splice(cache_list.begin(), cache_list, it->second);
            return &(*it->second);
        }
        return nullptr;
    }

    // Add to cache
    void add_to_cache(uint64_t offset, const std::vector<uint8_t>& data, lightusd_fetch_tag_t tag) {
        // Check if already in cache
        auto existing = cache_map.find(offset);
        if (existing != cache_map.end()) {
            // Update and move to front
            existing->second->data = data;
            cache_list.splice(cache_list.begin(), cache_list, existing->second);
            return;
        }

        // Evict if necessary
        while (cache_limit > 0 && cache_size + data.size() > cache_limit && !cache_list.empty()) {
            evict_lru();
        }

        // Add new entry at front
        cache_list.push_front({offset, data, tag});
        cache_map[offset] = cache_list.begin();
        cache_size += data.size();
    }

    // Evict least recently used entry
    void evict_lru() {
        if (cache_list.empty()) return;

        auto& entry = cache_list.back();
        cache_size -= entry.data.size();
        cache_map.erase(entry.offset);
        cache_list.pop_back();
        evictions++;
    }

    void clear() {
        cache_list.clear();
        cache_map.clear();
        cache_size = 0;
    }

    // Non-copyable
    CachedFetchHandler(const CachedFetchHandler&) = delete;
    CachedFetchHandler& operator=(const CachedFetchHandler&) = delete;
};

// Cached callback function
static void cached_fetch_callback(const lightusd_fetch_request_t* req,
                                   lightusd_fetch_response_t* resp,
                                   void* userdata) {
    auto* ctx = static_cast<CachedFetchHandler*>(userdata);

    if (!ctx || !ctx->valid) {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = ctx ? ctx->error_message.c_str() : "Invalid cached context";
        return;
    }

    ctx->total_fetches++;

    // Bounds check
    if (req->offset + req->size > ctx->file_size) {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = "Request exceeds file size";
        return;
    }

    // Check cache
    const CacheEntry* cached = ctx->find_in_cache(req->offset);
    if (cached && cached->data.size() >= req->size) {
        // Cache hit
        ctx->cache_hits++;
        resp->status = LIGHTUSD_FETCH_OK;
        resp->data = cached->data.data();
        resp->data_size = req->size;
        return;
    }

    // Cache miss - read from file
    ctx->cache_misses++;

    std::vector<uint8_t> data(req->size);
    ctx->file.seekg(static_cast<std::streamoff>(req->offset), std::ios::beg);
    if (!ctx->file.good()) {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = "Seek failed";
        return;
    }

    ctx->file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(req->size));
    size_t bytes_read = static_cast<size_t>(ctx->file.gcount());

    if (bytes_read == 0 && req->size > 0) {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = "Read failed";
        return;
    }

    ctx->bytes_read += bytes_read;

    // Add to cache
    ctx->add_to_cache(req->offset, data, req->tag);

    // Return from cache (pointer stability after add_to_cache)
    const CacheEntry* entry = ctx->find_in_cache(req->offset);
    if (entry) {
        resp->status = LIGHTUSD_FETCH_OK;
        resp->data = entry->data.data();
        resp->data_size = bytes_read;
    } else {
        resp->status = LIGHTUSD_FETCH_ERROR;
        resp->error_message = "Cache insertion failed";
    }
}

// Global storage for auto-managed cached handlers
static std::mutex g_cached_handlers_mutex;
static std::list<std::unique_ptr<CachedFetchHandler>> g_cached_handlers;

lightusd_fetch_handler_t create_cached_fetch_handler(const std::string& filepath,
                                                      size_t cache_size_bytes) {
    auto ctx = std::make_unique<CachedFetchHandler>();
    if (!ctx->open(filepath, cache_size_bytes)) {
        lightusd_fetch_handler_t handler;
        lightusd_fetch_handler_init(&handler);
        return handler;
    }

    lightusd_fetch_handler_t handler;
    handler.fetch_callback = cached_fetch_callback;
    handler.userdata = ctx.get();
    handler.max_section_size = 0;

    // Store in global list for lifetime management
    std::lock_guard<std::mutex> lock(g_cached_handlers_mutex);
    g_cached_handlers.push_back(std::move(ctx));

    return handler;
}

lightusd_fetch_handler_t create_cached_fetch_handler(const std::string& filepath,
                                                      size_t cache_size_bytes,
                                                      CachedFetchContext* out_context) {
    auto* ctx = new CachedFetchHandler();
    if (!ctx->open(filepath, cache_size_bytes)) {
        lightusd_fetch_handler_t handler;
        lightusd_fetch_handler_init(&handler);
        delete ctx;
        if (out_context) *out_context = CachedFetchContext();
        return handler;
    }

    lightusd_fetch_handler_t handler;
    handler.fetch_callback = cached_fetch_callback;
    handler.userdata = ctx;
    handler.max_section_size = 0;

    if (out_context) {
        *out_context = CachedFetchContext(ctx);
    } else {
        // If no out_context, store in global list
        std::lock_guard<std::mutex> lock(g_cached_handlers_mutex);
        g_cached_handlers.push_back(std::unique_ptr<CachedFetchHandler>(ctx));
    }

    return handler;
}

// Deleter implementation
void CachedFetchHandlerDeleter::operator()(CachedFetchHandler* p) const {
    if (p) {
        p->close();
        delete p;
    }
}

void destroy_cached_fetch_handler(CachedFetchHandler* ctx) {
    if (!ctx) return;

    // Check if it's in global list
    std::lock_guard<std::mutex> lock(g_cached_handlers_mutex);
    for (auto it = g_cached_handlers.begin(); it != g_cached_handlers.end(); ++it) {
        if (it->get() == ctx) {
            g_cached_handlers.erase(it);
            return;
        }
    }

    // If not in global list, assume caller manages it
    ctx->close();
}

CacheStats get_cache_stats(const CachedFetchHandler* ctx) {
    CacheStats stats = {};
    if (!ctx) return stats;

    stats.cache_size_bytes = ctx->cache_size;
    stats.cache_limit_bytes = ctx->cache_limit;
    stats.total_fetches = ctx->total_fetches;
    stats.cache_hits = ctx->cache_hits;
    stats.cache_misses = ctx->cache_misses;
    stats.bytes_read_from_file = ctx->bytes_read;
    stats.evictions = ctx->evictions;

    return stats;
}

void clear_cache(CachedFetchHandler* ctx) {
    if (ctx) {
        ctx->clear();
    }
}

} // namespace v1
} // namespace lightusd
