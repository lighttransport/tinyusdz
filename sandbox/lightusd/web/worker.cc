// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Web Worker entry point for progressive loading
//
// This runs in a dedicated Web Worker thread, handling:
// - USD parsing (structure and geometry)
// - Asset resolution
// - Mesh conversion
//
// Communication with main thread via postMessage

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "lightusd/streaming_loader.hh"
#include "lightusd/usdz_archive.hh"

#include <string>
#include <vector>
#include <cstring>

using namespace emscripten;
using namespace lightusd;

// ============================================================================
// Global State
// ============================================================================

namespace {

std::unique_ptr<StreamingLoader> g_loader;
std::shared_ptr<AssetCache> g_asset_cache;

// Convert PrimSkeleton to JavaScript object
val skeleton_to_js(const PrimSkeleton& skel) {
    val obj = val::object();
    obj.set("path", skel.path);
    obj.set("name", skel.name);
    obj.set("typeName", skel.type_name);
    obj.set("parentPath", skel.parent_path);

    val children = val::array();
    for (const auto& child : skel.child_paths) {
        children.call<void>("push", child);
    }
    obj.set("childPaths", children);

    obj.set("hasGeometry", skel.has_geometry);
    obj.set("hasMaterial", skel.has_material);
    obj.set("hasTransform", skel.has_transform);
    obj.set("hasTimesamples", skel.has_timesamples);
    obj.set("estimatedVertices", skel.estimated_vertices);
    obj.set("estimatedFaces", skel.estimated_faces);

    return obj;
}

// Convert PrimGeometry to JavaScript object with transferable buffers
val geometry_to_js(const PrimGeometry& geom) {
    val obj = val::object();
    obj.set("path", geom.path);
    obj.set("materialIndex", geom.material_index);

    // Positions (Float32Array view into WASM memory)
    if (!geom.positions.empty()) {
        obj.set("positions", val(typed_memory_view(
            geom.positions.size(), geom.positions.data())));
    }

    // Normals
    if (!geom.normals.empty()) {
        obj.set("normals", val(typed_memory_view(
            geom.normals.size(), geom.normals.data())));
    }

    // Texcoords
    if (!geom.texcoords.empty()) {
        obj.set("texcoords", val(typed_memory_view(
            geom.texcoords.size(), geom.texcoords.data())));
    }

    // Tangents
    if (!geom.tangents.empty()) {
        obj.set("tangents", val(typed_memory_view(
            geom.tangents.size(), geom.tangents.data())));
    }

    // Indices (Uint32Array)
    if (!geom.indices.empty()) {
        obj.set("indices", val(typed_memory_view(
            geom.indices.size(), geom.indices.data())));
    }

    // Bounds
    val boundsMin = val::array();
    boundsMin.call<void>("push", geom.mesh.bounds.min.x);
    boundsMin.call<void>("push", geom.mesh.bounds.min.y);
    boundsMin.call<void>("push", geom.mesh.bounds.min.z);
    obj.set("boundsMin", boundsMin);

    val boundsMax = val::array();
    boundsMax.call<void>("push", geom.mesh.bounds.max.x);
    boundsMax.call<void>("push", geom.mesh.bounds.max.y);
    boundsMax.call<void>("push", geom.mesh.bounds.max.z);
    obj.set("boundsMax", boundsMax);

    // Transform (4x4 matrix)
    obj.set("transform", val(typed_memory_view(16, geom.mesh.transform.m)));

    obj.set("doubleSided", geom.mesh.double_sided);

    return obj;
}

// Convert AssetRequest to JavaScript object
val asset_request_to_js(const AssetRequest& req) {
    val obj = val::object();
    obj.set("path", req.path);
    obj.set("resolvedUrl", req.resolved_url);
    obj.set("required", req.required);
    obj.set("requestingPrim", req.requesting_prim);

    std::string type_str;
    switch (req.type) {
        case AssetType::UsdFile: type_str = "usd"; break;
        case AssetType::UsdzArchive: type_str = "usdz"; break;
        case AssetType::Texture: type_str = "texture"; break;
        default: type_str = "other"; break;
    }
    obj.set("type", type_str);

    return obj;
}

} // anonymous namespace

// ============================================================================
// Worker API (called from JavaScript)
// ============================================================================

// Initialize the worker
void worker_init() {
    g_asset_cache = std::make_shared<AssetCache>();
    g_loader = std::make_unique<StreamingLoader>();
    g_loader->set_asset_cache(g_asset_cache);
}

