// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDZ Archive Reader implementation

#include "lightusd/usdz_archive.hh"
#include "lightusd/stage.hh"
#include "lightusd/usda_reader.hh"
#include "lightusd/usdc_reader.hh"
#include "lightusd/render_data.hh"

#include <algorithm>
#include <cstring>

namespace lightusd {
namespace v1 {

// ============================================================================
// ZIP format constants and structures
// ============================================================================

namespace {

// ZIP magic signatures
constexpr uint32_t kLocalFileHeaderSig = 0x04034b50;  // "PK\x03\x04"
constexpr uint32_t kCentralDirSig = 0x02014b50;       // "PK\x01\x02"
constexpr uint32_t kEndOfCentralDirSig = 0x06054b50;  // "PK\x05\x06"

// Minimum sizes
constexpr size_t kLocalFileHeaderSize = 30;
constexpr size_t kCentralDirHeaderSize = 46;
constexpr size_t kEndOfCentralDirSize = 22;

// USDZ requires 64-byte alignment
constexpr size_t kUsdzAlignment = 64;

// Read little-endian integers
inline uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Get file extension (lowercase)
std::string get_extension(const std::string& filename) {
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = filename.substr(dot);
    for (auto& c : ext) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    return ext;
}

// Normalize path (remove ./ prefix, convert backslash)
std::string normalize_asset_path(const std::string& path) {
    std::string result = path;

    // Convert backslashes to forward slashes
    for (auto& c : result) {
        if (c == '\\') c = '/';
    }

    // Remove leading ./
    while (result.size() >= 2 && result[0] == '.' && result[1] == '/') {
        result = result.substr(2);
    }

    return result;
}

} // anonymous namespace

// ============================================================================
// UsdzArchive::Impl
// ============================================================================

class UsdzArchive::Impl {
public:
    std::vector<uint8_t> data_;
    std::map<std::string, UsdzAssetInfo> assets_;
    std::string root_layer_;
    bool is_open_ = false;

    size_t max_archive_size_ = 1024 * 1024 * 1024;  // 1 GB
    size_t max_asset_count_ = 10000;

    Result<void> parse_zip() {
        if (data_.size() < kLocalFileHeaderSize) {
            return Error("File too small to be valid ZIP");
        }

        // Verify ZIP signature
        if (read_u32(data_.data()) != kLocalFileHeaderSig) {
            return Error("Invalid ZIP signature (not PK\\x03\\x04)");
        }

        size_t offset = 0;
        std::string first_usdc;
        std::string first_usda;

        // Parse local file headers
        while (offset + kLocalFileHeaderSize <= data_.size()) {
            uint32_t sig = read_u32(data_.data() + offset);

            // Check for end of local file headers
            if (sig == kCentralDirSig || sig == kEndOfCentralDirSig) {
                break;
            }

            if (sig != kLocalFileHeaderSig) {
                return Error("Invalid local file header signature at offset " +
                            std::to_string(offset));
            }

            // Parse local file header
            const uint8_t* header = data_.data() + offset;

            uint16_t compression = read_u16(header + 8);
            uint32_t crc32_val = read_u32(header + 14);
            uint32_t compressed_size = read_u32(header + 18);
            uint32_t uncompressed_size = read_u32(header + 22);
            uint16_t filename_len = read_u16(header + 26);
            uint16_t extra_len = read_u16(header + 28);

            // USDZ requires uncompressed data
            if (compression != 0) {
                return Error("USDZ requires uncompressed ZIP entries (compression method must be 0, got " +
                            std::to_string(compression) + ")");
            }

            // Sanity checks
            if (compressed_size != uncompressed_size) {
                return Error("Compressed and uncompressed sizes must match for uncompressed entries");
            }

            // Calculate data offset
            size_t header_end = offset + kLocalFileHeaderSize + filename_len + extra_len;
            size_t data_offset = header_end;

            // USDZ requires 64-byte alignment
            if ((data_offset % kUsdzAlignment) != 0) {
                // Alignment is via extra field, so this might be okay
                // Just warn but don't fail
            }

            // Validate bounds
            if (data_offset + uncompressed_size > data_.size()) {
                return Error("Asset data extends beyond archive size");
            }

            // Extract filename
            if (offset + kLocalFileHeaderSize + filename_len > data_.size()) {
                return Error("Filename extends beyond archive size");
            }
            std::string filename(reinterpret_cast<const char*>(header + 30), filename_len);
            filename = normalize_asset_path(filename);

            // Skip directories (filenames ending with /)
            if (!filename.empty() && filename.back() != '/') {
                // Check asset count limit
                if (assets_.size() >= max_asset_count_) {
                    return Error("Too many assets in archive (limit: " +
                                std::to_string(max_asset_count_) + ")");
                }

                UsdzAssetInfo info;
                info.filename = filename;
                info.offset = data_offset;
                info.size = uncompressed_size;
                info.crc32 = crc32_val;

                assets_[filename] = info;

                // Track root layer candidates
                std::string ext = get_extension(filename);
                if (ext == ".usdc" && first_usdc.empty()) {
                    first_usdc = filename;
                } else if (ext == ".usda" && first_usda.empty()) {
                    first_usda = filename;
                }
            }

            // Move to next entry
            offset = data_offset + uncompressed_size;
        }

        // Determine root layer (prefer USDC)
        if (!first_usdc.empty()) {
            root_layer_ = first_usdc;
        } else if (!first_usda.empty()) {
            root_layer_ = first_usda;
        }

        is_open_ = true;
        return {};
    }
};

// ============================================================================
// UsdzArchive public interface
// ============================================================================

UsdzArchive::UsdzArchive() : impl_(new Impl()) {}
UsdzArchive::~UsdzArchive() = default;
UsdzArchive::UsdzArchive(UsdzArchive&&) noexcept = default;
UsdzArchive& UsdzArchive::operator=(UsdzArchive&&) noexcept = default;

bool UsdzArchive::is_usdz(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    // Check for ZIP signature "PK\x03\x04"
    return data[0] == 0x50 && data[1] == 0x4b &&
           data[2] == 0x03 && data[3] == 0x04;
}

Result<void> UsdzArchive::open(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return Error("Empty data");
    }

