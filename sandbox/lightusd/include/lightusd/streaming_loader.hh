// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Progressive/Streaming USD Loader
//
// Architecture for non-blocking USD loading:
//
//   Main Thread (Three.js)              Worker Thread (WASM)
//   ─────────────────────               ───────────────────
//   ProgressiveScene                    StreamingLoader
//   ├─ onPrimDiscovered()  ◄────────── STRUCTURE_READY
//   ├─ onPrimReady()       ◄────────── PRIM_READY
//   ├─ onAssetNeeded()     ◄────────── NEED_ASSET
//   │
//   ├─ requestLoadPrim() ──────────►  LOAD_PRIM
//   ├─ provideAsset()    ──────────►  ASSET_DATA
//   └─ setPriority()     ──────────►  SET_PRIORITY
//
// Key features:
// - Structure parsing is fast (hierarchy only, no geometry)
// - Geometry loads on-demand per prim
// - Never blocks the render loop
// - Priority queue for visible-first loading

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/render_data.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Stage;
class Prim;

// ============================================================================
// Load State and Messages (for Worker ↔ Main communication)
// ============================================================================

/// Loader state
enum class LoaderState : uint8_t {
    Idle,           // Not started
    Parsing,        // Parsing structure
    Ready,          // Structure ready, accepting load requests
    Error,          // Fatal error occurred
};

/// Prim load state
enum class PrimLoadState : uint8_t {
    Skeleton,       // Structure only, no geometry
    Queued,         // In load queue
    Loading,        // Currently loading attributes
    WaitingAsset,   // Blocked on external asset
    Ready,          // Fully loaded, renderable
    Error,          // Failed to load
};

/// Asset type for fetch requests
enum class AssetType : uint8_t {
    UsdFile,        // .usd, .usda, .usdc
    UsdzArchive,    // .usdz
    Texture,        // .png, .jpg, .exr, etc.
    Other,
};

/// Prim skeleton (structure without geometry)
struct PrimSkeleton {
    std::string path;
    std::string name;
    std::string type_name;
    std::string parent_path;    // Empty for root prims
    std::vector<std::string> child_paths;

    // Hints about what this prim contains (without loading)
    bool has_geometry = false;      // Has points/mesh data
    bool has_material = false;      // Has material binding
    bool has_transform = false;     // Has xformOp
    bool has_timesamples = false;   // Has animated attributes

    // Estimated complexity for prioritization
    uint32_t estimated_vertices = 0;
    uint32_t estimated_faces = 0;
};

/// Asset fetch request
struct AssetRequest {
    std::string path;           // Asset path (from USD)
    std::string resolved_url;   // Full URL for fetching
    AssetType type = AssetType::Other;
    bool required = true;       // If false, can skip on failure
    std::string requesting_prim; // Which prim needs this asset
};

/// Loaded prim geometry (transferable to main thread)
struct PrimGeometry {
    std::string path;
    RenderMesh mesh;
    int32_t material_index = -1;

    // For transferable buffers (WebGPU-ready)
    std::vector<float> positions;   // vec3
    std::vector<float> normals;     // vec3
    std::vector<float> texcoords;   // vec2
    std::vector<float> tangents;    // vec4
    std::vector<uint32_t> indices;
};

// ============================================================================
// Asset Cache (shared between loader and fetcher)
// ============================================================================

/// Thread-safe asset cache
class AssetCache {
public:
    AssetCache();
    ~AssetCache();

    /// Check if asset is cached
    bool has(const std::string& path) const;

    /// Get cached asset data (nullptr if not found)
    const std::vector<uint8_t>* get(const std::string& path) const;

    /// Add asset to cache
    void put(const std::string& path, std::vector<uint8_t> data);

    /// Remove asset from cache
    bool remove(const std::string& path);

    /// Clear all cached assets
    void clear();

    /// Get total cached size in bytes
    size_t total_size() const;

    /// Set maximum cache size (evicts LRU when exceeded)
    void set_max_size(size_t bytes);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Priority Queue for Load Requests
// ============================================================================

/// Load priority levels
enum class LoadPriority : uint8_t {
    Immediate = 0,   // Currently visible in frustum
    High = 1,        // Near camera / likely to be visible soon
    Normal = 2,      // Default priority
    Low = 3,         // Background loading
    Deferred = 4,    // Only load if idle
};

/// Load request for a prim
struct LoadRequest {
    std::string prim_path;
    LoadPriority priority = LoadPriority::Normal;
    double time = 0.0;          // Time code for animated data
    bool need_tangents = true;

    // For priority queue ordering
    bool operator<(const LoadRequest& other) const {
        return static_cast<int>(priority) > static_cast<int>(other.priority);
    }
};

// ============================================================================
// Streaming Loader (runs in worker thread)
// ============================================================================

/// Progressive USD loader with on-demand attribute loading
class StreamingLoader {
public:
    StreamingLoader();
    ~StreamingLoader();

