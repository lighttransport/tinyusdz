// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightEXR - Minimal, secure, portable OpenEXR loader
//
// Features:
// - Secure parsing with bounds checking and memory limits
// - Support for scanline images (tiles not yet supported)
// - Compression: None, RLE, ZIP (single scanline), ZIPS
// - Pixel types: Half (16-bit float), Float (32-bit), UInt (32-bit)
// - Standard channels: R, G, B, A, Y (luminance)
//
// Limitations (for minimal implementation):
// - No tile support (scanline only)
// - No deep data support
// - No multipart support
// - Limited compression support
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <map>

namespace lightusd {
namespace v1 {
namespace lightexr {

// ============================================================================
// Constants
// ============================================================================

static constexpr uint32_t kMagicNumber = 20000630;  // EXR magic number
static constexpr uint32_t kVersionNumber = 2;        // EXR version

// Maximum limits for security
static constexpr size_t kMaxImageWidth = 65536;
static constexpr size_t kMaxImageHeight = 65536;
static constexpr size_t kMaxChannels = 64;
static constexpr size_t kMaxAttributeSize = 16 * 1024 * 1024;  // 16MB
static constexpr size_t kMaxMemoryUsage = 2ULL * 1024 * 1024 * 1024;  // 2GB

// ============================================================================
// Enums
// ============================================================================

/// Pixel data type
enum class PixelType : uint32_t {
    UInt = 0,    // 32-bit unsigned integer
    Half = 1,    // 16-bit floating point
    Float = 2,   // 32-bit floating point
};

/// Compression method
enum class Compression : uint8_t {
    None = 0,     // No compression
    RLE = 1,      // Run-length encoding
    ZIPS = 2,     // ZIP single scanline
    ZIP = 3,      // ZIP 16 scanlines
    PIZ = 4,      // PIZ wavelet compression (not supported)
    PXR24 = 5,    // Lossy 24-bit float (not supported)
    B44 = 6,      // Lossy 4x4 block (not supported)
    B44A = 7,     // Lossy 4x4 with alpha (not supported)
    DWAA = 8,     // Lossy DCT (not supported)
    DWAB = 9,     // Lossy DCT 256 scanlines (not supported)
};

/// Line order
enum class LineOrder : uint8_t {
    IncreasingY = 0,  // Top to bottom
    DecreasingY = 1,  // Bottom to top
    RandomY = 2,      // Random access (tiles)
};

// ============================================================================
// Channel
// ============================================================================

/// Channel description
struct Channel {
    std::string name;           // Channel name (R, G, B, A, Y, etc.)
    PixelType pixel_type;       // Data type
    uint32_t x_sampling;        // X subsampling
    uint32_t y_sampling;        // Y subsampling
    bool p_linear;              // Perceptually linear

    Channel() : pixel_type(PixelType::Half), x_sampling(1), y_sampling(1), p_linear(false) {}

    /// Get bytes per pixel for this channel
    size_t bytes_per_pixel() const {
        switch (pixel_type) {
            case PixelType::UInt: return 4;
            case PixelType::Half: return 2;
            case PixelType::Float: return 4;
        }
        return 0;
    }
};

// ============================================================================
// Box2i - Integer bounding box
// ============================================================================

struct Box2i {
    int32_t x_min, y_min;
    int32_t x_max, y_max;

    Box2i() : x_min(0), y_min(0), x_max(0), y_max(0) {}
    Box2i(int32_t xmin, int32_t ymin, int32_t xmax, int32_t ymax)
        : x_min(xmin), y_min(ymin), x_max(xmax), y_max(ymax) {}

    int32_t width() const { return x_max - x_min + 1; }
    int32_t height() const { return y_max - y_min + 1; }
};

// ============================================================================
// V2f - 2D float vector
// ============================================================================

struct V2f {
    float x, y;
    V2f() : x(0), y(0) {}
    V2f(float x_, float y_) : x(x_), y(y_) {}
};

// ============================================================================
// Chromaticities
// ============================================================================

struct Chromaticities {
    V2f red, green, blue, white;

    Chromaticities() {
        // Default: Rec. 709 / sRGB primaries
        red = V2f(0.6400f, 0.3300f);
        green = V2f(0.3000f, 0.6000f);
        blue = V2f(0.1500f, 0.0600f);
        white = V2f(0.3127f, 0.3290f);  // D65
    }
};

// ============================================================================
// EXRHeader
// ============================================================================

/// EXR file header information
struct EXRHeader {
    // Required attributes
    Box2i data_window;
    Box2i display_window;
    std::vector<Channel> channels;
    Compression compression;
    LineOrder line_order;
    float pixel_aspect_ratio;
    V2f screen_window_center;
    float screen_window_width;

    // Optional attributes
    Chromaticities chromaticities;
    bool has_chromaticities;
    std::string owner;
    std::string comments;
    float exposure_time;  // In seconds
    bool has_exposure_time;

    // Version flags
    bool is_tiled;
    bool has_long_names;
    bool is_deep;
    bool is_multipart;

    EXRHeader()
        : compression(Compression::None)
        , line_order(LineOrder::IncreasingY)
        , pixel_aspect_ratio(1.0f)
        , screen_window_width(1.0f)
        , has_chromaticities(false)
        , exposure_time(0.0f)
        , has_exposure_time(false)
        , is_tiled(false)
        , has_long_names(false)
        , is_deep(false)
        , is_multipart(false) {}

