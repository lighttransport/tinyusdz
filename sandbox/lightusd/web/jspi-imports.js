// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - JSPI (JavaScript Promise Integration) Imports
//
// This file is included via --pre-js in the Emscripten build when JSPI is enabled.
// JSPI is more efficient than Asyncify as it uses WebAssembly stack switching.
//
// Browser support:
// - Chrome 109+ (enabled by default)
// - Firefox 121+ (enabled by default)
// - Safari: behind flag
//
// Unlike Asyncify, JSPI functions return Promises directly.

// Fetch cache for deduplication and performance
var lightusdFetchCache = new Map();
var lightusdFetchCacheMaxSize = 100;
var lightusdFetchCacheHits = 0;
var lightusdFetchCacheMisses = 0;

// Pending fetches for deduplication
var lightusdPendingFetches = new Map();

/**
 * Check if JSPI is supported in this browser
 */
function lightusdCheckJSPISupport() {
    try {
        // JSPI requires WebAssembly.Suspending and WebAssembly.promising
        return typeof WebAssembly.Suspending === 'function' ||
               typeof WebAssembly.promising === 'function';
    } catch (e) {
        return false;
    }
}

/**
 * Add result to cache with LRU eviction
 */
function lightusdCacheResult(url, result) {
    if (lightusdFetchCache.size >= lightusdFetchCacheMaxSize) {
        // Remove oldest entry (first key)
        var firstKey = lightusdFetchCache.keys().next().value;
        lightusdFetchCache.delete(firstKey);
    }
    lightusdFetchCache.set(url, result);
}

/**
 * JSPI-style async fetch - returns a Promise
 * This function is imported by WASM via JSPI_IMPORTS
 *
 * With JSPI, this function returns a Promise. When WASM calls it,
 * the WebAssembly stack is suspended until the Promise resolves.
 */
function js_fetch_asset_sync(urlPtr, urlLen, outDataPtr, outSizePtr, outStatusPtr) {
    var url = UTF8ToString(urlPtr, urlLen);

    // Check cache first (synchronous path)
    var cached = lightusdFetchCache.get(url);
    if (cached) {
        lightusdFetchCacheHits++;
        var dataPtr = _malloc(cached.data.length);
        HEAPU8.set(cached.data, dataPtr);
        setValue(outDataPtr, dataPtr, '*');
        setValue(outSizePtr, cached.data.length, 'i32');
        setValue(outStatusPtr, cached.status, 'i32');
        return Promise.resolve(0);
    }

    lightusdFetchCacheMisses++;

    // Check if there's already a pending fetch for this URL
    var pending = lightusdPendingFetches.get(url);
    if (pending) {
        return pending.then(function(result) {
            var dataPtr = _malloc(result.data.length);
            HEAPU8.set(result.data, dataPtr);
            setValue(outDataPtr, dataPtr, '*');
            setValue(outSizePtr, result.data.length, 'i32');
            setValue(outStatusPtr, result.status, 'i32');
            return 0;
        }).catch(function(err) {
            console.error('LightUSD fetch error (cached promise):', url, err);
            setValue(outStatusPtr, 0, 'i32');
            return -1;
        });
    }

    // Start new fetch - return Promise for JSPI
    var fetchPromise = fetch(url)
        .then(function(response) {
            return response.arrayBuffer().then(function(buffer) {
                return {
                    data: new Uint8Array(buffer),
                    status: response.status
                };
            });
        });

    lightusdPendingFetches.set(url, fetchPromise);

    return fetchPromise
        .then(function(result) {
            lightusdPendingFetches.delete(url);
            lightusdCacheResult(url, result);

            var dataPtr = _malloc(result.data.length);
            HEAPU8.set(result.data, dataPtr);
            setValue(outDataPtr, dataPtr, '*');
            setValue(outSizePtr, result.data.length, 'i32');
            setValue(outStatusPtr, result.status, 'i32');
            return 0;
        })
        .catch(function(err) {
            lightusdPendingFetches.delete(url);
            console.error('LightUSD fetch error:', url, err);
            setValue(outStatusPtr, 0, 'i32');
            return -1;
        });
}

/**
 * Non-blocking fetch for callback-based API (same as Asyncify version)
 */
function js_fetch_asset(urlPtr, urlLen, requestId, outDataPtr, outSizePtr, outStatusPtr) {
    var url = UTF8ToString(urlPtr, urlLen);

    // Check cache first
    var cached = lightusdFetchCache.get(url);
    if (cached) {
        lightusdFetchCacheHits++;
        var dataPtr = _malloc(cached.data.length);
        HEAPU8.set(cached.data, dataPtr);
        setValue(outDataPtr, dataPtr, '*');
        setValue(outSizePtr, cached.data.length, 'i32');
        setValue(outStatusPtr, cached.status, 'i32');
        return 0;
    }

    lightusdFetchCacheMisses++;

    // Start async fetch
    fetch(url)
        .then(function(response) {
            return response.arrayBuffer().then(function(buffer) {
                return {
                    data: new Uint8Array(buffer),
                    status: response.status
                };
            });
        })
        .then(function(result) {
            lightusdCacheResult(url, result);

            var dataPtr = _malloc(result.data.length);
            HEAPU8.set(result.data, dataPtr);

            if (typeof _lightusd_fetch_complete === 'function') {
                _lightusd_fetch_complete(requestId, dataPtr, result.data.length, result.status, 0);
            }
            _free(dataPtr);
        })
        .catch(function(err) {
            var errorMsg = err.message || 'Network error';
            var errorPtr = allocateUTF8(errorMsg);

            if (typeof _lightusd_fetch_complete === 'function') {
                _lightusd_fetch_complete(requestId, 0, 0, 0, errorPtr);
            }
            _free(errorPtr);
        });

    // Return -1 to indicate async operation in progress
    setValue(outStatusPtr, -1, 'i32');
    return -1;
}

/**
 * Free buffer allocated by JavaScript
 */
function js_free_buffer(ptr) {
    if (ptr) {
        _free(ptr);
    }
}

// Export utility functions to Module
if (typeof Module !== 'undefined') {
    Module['isJSPISupported'] = lightusdCheckJSPISupport;

    Module['clearFetchCache'] = function() {
        lightusdFetchCache.clear();
        lightusdFetchCacheHits = 0;
        lightusdFetchCacheMisses = 0;
    };

    Module['getFetchCacheStats'] = function() {
        return {
            size: lightusdFetchCache.size,
            maxSize: lightusdFetchCacheMaxSize,
            hitCount: lightusdFetchCacheHits,
            missCount: lightusdFetchCacheMisses
        };
    };

    Module['setFetchCacheMaxSize'] = function(size) {
        lightusdFetchCacheMaxSize = size;
    };

    Module['prefetchAssets'] = function(urls) {
        return Promise.all(urls.map(function(url) {
            return fetch(url)
                .then(function(r) { return r.arrayBuffer(); })
                .then(function(buffer) {
                    lightusdCacheResult(url, {
                        data: new Uint8Array(buffer),
                        status: 200
                    });
                })
                .catch(function(err) {
                    console.warn('LightUSD prefetch failed:', url, err);
                });
        }));
    };

    // Log JSPI support status
    if (lightusdCheckJSPISupport()) {
        console.log('LightUSD: JSPI supported in this browser');
    } else {
        console.warn('LightUSD: JSPI not supported - async operations may not work');
    }
}
