// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightHDR - Minimal, secure, portable Radiance HDR (RGBE) loader
//
// Features:
// - Secure parsing with bounds checking and memory limits
// - Support for both old and new RLE formats
// - RGBE to float conversion
// - Exposure adjustment support
//
// Format reference: Radiance HDR (.hdr, .pic) format
// - Header with key=value pairs
// - Resolution specification (-Y height +X width for standard orientation)
// - RGBE pixel data (R, G, B, Exponent)
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace lightusd {
namespace v1 {
namespace lighthdr {

// ============================================================================
// Constants
// ============================================================================

// Maximum limits for security
static constexpr size_t kMaxImageWidth = 65536;
static constexpr size_t kMaxImageHeight = 65536;
static constexpr size_t kMaxMemoryUsage = 2ULL * 1024 * 1024 * 1024;  // 2GB

// ============================================================================
// HDRHeader
// ============================================================================

/// HDR file header information
struct HDRHeader {
    uint32_t width;
    uint32_t height;

    // Optional header fields
    float exposure;           // Exposure value (default 1.0)
    float gamma;              // Gamma value (default 1.0)
    std::string software;     // SOFTWARE field
    std::string format;       // FORMAT field (should be "32-bit_rle_rgbe")
    float pixel_aspect;       // Pixel aspect ratio (default 1.0)
    float primaries[8];       // Color primaries (Rx,Ry,Gx,Gy,Bx,By,Wx,Wy)
    bool has_primaries;

    // Orientation
    bool flip_x;              // +X means left-to-right (normal)
    bool flip_y;              // -Y means top-to-bottom (normal)

    HDRHeader()
        : width(0)
        , height(0)
        , exposure(1.0f)
        , gamma(1.0f)
        , pixel_aspect(1.0f)
        , has_primaries(false)
        , flip_x(false)
        , flip_y(false) {
        for (int i = 0; i < 8; ++i) primaries[i] = 0.0f;
    }
};

// ============================================================================
// HDRImage
// ============================================================================

/// Loaded HDR image data
struct HDRImage {
    HDRHeader header;

    // Pixel data as float RGB (3 channels) or RGBA (4 channels)
    // Layout: [height][width][channels] in row-major order
    std::vector<float> pixels;

    // Number of channels (3 or 4)
    uint32_t channels;

    HDRImage() : channels(3) {}

    /// Get pointer to pixel at (x, y)
    float* pixel(int x, int y) {
        if (x < 0 || y < 0 ||
            static_cast<uint32_t>(x) >= header.width ||
            static_cast<uint32_t>(y) >= header.height) {
            return nullptr;
        }
        size_t idx = (static_cast<size_t>(y) * header.width + x) * channels;
        return &pixels[idx];
    }

    const float* pixel(int x, int y) const {
        if (x < 0 || y < 0 ||
            static_cast<uint32_t>(x) >= header.width ||
            static_cast<uint32_t>(y) >= header.height) {
            return nullptr;
        }
        size_t idx = (static_cast<size_t>(y) * header.width + x) * channels;
        return &pixels[idx];
    }
};

// ============================================================================
// Load Options
// ============================================================================

/// Options for loading HDR files
struct LoadOptions {
    // Memory limit (0 = use default)
    size_t max_memory;

    // Output as RGBA (add alpha channel = 1.0)
    bool output_rgba;

    // Apply exposure from header
    bool apply_exposure;

    // Flip image vertically (OpenGL convention)
    bool flip_vertically;

    LoadOptions()
        : max_memory(0)
        , output_rgba(true)
        , apply_exposure(true)
        , flip_vertically(false) {}
};

// ============================================================================
// Result
// ============================================================================

/// Result of HDR operations
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

/// Load HDR image from file
/// @param filename Path to HDR file
/// @param image Output image data
/// @param options Load options
/// @return Result with success status and error message
Result LoadHDR(const std::string& filename, HDRImage* image,
               const LoadOptions& options = LoadOptions());

/// Load HDR image from memory
/// @param data Pointer to HDR data
/// @param size Size of data in bytes
/// @param image Output image data
/// @param options Load options
/// @return Result with success status and error message
Result LoadHDRFromMemory(const uint8_t* data, size_t size, HDRImage* image,
                         const LoadOptions& options = LoadOptions());

/// Parse HDR header only (no pixel data)
/// @param filename Path to HDR file
/// @param header Output header data
/// @return Result with success status and error message
Result ParseHDRHeader(const std::string& filename, HDRHeader* header);

/// Parse HDR header from memory
/// @param data Pointer to HDR data
/// @param size Size of data in bytes
/// @param header Output header data
/// @return Result with success status and error message
Result ParseHDRHeaderFromMemory(const uint8_t* data, size_t size, HDRHeader* header);

/// Check if data is a valid HDR file (checks magic signature)
/// @param data Pointer to data
/// @param size Size of data (at least 10 bytes needed)
/// @return true if data starts with HDR signature
bool IsHDR(const uint8_t* data, size_t size);

/// Convert RGBE (4 bytes) to RGB float (3 floats)
/// @param rgbe Input RGBE value (R, G, B, E)
/// @param rgb Output RGB float values
void RGBEToFloat(const uint8_t rgbe[4], float rgb[3]);

/// Convert RGB float (3 floats) to RGBE (4 bytes)
/// @param rgb Input RGB float values
/// @param rgbe Output RGBE value (R, G, B, E)
void FloatToRGBE(const float rgb[3], uint8_t rgbe[4]);

// ============================================================================
// Writer API
// ============================================================================

/// Save options for writing HDR files
struct SaveOptions {
    // Include exposure in header
    float exposure;

    // Include gamma in header
    float gamma;

    SaveOptions()
        : exposure(1.0f)
        , gamma(1.0f) {}
};

/// Save HDR image to file
/// @param filename Path to output file
/// @param width Image width
/// @param height Image height
/// @param channels Number of channels (3 or 4, alpha ignored)
/// @param pixels Pixel data (float, row-major, [height][width][channels])
/// @param options Save options
/// @return Result with success status and error message
Result SaveHDR(const std::string& filename, int width, int height,
               int channels, const float* pixels,
               const SaveOptions& options = SaveOptions());

}  // namespace lighthdr
}  // namespace v1
}  // namespace lightusd