    /// Get image width
    int32_t width() const { return data_window.width(); }

    /// Get image height
    int32_t height() const { return data_window.height(); }

    /// Find channel by name (returns nullptr if not found)
    const Channel* find_channel(const std::string& name) const {
        for (const auto& ch : channels) {
            if (ch.name == name) return &ch;
        }
        return nullptr;
    }

    /// Check if image has alpha channel
    bool has_alpha() const { return find_channel("A") != nullptr; }

    /// Check if image is grayscale (Y channel only)
    bool is_grayscale() const {
        return find_channel("Y") != nullptr && find_channel("R") == nullptr;
    }
};

// ============================================================================
// EXRImage
// ============================================================================

/// Loaded EXR image data
struct EXRImage {
    EXRHeader header;

    // Pixel data as float (converted from half/uint as needed)
    // Layout: [height][width][num_channels] in row-major order
    std::vector<float> pixels;

    // Channel order in pixels array
    std::vector<std::string> channel_order;

    /// Get pointer to pixel at (x, y)
    /// Returns nullptr if out of bounds
    float* pixel(int x, int y) {
        if (x < 0 || y < 0 || x >= header.width() || y >= header.height()) {
            return nullptr;
        }
        size_t idx = (static_cast<size_t>(y) * header.width() + x) * channel_order.size();
        return &pixels[idx];
    }

    const float* pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= header.width() || y >= header.height()) {
            return nullptr;
        }
        size_t idx = (static_cast<size_t>(y) * header.width() + x) * channel_order.size();
        return &pixels[idx];
    }

    /// Get number of channels in pixel data
    size_t num_channels() const { return channel_order.size(); }

    /// Get channel index by name (-1 if not found)
    int channel_index(const std::string& name) const {
        for (size_t i = 0; i < channel_order.size(); ++i) {
            if (channel_order[i] == name) return static_cast<int>(i);
        }
        return -1;
    }
};

// ============================================================================
// Load Options
// ============================================================================

/// Options for loading EXR files
struct LoadOptions {
    // Memory limit (0 = use default)
    size_t max_memory;

    // Requested channels (empty = load all)
    std::vector<std::string> requested_channels;

    // Convert to RGBA order (adds missing channels with default values)
    bool convert_to_rgba;

    // Flip image vertically (OpenGL convention)
    bool flip_vertically;

    LoadOptions()
        : max_memory(0)
        , convert_to_rgba(false)
        , flip_vertically(false) {}
};

// ============================================================================
// Result
// ============================================================================

/// Result of EXR operations
struct Result {
    bool success;
    std::string error;

    Result() : success(true) {}
    Result(bool ok, const std::string& err = "") : success(ok), error(err) {}

    operator bool() const { return success; }

    static Result Ok() { return Result(true); }
    static Result Error(const std::string& msg) { return Result(false, msg); }
};

// ============================================================================
// API Functions
// ============================================================================

/// Load EXR image from file
/// @param filename Path to EXR file
/// @param image Output image data
/// @param options Load options
/// @return Result with success status and error message
Result LoadEXR(const std::string& filename, EXRImage* image,
               const LoadOptions& options = LoadOptions());

/// Load EXR image from memory
/// @param data Pointer to EXR data
/// @param size Size of data in bytes
/// @param image Output image data
/// @param options Load options
/// @return Result with success status and error message
Result LoadEXRFromMemory(const uint8_t* data, size_t size, EXRImage* image,
                         const LoadOptions& options = LoadOptions());

/// Parse EXR header only (no pixel data)
/// @param filename Path to EXR file
/// @param header Output header data
/// @return Result with success status and error message
Result ParseEXRHeader(const std::string& filename, EXRHeader* header);

/// Parse EXR header from memory
/// @param data Pointer to EXR data
/// @param size Size of data in bytes
/// @param header Output header data
/// @return Result with success status and error message
Result ParseEXRHeaderFromMemory(const uint8_t* data, size_t size, EXRHeader* header);

/// Check if data is a valid EXR file (checks magic number)
/// @param data Pointer to data
/// @param size Size of data (at least 4 bytes needed)
/// @return true if data starts with EXR magic number
bool IsEXR(const uint8_t* data, size_t size);

/// Convert half-float (16-bit) to float (32-bit)
/// @param h Half-float value
/// @return Float value
float HalfToFloat(uint16_t h);

/// Convert float (32-bit) to half-float (16-bit)
/// @param f Float value
/// @return Half-float value
uint16_t FloatToHalf(float f);

// ============================================================================
// Writer API (minimal)
// ============================================================================

/// Save options for writing EXR files
struct SaveOptions {
    Compression compression;
    bool write_half;  // Write as half-float (default) or float

    SaveOptions()
        : compression(Compression::ZIPS)
        , write_half(true) {}
};

/// Save EXR image to file
/// @param filename Path to output file
/// @param width Image width
/// @param height Image height
/// @param num_channels Number of channels (1-4)
/// @param pixels Pixel data (float, row-major, [height][width][channels])
/// @param channel_names Channel names (nullptr = use R,G,B,A)
/// @param options Save options
/// @return Result with success status and error message
Result SaveEXR(const std::string& filename, int width, int height,
               int num_channels, const float* pixels,
               const char* const* channel_names = nullptr,
               const SaveOptions& options = SaveOptions());

} // namespace lightexr
} // namespace v1
} // namespace lightusd
