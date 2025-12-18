// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Progressive/Streaming USD Loader implementation

#include "lightusd/streaming_loader.hh"
#include "lightusd/stage.hh"
#include "lightusd/prim.hh"
#include "lightusd/usda_reader.hh"
#include "lightusd/usdc_reader.hh"
#include "lightusd/usdz_archive.hh"
#include "lightusd/render_data.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace lightusd {
namespace v1 {

// ============================================================================
// AssetCache Implementation
// ============================================================================

class AssetCache::Impl {
public:
    struct CacheEntry {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point last_access;
        size_t size;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> entries_;
    size_t total_size_ = 0;
    size_t max_size_ = 512 * 1024 * 1024;  // 512 MB default

    void evict_lru() {
        if (entries_.empty()) return;

        // Find least recently used
        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.last_access < oldest->second.last_access) {
                oldest = it;
            }
        }

        total_size_ -= oldest->second.size;
        entries_.erase(oldest);
    }
};

AssetCache::AssetCache() : impl_(new Impl()) {}
AssetCache::~AssetCache() = default;

bool AssetCache::has(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->entries_.count(path) > 0;
}

const std::vector<uint8_t>* AssetCache::get(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->entries_.find(path);
    if (it == impl_->entries_.end()) return nullptr;

    it->second.last_access = std::chrono::steady_clock::now();
    return &it->second.data;
}

void AssetCache::put(const std::string& path, std::vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    size_t size = data.size();

    // Evict if needed
    while (impl_->total_size_ + size > impl_->max_size_ && !impl_->entries_.empty()) {
        impl_->evict_lru();
    }

    // Remove existing entry if present
    auto it = impl_->entries_.find(path);
    if (it != impl_->entries_.end()) {
        impl_->total_size_ -= it->second.size;
        impl_->entries_.erase(it);
    }

    // Add new entry
    Impl::CacheEntry entry;
    entry.data = std::move(data);
    entry.last_access = std::chrono::steady_clock::now();
    entry.size = size;

    impl_->entries_[path] = std::move(entry);
    impl_->total_size_ += size;
}

bool AssetCache::remove(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->entries_.find(path);
    if (it == impl_->entries_.end()) return false;

    impl_->total_size_ -= it->second.size;
    impl_->entries_.erase(it);
    return true;
}

void AssetCache::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->entries_.clear();
    impl_->total_size_ = 0;
}

size_t AssetCache::total_size() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->total_size_;
}

void AssetCache::set_max_size(size_t bytes) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->max_size_ = bytes;

    // Evict if over new limit
    while (impl_->total_size_ > impl_->max_size_ && !impl_->entries_.empty()) {
        impl_->evict_lru();
    }
}

// ============================================================================
// StreamingLoader Implementation
// ============================================================================

class StreamingLoader::Impl {
public:
    // Configuration
    std::shared_ptr<AssetCache> asset_cache_;
    std::string base_url_;
    uint32_t time_budget_ms_ = 16;  // One frame at 60fps

    // State
    LoaderState state_ = LoaderState::Idle;
    std::string error_;

    // Parsed data
    std::unique_ptr<Stage> stage_;
    std::unique_ptr<UsdzArchive> usdz_archive_;
    std::unordered_map<std::string, PrimSkeleton> prim_skeletons_;
    std::unordered_map<std::string, PrimLoadState> prim_states_;

    // Load queue (priority-based)
    std::priority_queue<LoadRequest> load_queue_;
    std::unordered_set<std::string> queued_paths_;

    // Asset requests
    std::vector<AssetRequest> pending_assets_;
    std::unordered_map<std::string, std::string> asset_errors_;
    std::unordered_set<std::string> prims_waiting_assets_;

    // Ready prims (to be picked up by main thread)
    mutable std::mutex ready_mutex_;
    std::queue<PrimGeometry> ready_prims_;

    // Helper: Check if prim type has geometry
    static bool type_has_geometry(const std::string& type_name) {
        static const std::unordered_set<std::string> geometry_types = {
            "Mesh", "Points", "BasisCurves", "NurbsCurves", "NurbsPatch",
            "Cube", "Sphere", "Cylinder", "Cone", "Capsule"
        };
        return geometry_types.count(type_name) > 0;
    }