    // Non-copyable
    StreamingLoader(const StreamingLoader&) = delete;
    StreamingLoader& operator=(const StreamingLoader&) = delete;

    // === Configuration ===

    /// Set shared asset cache
    void set_asset_cache(std::shared_ptr<AssetCache> cache);

    /// Set base URL for resolving relative paths
    void set_base_url(const std::string& url);

    /// Set maximum time per process_queue() call (for chunking)
    void set_time_budget_ms(uint32_t ms);

    // === Phase 1: Structure Parsing ===

    /// Parse USD structure (hierarchy only, no geometry)
    /// This is fast and can be done synchronously
    /// @param data USD file data
    /// @param size Data size
    /// @param filename Filename for format detection
    /// @return List of prim skeletons or error
    Result<std::vector<PrimSkeleton>> parse_structure(
        const uint8_t* data, size_t size, const std::string& filename);

    /// Parse USDZ structure (also indexes archive assets)
    Result<std::vector<PrimSkeleton>> parse_usdz_structure(
        const uint8_t* data, size_t size);

    // === Phase 2: On-Demand Loading ===

    /// Request geometry loading for a prim
    void request_load(const LoadRequest& request);

    /// Request geometry loading for multiple prims
    void request_load_batch(const std::vector<LoadRequest>& requests);

    /// Change priority of a pending request
    void set_priority(const std::string& prim_path, LoadPriority priority);

    /// Cancel pending load request
    void cancel_load(const std::string& prim_path);

    /// Cancel all pending requests
    void cancel_all();

    /// Process load queue (call periodically from worker)
    /// Returns number of prims processed
    /// @param max_count Maximum prims to process (0 = time-budget based)
    uint32_t process_queue(uint32_t max_count = 0);

    // === Asset Management ===

    /// Provide fetched asset data
    void provide_asset(const std::string& path, std::vector<uint8_t> data);

    /// Mark asset as failed (will skip prims that need it)
    void fail_asset(const std::string& path, const std::string& error);

    /// Get list of pending asset requests
    std::vector<AssetRequest> pending_assets() const;

    // === State Queries ===

    /// Get current loader state
    LoaderState state() const;

    /// Get prim load state
    PrimLoadState prim_state(const std::string& path) const;

    /// Get last error message
    const std::string& error() const;

    /// Get number of pending load requests
    size_t pending_count() const;

    /// Get number of prims waiting for assets
    size_t waiting_asset_count() const;

    // === Results (poll from main thread) ===

    /// Check if there are ready prims to retrieve
    bool has_ready_prims() const;

    /// Get and remove next ready prim geometry
    /// Returns nullopt if none ready
    std::optional<PrimGeometry> take_ready_prim();

    /// Get all ready prims at once
    std::vector<PrimGeometry> take_all_ready_prims();

    // === Stage Access (after structure parsing) ===

    /// Get the parsed stage (for advanced queries)
    const Stage* stage() const;

    /// Get total prim count
    size_t prim_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Worker Message Types (for postMessage serialization)
// ============================================================================

/// Message types from Worker to Main
enum class WorkerMessageType : uint8_t {
    StructureReady,     // Structure parsing complete
    PrimReady,          // Prim geometry loaded
    NeedAsset,          // Request asset fetch
    Progress,           // Loading progress update
    Error,              // Error occurred
};

/// Message types from Main to Worker
enum class MainMessageType : uint8_t {
    LoadFile,           // Start loading a file
    LoadPrim,           // Request prim geometry
    ProvidAsset,        // Provide fetched asset
    SetPriority,        // Change load priority
    Cancel,             // Cancel load request
};

// ============================================================================
// Helper: Frustum-based priority calculation
// ============================================================================

/// Calculate load priority based on camera frustum
/// @param prim_bounds Prim bounding box [min, max]
/// @param camera_pos Camera world position
/// @param camera_dir Camera forward direction
/// @param frustum_planes 6 frustum planes
/// @return Suggested priority
LoadPriority calculate_priority(
    const float* prim_bounds,   // [minX, minY, minZ, maxX, maxY, maxZ]
    const float* camera_pos,    // [x, y, z]
    const float* camera_dir,    // [x, y, z]
    const float* frustum_planes // 6 planes, 4 floats each [a,b,c,d]
);

// ============================================================================
// JavaScript Integration Helpers
// ============================================================================

#ifdef __EMSCRIPTEN__

/// Initialize worker-side loader
/// Call from worker thread startup
void worker_init();

/// Process incoming message from main thread
/// @param msg_type Message type
/// @param data Message data
void worker_handle_message(MainMessageType msg_type, const uint8_t* data, size_t size);

/// Poll for outgoing messages to main thread
/// @param out_type Output message type
/// @param out_data Output message data (caller frees)
/// @param out_size Output data size
/// @return true if message available
bool worker_poll_message(WorkerMessageType* out_type,
                         uint8_t** out_data,
                         size_t* out_size);

#endif // __EMSCRIPTEN__

} // namespace v1
} // namespace lightusd