    if (size > impl_->max_archive_size_) {
        return Error("Archive exceeds maximum size limit (" +
                    std::to_string(impl_->max_archive_size_ / (1024*1024)) + " MB)");
    }

    if (!is_usdz(data, size)) {
        return Error("Not a valid USDZ file (invalid ZIP signature)");
    }

    // Copy data
    impl_->data_.assign(data, data + size);
    impl_->assets_.clear();
    impl_->root_layer_.clear();
    impl_->is_open_ = false;

    return impl_->parse_zip();
}

Result<void> UsdzArchive::open(std::vector<uint8_t>&& data) {
    if (data.empty()) {
        return Error("Empty data");
    }

    if (data.size() > impl_->max_archive_size_) {
        return Error("Archive exceeds maximum size limit");
    }

    if (!is_usdz(data.data(), data.size())) {
        return Error("Not a valid USDZ file (invalid ZIP signature)");
    }

    // Take ownership
    impl_->data_ = std::move(data);
    impl_->assets_.clear();
    impl_->root_layer_.clear();
    impl_->is_open_ = false;

    return impl_->parse_zip();
}

bool UsdzArchive::is_open() const {
    return impl_->is_open_;
}

void UsdzArchive::close() {
    impl_->data_.clear();
    impl_->assets_.clear();
    impl_->root_layer_.clear();
    impl_->is_open_ = false;
}

size_t UsdzArchive::archive_size() const {
    return impl_->data_.size();
}

size_t UsdzArchive::asset_count() const {
    return impl_->assets_.size();
}

std::vector<std::string> UsdzArchive::asset_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->assets_.size());
    for (const auto& [name, info] : impl_->assets_) {
        names.push_back(name);
    }
    return names;
}

bool UsdzArchive::has_asset(const std::string& name) const {
    std::string normalized = normalize_asset_path(name);
    return impl_->assets_.count(normalized) > 0;
}

const UsdzAssetInfo* UsdzArchive::get_asset_info(const std::string& name) const {
    std::string normalized = normalize_asset_path(name);
    auto it = impl_->assets_.find(normalized);
    return it != impl_->assets_.end() ? &it->second : nullptr;
}

const std::map<std::string, UsdzAssetInfo>& UsdzArchive::assets() const {
    return impl_->assets_;
}

Result<std::vector<uint8_t>> UsdzArchive::read_asset(const std::string& name) const {
    const uint8_t* ptr = nullptr;
    size_t size = 0;
    if (!get_asset_ptr(name, &ptr, &size)) {
        return Error("Asset not found: " + name);
    }

    return std::vector<uint8_t>(ptr, ptr + size);
}

bool UsdzArchive::get_asset_ptr(const std::string& name,
                                 const uint8_t** out_data,
                                 size_t* out_size) const {
    std::string normalized = normalize_asset_path(name);
    auto it = impl_->assets_.find(normalized);
    if (it == impl_->assets_.end()) {
        return false;
    }

    const UsdzAssetInfo& info = it->second;
    *out_data = impl_->data_.data() + info.offset;
    *out_size = info.size;
    return true;
}

std::string UsdzArchive::root_layer_name() const {
    return impl_->root_layer_;
}

bool UsdzArchive::has_root_layer() const {
    return !impl_->root_layer_.empty();
}

Result<Stage> UsdzArchive::load_stage() const {
    if (!is_open()) {
        return Error("Archive not open");
    }

    if (!has_root_layer()) {
        return Error("No root USD layer found in archive");
    }

    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!get_asset_ptr(impl_->root_layer_, &data, &size)) {
        return Error("Failed to read root layer: " + impl_->root_layer_);
    }

    std::string ext = get_extension(impl_->root_layer_);

    if (ext == ".usdc") {
        // Parse USDC
        UsdcReader reader;
        auto read_result = reader.read(data, size);
        if (!read_result) {
            return Error("Failed to parse USDC: " + read_result.error().message);
        }
        auto stage_result = reader.reconstruct_stage();
        if (!stage_result) {
            return Error("Failed to reconstruct stage: " + stage_result.error().message);
        }
        return std::move(stage_result).value();
    } else if (ext == ".usda" || ext == ".usd") {
        // Parse USDA
        std::string content(reinterpret_cast<const char*>(data), size);
        auto result = read_usda_string(content);
        if (!result.ok()) {
            return Error("Failed to parse USDA: " + result.error_summary);
        }
        return std::move(result.stage);
    }

    return Error("Unsupported root layer format: " + ext);
}