    // Helper: Get file extension (lowercase)
    static std::string get_extension(const std::string& path) {
        size_t dot = path.rfind('.');
        if (dot == std::string::npos) return "";
        std::string ext = path.substr(dot);
        for (auto& c : ext) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        }
        return ext;
    }

    // Helper: Determine asset type from path
    static AssetType get_asset_type(const std::string& path) {
        std::string ext = get_extension(path);
        if (ext == ".usd" || ext == ".usda" || ext == ".usdc") {
            return AssetType::UsdFile;
        }
        if (ext == ".usdz") {
            return AssetType::UsdzArchive;
        }
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".exr" || ext == ".hdr" || ext == ".tif" || ext == ".tiff") {
            return AssetType::Texture;
        }
        return AssetType::Other;
    }

    // Build skeleton from prim (without loading attributes)
    PrimSkeleton build_skeleton(const Prim& prim, const std::string& parent_path) {
        PrimSkeleton skel;
        skel.name = prim.name();
        skel.path = parent_path.empty() ? "/" + skel.name : parent_path + "/" + skel.name;
        skel.type_name = prim.type_name();
        skel.parent_path = parent_path;

        // Check what this prim has (without loading full values)
        skel.has_geometry = type_has_geometry(skel.type_name);
        skel.has_transform = false;
        skel.has_material = false;

        // Scan property names for hints
        auto prop_names = prim.property_names();
        for (const auto& name : prop_names) {
            if (name.find("xformOp") == 0 || name == "xformOpOrder") {
                skel.has_transform = true;
            }
            if (name == "material:binding") {
                skel.has_material = true;
            }
        }

        // Build child paths
        for (size_t i = 0; i < prim.child_count(); ++i) {
            const Prim* child = prim.child(i);
            if (child) {
                std::string child_path = skel.path + "/" + child->name();
                skel.child_paths.push_back(child_path);
            }
        }

        return skel;
    }

    // Recursively build skeletons for all prims
    void build_skeletons_recursive(const Prim& prim, const std::string& parent_path) {
        PrimSkeleton skel = build_skeleton(prim, parent_path);
        std::string path = skel.path;

        prim_skeletons_[path] = std::move(skel);
        prim_states_[path] = PrimLoadState::Skeleton;

        // Recurse to children
        for (size_t i = 0; i < prim.child_count(); ++i) {
            const Prim* child = prim.child(i);
            if (child) {
                build_skeletons_recursive(*child, path);
            }
        }
    }

    // Load geometry for a single prim
    Result<PrimGeometry> load_prim_geometry(const std::string& path, double time) {
        // Find the prim in stage
        auto prim_result = stage_->get_prim_at_path(Path(path));
        if (!prim_result) {
            return Error("Prim not found: " + path);
        }
        const Prim* prim = prim_result.value();
        if (!prim) {
            return Error("Prim is null: " + path);
        }

        auto skel_it = prim_skeletons_.find(path);
        if (skel_it == prim_skeletons_.end() || !skel_it->second.has_geometry) {
            return Error("Prim has no geometry: " + path);
        }

        // Convert to render mesh
        RenderConverterConfig config;
        config.time = time;
        config.triangulate = true;
        config.compute_normals = true;
        config.compute_tangents = true;

        RenderConverter converter;
        auto mesh_result = converter.convert_mesh(*prim, config);
        if (!mesh_result) {
            return Error("Failed to convert mesh: " + mesh_result.error().message);
        }

        PrimGeometry geom;
        geom.path = path;
        geom.mesh = std::move(mesh_result).value();

        // Copy data to transferable buffers
        if (!geom.mesh.positions.empty()) {
            geom.positions = geom.mesh.positions.data;
        }
        if (!geom.mesh.normals.empty()) {
            geom.normals = geom.mesh.normals.data;
        }
        if (!geom.mesh.texcoords0.empty()) {
            geom.texcoords = geom.mesh.texcoords0.data;
        }
        if (!geom.mesh.tangents.empty()) {
            geom.tangents = geom.mesh.tangents.data;
        }
        geom.indices = geom.mesh.indices;

        return geom;
    }

    // Process one item from the queue
    bool process_one() {
        if (load_queue_.empty()) return false;

        LoadRequest req = load_queue_.top();
        load_queue_.pop();
        queued_paths_.erase(req.prim_path);

        // Update state
        prim_states_[req.prim_path] = PrimLoadState::Loading;

        // Load geometry
        auto result = load_prim_geometry(req.prim_path, req.time);
        if (!result) {
            prim_states_[req.prim_path] = PrimLoadState::Error;
            return true;
        }

        // Add to ready queue
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            ready_prims_.push(std::move(result).value());
        }

        prim_states_[req.prim_path] = PrimLoadState::Ready;
        return true;
    }
};