// Parse USD structure from binary data
// Returns: { ok: bool, error?: string, skeletons?: PrimSkeleton[] }
val parse_structure(const std::string& data, const std::string& filename) {
    val result = val::object();

    if (!g_loader) {
        result.set("ok", false);
        result.set("error", "Worker not initialized");
        return result;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
    size_t size = data.size();

    // Check if USDZ
    Result<std::vector<PrimSkeleton>> parse_result;
    if (UsdzArchive::is_usdz(bytes, size)) {
        parse_result = g_loader->parse_usdz_structure(bytes, size);
    } else {
        parse_result = g_loader->parse_structure(bytes, size, filename);
    }

    if (!parse_result) {
        result.set("ok", false);
        result.set("error", parse_result.error().message);
        return result;
    }

    // Convert skeletons to JS array
    val skeletons = val::array();
    for (const auto& skel : parse_result.value()) {
        skeletons.call<void>("push", skeleton_to_js(skel));
    }

    result.set("ok", true);
    result.set("skeletons", skeletons);
    result.set("primCount", static_cast<int>(parse_result.value().size()));

    return result;
}

// Request geometry loading for a prim
void request_load_prim(const std::string& path, int priority, double time) {
    if (!g_loader) return;

    LoadRequest req;
    req.prim_path = path;
    req.priority = static_cast<LoadPriority>(priority);
    req.time = time;

    g_loader->request_load(req);
}

// Request geometry loading for multiple prims
void request_load_prims(const val& paths, int priority, double time) {
    if (!g_loader) return;

    std::vector<LoadRequest> requests;
    unsigned len = paths["length"].as<unsigned>();

    for (unsigned i = 0; i < len; ++i) {
        LoadRequest req;
        req.prim_path = paths[i].as<std::string>();
        req.priority = static_cast<LoadPriority>(priority);
        req.time = time;
        requests.push_back(req);
    }

    g_loader->request_load_batch(requests);
}

// Set priority for a pending load request
void set_priority(const std::string& path, int priority) {
    if (!g_loader) return;
    g_loader->set_priority(path, static_cast<LoadPriority>(priority));
}

// Cancel pending load request
void cancel_load(const std::string& path) {
    if (!g_loader) return;
    g_loader->cancel_load(path);
}

// Cancel all pending requests
void cancel_all() {
    if (!g_loader) return;
    g_loader->cancel_all();
}

// Provide fetched asset data
void provide_asset(const std::string& path, const std::string& data) {
    if (!g_asset_cache) return;

    std::vector<uint8_t> bytes(data.begin(), data.end());
    g_asset_cache->put(path, std::move(bytes));
}

// Process load queue and return results
// Returns: { processed: number, ready: PrimGeometry[], pendingAssets: AssetRequest[] }
val process_queue(int max_count) {
    val result = val::object();

    if (!g_loader) {
        result.set("processed", 0);
        result.set("ready", val::array());
        result.set("pendingAssets", val::array());
        return result;
    }

    // Process queue
    uint32_t processed = g_loader->process_queue(max_count);
    result.set("processed", static_cast<int>(processed));

    // Get ready prims
    val ready = val::array();
    auto ready_prims = g_loader->take_all_ready_prims();
    for (const auto& geom : ready_prims) {
        ready.call<void>("push", geometry_to_js(geom));
    }
    result.set("ready", ready);

    // Get pending asset requests
    val pending = val::array();
    for (const auto& req : g_loader->pending_assets()) {
        pending.call<void>("push", asset_request_to_js(req));
    }
    result.set("pendingAssets", pending);

    // Status info
    result.set("pendingCount", static_cast<int>(g_loader->pending_count()));
    result.set("waitingAssetCount", static_cast<int>(g_loader->waiting_asset_count()));

    return result;
}

// Get current loader state
// Returns: "idle" | "parsing" | "ready" | "error"
std::string get_state() {
    if (!g_loader) return "idle";

    switch (g_loader->state()) {
        case LoaderState::Idle: return "idle";
        case LoaderState::Parsing: return "parsing";
        case LoaderState::Ready: return "ready";
        case LoaderState::Error: return "error";
    }
    return "idle";
}

// Get prim load state
// Returns: "skeleton" | "queued" | "loading" | "waitingAsset" | "ready" | "error"
std::string get_prim_state(const std::string& path) {
    if (!g_loader) return "skeleton";

    switch (g_loader->prim_state(path)) {
        case PrimLoadState::Skeleton: return "skeleton";
        case PrimLoadState::Queued: return "queued";
        case PrimLoadState::Loading: return "loading";
        case PrimLoadState::WaitingAsset: return "waitingAsset";
        case PrimLoadState::Ready: return "ready";
        case PrimLoadState::Error: return "error";
    }
    return "skeleton";
}

// Get last error message
std::string get_error() {
    if (!g_loader) return "";
    return g_loader->error();
}

// Get pending count
int get_pending_count() {
    if (!g_loader) return 0;
    return static_cast<int>(g_loader->pending_count());
}

// Check if has ready prims
bool has_ready_prims() {
    if (!g_loader) return false;
    return g_loader->has_ready_prims();
}

// Set time budget for processing
void set_time_budget(int ms) {
    if (!g_loader) return;
    g_loader->set_time_budget_ms(ms);
}

// Set asset cache max size
void set_cache_max_size(int mb) {
    if (!g_asset_cache) return;
    g_asset_cache->set_max_size(static_cast<size_t>(mb) * 1024 * 1024);
}

// Clear asset cache
void clear_cache() {
    if (!g_asset_cache) return;
    g_asset_cache->clear();
}

// ============================================================================
// Emscripten Bindings
// ============================================================================

EMSCRIPTEN_BINDINGS(lightusd_worker) {
    // Initialization
    function("workerInit", &worker_init);

    // Parsing
    function("parseStructure", &parse_structure);

    // Load requests
    function("requestLoadPrim", &request_load_prim);
    function("requestLoadPrims", &request_load_prims);
    function("setPriority", &set_priority);
    function("cancelLoad", &cancel_load);
    function("cancelAll", &cancel_all);

    // Asset provision
    function("provideAsset", &provide_asset);

    // Processing
    function("processQueue", &process_queue);

    // State queries
    function("getState", &get_state);
    function("getPrimState", &get_prim_state);
    function("getError", &get_error);
    function("getPendingCount", &get_pending_count);
    function("hasReadyPrims", &has_ready_prims);

    // Configuration
    function("setTimeBudget", &set_time_budget);
    function("setCacheMaxSize", &set_cache_max_size);
    function("clearCache", &clear_cache);
}
