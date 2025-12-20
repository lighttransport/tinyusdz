// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightHDR - Minimal, secure, portable Radiance HDR (RGBE) loader implementation

#include "lightusd/lighthdr.hh"
#include "lightusd/debug.hh"

#include <cstring>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <algorithm>

namespace lightusd {
namespace v1 {
namespace lighthdr {

// Safe string to float conversion (no exceptions)
static float safe_stof(const std::string& s, float default_val = 0.0f) {
    if (s.empty()) return default_val;
    char* end = nullptr;
    float val = std::strtof(s.c_str(), &end);
    if (end == s.c_str()) return default_val;  // No conversion
    return val;
}

// ============================================================================
// RGBE Conversion
// ============================================================================

void RGBEToFloat(const uint8_t rgbe[4], float rgb[3]) {
    if (rgbe[3] == 0) {
        // Zero exponent means black
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
    } else {
        // Convert from RGBE to float
        // Value = mantissa * 2^(exponent - 128 - 8)
        float f = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - (128 + 8));
        rgb[0] = (rgbe[0] + 0.5f) * f;
        rgb[1] = (rgbe[1] + 0.5f) * f;
        rgb[2] = (rgbe[2] + 0.5f) * f;
    }
}

void FloatToRGBE(const float rgb[3], uint8_t rgbe[4]) {
    float max_val = std::max({rgb[0], rgb[1], rgb[2]});

    if (max_val < 1e-32f) {
        // Too small, output black
        rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
    } else {
        int exp;
        float mantissa = std::frexp(max_val, &exp);
        float scale = mantissa * 256.0f / max_val;

        rgbe[0] = static_cast<uint8_t>(std::min(255.0f, rgb[0] * scale));
        rgbe[1] = static_cast<uint8_t>(std::min(255.0f, rgb[1] * scale));
        rgbe[2] = static_cast<uint8_t>(std::min(255.0f, rgb[2] * scale));
        rgbe[3] = static_cast<uint8_t>(exp + 128);
    }
}

// ============================================================================
// Internal: Binary reader
// ============================================================================

class HDRReader {
public:
    HDRReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    bool eof() const { return pos_ >= size_; }
    size_t position() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }

    bool read_byte(uint8_t& b) {
        if (pos_ >= size_) return false;
        b = data_[pos_++];
        return true;
    }

