// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDZ Archive Reader
// Parses USDZ files (uncompressed ZIP archives with 64-byte alignment)

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/stage.hh"
#include "lightusd/render_data.hh"

namespace lightusd {
namespace v1 {

/// Information about a single asset within a USDZ archive
struct UsdzAssetInfo {
    std::string filename;       // Filename within archive (e.g., "textures/diffuse.png")
    size_t offset = 0;          // Byte offset in archive
    size_t size = 0;            // Uncompressed size in bytes
    uint32_t crc32 = 0;         // CRC32 checksum (optional validation)
};

/// USDZ Archive - manages reading from USDZ (uncompressed ZIP) files
///
/// USDZ format requirements:
/// - Must be valid ZIP with uncompressed entries (compression method = 0)
/// - Assets must be aligned to 64-byte boundaries
/// - Must contain at least one .usdc or .usda file as root layer
class UsdzArchive {
public:
    UsdzArchive();
    ~UsdzArchive();

    // Non-copyable, movable
    UsdzArchive(const UsdzArchive&) = delete;
    UsdzArchive& operator=(const UsdzArchive&) = delete;
    UsdzArchive(UsdzArchive&&) noexcept;
    UsdzArchive& operator=(UsdzArchive&&) noexcept;

    /// Check if data is a valid USDZ archive (checks ZIP magic bytes)
    /// @param data Pointer to data
    /// @param size Size in bytes
    /// @return true if data starts with ZIP signature
    static bool is_usdz(const uint8_t* data, size_t size);

    /// Check if data is a valid USDZ archive
    static bool is_usdz(const std::vector<uint8_t>& data) {
        return is_usdz(data.data(), data.size());
    }

    /// Open USDZ archive from memory
    /// @param data USDZ file data (will be copied)
    /// @param size Size in bytes
    /// @return Success or error with description
    Result<void> open(const uint8_t* data, size_t size);

    /// Open USDZ archive from memory (vector version)
    Result<void> open(const std::vector<uint8_t>& data) {
        return open(data.data(), data.size());
    }

    /// Open USDZ archive from memory (takes ownership, avoids copy)
    Result<void> open(std::vector<uint8_t>&& data);

    /// Check if archive is open
    bool is_open() const;

    /// Close archive and release memory
    void close();

    /// Get total archive size in bytes
    size_t archive_size() const;

    // === Asset enumeration ===

    /// Get number of assets in archive
    size_t asset_count() const;

    /// Get list of all asset filenames
    std::vector<std::string> asset_names() const;

    /// Check if asset exists
    bool has_asset(const std::string& name) const;

    /// Get asset info by name
    const UsdzAssetInfo* get_asset_info(const std::string& name) const;

    /// Get all asset infos
    const std::map<std::string, UsdzAssetInfo>& assets() const;

    // === Asset reading ===

    /// Read asset data by name
    /// @param name Asset filename (e.g., "model.usdc" or "textures/diffuse.png")
    /// @return Asset data or error
    Result<std::vector<uint8_t>> read_asset(const std::string& name) const;

    /// Get raw pointer to asset data (zero-copy access)
    /// @param name Asset filename
    /// @param out_data Output pointer to data
    /// @param out_size Output size in bytes
    /// @return true if asset exists
    bool get_asset_ptr(const std::string& name,
                       const uint8_t** out_data,
                       size_t* out_size) const;

    // === Root layer detection ===

    /// Get the root USD layer filename (first .usdc or .usda found)
    /// Prefers .usdc over .usda if both exist
    std::string root_layer_name() const;

    /// Check if archive has a valid root layer
    bool has_root_layer() const;

    // === Convenience loading ===

    /// Load the root USD layer as a Stage
    /// @return Stage or error
    Result<Stage> load_stage() const;

    // === Memory limits (security) ===

    /// Set maximum allowed archive size (default: 1GB)
    void set_max_archive_size(size_t bytes);
    size_t max_archive_size() const;

    /// Set maximum number of assets (default: 10000)
    void set_max_asset_count(size_t count);
    size_t max_asset_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// USDZ Asset Resolution
// ============================================================================

/// Asset resolution callbacks for USDZ archives
/// Compatible with TinyUSDZ's AssetResolutionHandler interface
struct UsdzAssetHandler {
    /// Resolve asset path within USDZ
    /// @return 0 on success, -1 if not found
    static int resolve(const char* asset_path,
                      const std::vector<std::string>& search_paths,
                      std::string* resolved_path,
                      std::string* error,
                      void* userdata);

    /// Get asset size
    /// @return 0 on success, negative on error
    static int size(const char* resolved_path,
                   uint64_t* size_bytes,
                   std::string* error,
                   void* userdata);

    /// Read asset data
    /// @return 0 on success, negative on error
    static int read(const char* resolved_path,
                   uint64_t requested_bytes,
                   uint8_t* buffer,
                   uint64_t* bytes_read,
                   std::string* error,
                   void* userdata);
};

// ============================================================================
// High-level USDZ loading API
// ============================================================================

/// Result of loading a USDZ file
struct UsdzLoadResult {
    bool ok = false;
    Stage stage;
    UsdzArchive archive;  // Keep alive for texture access
    std::string error;
    std::string warning;
};

/// Load USDZ from memory buffer
/// @param data USDZ file data
/// @param size Size in bytes
/// @return Load result with Stage and archive
UsdzLoadResult load_usdz(const uint8_t* data, size_t size);

/// Load USDZ from memory buffer (vector version)
inline UsdzLoadResult load_usdz(const std::vector<uint8_t>& data) {
    return load_usdz(data.data(), data.size());
}

// ============================================================================
// USDZ + RenderScene integration
// ============================================================================

/// Configuration for USDZ to RenderScene conversion
struct UsdzRenderConfig {
    double time = 0.0;
    bool triangulate = true;
    bool compute_normals = true;
    bool compute_tangents = true;

    // Texture handling
    enum class TextureMode {
        FileDataOnly,    // Keep raw file bytes for browser decode (default)
        DecodeInWasm,    // Decode PNG/JPG in WASM (slower, more memory)
        Skip,            // Don't load textures
    };
    TextureMode texture_mode = TextureMode::FileDataOnly;
};

/// Result of converting USDZ to RenderScene
struct UsdzRenderResult {
    bool ok = false;
    RenderScene scene;
    std::string error;
    std::string warning;
};

/// Convert USDZ directly to RenderScene
/// Handles texture loading from the archive automatically
/// @param data USDZ file data
/// @param size Size in bytes
/// @param config Conversion configuration
/// @return RenderScene or error
UsdzRenderResult usdz_to_render_scene(const uint8_t* data, size_t size,
                                       const UsdzRenderConfig& config = {});

inline UsdzRenderResult usdz_to_render_scene(const std::vector<uint8_t>& data,
                                              const UsdzRenderConfig& config = {}) {
    return usdz_to_render_scene(data.data(), data.size(), config);
}

} // namespace v1
} // namespace lightusd
