// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Async Fetch Implementation

#include "lightusd/async_fetch.hh"

#include <atomic>
#include <mutex>
#include <queue>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace lightusd {
namespace v1 {

// ============================================================================
// Internal State
// ============================================================================

namespace {

// Global fetch handler set by JavaScript
FetchCallback g_fetch_handler = nullptr;

// Request ID counter
std::atomic<uint32_t> g_next_request_id{1};

// Pending requests
struct PendingRequest {
    std::string url;
    FetchConfig config;
    FetchCompleteCallback callback;
};

std::mutex g_requests_mutex;
std::unordered_map<FetchRequestId, PendingRequest> g_pending_requests;

// Completed results (for polling)
std::mutex g_results_mutex;
std::queue<std::pair<FetchRequestId, FetchResult>> g_completed_results;

}  // anonymous namespace

// ============================================================================
// Handler Registration
// ============================================================================

void set_fetch_handler(FetchCallback handler) {
    g_fetch_handler = handler;
}

// ============================================================================
// Sync-style Async Fetch (Asyncify or JSPI)
// ============================================================================

#if defined(LIGHTUSD_ASYNCIFY) || defined(LIGHTUSD_JSPI)

FetchResult async_fetch(const std::string& url, const FetchConfig& config) {
    FetchResult result;

    // Resolve relative URLs
    std::string full_url = url;
    if (!config.base_url.empty() &&
        url.find("://") == std::string::npos &&
        url[0] != '/') {
        full_url = config.base_url + url;
    }

#ifdef __EMSCRIPTEN__
    uint8_t* data = nullptr;
    size_t size = 0;
    int status = 0;

    // This call will suspend WASM execution via Asyncify or JSPI
    // JavaScript handles the actual fetch and resumes when done
    // - Asyncify: Uses Asyncify.handleSleep() to suspend/resume
    // - JSPI: Returns a Promise that suspends via WebAssembly stack switching
    int ret = js_fetch_asset_sync(
        full_url.c_str(),
        full_url.size(),
        &data,
        &size,
        &status
    );

    result.status_code = status;
    result.resolved_url = full_url;

    if (ret == 0 && data != nullptr && status >= 200 && status < 300) {
        result.ok = true;
        result.data.assign(data, data + size);
        js_free_buffer(data);
    } else {
        result.ok = false;
        if (status == 0) {
            result.error = "Network error";
        } else if (status == 404) {
            result.error = "Not found: " + full_url;
        } else {
            result.error = "HTTP error " + std::to_string(status);
        }
    }
#else
    // Non-Emscripten builds: return error
    result.ok = false;
    result.error = "Async fetch not available outside Emscripten";
#endif

    return result;
}

std::vector<FetchResult> async_fetch_all(
    const std::vector<std::string>& urls,
    const FetchConfig& config) {

    std::vector<FetchResult> results;
    results.reserve(urls.size());

    // For now, fetch sequentially
    // TODO: Implement parallel fetching with Promise.all
    for (const auto& url : urls) {
        results.push_back(async_fetch(url, config));
    }

    return results;
}

#endif // LIGHTUSD_ASYNCIFY || LIGHTUSD_JSPI

// ============================================================================
// Non-blocking Callback-based Fetch
// ============================================================================

FetchRequestId fetch_async(
    const std::string& url,
    const FetchConfig& config,
    FetchCompleteCallback callback) {

    FetchRequestId request_id = g_next_request_id.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(g_requests_mutex);
        g_pending_requests[request_id] = PendingRequest{url, config, callback};
    }

#ifdef __EMSCRIPTEN__
    // Resolve URL
    std::string full_url = url;
    if (!config.base_url.empty() &&
        url.find("://") == std::string::npos &&
        url[0] != '/') {
        full_url = config.base_url + url;
    }

    uint8_t* data = nullptr;
    size_t size = 0;
    int status = 0;

    // Non-asyncify version - this will return immediately with status -1
    // if the fetch is pending, and the result will be delivered via callback
    int ret = js_fetch_asset(
        full_url.c_str(),
        full_url.size(),
        request_id,
        &data,
        &size,
        &status
    );

    if (ret == 0 && status > 0) {
        // Immediate result (cached or synchronous)
        FetchResult result;
        result.ok = (status >= 200 && status < 300);
        result.status_code = status;
        result.resolved_url = full_url;

        if (result.ok && data != nullptr) {
            result.data.assign(data, data + size);
            js_free_buffer(data);
        } else {
            result.error = "HTTP error " + std::to_string(status);
        }

        // Queue result for polling
        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            g_completed_results.push({request_id, std::move(result)});
        }
    }
    // Otherwise, result will be delivered asynchronously
#else
    // Non-Emscripten: immediately fail
    FetchResult result;
    result.ok = false;
    result.error = "Async fetch not available outside Emscripten";

    std::lock_guard<std::mutex> lock(g_results_mutex);
    g_completed_results.push({request_id, std::move(result)});
#endif

    return request_id;
}

bool fetch_cancel(FetchRequestId request_id) {
    std::lock_guard<std::mutex> lock(g_requests_mutex);
    return g_pending_requests.erase(request_id) > 0;
}

uint32_t fetch_poll(uint32_t max_callbacks) {
    uint32_t processed = 0;

    while (processed < max_callbacks) {
        FetchRequestId request_id;
        FetchResult result;
        FetchCompleteCallback callback;

        // Get next completed result
        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            if (g_completed_results.empty()) break;

            auto& front = g_completed_results.front();
            request_id = front.first;
            result = std::move(front.second);
            g_completed_results.pop();
        }

        // Find and call the callback
        {
            std::lock_guard<std::mutex> lock(g_requests_mutex);
            auto it = g_pending_requests.find(request_id);
            if (it != g_pending_requests.end()) {
                callback = std::move(it->second.callback);
                g_pending_requests.erase(it);
            }
        }

        if (callback) {
            callback(result);
        }

        processed++;
    }

    return processed;
}

// ============================================================================
// Callback from JavaScript (receives fetch results)
// ============================================================================

#ifdef __EMSCRIPTEN__

extern "C" {

// Called by JavaScript when an async fetch completes
EMSCRIPTEN_KEEPALIVE
void lightusd_fetch_complete(
    uint32_t request_id,
    const uint8_t* data,
    size_t size,
    int status,
    const char* error) {

    FetchResult result;
    result.status_code = status;

    if (status >= 200 && status < 300 && data != nullptr) {
        result.ok = true;
        result.data.assign(data, data + size);
    } else {
        result.ok = false;
        result.error = error ? error : ("HTTP error " + std::to_string(status));
    }

    // Queue result for polling
    std::lock_guard<std::mutex> lock(g_results_mutex);
    g_completed_results.push({request_id, std::move(result)});
}

}  // extern "C"

#endif // __EMSCRIPTEN__

}  // namespace v1
}  // namespace lightusd