    bool read_bytes(uint8_t* dest, size_t count) {
        if (pos_ + count > size_) return false;
        std::memcpy(dest, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    // Read a line (up to newline or max_len)
    bool read_line(std::string& line, size_t max_len = 1024) {
        line.clear();
        while (pos_ < size_ && line.size() < max_len) {
            char c = static_cast<char>(data_[pos_++]);
            if (c == '\n') return true;
            if (c == '\r') {
                // Handle CRLF
                if (pos_ < size_ && data_[pos_] == '\n') pos_++;
                return true;
            }
            line += c;
        }
        return !line.empty() || pos_ < size_;
    }

    const uint8_t* current_ptr() const {
        return (pos_ < size_) ? data_ + pos_ : nullptr;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

// ============================================================================
// Check if HDR format
// ============================================================================

bool IsHDR(const uint8_t* data, size_t size) {
    if (size < 10) return false;

    // Check for "#?RADIANCE" or "#?RGBE" signature
    if (data[0] == '#' && data[1] == '?') {
        // Check for known format strings
        if (size >= 10 && std::memcmp(data + 2, "RADIANCE", 8) == 0) return true;
        if (size >= 6 && std::memcmp(data + 2, "RGBE", 4) == 0) return true;
        // Accept any #? header as potentially valid
        return true;
    }

    return false;
}

// ============================================================================
// Internal: Header parsing
// ============================================================================

static Result ParseHeaderInternal(HDRReader& reader, HDRHeader& header, size_t& data_start) {
    std::string line;

    // Read magic line
    if (!reader.read_line(line)) {
        return Result::Error("Failed to read HDR header");
    }

    if (line.size() < 2 || line[0] != '#' || line[1] != '?') {
        return Result::Error("Invalid HDR magic signature");
    }

    // Parse header fields
    while (reader.read_line(line)) {
        if (line.empty()) {
            // Empty line marks end of header
            break;
        }

        // Skip comments
        if (line[0] == '#') continue;

        // Parse key=value pairs
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            if (key == "FORMAT") {
                header.format = value;
            } else if (key == "EXPOSURE") {
                header.exposure = safe_stof(value, 1.0f);
            } else if (key == "GAMMA") {
                header.gamma = safe_stof(value, 1.0f);
            } else if (key == "SOFTWARE") {
                header.software = value;
            } else if (key == "PIXASPECT") {
                header.pixel_aspect = safe_stof(value, 1.0f);
            } else if (key == "PRIMARIES") {
                // Parse 8 float values for color primaries
                // Format: Rx Ry Gx Gy Bx By Wx Wy
                size_t pos = 0;
                for (int i = 0; i < 8 && pos < value.size(); ++i) {
                    while (pos < value.size() && (value[pos] == ' ' || value[pos] == '\t')) pos++;
                    size_t end = pos;
                    while (end < value.size() && value[end] != ' ' && value[end] != '\t') end++;
                    if (end > pos) {
                        header.primaries[i] = safe_stof(value.substr(pos, end - pos), 0.0f);
                    }
                    pos = end;
                }
                header.has_primaries = true;
            }
        }
    }

    // Read resolution line
    if (!reader.read_line(line)) {
        return Result::Error("Missing resolution line");
    }

    // Parse resolution: "-Y height +X width" or similar
    // Format: [+-][XY] size [+-][XY] size
    int width = 0, height = 0;
    bool got_x = false, got_y = false;

    size_t pos = 0;
    for (int i = 0; i < 2; ++i) {
        // Skip whitespace
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        if (pos >= line.size()) break;

        // Read sign
        bool negative = false;
        if (line[pos] == '-') {
            negative = true;
            pos++;
        } else if (line[pos] == '+') {
            pos++;
        }

        if (pos >= line.size()) break;

        // Read axis
        char axis = line[pos++];

        // Skip whitespace
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) pos++;

        // Read size
        int size = 0;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
            size = size * 10 + (line[pos] - '0');
            pos++;
        }

        if (axis == 'X' || axis == 'x') {
            width = size;
            header.flip_x = negative;  // -X means right-to-left
            got_x = true;
        } else if (axis == 'Y' || axis == 'y') {
            height = size;
            header.flip_y = !negative;  // -Y means top-to-bottom (normal)
            got_y = true;
        }
    }

    if (!got_x || !got_y || width <= 0 || height <= 0) {
        return Result::Error("Invalid resolution specification");
    }

    if (static_cast<size_t>(width) > kMaxImageWidth ||
        static_cast<size_t>(height) > kMaxImageHeight) {
        return Result::Error("Image dimensions exceed maximum");
    }

    header.width = static_cast<uint32_t>(width);
    header.height = static_cast<uint32_t>(height);

    data_start = reader.position();
    return Result::Ok();
}

// ============================================================================
// Internal: RLE decompression
// ============================================================================

// New RLE format (adaptive RLE per scanline)
static bool DecodeRLEScanline(HDRReader& reader, uint8_t* scanline, int width) {
    // Each scanline starts with 4 bytes: 2, 2, width_hi, width_lo
    uint8_t header[4];
    if (!reader.read_bytes(header, 4)) return false;

    // Check for new RLE format marker
    if (header[0] != 2 || header[1] != 2) {
        // Not new RLE format, treat as raw or old RLE
        // This shouldn't happen if we detected it correctly
        return false;
    }

    int scanline_width = (header[2] << 8) | header[3];
    if (scanline_width != width) return false;

    // Decode each channel separately
    for (int ch = 0; ch < 4; ++ch) {
        uint8_t* dst = scanline + ch;
        int count = 0;

        while (count < width) {
            uint8_t byte;
            if (!reader.read_byte(byte)) return false;

            if (byte > 128) {
                // Run of same value
                int run_len = byte - 128;
                if (count + run_len > width) return false;

                uint8_t value;
                if (!reader.read_byte(value)) return false;

                for (int i = 0; i < run_len; ++i) {
                    *dst = value;
                    dst += 4;
                }
                count += run_len;
            } else {
                // Literal values
                int lit_len = byte;
                if (lit_len == 0) return false;  // Invalid
                if (count + lit_len > width) return false;

                for (int i = 0; i < lit_len; ++i) {
                    uint8_t value;
                    if (!reader.read_byte(value)) return false;
                    *dst = value;
                    dst += 4;
                }
                count += lit_len;
            }
        }
    }

    return true;
}

// Old RLE format or uncompressed
static bool DecodeOldFormat(HDRReader& reader, uint8_t* pixels, int width, int height) {
    uint8_t rgbe[4];
    int shift = 0;

    for (int y = 0; y < height; ++y) {
        uint8_t* scanline = pixels + y * width * 4;

        for (int x = 0; x < width; ) {
            if (!reader.read_bytes(rgbe, 4)) return false;

            // Check for old RLE marker (1, 1, 1, exp)
            if (rgbe[0] == 1 && rgbe[1] == 1 && rgbe[2] == 1) {
                // Repeat previous pixel
                int count = rgbe[3] << shift;
                if (x + count > width) return false;
                if (x == 0) return false;  // Can't repeat at start

                uint8_t* prev = scanline + (x - 1) * 4;
                for (int i = 0; i < count; ++i) {
                    scanline[x * 4 + 0] = prev[0];
                    scanline[x * 4 + 1] = prev[1];
                    scanline[x * 4 + 2] = prev[2];
                    scanline[x * 4 + 3] = prev[3];
                    x++;
                }
                shift += 8;
            } else {
                // Normal pixel
                scanline[x * 4 + 0] = rgbe[0];
                scanline[x * 4 + 1] = rgbe[1];
                scanline[x * 4 + 2] = rgbe[2];
                scanline[x * 4 + 3] = rgbe[3];
                x++;
                shift = 0;
            }
        }
    }

    return true;
}

// ============================================================================
// Internal: Pixel loading
// ============================================================================

static Result LoadPixelData(HDRReader& reader, HDRImage& image, const LoadOptions& options) {
    uint32_t width = image.header.width;
    uint32_t height = image.header.height;

    // Check memory limit
    size_t max_memory = options.max_memory > 0 ? options.max_memory : kMaxMemoryUsage;
    size_t required = static_cast<size_t>(width) * height * 4 * sizeof(float);
    if (required > max_memory) {
        return Result::Error("Image exceeds memory limit");
    }

    // Allocate RGBE buffer
    std::vector<uint8_t> rgbe_data(width * height * 4);

    // Check for new RLE format by peeking at first scanline
    // New RLE starts with: 2, 2, width_hi, width_lo
    const uint8_t* peek = reader.current_ptr();
    bool is_new_rle = (reader.remaining() >= 4 &&
                       peek[0] == 2 && peek[1] == 2 &&
                       ((peek[2] << 8) | peek[3]) == static_cast<int>(width));

    if (is_new_rle && width >= 8 && width <= 32767) {
        // Decode using new RLE format (per scanline)
        for (uint32_t y = 0; y < height; ++y) {
            if (!DecodeRLEScanline(reader, rgbe_data.data() + y * width * 4, width)) {
                return Result::Error("Failed to decode RLE scanline");
            }
        }
    } else {
        // Use old format (uncompressed or old RLE)
        if (!DecodeOldFormat(reader, rgbe_data.data(), width, height)) {
            return Result::Error("Failed to decode HDR data");
        }
    }

    // Convert RGBE to float
    uint32_t out_channels = options.output_rgba ? 4 : 3;
    image.channels = out_channels;
    image.pixels.resize(width * height * out_channels);

    float exposure_scale = options.apply_exposure ? (1.0f / image.header.exposure) : 1.0f;

    for (uint32_t y = 0; y < height; ++y) {
        uint32_t src_y = y;
        uint32_t dst_y = options.flip_vertically ? (height - 1 - y) : y;

        // Apply header flip_y if needed
        if (image.header.flip_y) {
            src_y = height - 1 - src_y;
        }

        for (uint32_t x = 0; x < width; ++x) {
            uint32_t src_x = x;
            if (image.header.flip_x) {
                src_x = width - 1 - src_x;
            }

            const uint8_t* rgbe = rgbe_data.data() + (src_y * width + src_x) * 4;
            float* dst = image.pixels.data() + (dst_y * width + x) * out_channels;

            float rgb[3];
            RGBEToFloat(rgbe, rgb);

            dst[0] = rgb[0] * exposure_scale;
            dst[1] = rgb[1] * exposure_scale;
            dst[2] = rgb[2] * exposure_scale;

            if (out_channels == 4) {
                dst[3] = 1.0f;
            }
        }
    }

    return Result::Ok();
}

// ============================================================================
// Public API
// ============================================================================

Result ParseHDRHeaderFromMemory(const uint8_t* data, size_t size, HDRHeader* header) {
    if (!data || size == 0 || !header) {
        return Result::Error("Invalid arguments");
    }

    HDRReader reader(data, size);
    size_t data_start;
    return ParseHeaderInternal(reader, *header, data_start);
}

Result ParseHDRHeader(const std::string& filename, HDRHeader* header) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result::Error("Failed to open file: " + filename);
    }

