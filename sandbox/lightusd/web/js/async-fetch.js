// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// LightUSD - Async Fetch JavaScript Implementation
//
// This module provides the JavaScript side of the async fetch interface.
// It implements the functions imported by WASM via Asyncify.

/**
 * Initialize the async fetch system.
 * Call this after the WASM module is loaded.
 * @param {Object} Module - The Emscripten module instance
 */
export function initAsyncFetch(Module) {
    // Cache for fetch results
    const fetchCache = new Map();
    const MAX_CACHE_SIZE = 100;

    // Pending fetches for deduplication
    const pendingFetches = new Map();

    /**
     * Add result to cache with LRU eviction
     */
    function cacheResult(url, data) {
        if (fetchCache.size >= MAX_CACHE_SIZE) {
            // Remove oldest entry
            const firstKey = fetchCache.keys().next().value;
            fetchCache.delete(firstKey);
        }
        fetchCache.set(url, data);
    }

    /**
     * Allocate buffer in WASM memory and copy data
     */
    function allocateBuffer(data) {
        const ptr = Module._malloc(data.length);
        Module.HEAPU8.set(data, ptr);
        return ptr;
    }

    /**
     * Synchronous fetch using Asyncify
     * This function is imported by WASM and will cause execution to suspend
     */
    Module['js_fetch_asset_sync'] = function(urlPtr, urlLen, outDataPtr, outSizePtr, outStatusPtr) {
        const url = Module.UTF8ToString(urlPtr, urlLen);

        // Check cache first
        const cached = fetchCache.get(url);
        if (cached) {
            const dataPtr = allocateBuffer(cached.data);
            Module.setValue(outDataPtr, dataPtr, '*');
            Module.setValue(outSizePtr, cached.data.length, 'i32');
            Module.setValue(outStatusPtr, cached.status, 'i32');
            return 0;
        }

        // Return a promise that Asyncify will await
        return Asyncify.handleSleep(function(wakeUp) {
            // Check for pending fetch of same URL
            if (pendingFetches.has(url)) {
                pendingFetches.get(url).then(result => {
                    const dataPtr = allocateBuffer(result.data);
                    Module.setValue(outDataPtr, dataPtr, '*');
                    Module.setValue(outSizePtr, result.data.length, 'i32');
                    Module.setValue(outStatusPtr, result.status, 'i32');
                    wakeUp(0);
                }).catch(err => {
                    Module.setValue(outStatusPtr, 0, 'i32');
                    wakeUp(-1);
                });
                return;
            }

            // Start new fetch
            const fetchPromise = fetch(url)
                .then(response => {
                    return response.arrayBuffer().then(buffer => ({
                        data: new Uint8Array(buffer),
                        status: response.status
                    }));
                });

            pendingFetches.set(url, fetchPromise);

            fetchPromise
                .then(result => {
                    pendingFetches.delete(url);
                    cacheResult(url, result);

                    const dataPtr = allocateBuffer(result.data);
                    Module.setValue(outDataPtr, dataPtr, '*');
                    Module.setValue(outSizePtr, result.data.length, 'i32');
                    Module.setValue(outStatusPtr, result.status, 'i32');
                    wakeUp(0);
                })
                .catch(err => {
                    pendingFetches.delete(url);
                    console.error('Fetch error:', url, err);
                    Module.setValue(outStatusPtr, 0, 'i32');
                    wakeUp(-1);
                });
        });
    };

    /**
     * Non-blocking fetch (for callback-based API)
     */
    Module['js_fetch_asset'] = function(urlPtr, urlLen, requestId, outDataPtr, outSizePtr, outStatusPtr) {
        const url = Module.UTF8ToString(urlPtr, urlLen);

        // Check cache first
        const cached = fetchCache.get(url);
        if (cached) {
            const dataPtr = allocateBuffer(cached.data);
            Module.setValue(outDataPtr, dataPtr, '*');
            Module.setValue(outSizePtr, cached.data.length, 'i32');
            Module.setValue(outStatusPtr, cached.status, 'i32');
            return 0;
        }

        // Start async fetch
        fetch(url)
            .then(response => {
                return response.arrayBuffer().then(buffer => ({
                    data: new Uint8Array(buffer),
                    status: response.status
                }));
            })
            .then(result => {
                cacheResult(url, result);

                // Call back into WASM with result
                const dataPtr = allocateBuffer(result.data);
                Module._lightusd_fetch_complete(
                    requestId,
                    dataPtr,
                    result.data.length,
                    result.status,
                    0  // no error
                );
                Module._free(dataPtr);
            })
            .catch(err => {
                // Allocate error string
                const errorStr = err.message || 'Network error';
                const errorPtr = Module.allocateUTF8(errorStr);

                Module._lightusd_fetch_complete(
                    requestId,
                    0,  // no data
                    0,
                    0,  // network error status
                    errorPtr
                );
                Module._free(errorPtr);
            });

        // Return -1 to indicate async operation in progress
        Module.setValue(outStatusPtr, -1, 'i32');
        return -1;
    };

    /**
     * Free buffer allocated by JS
     */
    Module['js_free_buffer'] = function(ptr) {
        if (ptr) {
            Module._free(ptr);
        }
    };

    console.log('LightUSD: Async fetch initialized');
}

/**
 * Pre-fetch assets into cache
 * @param {string[]} urls - URLs to pre-fetch
 * @returns {Promise<void>}
 */
export async function prefetchAssets(urls) {
    const promises = urls.map(url =>
        fetch(url)
            .then(r => r.arrayBuffer())
            .catch(err => {
                console.warn('Prefetch failed:', url, err);
                return null;
            })
    );
    await Promise.all(promises);
}

export default { initAsyncFetch, prefetchAssets };
