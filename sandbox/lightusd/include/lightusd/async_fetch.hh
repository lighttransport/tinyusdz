// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Async Fetch Interface for WASM
//
// This module provides async asset fetching when compiled with Asyncify or JSPI.
// When enabled, fetch operations suspend WASM execution and resume when the
// JavaScript fetch completes.
//
// Asyncify: Uses code transformation to save/restore stack (works everywhere)
// JSPI: Uses WebAssembly stack switching (more efficient, requires browser support)
//
// Usage:
//   auto result = async_fetch("textures/diffuse.png");
//   if (result.ok) {
//       // Use result.data
//   }

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lightusd {
namespace v1 {

// ============================================================================
// Async Fetch Result
// ============================================================================

/// Result of an async fetch operation
struct FetchResult {
    bool ok = false;
    int status_code = 0;
    std::string error;
    std::vector<uint8_t> data;
    std::string mime_type;
    std::string resolved_url;
};

// ============================================================================
// Async Fetch Configuration
// ============================================================================

/// Configuration for fetch operations
struct FetchConfig {
    /// Base URL for resolving relative paths
    std::string base_url;

    /// Request timeout in milliseconds (0 = no timeout)
    uint32_t timeout_ms = 30000;

    /// Enable CORS mode
    bool cors = true;

    /// Custom headers (name: value pairs)
    std::vector<std::pair<std::string, std::string>> headers;

    /// Retry count on failure
    uint32_t retry_count = 0;
};

// ============================================================================
// Async Fetch Handler (set by JavaScript)
// ============================================================================

/// Callback type for providing fetch results
/// @param request_id Unique request identifier
/// @param data Fetched data (nullptr on error)
/// @param size Data size in bytes
/// @param status HTTP status code (0 on network error)
/// @param error Error message (nullptr on success)
using FetchCallback = void (*)(
    uint32_t request_id,
    const uint8_t* data,
    size_t size,
    int status,
    const char* error
);

/// Set the JavaScript fetch handler
/// This is called from JS to provide a function that can fetch URLs
/// @param handler Function pointer that initiates fetches
void set_fetch_handler(FetchCallback handler);

// ============================================================================
// Sync-style Async Fetch (Asyncify or JSPI)
// ============================================================================

#if defined(LIGHTUSD_ASYNCIFY) || defined(LIGHTUSD_JSPI)

/// Fetch a URL synchronously (uses Asyncify/JSPI to suspend/resume)
/// This function will suspend WASM execution until the fetch completes.
/// @param url URL to fetch (absolute or relative to base_url)
/// @param config Fetch configuration
/// @return Fetch result with data or error
FetchResult async_fetch(const std::string& url, const FetchConfig& config = {});

/// Fetch multiple URLs in parallel
/// All fetches are initiated, then WASM suspends until all complete.
/// @param urls URLs to fetch
/// @param config Shared fetch configuration
/// @return Vector of results in same order as input URLs
std::vector<FetchResult> async_fetch_all(
    const std::vector<std::string>& urls,
    const FetchConfig& config = {}
);

#endif // LIGHTUSD_ASYNCIFY || LIGHTUSD_JSPI

// ============================================================================
// Non-blocking Fetch (callback-based, works without Asyncify/JSPI)
// ============================================================================

/// Request ID for tracking async operations
using FetchRequestId = uint32_t;

/// Callback for fetch completion
using FetchCompleteCallback = std::function<void(const FetchResult&)>;

/// Start a non-blocking fetch
/// @param url URL to fetch
/// @param config Fetch configuration
/// @param callback Called when fetch completes
/// @return Request ID for cancellation
FetchRequestId fetch_async(
    const std::string& url,
    const FetchConfig& config,
    FetchCompleteCallback callback
);

/// Cancel a pending fetch
/// @param request_id Request to cancel
/// @return true if request was found and cancelled
bool fetch_cancel(FetchRequestId request_id);

/// Poll for completed fetches (call from main loop)
/// @param max_callbacks Maximum callbacks to process
/// @return Number of callbacks processed
uint32_t fetch_poll(uint32_t max_callbacks = 10);

// ============================================================================
// JavaScript Import Declarations (Emscripten)
// ============================================================================

#ifdef __EMSCRIPTEN__

extern "C" {

/// JavaScript function to initiate an async fetch
/// Declared as ASYNCIFY_IMPORTS or via JSPI in CMake
/// @param url URL to fetch
/// @param url_len URL length
/// @param request_id Request identifier for callback
/// @param out_data Output buffer pointer (filled by JS)
/// @param out_size Output size pointer
/// @param out_status Output status code pointer
/// @return 0 on success, negative on error
extern int js_fetch_asset(
    const char* url,
    size_t url_len,
    uint32_t request_id,
    uint8_t** out_data,
    size_t* out_size,
    int* out_status
) __attribute__((import_name("js_fetch_asset")));

/// Synchronous version that blocks until complete (Asyncify)
extern int js_fetch_asset_sync(
    const char* url,
    size_t url_len,
    uint8_t** out_data,
    size_t* out_size,
    int* out_status
) __attribute__((import_name("js_fetch_asset_sync")));

/// Parallel fetch multiple URLs using Promise.all (Asyncify/JSPI)
/// @param urls Array of null-terminated URL strings
/// @param url_count Number of URLs
/// @param out_data Array of output buffer pointers (filled by JS)
/// @param out_sizes Array of output sizes
/// @param out_statuses Array of output status codes
/// @return 0 on success (individual results may still have errors)
extern int js_fetch_assets_parallel(
    const char* const* urls,
    size_t url_count,
    uint8_t** out_data,
    size_t* out_sizes,
    int* out_statuses
) __attribute__((import_name("js_fetch_assets_parallel")));

/// Free memory allocated by JS
extern void js_free_buffer(uint8_t* ptr) __attribute__((import_name("js_free_buffer")));

}  // extern "C"

#endif // __EMSCRIPTEN__

}  // namespace v1
}  // namespace lightusd