// ============================================================================
// StreamingLoader Public Interface
// ============================================================================

StreamingLoader::StreamingLoader() : impl_(new Impl()) {
    impl_->asset_cache_ = std::make_shared<AssetCache>();
}

StreamingLoader::~StreamingLoader() = default;

void StreamingLoader::set_asset_cache(std::shared_ptr<AssetCache> cache) {
    impl_->asset_cache_ = cache;
}

void StreamingLoader::set_base_url(const std::string& url) {
    impl_->base_url_ = url;
}

void StreamingLoader::set_time_budget_ms(uint32_t ms) {
    impl_->time_budget_ms_ = ms;
}

Result<std::vector<PrimSkeleton>> StreamingLoader::parse_structure(
    const uint8_t* data, size_t size, const std::string& filename) {

    impl_->state_ = LoaderState::Parsing;
    impl_->prim_skeletons_.clear();
    impl_->prim_states_.clear();

    std::string ext = Impl::get_extension(filename);

    if (ext == ".usda" || ext == ".usd") {
        // Parse USDA
        std::string content(reinterpret_cast<const char*>(data), size);
        auto result = read_usda_string(content);
        if (!result.ok()) {
            impl_->state_ = LoaderState::Error;
            impl_->error_ = result.error_summary;
            return Error(result.error_summary);
        }
        impl_->stage_ = std::make_unique<Stage>(std::move(result.stage));
    }
    else if (ext == ".usdc") {
        // Parse USDC
        UsdcReader reader;
        auto read_result = reader.read(data, size);
        if (!read_result) {
            impl_->state_ = LoaderState::Error;
            impl_->error_ = read_result.error().message;
            return Error(read_result.error().message);
        }
        auto stage_result = reader.reconstruct_stage();
        if (!stage_result) {
            impl_->state_ = LoaderState::Error;
            impl_->error_ = stage_result.error().message;
            return Error(stage_result.error().message);
        }
        impl_->stage_ = std::make_unique<Stage>(std::move(stage_result).value());
    }
    else {
        impl_->state_ = LoaderState::Error;
        impl_->error_ = "Unknown file format: " + ext;
        return Error(impl_->error_);
    }

    // Build skeletons from stage
    for (size_t i = 0; i < impl_->stage_->root_prim_count(); ++i) {
        const Prim* root = impl_->stage_->root_prim(i);
        if (root) {
            impl_->build_skeletons_recursive(*root, "");
        }
    }

    // Collect results
    std::vector<PrimSkeleton> skeletons;
    skeletons.reserve(impl_->prim_skeletons_.size());
    for (const auto& [path, skel] : impl_->prim_skeletons_) {
        skeletons.push_back(skel);
    }

    impl_->state_ = LoaderState::Ready;
    return skeletons;
}