    size_t size = file.tellg();
    file.seekg(0);

    // Only need to read header (first ~4KB should be enough)
    size_t read_size = std::min(size, static_cast<size_t>(4 * 1024));
    std::vector<uint8_t> data(read_size);
    file.read(reinterpret_cast<char*>(data.data()), read_size);

    return ParseHDRHeaderFromMemory(data.data(), data.size(), header);
}

Result LoadHDRFromMemory(const uint8_t* data, size_t size, HDRImage* image,
                         const LoadOptions& options) {
    if (!data || size == 0 || !image) {
        return Result::Error("Invalid arguments");
    }

    HDRReader reader(data, size);
    size_t data_start;

    // Parse header
    Result result = ParseHeaderInternal(reader, image->header, data_start);
    if (!result) return result;

    // Load pixel data
    return LoadPixelData(reader, *image, options);
}

Result LoadHDR(const std::string& filename, HDRImage* image,
               const LoadOptions& options) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result::Error("Failed to open file: " + filename);
    }

    size_t size = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!file) {
        return Result::Error("Failed to read file: " + filename);
    }

    return LoadHDRFromMemory(data.data(), data.size(), image, options);
}

// ============================================================================
// Writer
// ============================================================================

Result SaveHDR(const std::string& filename, int width, int height,
               int channels, const float* pixels,
               const SaveOptions& options) {
    if (width <= 0 || height <= 0 || channels < 3) {
        return Result::Error("Invalid image dimensions");
    }
    if (!pixels) {
        return Result::Error("Null pixel data");
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return Result::Error("Failed to create file: " + filename);
    }

    // Write header
    file << "#?RADIANCE\n";
    file << "# Written by LightHDR\n";
    file << "FORMAT=32-bit_rle_rgbe\n";

    if (options.exposure != 1.0f) {
        file << "EXPOSURE=" << options.exposure << "\n";
    }
    if (options.gamma != 1.0f) {
        file << "GAMMA=" << options.gamma << "\n";
    }

    file << "\n";  // Empty line marks end of header

    // Write resolution (standard orientation: -Y height +X width)
    file << "-Y " << height << " +X " << width << "\n";

    // Write pixel data using new RLE format
    std::vector<uint8_t> scanline(width * 4);
    std::vector<uint8_t> rle_buffer;
    rle_buffer.reserve(width * 4 * 2);  // Worst case

    for (int y = 0; y < height; ++y) {
        // Convert row to RGBE
        for (int x = 0; x < width; ++x) {
            const float* src = pixels + (y * width + x) * channels;
            float rgb[3] = {src[0], src[1], src[2]};
            FloatToRGBE(rgb, scanline.data() + x * 4);
        }

        // Write RLE header
        uint8_t header[4] = {2, 2,
                             static_cast<uint8_t>((width >> 8) & 0xFF),
                             static_cast<uint8_t>(width & 0xFF)};
        file.write(reinterpret_cast<const char*>(header), 4);

        // RLE encode each channel
        for (int ch = 0; ch < 4; ++ch) {
            rle_buffer.clear();

            int x = 0;
            while (x < width) {
                // Look for runs
                uint8_t run_val = scanline[x * 4 + ch];
                int run_len = 1;

                while (x + run_len < width && run_len < 127 &&
                       scanline[(x + run_len) * 4 + ch] == run_val) {
                    run_len++;
                }

                if (run_len > 2) {
                    // Emit run
                    rle_buffer.push_back(static_cast<uint8_t>(run_len + 128));
                    rle_buffer.push_back(run_val);
                    x += run_len;
                } else {
                    // Collect literals
                    int lit_start = x;
                    int lit_len = 0;

                    while (x < width && lit_len < 127) {
                        // Check if a run starts here
                        uint8_t val = scanline[x * 4 + ch];
                        int potential_run = 1;
                        while (x + potential_run < width && potential_run < 127 &&
                               scanline[(x + potential_run) * 4 + ch] == val) {
                            potential_run++;
                        }

                        if (potential_run > 2 && lit_len > 0) {
                            // Run found, stop literals here
                            break;
                        }

                        lit_len++;
                        x++;

                        if (potential_run > 2) {
                            // This was a run, it's already included
                            break;
                        }
                    }

                    // Emit literals
                    rle_buffer.push_back(static_cast<uint8_t>(lit_len));
                    for (int i = 0; i < lit_len; ++i) {
                        rle_buffer.push_back(scanline[(lit_start + i) * 4 + ch]);
                    }
                }
            }

            file.write(reinterpret_cast<const char*>(rle_buffer.data()), rle_buffer.size());
        }
    }

    if (!file) {
        return Result::Error("Failed to write file");
    }

    return Result::Ok();
}

}  // namespace lighthdr
}  // namespace v1
}  // namespace lightusd
