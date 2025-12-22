// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Async/Coroutine Loading Example
//
// This example demonstrates:
// 1. Progressive/streaming USD loading with StreamingLoader
// 2. C++20 coroutine-based async fetching (when available)
// 3. Priority-based loading for visible-first rendering
//
// Build with: -DLIGHTUSD_COROUTINE=ON -DLIGHTUSD_WASM_JSPI=ON (or ASYNCIFY)

#include "lightusd/lightusd.hh"
#include "lightusd/streaming_loader.hh"

#include <cstdio>
#include <cstring>

// ============================================================================
// Example 1: StreamingLoader for Progressive Loading
// ============================================================================

void streaming_loader_example() {
    printf("=== StreamingLoader Example ===\n\n");

    using namespace lightusd::v1;

    // Create streaming loader with asset cache
    auto cache = std::make_shared<AssetCache>();
    cache->set_max_size(64 * 1024 * 1024);  // 64 MB cache

    StreamingLoader loader;
    loader.set_asset_cache(cache);
    loader.set_base_url("https://example.com/assets/");
    loader.set_time_budget_ms(16);  // One frame at 60fps

    // Parse structure from USDA (fast - hierarchy only)
    const char* usda = R"(#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World" {
    def Mesh "Hero" {
        float3[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
    }

    def Mesh "Background" {
        float3[] points = [(0,0,0), (10,0,0), (10,10,0), (0,10,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
    }

    def Mesh "Detail" {
        float3[] points = [(0,0,0), (0.1,0,0), (0.1,0.1,0)]
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
    }
}
)";

    auto result = loader.parse_structure(
        reinterpret_cast<const uint8_t*>(usda),
        strlen(usda),
        "scene.usda"
    );

    if (!result) {
        printf("Parse failed: %s\n", result.error().message.c_str());
        return;
    }

    printf("Parsed %zu prims (structure only, no geometry yet)\n\n",
           result.value().size());

    // Print discovered prim skeletons
    printf("Discovered prims:\n");
    for (const auto& skel : result.value()) {
        printf("  %s (%s)%s\n",
               skel.path.c_str(),
               skel.type_name.c_str(),
               skel.has_geometry ? " [has geometry]" : "");
    }

    // Request geometry loading with priorities (visible-first)
    printf("\nRequesting geometry loads:\n");

    // Hero mesh is visible - load immediately
    loader.request_load({"/World/Hero", LoadPriority::Immediate, 0.0, true});
    printf("  /World/Hero -> Immediate priority\n");

    // Background is partially visible
    loader.request_load({"/World/Background", LoadPriority::Normal, 0.0, true});
    printf("  /World/Background -> Normal priority\n");

    // Detail is far away, load later
    loader.request_load({"/World/Detail", LoadPriority::Low, 0.0, true});
    printf("  /World/Detail -> Low priority\n");

    printf("\nPending loads: %zu\n", loader.pending_count());

    // Process queue (in real app, call this each frame)
    printf("\nProcessing load queue...\n");
    uint32_t processed = loader.process_queue(10);
    printf("Processed %u prims\n", processed);

    // Take ready geometry
    auto ready_prims = loader.take_all_ready_prims();
    printf("\nReady prims: %zu\n", ready_prims.size());

    for (const auto& geom : ready_prims) {
        printf("  %s: %zu vertices, %zu indices\n",
               geom.path.c_str(),
               geom.positions.size() / 3,
               geom.indices.size());
    }

    printf("\n");
}

// ============================================================================
// Example 2: C++20 Coroutine Async Loading (WASM)
// ============================================================================

#if defined(LIGHTUSD_COROUTINE) && __cplusplus >= 202002L

#include "lightusd/coro_fetch.hh"

using namespace lightusd::v1;

// Load a USD file and all its textures using coroutines
Task<bool> load_scene_async(const std::string& url) {
    printf("Starting async load: %s\n", url.c_str());

    // Fetch the main USD file
    FetchResult usd_result = co_await coro_fetch(url);

    if (!usd_result.ok) {
        printf("Failed to fetch USD: %s\n", usd_result.error.c_str());
        co_return false;
    }

    printf("Fetched USD: %zu bytes\n", usd_result.data.size());

    // Parse the stage
    StreamingLoader loader;
    auto parse_result = loader.parse_structure(
        usd_result.data.data(),
        usd_result.data.size(),
        url
    );

    if (!parse_result) {
        printf("Failed to parse: %s\n", parse_result.error().message.c_str());
        co_return false;
    }

    printf("Parsed %zu prims\n", parse_result.value().size());

    // Fetch textures in parallel (example URLs)
    std::vector<std::string> texture_urls = {
        "textures/diffuse.png",
        "textures/normal.png",
        "textures/roughness.png"
    };

    printf("Fetching %zu textures...\n", texture_urls.size());
    auto texture_results = co_await coro_fetch_all(texture_urls);

    size_t success_count = 0;
    for (size_t i = 0; i < texture_results.size(); ++i) {
        if (texture_results[i].ok) {
            printf("  %s: %zu bytes\n",
                   texture_urls[i].c_str(),
                   texture_results[i].data.size());
            success_count++;
        } else {
            printf("  %s: FAILED (%s)\n",
                   texture_urls[i].c_str(),
                   texture_results[i].error.c_str());
        }
    }

    printf("Loaded %zu/%zu textures\n", success_count, texture_urls.size());
    co_return true;
}

// Generator example: yield prims as they load
Generator<PrimGeometry> load_prims_progressive(StreamingLoader& loader) {
    // Process prims one at a time, yielding each as it becomes ready
    while (loader.pending_count() > 0) {
        loader.process_queue(1);

        while (loader.has_ready_prims()) {
            auto geom = loader.take_ready_prim();
            if (geom) {
                co_yield std::move(*geom);
            }
        }
    }
}

void coroutine_example() {
    printf("=== C++20 Coroutine Example ===\n\n");

    // Note: In WASM, this would use Asyncify/JSPI for actual async execution.
    // On native, this demonstrates the API but executes synchronously.

    printf("Coroutine API available!\n");
    printf("In WASM with JSPI/Asyncify, async operations suspend the WASM stack.\n\n");

    // Example: progressive loading with generator
    StreamingLoader loader;
    const char* usda = R"(#usda 1.0
def Mesh "A" { float3[] points = [(0,0,0)] }
def Mesh "B" { float3[] points = [(1,1,1)] }
)";

    auto result = loader.parse_structure(
        reinterpret_cast<const uint8_t*>(usda),
        strlen(usda),
        "test.usda"
    );

    if (result) {
        loader.request_load({"/A", LoadPriority::Normal, 0.0, true});
        loader.request_load({"/B", LoadPriority::Normal, 0.0, true});

        printf("Progressive loading with Generator:\n");
        for (auto& geom : load_prims_progressive(loader)) {
            printf("  Yielded: %s\n", geom.path.c_str());
        }
    }

    printf("\n");
}

#else

void coroutine_example() {
    printf("=== C++20 Coroutine Example ===\n\n");
    printf("Coroutines not available.\n");
    printf("Build with C++20 and -DLIGHTUSD_COROUTINE=ON\n\n");
}

#endif // LIGHTUSD_COROUTINE

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("LightUSD Async/Streaming Examples\n");
    printf("==================================\n\n");

    streaming_loader_example();
    coroutine_example();

    return 0;
}
