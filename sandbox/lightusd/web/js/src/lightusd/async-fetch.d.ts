// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Async Fetch TypeScript Declarations
//
// Supports multiple async mechanisms:
// - Asyncify: Universal support, uses code transformation
// - JSPI: More efficient, requires Chrome 109+, Firefox 121+, or Safari (behind flag)
// - Coroutine: C++20 coroutine API (C++ side only, requires Asyncify or JSPI)

import type { LightUSDModule } from './lightusd';

/**
 * Result of an async fetch operation
 */
export interface FetchResult {
    /** Whether the fetch succeeded */
    ok: boolean;
    /** HTTP status code (0 for network errors) */
    statusCode: number;
    /** Error message if failed */
    error?: string;
    /** Fetched data as Uint8Array */
    data?: Uint8Array;
    /** MIME type of the response */
    mimeType?: string;
    /** Final URL after redirects */
    resolvedUrl?: string;
}

/**
 * Configuration for fetch operations
 */
export interface FetchConfig {
    /** Base URL for resolving relative paths */
    baseUrl?: string;
    /** Request timeout in milliseconds (default: 30000) */
    timeoutMs?: number;
    /** Enable CORS mode (default: true) */
    cors?: boolean;
    /** Custom headers */
    headers?: Record<string, string>;
    /** Number of retries on failure (default: 0) */
    retryCount?: number;
}

/**
 * Initialize the async fetch system.
 * Call this after the WASM module is loaded.
 *
 * @example
 * ```typescript
 * import createLightUSD from '@lightusd/web';
 * import { initAsyncFetch } from '@lightusd/web/async-fetch';
 *
 * const Module = await createLightUSD();
 * initAsyncFetch(Module);
 * ```
 */
export function initAsyncFetch(Module: LightUSDModule): void;

/**
 * Pre-fetch assets into the cache.
 * Useful for preloading textures or referenced USD files.
 *
 * @example
 * ```typescript
 * await prefetchAssets([
 *     'textures/diffuse.png',
 *     'textures/normal.png',
 *     'materials.usda'
 * ]);
 * ```
 */
export function prefetchAssets(urls: string[]): Promise<void>;

/**
 * Clear the fetch cache
 */
export function clearFetchCache(): void;

/**
 * Get cache statistics
 */
export function getFetchCacheStats(): {
    size: number;
    maxSize: number;
    hitCount: number;
    missCount: number;
};

/**
 * Check if JSPI (JavaScript Promise Integration) is supported in this browser.
 * JSPI is more efficient than Asyncify but requires browser support.
 *
 * Browser support:
 * - Chrome 109+ (enabled by default)
 * - Firefox 121+ (enabled by default)
 * - Safari: behind flag
 *
 * @returns true if JSPI is supported
 */
export function isJSPISupported(): boolean;

export default {
    initAsyncFetch,
    prefetchAssets,
    clearFetchCache,
    getFetchCacheStats,
    isJSPISupported
};