Result<std::vector<PrimSkeleton>> StreamingLoader::parse_usdz_structure(
    const uint8_t* data, size_t size) {

    impl_->state_ = LoaderState::Parsing;
    impl_->prim_skeletons_.clear();
    impl_->prim_states_.clear();

    // Open USDZ archive
    impl_->usdz_archive_ = std::make_unique<UsdzArchive>();
    auto open_result = impl_->usdz_archive_->open(data, size);
    if (!open_result) {
        impl_->state_ = LoaderState::Error;
        impl_->error_ = open_result.error().message;
        return Error(impl_->error_);
    }

    // Load stage from archive
    auto stage_result = impl_->usdz_archive_->load_stage();
    if (!stage_result) {
        impl_->state_ = LoaderState::Error;
        impl_->error_ = stage_result.error().message;
        return Error(impl_->error_);
    }

    impl_->stage_ = std::make_unique<Stage>(std::move(stage_result).value());

    // Build skeletons
    for (size_t i = 0; i < impl_->stage_->root_prim_count(); ++i) {
        const Prim* root = impl_->stage_->root_prim(i);
        if (root) {
            impl_->build_skeletons_recursive(*root, "");
        }
    }

    // Collect results
    std::vector<PrimSkeleton> skeletons;
    skeletons.reserve(impl_->prim_skeletons_.size());
    for (const auto& [path, skel] : impl_->prim_skeletons_) {
        skeletons.push_back(skel);
    }

    impl_->state_ = LoaderState::Ready;
    return skeletons;
}

void StreamingLoader::request_load(const LoadRequest& request) {
    if (impl_->state_ != LoaderState::Ready) return;

    // Check if already queued or loaded
    if (impl_->queued_paths_.count(request.prim_path)) return;

    auto state_it = impl_->prim_states_.find(request.prim_path);
    if (state_it == impl_->prim_states_.end()) return;
    if (state_it->second != PrimLoadState::Skeleton &&
        state_it->second != PrimLoadState::Error) return;

    impl_->load_queue_.push(request);
    impl_->queued_paths_.insert(request.prim_path);
    impl_->prim_states_[request.prim_path] = PrimLoadState::Queued;
}

void StreamingLoader::request_load_batch(const std::vector<LoadRequest>& requests) {
    for (const auto& req : requests) {
        request_load(req);
    }
}

void StreamingLoader::set_priority(const std::string& prim_path, LoadPriority priority) {
    // Note: std::priority_queue doesn't support update-in-place
    // For simplicity, we'd need to rebuild the queue or use a different data structure
    // For now, this is a no-op placeholder
    (void)prim_path;
    (void)priority;
}

void StreamingLoader::cancel_load(const std::string& prim_path) {
    impl_->queued_paths_.erase(prim_path);
    auto state_it = impl_->prim_states_.find(prim_path);
    if (state_it != impl_->prim_states_.end() &&
        state_it->second == PrimLoadState::Queued) {
        state_it->second = PrimLoadState::Skeleton;
    }
}

void StreamingLoader::cancel_all() {
    while (!impl_->load_queue_.empty()) {
        impl_->load_queue_.pop();
    }
    impl_->queued_paths_.clear();

    for (auto& [path, state] : impl_->prim_states_) {
        if (state == PrimLoadState::Queued) {
            state = PrimLoadState::Skeleton;
        }
    }
}

uint32_t StreamingLoader::process_queue(uint32_t max_count) {
    if (impl_->state_ != LoaderState::Ready) return 0;

    auto start = std::chrono::steady_clock::now();
    uint32_t processed = 0;

    while (!impl_->load_queue_.empty()) {
        // Check count limit
        if (max_count > 0 && processed >= max_count) break;

        // Check time budget
        if (max_count == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
            if (elapsed.count() >= impl_->time_budget_ms_) break;
        }

        if (impl_->process_one()) {
            processed++;
        }
    }

    return processed;
}

void StreamingLoader::provide_asset(const std::string& path, std::vector<uint8_t> data) {
    impl_->asset_cache_->put(path, std::move(data));

    // TODO: Resume prims waiting for this asset
}

void StreamingLoader::fail_asset(const std::string& path, const std::string& error) {
    impl_->asset_errors_[path] = error;

    // TODO: Mark prims waiting for this asset as failed
}

std::vector<AssetRequest> StreamingLoader::pending_assets() const {
    return impl_->pending_assets_;
}

LoaderState StreamingLoader::state() const {
    return impl_->state_;
}