void UsdzArchive::set_max_archive_size(size_t bytes) {
    impl_->max_archive_size_ = bytes;
}

size_t UsdzArchive::max_archive_size() const {
    return impl_->max_archive_size_;
}

void UsdzArchive::set_max_asset_count(size_t count) {
    impl_->max_asset_count_ = count;
}

size_t UsdzArchive::max_asset_count() const {
    return impl_->max_asset_count_;
}

// ============================================================================
// UsdzAssetHandler callbacks (TinyUSDZ compatible)
// ============================================================================

int UsdzAssetHandler::resolve(const char* asset_path,
                               const std::vector<std::string>& /*search_paths*/,
                               std::string* resolved_path,
                               std::string* error,
                               void* userdata) {
    if (!userdata || !asset_path || !resolved_path) {
        if (error) *error = "Invalid parameters";
        return -2;
    }

    auto* archive = static_cast<UsdzArchive*>(userdata);
    std::string path = normalize_asset_path(asset_path);

    if (archive->has_asset(path)) {
        *resolved_path = path;
        return 0;  // Success
    }

    if (error) *error = "Asset not found in USDZ: " + path;
    return -1;  // Not found
}

int UsdzAssetHandler::size(const char* resolved_path,
                           uint64_t* size_bytes,
                           std::string* error,
                           void* userdata) {
    if (!userdata || !resolved_path || !size_bytes) {
        if (error) *error = "Invalid parameters";
        return -2;
    }

    auto* archive = static_cast<UsdzArchive*>(userdata);
    const UsdzAssetInfo* info = archive->get_asset_info(resolved_path);

    if (!info) {
        if (error) *error = "Asset not found: " + std::string(resolved_path);
        return -1;
    }

    *size_bytes = info->size;
    return 0;
}

int UsdzAssetHandler::read(const char* resolved_path,
                           uint64_t requested_bytes,
                           uint8_t* buffer,
                           uint64_t* bytes_read,
                           std::string* error,
                           void* userdata) {
    if (!userdata || !resolved_path || !buffer || !bytes_read) {
        if (error) *error = "Invalid parameters";
        return -2;
    }

    auto* archive = static_cast<UsdzArchive*>(userdata);
    const uint8_t* data = nullptr;
    size_t size = 0;

    if (!archive->get_asset_ptr(resolved_path, &data, &size)) {
        if (error) *error = "Asset not found: " + std::string(resolved_path);
        return -1;
    }

    size_t to_read = std::min(static_cast<size_t>(requested_bytes), size);
    std::memcpy(buffer, data, to_read);
    *bytes_read = to_read;
    return 0;
}

// ============================================================================
// High-level USDZ loading
// ============================================================================

UsdzLoadResult load_usdz(const uint8_t* data, size_t size) {
    UsdzLoadResult result;

    // Open archive
    auto open_result = result.archive.open(data, size);
    if (!open_result) {
        result.error = open_result.error().message;
        return result;
    }

    // Load stage
    auto stage_result = result.archive.load_stage();
    if (!stage_result) {
        result.error = stage_result.error().message;
        return result;
    }

    result.stage = std::move(stage_result).value();
    result.ok = true;
    return result;
}

// ============================================================================
// USDZ to RenderScene conversion
// ============================================================================

UsdzRenderResult usdz_to_render_scene(const uint8_t* data, size_t size,
                                       const UsdzRenderConfig& config) {
    UsdzRenderResult result;

    // Load USDZ
    UsdzLoadResult load_result = load_usdz(data, size);
    if (!load_result.ok) {
        result.error = load_result.error;
        return result;
    }

    result.warning = load_result.warning;

    // Configure converter
    RenderConverterConfig render_config;
    render_config.time = config.time;
    render_config.triangulate = config.triangulate;
    render_config.compute_normals = config.compute_normals;
    render_config.compute_tangents = config.compute_tangents;

    // Set up texture loading from archive
    if (config.texture_mode != UsdzRenderConfig::TextureMode::Skip) {
        render_config.texture_decode =
            (config.texture_mode == UsdzRenderConfig::TextureMode::FileDataOnly)
                ? TextureDecodeMode::Browser
                : TextureDecodeMode::Native;
        render_config.load_textures = true;
    } else {
        render_config.load_textures = false;
    }

    // Convert
    RenderConverter converter;
    auto convert_result = converter.convert(load_result.stage, render_config);
    if (!convert_result) {
        result.error = convert_result.error().message;
        return result;
    }

    result.scene = std::move(convert_result).value();
    result.ok = true;
    return result;
}

} // namespace v1
} // namespace lightusd
