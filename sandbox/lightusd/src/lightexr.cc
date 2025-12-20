// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightEXR - Minimal, secure, portable OpenEXR loader implementation

#include "lightusd/lightexr.hh"
#include "lightusd/debug.hh"

#include <cstring>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <limits>

namespace lightusd {
namespace v1 {
namespace lightexr {

// ============================================================================
// Internal: Safe binary reader
// ============================================================================

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    bool eof() const { return pos_ >= size_; }
    size_t position() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }

    bool seek(size_t pos) {
        if (pos > size_) return false;
        pos_ = pos;
        return true;
    }

    bool skip(size_t bytes) {
        if (pos_ + bytes > size_) return false;
        pos_ += bytes;
        return true;
    }

    bool read_bytes(void* dest, size_t count) {
        if (pos_ + count > size_) return false;
        std::memcpy(dest, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    bool read_u8(uint8_t& v) { return read_bytes(&v, 1); }
    bool read_u16(uint16_t& v) { return read_bytes(&v, 2); }
    bool read_u32(uint32_t& v) { return read_bytes(&v, 4); }
    bool read_u64(uint64_t& v) { return read_bytes(&v, 8); }
    bool read_i32(int32_t& v) { return read_bytes(&v, 4); }
    bool read_f32(float& v) { return read_bytes(&v, 4); }

    bool read_string(std::string& s, size_t max_len = 256) {
        s.clear();
        for (size_t i = 0; i < max_len; ++i) {
            if (pos_ >= size_) return false;
            char c = static_cast<char>(data_[pos_++]);
            if (c == '\0') return true;
            s += c;
        }
        return false;  // String too long
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
// Half-float conversion
// ============================================================================

// Based on public domain code
// IEEE 754 half-float: 1 sign, 5 exponent, 10 mantissa

float HalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 1;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;

    uint32_t f;

    if (exp == 0) {
        if (mant == 0) {
            // Zero
            f = sign << 31;
        } else {
            // Denormalized number
            exp = 1;
            while ((mant & 0x400) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x3FF;
            f = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        // Inf or NaN
        f = (sign << 31) | 0x7F800000 | (mant << 13);
    } else {
        // Normalized number
        f = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
    }

    float result;
    std::memcpy(&result, &f, 4);
    return result;
}

uint16_t FloatToHalf(float f) {
    uint32_t fi;
    std::memcpy(&fi, &f, 4);

    uint32_t sign = (fi >> 31) & 1;
    int32_t exp = ((fi >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = fi & 0x7FFFFF;

    uint16_t h;

    if (exp <= 0) {
        if (exp < -10) {
            // Too small, become zero
            h = static_cast<uint16_t>(sign << 15);
        } else {
            // Denormalized
            mant |= 0x800000;
            int shift = 14 - exp;
            h = static_cast<uint16_t>((sign << 15) | (mant >> shift));
        }
    } else if (exp >= 31) {
        // Overflow to infinity
        h = static_cast<uint16_t>((sign << 15) | 0x7C00);
    } else {
        // Normalized
        h = static_cast<uint16_t>((sign << 15) | (exp << 10) | (mant >> 13));
    }

    return h;
}

// ============================================================================
// Minimal DEFLATE decompressor (for ZIP/ZIPS support)
// ============================================================================
// Based on public domain puff.c by Mark Adler

namespace deflate {

static constexpr int kMaxBits = 15;
static constexpr int kMaxLitLenCodes = 286;
static constexpr int kMaxDistCodes = 30;

struct HuffmanTable {
    uint16_t count[kMaxBits + 1];  // Number of symbols for each length
    uint16_t symbol[kMaxLitLenCodes];  // Symbols sorted by code length
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0), bit_buf_(0), bit_cnt_(0) {}

    bool need_bits(int n) {
        while (bit_cnt_ < n) {
            if (pos_ >= size_) return false;
            bit_buf_ |= static_cast<uint32_t>(data_[pos_++]) << bit_cnt_;
            bit_cnt_ += 8;
        }
        return true;
    }

    uint32_t peek_bits(int n) {
        return bit_buf_ & ((1u << n) - 1);
    }

    void drop_bits(int n) {
        bit_buf_ >>= n;
        bit_cnt_ -= n;
    }

    uint32_t get_bits(int n) {
        if (!need_bits(n)) return 0;
        uint32_t val = peek_bits(n);
        drop_bits(n);
        return val;
    }

    void align_to_byte() {
        bit_buf_ = 0;
        bit_cnt_ = 0;
    }

    size_t position() const { return pos_; }
    const uint8_t* current_ptr() const { return data_ + pos_; }
    size_t remaining() const { return size_ - pos_; }

    bool read_byte(uint8_t& b) {
        if (pos_ >= size_) return false;
        b = data_[pos_++];
        return true;
    }

    bool skip(size_t n) {
        if (pos_ + n > size_) return false;
        pos_ += n;
        return true;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    uint32_t bit_buf_;
    int bit_cnt_;
};

static bool build_huffman(HuffmanTable& h, const uint16_t* lengths, int n) {
    std::memset(h.count, 0, sizeof(h.count));

    // Count codes for each length
    for (int i = 0; i < n; ++i) {
        if (lengths[i] > kMaxBits) return false;
        h.count[lengths[i]]++;
    }

    // Check for invalid code set
    int left = 1;
    for (int i = 1; i <= kMaxBits; ++i) {
        left <<= 1;
        left -= h.count[i];
        if (left < 0) return false;
    }

    // Generate offsets for each length
    uint16_t offs[kMaxBits + 1];
    offs[1] = 0;
    for (int i = 1; i < kMaxBits; ++i) {
        offs[i + 1] = offs[i] + h.count[i];
    }

    // Assign symbols to codes
    for (int i = 0; i < n; ++i) {
        if (lengths[i] != 0) {
            h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
        }
    }

    return true;
}

static int decode(BitReader& bits, const HuffmanTable& h) {
    int code = 0;
    int first = 0;
    int index = 0;

    for (int len = 1; len <= kMaxBits; ++len) {
        if (!bits.need_bits(1)) return -1;
        code |= bits.peek_bits(1);
        bits.drop_bits(1);

        int count = h.count[len];
        if (code < first + count) {
            return h.symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    return -1;  // Invalid code
}

// Fixed Huffman tables (precomputed)
static HuffmanTable s_fixed_lit_len;
static HuffmanTable s_fixed_dist;
static bool s_fixed_tables_built = false;

static void build_fixed_tables() {
    if (s_fixed_tables_built) return;

    uint16_t lengths[kMaxLitLenCodes];
    // 0-143: 8 bits, 144-255: 9 bits, 256-279: 7 bits, 280-287: 8 bits
    int i;
    for (i = 0; i < 144; ++i) lengths[i] = 8;
    for (; i < 256; ++i) lengths[i] = 9;
    for (; i < 280; ++i) lengths[i] = 7;
    for (; i < 288; ++i) lengths[i] = 8;
    build_huffman(s_fixed_lit_len, lengths, 288);

    for (i = 0; i < 30; ++i) lengths[i] = 5;
    build_huffman(s_fixed_dist, lengths, 30);

    s_fixed_tables_built = true;
}

// Length and distance base values and extra bits
static const uint16_t kLengthBase[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t kLengthExtra[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t kDistBase[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t kDistExtra[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static bool inflate_block(BitReader& bits, const HuffmanTable& lit_len,
                          const HuffmanTable& dist, std::vector<uint8_t>& out,
                          size_t max_size) {
    while (true) {
        int sym = decode(bits, lit_len);
        if (sym < 0) return false;

        if (sym < 256) {
            // Literal byte
            if (out.size() >= max_size) return false;
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            // End of block
            return true;
        } else {
            // Length/distance pair
            sym -= 257;
            if (sym >= 29) return false;

            uint32_t len = kLengthBase[sym] + bits.get_bits(kLengthExtra[sym]);

            int dist_sym = decode(bits, dist);
            if (dist_sym < 0 || dist_sym >= 30) return false;

            uint32_t distance = kDistBase[dist_sym] + bits.get_bits(kDistExtra[dist_sym]);

            if (distance > out.size()) return false;
            if (out.size() + len > max_size) return false;

            // Copy from back reference
            size_t src = out.size() - distance;
            for (uint32_t i = 0; i < len; ++i) {
                out.push_back(out[src + i]);
            }
        }
    }
}

static bool inflate_dynamic(BitReader& bits, std::vector<uint8_t>& out, size_t max_size) {
    // Read code lengths for code length alphabet
    int hlit = bits.get_bits(5) + 257;
    int hdist = bits.get_bits(5) + 1;
    int hclen = bits.get_bits(4) + 4;

    if (hlit > kMaxLitLenCodes || hdist > kMaxDistCodes) return false;

    static const uint8_t kOrder[] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    uint16_t lengths[19] = {0};
    for (int i = 0; i < hclen; ++i) {
        lengths[kOrder[i]] = static_cast<uint16_t>(bits.get_bits(3));
    }

    HuffmanTable code_len;
    if (!build_huffman(code_len, lengths, 19)) return false;

    // Read literal/length and distance code lengths
    uint16_t all_lengths[kMaxLitLenCodes + kMaxDistCodes];
    int n = 0;
    int total = hlit + hdist;

    while (n < total) {
        int sym = decode(bits, code_len);
        if (sym < 0) return false;

        if (sym < 16) {
            all_lengths[n++] = static_cast<uint16_t>(sym);
        } else if (sym == 16) {
            if (n == 0) return false;
            int count = bits.get_bits(2) + 3;
            uint16_t last = all_lengths[n - 1];
            while (count-- > 0 && n < total) {
                all_lengths[n++] = last;
            }
        } else if (sym == 17) {
            int count = bits.get_bits(3) + 3;
            while (count-- > 0 && n < total) {
                all_lengths[n++] = 0;
            }
        } else {  // sym == 18
            int count = bits.get_bits(7) + 11;
            while (count-- > 0 && n < total) {
                all_lengths[n++] = 0;
            }
        }
    }

    HuffmanTable lit_len, dist;
    if (!build_huffman(lit_len, all_lengths, hlit)) return false;
    if (!build_huffman(dist, all_lengths + hlit, hdist)) return false;

    return inflate_block(bits, lit_len, dist, out, max_size);
}

bool Inflate(const uint8_t* src, size_t src_size,
             std::vector<uint8_t>& out, size_t max_size) {
    out.clear();
    out.reserve(std::min(max_size, src_size * 4));

    build_fixed_tables();

    BitReader bits(src, src_size);

    bool last = false;
    while (!last) {
        if (!bits.need_bits(3)) return false;

        last = bits.get_bits(1) != 0;
        int type = bits.get_bits(2);

        if (type == 0) {
            // Stored block (no compression)
            bits.align_to_byte();
            uint8_t len_lo, len_hi, nlen_lo, nlen_hi;
            if (!bits.read_byte(len_lo) || !bits.read_byte(len_hi) ||
                !bits.read_byte(nlen_lo) || !bits.read_byte(nlen_hi)) {
                return false;
            }
            uint16_t len = len_lo | (len_hi << 8);
            uint16_t nlen = nlen_lo | (nlen_hi << 8);
            if (len != (uint16_t)~nlen) return false;

            if (out.size() + len > max_size) return false;
            const uint8_t* ptr = bits.current_ptr();
            if (!bits.skip(len)) return false;
            out.insert(out.end(), ptr, ptr + len);
        } else if (type == 1) {
            // Fixed Huffman codes
            if (!inflate_block(bits, s_fixed_lit_len, s_fixed_dist, out, max_size)) {
                return false;
            }
        } else if (type == 2) {
            // Dynamic Huffman codes
            if (!inflate_dynamic(bits, out, max_size)) {
                return false;
            }
        } else {
            // Invalid block type
            return false;
        }
    }

    return true;
}

}  // namespace deflate

// ============================================================================
// RLE Decompression
// ============================================================================

static bool DecodeRLE(const uint8_t* src, size_t src_size,
                      std::vector<uint8_t>& out, size_t expected_size) {
    out.clear();
    out.reserve(expected_size);

    size_t pos = 0;
    while (pos < src_size && out.size() < expected_size) {
        int8_t run = static_cast<int8_t>(src[pos++]);

        if (run >= 0) {
            // Literal run: copy run+1 bytes
            size_t count = static_cast<size_t>(run) + 1;
            if (pos + count > src_size) return false;
            if (out.size() + count > expected_size) return false;
            out.insert(out.end(), src + pos, src + pos + count);
            pos += count;
        } else {
            // Repeated byte: repeat -run+1 times
            size_t count = static_cast<size_t>(-run) + 1;
            if (pos >= src_size) return false;
            if (out.size() + count > expected_size) return false;
            uint8_t byte = src[pos++];
            for (size_t i = 0; i < count; ++i) {
                out.push_back(byte);
            }
        }
    }

    return out.size() == expected_size;
}

// ============================================================================
// Internal: Header parsing
// ============================================================================

static bool ParseChannel(BinaryReader& reader, Channel& ch) {
    if (!reader.read_string(ch.name)) return false;

    uint32_t pixel_type;
    if (!reader.read_u32(pixel_type)) return false;
    if (pixel_type > 2) return false;
    ch.pixel_type = static_cast<PixelType>(pixel_type);

    uint8_t p_linear;
    if (!reader.read_u8(p_linear)) return false;
    ch.p_linear = (p_linear != 0);

    if (!reader.skip(3)) return false;  // Reserved

    if (!reader.read_u32(ch.x_sampling)) return false;
    if (!reader.read_u32(ch.y_sampling)) return false;

    if (ch.x_sampling == 0 || ch.y_sampling == 0) return false;

    return true;
}

static bool ParseChannelList(BinaryReader& reader, std::vector<Channel>& channels) {
    channels.clear();
    while (true) {
        Channel ch;
        size_t start_pos = reader.position();

        // Check for null terminator (empty channel name)
        uint8_t first_byte;
        if (!reader.read_u8(first_byte)) return false;
        if (first_byte == 0) break;  // End of channel list

        // Seek back and parse full channel
        reader.seek(start_pos);
        if (!ParseChannel(reader, ch)) return false;

        if (channels.size() >= kMaxChannels) return false;
        channels.push_back(ch);
    }
    return true;
}

static bool ParseBox2i(BinaryReader& reader, Box2i& box) {
    return reader.read_i32(box.x_min) && reader.read_i32(box.y_min) &&
           reader.read_i32(box.x_max) && reader.read_i32(box.y_max);
}

static bool ParseV2f(BinaryReader& reader, V2f& v) {
    return reader.read_f32(v.x) && reader.read_f32(v.y);
}

static bool ParseChromaticities(BinaryReader& reader, Chromaticities& chr) {
    return ParseV2f(reader, chr.red) && ParseV2f(reader, chr.green) &&
           ParseV2f(reader, chr.blue) && ParseV2f(reader, chr.white);
}

static Result ParseHeaderInternal(BinaryReader& reader, EXRHeader& header) {
    // Check magic number
    uint32_t magic;
    if (!reader.read_u32(magic)) {
        return Result::Error("Failed to read magic number");
    }
    if (magic != kMagicNumber) {
        return Result::Error("Invalid EXR magic number");
    }

    // Read version and flags
    uint32_t version;
    if (!reader.read_u32(version)) {
        return Result::Error("Failed to read version");
    }

    uint8_t version_num = version & 0xFF;
    if (version_num != kVersionNumber) {
        return Result::Error("Unsupported EXR version");
    }

    header.is_tiled = (version & 0x200) != 0;
    header.has_long_names = (version & 0x400) != 0;
    header.is_deep = (version & 0x800) != 0;
    header.is_multipart = (version & 0x1000) != 0;

    if (header.is_deep) {
        return Result::Error("Deep data not supported");
    }
    if (header.is_multipart) {
        return Result::Error("Multipart images not supported");
    }
    if (header.is_tiled) {
        return Result::Error("Tiled images not yet supported");
    }

    // Parse attributes
    bool has_channels = false;
    bool has_data_window = false;
    bool has_display_window = false;
    bool has_compression = false;
    bool has_line_order = false;
    bool has_pixel_aspect_ratio = false;
    bool has_screen_window_center = false;
    bool has_screen_window_width = false;

    while (true) {
        std::string attr_name;
        if (!reader.read_string(attr_name, header.has_long_names ? 255 : 31)) {
            return Result::Error("Failed to read attribute name");
        }
        if (attr_name.empty()) break;  // End of header

        std::string attr_type;
        if (!reader.read_string(attr_type, 31)) {
            return Result::Error("Failed to read attribute type");
        }

        uint32_t attr_size;
        if (!reader.read_u32(attr_size)) {
            return Result::Error("Failed to read attribute size");
        }

        if (attr_size > kMaxAttributeSize) {
            return Result::Error("Attribute too large");
        }

        size_t attr_start = reader.position();

        // Parse known attributes
        if (attr_name == "channels" && attr_type == "chlist") {
            if (!ParseChannelList(reader, header.channels)) {
                return Result::Error("Failed to parse channel list");
            }
            has_channels = true;
        } else if (attr_name == "dataWindow" && attr_type == "box2i") {
            if (!ParseBox2i(reader, header.data_window)) {
                return Result::Error("Failed to parse data window");
            }
            has_data_window = true;
        } else if (attr_name == "displayWindow" && attr_type == "box2i") {
            if (!ParseBox2i(reader, header.display_window)) {
                return Result::Error("Failed to parse display window");
            }
            has_display_window = true;
        } else if (attr_name == "compression" && attr_type == "compression") {
            uint8_t comp;
            if (!reader.read_u8(comp)) {
                return Result::Error("Failed to parse compression");
            }
            if (comp > 9) {
                return Result::Error("Unknown compression method");
            }
            header.compression = static_cast<Compression>(comp);
            has_compression = true;
        } else if (attr_name == "lineOrder" && attr_type == "lineOrder") {
            uint8_t order;
            if (!reader.read_u8(order)) {
                return Result::Error("Failed to parse line order");
            }
            if (order > 2) {
                return Result::Error("Unknown line order");
            }
            header.line_order = static_cast<LineOrder>(order);
            has_line_order = true;
        } else if (attr_name == "pixelAspectRatio" && attr_type == "float") {
            if (!reader.read_f32(header.pixel_aspect_ratio)) {
                return Result::Error("Failed to parse pixel aspect ratio");
            }
            has_pixel_aspect_ratio = true;
        } else if (attr_name == "screenWindowCenter" && attr_type == "v2f") {
            if (!ParseV2f(reader, header.screen_window_center)) {
                return Result::Error("Failed to parse screen window center");
            }
            has_screen_window_center = true;
        } else if (attr_name == "screenWindowWidth" && attr_type == "float") {
            if (!reader.read_f32(header.screen_window_width)) {
                return Result::Error("Failed to parse screen window width");
            }
            has_screen_window_width = true;
        } else if (attr_name == "chromaticities" && attr_type == "chromaticities") {
            if (!ParseChromaticities(reader, header.chromaticities)) {
                return Result::Error("Failed to parse chromaticities");
            }
            header.has_chromaticities = true;
        } else if (attr_name == "owner" && attr_type == "string") {
            std::vector<char> buf(attr_size + 1);
            if (!reader.read_bytes(buf.data(), attr_size)) {
                return Result::Error("Failed to parse owner");
            }
            buf[attr_size] = '\0';
            header.owner = buf.data();
        } else if (attr_name == "comments" && attr_type == "string") {
            std::vector<char> buf(attr_size + 1);
            if (!reader.read_bytes(buf.data(), attr_size)) {
                return Result::Error("Failed to parse comments");
            }
            buf[attr_size] = '\0';
            header.comments = buf.data();
        } else if (attr_name == "expTime" && attr_type == "float") {
            if (!reader.read_f32(header.exposure_time)) {
                return Result::Error("Failed to parse exposure time");
            }
            header.has_exposure_time = true;
        }

        // Skip to end of attribute
        if (!reader.seek(attr_start + attr_size)) {
            return Result::Error("Failed to skip attribute data");
        }
    }

    // Verify required attributes
    if (!has_channels) return Result::Error("Missing channels attribute");
    if (!has_data_window) return Result::Error("Missing dataWindow attribute");
    if (!has_display_window) return Result::Error("Missing displayWindow attribute");
    if (!has_compression) return Result::Error("Missing compression attribute");
    if (!has_line_order) return Result::Error("Missing lineOrder attribute");
    if (!has_pixel_aspect_ratio) return Result::Error("Missing pixelAspectRatio attribute");
    if (!has_screen_window_center) return Result::Error("Missing screenWindowCenter attribute");
    if (!has_screen_window_width) return Result::Error("Missing screenWindowWidth attribute");

    // Validate dimensions
    int32_t w = header.data_window.width();
    int32_t h = header.data_window.height();
    if (w <= 0 || h <= 0) {
        return Result::Error("Invalid image dimensions");
    }
    if (static_cast<size_t>(w) > kMaxImageWidth || static_cast<size_t>(h) > kMaxImageHeight) {
        return Result::Error("Image dimensions exceed maximum");
    }

    return Result::Ok();
}

// ============================================================================
// Internal: Scanline loading
// ============================================================================

static size_t GetScanlineSize(const EXRHeader& header) {
    size_t size = 0;
    int32_t width = header.data_window.width();

    for (const auto& ch : header.channels) {
        size_t ch_width = (width + ch.x_sampling - 1) / ch.x_sampling;
        size += ch_width * ch.bytes_per_pixel();
    }

    return size;
}

static int GetLinesPerBlock(Compression comp) {
    switch (comp) {
        case Compression::None:
        case Compression::RLE:
        case Compression::ZIPS:
            return 1;
        case Compression::ZIP:
            return 16;
        case Compression::PIZ:
            return 32;
        case Compression::PXR24:
            return 16;
        case Compression::B44:
        case Compression::B44A:
            return 32;
        case Compression::DWAA:
            return 32;
        case Compression::DWAB:
            return 256;
        default:
            return 1;
    }
}

static bool DecompressScanlines(const uint8_t* src, size_t src_size,
                                 std::vector<uint8_t>& out,
                                 Compression comp, size_t expected_size,
                                 size_t max_size) {
    out.clear();

    switch (comp) {
        case Compression::None:
            if (src_size != expected_size) return false;
            out.assign(src, src + src_size);
            return true;

        case Compression::RLE:
            return DecodeRLE(src, src_size, out, expected_size);

        case Compression::ZIPS:
        case Compression::ZIP:
            return deflate::Inflate(src, src_size, out, max_size);

        default:
            return false;  // Unsupported compression
    }
}

// EXR stores channels interleaved within each scanline, in alphabetical order
// Each scanline contains all channels concatenated

static void ReorderScanline(const uint8_t* src, size_t /*scanline_size*/,
                            int width, const std::vector<Channel>& channels,
                            std::vector<float>& out, int y_in_block,
                            const std::vector<std::string>& channel_order,
                            int image_height, bool flip_y) {
    // For each output pixel position
    size_t num_out_channels = channel_order.size();
    size_t offset = 0;

    // EXR stores each channel contiguously within the scanline
    // Order is alphabetical by channel name
    std::vector<size_t> channel_offsets(channels.size());
    for (size_t i = 0; i < channels.size(); ++i) {
        channel_offsets[i] = offset;
        size_t ch_width = (width + channels[i].x_sampling - 1) / channels[i].x_sampling;
        offset += ch_width * channels[i].bytes_per_pixel();
    }

    // Map output channel names to input channel indices
    std::vector<int> channel_map(num_out_channels, -1);
    for (size_t i = 0; i < num_out_channels; ++i) {
        for (size_t j = 0; j < channels.size(); ++j) {
            if (channels[j].name == channel_order[i]) {
                channel_map[i] = static_cast<int>(j);
                break;
            }
        }
    }

    // Convert pixels
    for (int x = 0; x < width; ++x) {
        int out_y = flip_y ? (image_height - 1 - y_in_block) : y_in_block;
        size_t out_idx = (static_cast<size_t>(out_y) * width + x) * num_out_channels;

        for (size_t c = 0; c < num_out_channels; ++c) {
            float value = 0.0f;

            int ch_idx = channel_map[c];
            if (ch_idx >= 0) {
                const Channel& ch = channels[ch_idx];
                size_t ch_x = x / ch.x_sampling;
                size_t ch_offset = channel_offsets[ch_idx] + ch_x * ch.bytes_per_pixel();

                switch (ch.pixel_type) {
                    case PixelType::Half: {
                        uint16_t h;
                        std::memcpy(&h, src + ch_offset, 2);
                        value = HalfToFloat(h);
                        break;
                    }
                    case PixelType::Float: {
                        std::memcpy(&value, src + ch_offset, 4);
                        break;
                    }
                    case PixelType::UInt: {
                        uint32_t u;
                        std::memcpy(&u, src + ch_offset, 4);
                        value = static_cast<float>(u);
                        break;
                    }
                }
            } else {
                // Missing channel - use default value
                if (channel_order[c] == "A") {
                    value = 1.0f;  // Default alpha = 1
                }
            }

            out[out_idx + c] = value;
        }
    }
}

static Result LoadPixelData(BinaryReader& reader, EXRImage& image,
                            const LoadOptions& options) {
    const EXRHeader& header = image.header;
    int32_t width = header.width();
    int32_t height = header.height();
    int32_t y_min = header.data_window.y_min;

    // Determine output channel order
    if (options.convert_to_rgba) {
        image.channel_order = {"R", "G", "B", "A"};
    } else {
        // Sort channels alphabetically (EXR order)
        for (const auto& ch : header.channels) {
            image.channel_order.push_back(ch.name);
        }
        std::sort(image.channel_order.begin(), image.channel_order.end());
    }

    // Allocate output
    size_t num_channels = image.channel_order.size();
    size_t total_pixels = static_cast<size_t>(width) * height * num_channels;

    size_t max_memory = options.max_memory > 0 ? options.max_memory : kMaxMemoryUsage;
    if (total_pixels * sizeof(float) > max_memory) {
        return Result::Error("Image exceeds memory limit");
    }

    image.pixels.resize(total_pixels, 0.0f);

    // Read scanline offset table
    int lines_per_block = GetLinesPerBlock(header.compression);
    int num_blocks = (height + lines_per_block - 1) / lines_per_block;

    std::vector<uint64_t> offsets(num_blocks);
    for (int i = 0; i < num_blocks; ++i) {
        if (!reader.read_u64(offsets[i])) {
            return Result::Error("Failed to read scanline offset");
        }
    }

    size_t scanline_size = GetScanlineSize(header);
    (void)lines_per_block;  // Used for multi-line blocks

    // Process each block
    for (int block = 0; block < num_blocks; ++block) {
        if (!reader.seek(offsets[block])) {
            return Result::Error("Invalid scanline offset");
        }

        int32_t y;
        uint32_t data_size;
        if (!reader.read_i32(y) || !reader.read_u32(data_size)) {
            return Result::Error("Failed to read scanline header");
        }

        if (data_size > reader.remaining()) {
            return Result::Error("Invalid scanline data size");
        }

        int block_y = y - y_min;
        int lines_in_block = std::min(lines_per_block, height - block_y);
        size_t expected_size = scanline_size * lines_in_block;

        std::vector<uint8_t> decompressed;
        const uint8_t* scanline_data = reader.current_ptr();

        if (!DecompressScanlines(scanline_data, data_size, decompressed,
                                  header.compression, expected_size, max_memory)) {
            return Result::Error("Failed to decompress scanline");
        }

        // Process each scanline in the block
        for (int line = 0; line < lines_in_block; ++line) {
            int abs_y = block_y + line;
            const uint8_t* line_data = decompressed.data() + line * scanline_size;

            ReorderScanline(line_data, scanline_size, width, header.channels,
                           image.pixels, abs_y, image.channel_order,
                           height, options.flip_vertically);
        }
    }

    return Result::Ok();
}

// ============================================================================
// Public API
// ============================================================================

bool IsEXR(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    uint32_t magic;
    std::memcpy(&magic, data, 4);
    return magic == kMagicNumber;
}

Result ParseEXRHeaderFromMemory(const uint8_t* data, size_t size, EXRHeader* header) {
    if (!data || size == 0 || !header) {
        return Result::Error("Invalid arguments");
    }

    BinaryReader reader(data, size);
    return ParseHeaderInternal(reader, *header);
}

Result ParseEXRHeader(const std::string& filename, EXRHeader* header) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result::Error("Failed to open file: " + filename);
    }

    size_t size = file.tellg();
    file.seekg(0);

    // Only need to read header (first ~8KB should be enough)
    size_t read_size = std::min(size, static_cast<size_t>(64 * 1024));
    std::vector<uint8_t> data(read_size);
    file.read(reinterpret_cast<char*>(data.data()), read_size);

    return ParseEXRHeaderFromMemory(data.data(), data.size(), header);
}

Result LoadEXRFromMemory(const uint8_t* data, size_t size, EXRImage* image,
                         const LoadOptions& options) {
    if (!data || size == 0 || !image) {
        return Result::Error("Invalid arguments");
    }

    BinaryReader reader(data, size);

    // Parse header
    Result result = ParseHeaderInternal(reader, image->header);
    if (!result) return result;

    // Check compression support
    Compression comp = image->header.compression;
    if (comp != Compression::None && comp != Compression::RLE &&
        comp != Compression::ZIPS && comp != Compression::ZIP) {
        return Result::Error("Unsupported compression method");
    }

    // Load pixel data
    return LoadPixelData(reader, *image, options);
}

Result LoadEXR(const std::string& filename, EXRImage* image,
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

    return LoadEXRFromMemory(data.data(), data.size(), image, options);
}

// ============================================================================
// Writer (minimal implementation)
// ============================================================================

namespace {

class BinaryWriter {
public:
    std::vector<uint8_t>& buffer;

    explicit BinaryWriter(std::vector<uint8_t>& buf) : buffer(buf) {}

    void write_u8(uint8_t v) { buffer.push_back(v); }
    void write_u16(uint16_t v) { write_bytes(&v, 2); }
    void write_u32(uint32_t v) { write_bytes(&v, 4); }
    void write_u64(uint64_t v) { write_bytes(&v, 8); }
    void write_i32(int32_t v) { write_bytes(&v, 4); }
    void write_f32(float v) { write_bytes(&v, 4); }

    void write_bytes(const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        buffer.insert(buffer.end(), ptr, ptr + size);
    }

    void write_string(const std::string& s) {
        buffer.insert(buffer.end(), s.begin(), s.end());
        buffer.push_back(0);
    }

    size_t position() const { return buffer.size(); }
};

void WriteAttribute(BinaryWriter& w, const char* name, const char* type,
                    const void* data, size_t size) {
    w.write_string(name);
    w.write_string(type);
    w.write_u32(static_cast<uint32_t>(size));
    w.write_bytes(data, size);
}

}  // namespace

Result SaveEXR(const std::string& filename, int width, int height,
               int num_channels, const float* pixels,
               const char* const* channel_names,
               const SaveOptions& options) {
    if (width <= 0 || height <= 0 || num_channels <= 0 || num_channels > 4) {
        return Result::Error("Invalid image dimensions");
    }
    if (!pixels) {
        return Result::Error("Null pixel data");
    }

    // Default channel names
    static const char* kDefaultNames[] = {"R", "G", "B", "A"};
    if (!channel_names) {
        channel_names = kDefaultNames;
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(width * height * num_channels * 2 + 1024);

    BinaryWriter writer(buffer);

    // Magic and version
    writer.write_u32(kMagicNumber);
    writer.write_u32(kVersionNumber);  // Scanline, no special flags

    // Channels attribute (sorted alphabetically)
    std::vector<std::pair<std::string, int>> sorted_channels;
    for (int i = 0; i < num_channels; ++i) {
        sorted_channels.push_back({channel_names[i], i});
    }
    std::sort(sorted_channels.begin(), sorted_channels.end());

    {
        std::vector<uint8_t> ch_data;
        BinaryWriter ch_writer(ch_data);
        for (const auto& ch : sorted_channels) {
            ch_writer.write_string(ch.first);
            ch_writer.write_u32(options.write_half ? 1 : 2);  // Half or Float
            ch_writer.write_u8(0);  // pLinear
            ch_writer.write_bytes("\0\0\0", 3);  // Reserved
            ch_writer.write_u32(1);  // xSampling
            ch_writer.write_u32(1);  // ySampling
        }
        ch_writer.write_u8(0);  // End of channel list

        WriteAttribute(writer, "channels", "chlist", ch_data.data(), ch_data.size());
    }

    // Compression
    {
        uint8_t comp = static_cast<uint8_t>(options.compression);
        WriteAttribute(writer, "compression", "compression", &comp, 1);
    }

    // Data window
    {
        int32_t box[4] = {0, 0, width - 1, height - 1};
        WriteAttribute(writer, "dataWindow", "box2i", box, 16);
    }

    // Display window
    {
        int32_t box[4] = {0, 0, width - 1, height - 1};
        WriteAttribute(writer, "displayWindow", "box2i", box, 16);
    }

    // Line order
    {
        uint8_t order = 0;  // Increasing Y
        WriteAttribute(writer, "lineOrder", "lineOrder", &order, 1);
    }

    // Pixel aspect ratio
    {
        float par = 1.0f;
        WriteAttribute(writer, "pixelAspectRatio", "float", &par, 4);
    }

    // Screen window center
    {
        float center[2] = {0.0f, 0.0f};
        WriteAttribute(writer, "screenWindowCenter", "v2f", center, 8);
    }

    // Screen window width
    {
        float sw = 1.0f;
        WriteAttribute(writer, "screenWindowWidth", "float", &sw, 4);
    }

    // End of header
    writer.write_u8(0);

    // Scanline offset table (placeholder)
    size_t offset_table_pos = buffer.size();
    for (int y = 0; y < height; ++y) {
        writer.write_u64(0);
    }

    // Write scanlines
    size_t bytes_per_channel = options.write_half ? 2 : 4;
    size_t scanline_size = width * num_channels * bytes_per_channel;

    std::vector<uint64_t> offsets(height);
    std::vector<uint8_t> scanline_buffer(scanline_size);

    for (int y = 0; y < height; ++y) {
        offsets[y] = buffer.size();

        // Build scanline (channels in sorted order, contiguous)
        size_t offset = 0;
        for (const auto& ch : sorted_channels) {
            int ch_idx = ch.second;
            for (int x = 0; x < width; ++x) {
                float value = pixels[(y * width + x) * num_channels + ch_idx];

                if (options.write_half) {
                    uint16_t h = FloatToHalf(value);
                    std::memcpy(scanline_buffer.data() + offset, &h, 2);
                    offset += 2;
                } else {
                    std::memcpy(scanline_buffer.data() + offset, &value, 4);
                    offset += 4;
                }
            }
        }

        // Write scanline header
        writer.write_i32(y);

        if (options.compression == Compression::None) {
            writer.write_u32(static_cast<uint32_t>(scanline_size));
            writer.write_bytes(scanline_buffer.data(), scanline_size);
        } else {
            // For simplicity, just write uncompressed
            // TODO: Implement RLE/ZIP compression
            writer.write_u32(static_cast<uint32_t>(scanline_size));
            writer.write_bytes(scanline_buffer.data(), scanline_size);
        }
    }

    // Update offset table
    for (int y = 0; y < height; ++y) {
        std::memcpy(buffer.data() + offset_table_pos + y * 8, &offsets[y], 8);
    }

    // Write to file
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return Result::Error("Failed to create file: " + filename);
    }

    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    if (!file) {
        return Result::Error("Failed to write file: " + filename);
    }

    return Result::Ok();
}

}  // namespace lightexr
}  // namespace v1
}  // namespace lightusd