PrimLoadState StreamingLoader::prim_state(const std::string& path) const {
    auto it = impl_->prim_states_.find(path);
    return it != impl_->prim_states_.end() ? it->second : PrimLoadState::Skeleton;
}

const std::string& StreamingLoader::error() const {
    return impl_->error_;
}

size_t StreamingLoader::pending_count() const {
    return impl_->load_queue_.size();
}

size_t StreamingLoader::waiting_asset_count() const {
    return impl_->prims_waiting_assets_.size();
}

bool StreamingLoader::has_ready_prims() const {
    std::lock_guard<std::mutex> lock(impl_->ready_mutex_);
    return !impl_->ready_prims_.empty();
}

std::optional<PrimGeometry> StreamingLoader::take_ready_prim() {
    std::lock_guard<std::mutex> lock(impl_->ready_mutex_);
    if (impl_->ready_prims_.empty()) {
        return std::nullopt;
    }

    PrimGeometry geom = std::move(impl_->ready_prims_.front());
    impl_->ready_prims_.pop();
    return geom;
}

std::vector<PrimGeometry> StreamingLoader::take_all_ready_prims() {
    std::lock_guard<std::mutex> lock(impl_->ready_mutex_);

    std::vector<PrimGeometry> result;
    result.reserve(impl_->ready_prims_.size());

    while (!impl_->ready_prims_.empty()) {
        result.push_back(std::move(impl_->ready_prims_.front()));
        impl_->ready_prims_.pop();
    }

    return result;
}

const Stage* StreamingLoader::stage() const {
    return impl_->stage_.get();
}

size_t StreamingLoader::prim_count() const {
    return impl_->prim_skeletons_.size();
}

// ============================================================================
// Priority Calculation Helper
// ============================================================================

LoadPriority calculate_priority(
    const float* prim_bounds,
    const float* camera_pos,
    const float* camera_dir,
    const float* frustum_planes) {

    if (!prim_bounds || !camera_pos) {
        return LoadPriority::Normal;
    }

    // Calculate prim center
    float center[3] = {
        (prim_bounds[0] + prim_bounds[3]) * 0.5f,
        (prim_bounds[1] + prim_bounds[4]) * 0.5f,
        (prim_bounds[2] + prim_bounds[5]) * 0.5f
    };

    // Distance from camera
    float dx = center[0] - camera_pos[0];
    float dy = center[1] - camera_pos[1];
    float dz = center[2] - camera_pos[2];
    float dist_sq = dx * dx + dy * dy + dz * dz;

    // Check if in front of camera
    if (camera_dir) {
        float dot = dx * camera_dir[0] + dy * camera_dir[1] + dz * camera_dir[2];
        if (dot < 0) {
            // Behind camera
            return LoadPriority::Low;
        }
    }

    // Frustum culling (if planes provided)
    if (frustum_planes) {
        // Simple sphere vs planes test using bounding sphere
        float radius_sq = 0;
        float sx = (prim_bounds[3] - prim_bounds[0]) * 0.5f;
        float sy = (prim_bounds[4] - prim_bounds[1]) * 0.5f;
        float sz = (prim_bounds[5] - prim_bounds[2]) * 0.5f;
        radius_sq = sx * sx + sy * sy + sz * sz;
        float radius = std::sqrt(radius_sq);

        for (int i = 0; i < 6; ++i) {
            const float* plane = frustum_planes + i * 4;
            float d = plane[0] * center[0] + plane[1] * center[1] +
                      plane[2] * center[2] + plane[3];
            if (d < -radius) {
                // Outside frustum
                return LoadPriority::Deferred;
            }
        }
    }

    // Priority based on distance
    if (dist_sq < 100.0f) {  // Within 10 units
        return LoadPriority::Immediate;
    } else if (dist_sq < 1000.0f) {  // Within ~31 units
        return LoadPriority::High;
    } else if (dist_sq < 10000.0f) {  // Within 100 units
        return LoadPriority::Normal;
    }

    return LoadPriority::Low;
}

// ============================================================================
// Emscripten Worker Integration
// ============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

namespace {
    std::unique_ptr<StreamingLoader> g_worker_loader;
    std::queue<std::pair<WorkerMessageType, std::vector<uint8_t>>> g_outgoing_messages;
}

void worker_init() {
    g_worker_loader = std::make_unique<StreamingLoader>();
}

void worker_handle_message(MainMessageType msg_type, const uint8_t* data, size_t size) {
    if (!g_worker_loader) return;

    switch (msg_type) {
        case MainMessageType::LoadFile: {
            // Data format: [filename_len:4][filename][file_data]
            if (size < 4) return;

            uint32_t filename_len = *reinterpret_cast<const uint32_t*>(data);
            if (size < 4 + filename_len) return;

            std::string filename(reinterpret_cast<const char*>(data + 4), filename_len);
            const uint8_t* file_data = data + 4 + filename_len;
            size_t file_size = size - 4 - filename_len;

            // Check if USDZ
            if (UsdzArchive::is_usdz(file_data, file_size)) {
                auto result = g_worker_loader->parse_usdz_structure(file_data, file_size);
                // TODO: Send STRUCTURE_READY message
            } else {
                auto result = g_worker_loader->parse_structure(file_data, file_size, filename);
                // TODO: Send STRUCTURE_READY message
            }
            break;
        }

        case MainMessageType::LoadPrim: {
            // Data format: [path_len:4][path][priority:1][time:8]
            if (size < 13) return;

            uint32_t path_len = *reinterpret_cast<const uint32_t*>(data);
            std::string path(reinterpret_cast<const char*>(data + 4), path_len);
            uint8_t priority = data[4 + path_len];
            double time = *reinterpret_cast<const double*>(data + 5 + path_len);

            LoadRequest req;
            req.prim_path = path;
            req.priority = static_cast<LoadPriority>(priority);
            req.time = time;

            g_worker_loader->request_load(req);
            break;
        }

        case MainMessageType::ProvidAsset: {
            // Data format: [path_len:4][path][asset_data]
            if (size < 4) return;

            uint32_t path_len = *reinterpret_cast<const uint32_t*>(data);
            std::string path(reinterpret_cast<const char*>(data + 4), path_len);
            std::vector<uint8_t> asset_data(data + 4 + path_len, data + size);

            g_worker_loader->provide_asset(path, std::move(asset_data));
            break;
        }

        case MainMessageType::SetPriority: {
            // Data format: [path_len:4][path][priority:1]
            if (size < 5) return;

            uint32_t path_len = *reinterpret_cast<const uint32_t*>(data);
            std::string path(reinterpret_cast<const char*>(data + 4), path_len);
            uint8_t priority = data[4 + path_len];

            g_worker_loader->set_priority(path, static_cast<LoadPriority>(priority));
            break;
        }

        case MainMessageType::Cancel: {
            // Data format: [path_len:4][path] or empty for cancel all
            if (size == 0) {
                g_worker_loader->cancel_all();
            } else if (size >= 4) {
                uint32_t path_len = *reinterpret_cast<const uint32_t*>(data);
                std::string path(reinterpret_cast<const char*>(data + 4), path_len);
                g_worker_loader->cancel_load(path);
            }
            break;
        }
    }
}

bool worker_poll_message(WorkerMessageType* out_type,
                         uint8_t** out_data,
                         size_t* out_size) {
    // Process queue first
    if (g_worker_loader) {
        g_worker_loader->process_queue(1);  // Process one item per poll

        // Check for ready prims
        while (g_worker_loader->has_ready_prims()) {
            auto geom = g_worker_loader->take_ready_prim();
            if (geom) {
                // Serialize geometry to message
                // TODO: Implement serialization
            }
        }
    }

    if (g_outgoing_messages.empty()) return false;

    auto& msg = g_outgoing_messages.front();
    *out_type = msg.first;
    *out_size = msg.second.size();
    *out_data = new uint8_t[*out_size];
    std::memcpy(*out_data, msg.second.data(), *out_size);

    g_outgoing_messages.pop();
    return true;
}

#endif // __EMSCRIPTEN__

} // namespace v1
} // namespace lightusd
