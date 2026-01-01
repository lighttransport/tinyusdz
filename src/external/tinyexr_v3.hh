/*
 * TinyEXR V3 - C++17 Wrapper
 *
 * A modern C++17 wrapper around the pure C TinyEXR API.
 * Provides RAII handle management, Result<T> error handling,
 * and range-based iteration over tiles and scanlines.
 *
 * Features:
 * - RAII wrappers for C handles (Context, Decoder, Image, Part)
 * - Result<T> with monadic operations (map, and_then, or_else)
 * - Range-based iteration for tiles and scanlines
 * - High-level convenience functions (load, save)
 * - No exceptions - uses Result<T> pattern
 * - Compatible with C++17 and later
 *
 * Copyright (c) 2024 TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TINYEXR_V3_HH_
#define TINYEXR_V3_HH_

#include "tinyexr_c.h"

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <cstring>
#include <algorithm>
#include <type_traits>

namespace tinyexr {
namespace v3 {

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * Extended error information with context.
 */
struct ErrorInfo {
    ExrResult code = EXR_SUCCESS;
    std::string message;
    std::string context;
    uint64_t byte_position = 0;

    ErrorInfo() = default;

    ErrorInfo(ExrResult c, std::string msg, std::string ctx = "", uint64_t pos = 0)
        : code(c), message(std::move(msg)), context(std::move(ctx)), byte_position(pos) {}

    explicit ErrorInfo(const ExrErrorInfo& info)
        : code(info.code)
        , message(info.message ? info.message : "")
        , context(info.context ? info.context : "")
        , byte_position(info.byte_position) {}

    bool is_success() const { return code == EXR_SUCCESS; }
    bool is_error() const { return code < 0; }

    std::string to_string() const {
        std::string result = exr_result_to_string(code);
        if (!message.empty()) {
            result += ": " + message;
        }
        if (!context.empty()) {
            result += " [" + context + "]";
        }
        if (byte_position > 0) {
            result += " at byte " + std::to_string(byte_position);
        }
        return result;
    }
};

/**
 * Result type for operations that can fail.
 * Similar to std::expected (C++23) but with additional features.
 */
template<typename T>
class Result {
public:
    bool success = false;
    T value{};
    std::vector<ErrorInfo> errors;
    std::vector<std::string> warnings;

    Result() = default;

    static Result<T> ok(T val) {
        Result<T> r;
        r.success = true;
        r.value = std::move(val);
        return r;
    }

    static Result<T> error(ErrorInfo err) {
        Result<T> r;
        r.success = false;
        r.errors.push_back(std::move(err));
        return r;
    }

    static Result<T> error(ExrResult code, const std::string& msg = "") {
        return error(ErrorInfo(code, msg));
    }

    explicit operator bool() const { return success; }

    T value_or(T default_val) const {
        return success ? value : default_val;
    }

    ErrorInfo first_error() const {
        return errors.empty() ? ErrorInfo() : errors[0];
    }

    std::string error_string() const {
        std::string result;
        for (size_t i = 0; i < errors.size(); i++) {
            if (i > 0) result += "\n";
            result += errors[i].to_string();
        }
        return result;
    }

    void add_warning(const std::string& warning) {
        warnings.push_back(warning);
    }

    void add_error(ErrorInfo err) {
        success = false;
        errors.push_back(std::move(err));
    }

    // Monadic operations
    template<typename F>
    auto map(F&& f) const -> Result<std::invoke_result_t<F, const T&>> {
        using U = std::invoke_result_t<F, const T&>;
        if (success) {
            return Result<U>::ok(std::forward<F>(f)(value));
        }
        Result<U> result;
        result.success = false;
        result.errors = errors;
        result.warnings = warnings;
        return result;
    }

    template<typename F>
    auto and_then(F&& f) const -> std::invoke_result_t<F, const T&> {
        using ResultType = std::invoke_result_t<F, const T&>;
        if (success) {
            return std::forward<F>(f)(value);
        }
        ResultType result;
        result.success = false;
        result.errors = errors;
        result.warnings = warnings;
        return result;
    }

    template<typename F>
    Result<T> or_else(F&& f) const {
        if (success) {
            return *this;
        }
        return std::forward<F>(f)(first_error());
    }
};

/**
 * Specialization for void results.
 */
template<>
class Result<void> {
public:
    bool success = true;
    std::vector<ErrorInfo> errors;
    std::vector<std::string> warnings;

    Result() = default;

    static Result<void> ok() {
        Result<void> r;
        r.success = true;
        return r;
    }

    static Result<void> error(ErrorInfo err) {
        Result<void> r;
        r.success = false;
        r.errors.push_back(std::move(err));
        return r;
    }

    static Result<void> error(ExrResult code, const std::string& msg = "") {
        return error(ErrorInfo(code, msg));
    }

    explicit operator bool() const { return success; }

    ErrorInfo first_error() const {
        return errors.empty() ? ErrorInfo() : errors[0];
    }

    std::string error_string() const {
        std::string result;
        for (size_t i = 0; i < errors.size(); i++) {
            if (i > 0) result += "\n";
            result += errors[i].to_string();
        }
        return result;
    }

    void add_warning(const std::string& warning) {
        warnings.push_back(warning);
    }

    void add_error(ErrorInfo err) {
        success = false;
        errors.push_back(std::move(err));
    }
};

/* ============================================================================
 * RAII Handle Wrappers
 * ============================================================================ */

namespace detail {

/**
 * Generic RAII wrapper for C handles.
 * Uses explicit deleter function pointer for portability.
 */
template<typename Handle, typename DeleterType>
class HandleWrapper {
protected:
    Handle handle_ = nullptr;
    DeleterType deleter_ = nullptr;

public:
    HandleWrapper() = default;
    HandleWrapper(Handle h, DeleterType d) : handle_(h), deleter_(d) {}

    ~HandleWrapper() {
        if (handle_ && deleter_) {
            deleter_(handle_);
        }
    }

    // Move-only semantics
    HandleWrapper(const HandleWrapper&) = delete;
    HandleWrapper& operator=(const HandleWrapper&) = delete;

    HandleWrapper(HandleWrapper&& other) noexcept
        : handle_(other.handle_), deleter_(other.deleter_) {
        other.handle_ = nullptr;
        other.deleter_ = nullptr;
    }

    HandleWrapper& operator=(HandleWrapper&& other) noexcept {
        if (this != &other) {
            if (handle_ && deleter_) deleter_(handle_);
            handle_ = other.handle_;
            deleter_ = other.deleter_;
            other.handle_ = nullptr;
            other.deleter_ = nullptr;
        }
        return *this;
    }

    Handle get() const { return handle_; }

    Handle release() {
        Handle h = handle_;
        handle_ = nullptr;
        return h;
    }

    explicit operator bool() const { return handle_ != nullptr; }

    void reset(Handle h = nullptr) {
        if (handle_ && deleter_) deleter_(handle_);
        handle_ = h;
    }
};

} // namespace detail

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

class Context;
class Decoder;
class Image;
class Part;
class TileRange;
class ScanlineRange;

/* ============================================================================
 * Context
 * ============================================================================ */

/**
 * Global context for TinyEXR operations.
 * Owns allocator settings and error state.
 */
class Context : public detail::HandleWrapper<ExrContext, void(*)(ExrContext)> {
    using Base = detail::HandleWrapper<ExrContext, void(*)(ExrContext)>;

public:
    struct CreateInfo {
        bool enable_validation = false;
        bool enable_debug = false;
        bool single_threaded = false;
        uint32_t max_threads = 0;  // 0 = auto

        static CreateInfo defaults() { return CreateInfo{}; }
    };

    Context() : Base(nullptr, exr_context_destroy) {}

    static Result<Context> create(const CreateInfo& info = CreateInfo::defaults()) {
        ExrContextCreateInfo c_info{};
        c_info.api_version = TINYEXR_C_API_VERSION;
        c_info.allocator = nullptr;  // Use default
        c_info.flags = 0;
        if (info.enable_validation) c_info.flags |= EXR_CONTEXT_ENABLE_VALIDATION;
        if (info.enable_debug) c_info.flags |= EXR_CONTEXT_ENABLE_DEBUG_INFO;
        if (info.single_threaded) c_info.flags |= EXR_CONTEXT_SINGLE_THREADED;
        c_info.max_threads = info.max_threads;

        ExrContext handle = nullptr;
        ExrResult result = exr_context_create(&c_info, &handle);
        if (result != EXR_SUCCESS) {
            return Result<Context>::error(result, "Failed to create context");
        }

        Context ctx;
        ctx.handle_ = handle;
        ctx.deleter_ = exr_context_destroy;
        return Result<Context>::ok(std::move(ctx));
    }

    std::string get_error_message() const {
        if (!handle_) return "";
        ExrErrorInfo info{};
        if (exr_get_last_error(handle_, &info) == EXR_SUCCESS && info.message) {
            return info.message;
        }
        return "";
    }

    void clear_errors() {
        if (handle_) {
            exr_clear_errors(handle_);
        }
    }
};

/* ============================================================================
 * Decoder
 * ============================================================================ */

/**
 * EXR file decoder.
 * Owns the data source and parsed image metadata.
 */
class Decoder : public detail::HandleWrapper<ExrDecoder, void(*)(ExrDecoder)> {
    using Base = detail::HandleWrapper<ExrDecoder, void(*)(ExrDecoder)>;

    Context* context_ = nullptr;
    std::vector<uint8_t> memory_data_;  // For memory source lifetime

public:
    Decoder() : Base(nullptr, exr_decoder_destroy) {}

    /**
     * Create decoder from memory buffer.
     * The buffer is copied internally.
     */
    static Result<Decoder> from_memory(Context& ctx, const uint8_t* data, size_t size) {
        if (!ctx) {
            return Result<Decoder>::error(EXR_ERROR_INVALID_HANDLE, "Invalid context");
        }

        Decoder decoder;
        decoder.context_ = &ctx;
        decoder.memory_data_.assign(data, data + size);

        ExrDataSource source{};
        ExrResult result = exr_data_source_from_memory(
            decoder.memory_data_.data(),
            decoder.memory_data_.size(),
            &source
        );
        if (result != EXR_SUCCESS) {
            return Result<Decoder>::error(result, "Failed to create data source");
        }

        ExrDecoderCreateInfo create_info{};
        create_info.source = source;
        create_info.scratch_pool = nullptr;
        create_info.flags = 0;

        ExrDecoder handle = nullptr;
        result = exr_decoder_create(ctx.get(), &create_info, &handle);
        if (result != EXR_SUCCESS) {
            return Result<Decoder>::error(result, "Failed to create decoder");
        }

        decoder.handle_ = handle;
        decoder.deleter_ = exr_decoder_destroy;
        return Result<Decoder>::ok(std::move(decoder));
    }

    /**
     * Create decoder from vector.
     */
    static Result<Decoder> from_memory(Context& ctx, const std::vector<uint8_t>& data) {
        return from_memory(ctx, data.data(), data.size());
    }

    /**
     * Parse EXR header and return Image handle.
     */
    Result<Image> parse_header();

    Context* context() const { return context_; }
};

/* ============================================================================
 * Image
 * ============================================================================ */

/**
 * Parsed EXR image metadata.
 * Provides access to header info, channels, and parts.
 */
class Image : public detail::HandleWrapper<ExrImage, void(*)(ExrImage)> {
    using Base = detail::HandleWrapper<ExrImage, void(*)(ExrImage)>;

    Decoder* decoder_ = nullptr;

public:
    Image() : Base(nullptr, exr_image_destroy) {}

    int width() const {
        ExrImageInfo info{};
        if (handle_ && exr_image_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.width;
        }
        return 0;
    }

    int height() const {
        ExrImageInfo info{};
        if (handle_ && exr_image_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.height;
        }
        return 0;
    }

    uint32_t channel_count() const {
        uint32_t count = 0;
        if (handle_) {
            exr_image_get_channel_count(handle_, &count);
        }
        return count;
    }

    uint32_t part_count() const {
        uint32_t count = 0;
        if (handle_) {
            exr_image_get_part_count(handle_, &count);
        }
        return count;
    }

    Result<ExrImageInfo> get_info() const {
        if (!handle_) {
            return Result<ExrImageInfo>::error(EXR_ERROR_INVALID_HANDLE);
        }
        ExrImageInfo info{};
        ExrResult result = exr_image_get_info(handle_, &info);
        if (result != EXR_SUCCESS) {
            return Result<ExrImageInfo>::error(result);
        }
        return Result<ExrImageInfo>::ok(info);
    }

    Result<ExrChannelInfo> get_channel(uint32_t index) const {
        if (!handle_) {
            return Result<ExrChannelInfo>::error(EXR_ERROR_INVALID_HANDLE);
        }
        ExrChannelInfo info{};
        ExrResult result = exr_image_get_channel(handle_, index, &info);
        if (result != EXR_SUCCESS) {
            return Result<ExrChannelInfo>::error(result);
        }
        return Result<ExrChannelInfo>::ok(info);
    }

    Result<Part> get_part(uint32_t index);

    Decoder* decoder() const { return decoder_; }

    friend class Decoder;
};

/* ============================================================================
 * Part
 * ============================================================================ */

/**
 * Single part within an EXR image.
 * Provides access to tiles or scanlines.
 */
class Part {
    ExrPart handle_ = nullptr;
    Image* image_ = nullptr;

public:
    Part() = default;
    Part(ExrPart h, Image* img) : handle_(h), image_(img) {}

    // Parts are lightweight and can be copied
    Part(const Part&) = default;
    Part& operator=(const Part&) = default;

    explicit operator bool() const { return handle_ != nullptr; }

    Result<ExrPartInfo> get_info() const {
        if (!handle_) {
            return Result<ExrPartInfo>::error(EXR_ERROR_INVALID_HANDLE);
        }
        ExrPartInfo info{};
        ExrResult result = exr_part_get_info(handle_, &info);
        if (result != EXR_SUCCESS) {
            return Result<ExrPartInfo>::error(result);
        }
        return Result<ExrPartInfo>::ok(info);
    }

    int width() const {
        ExrPartInfo info{};
        if (handle_ && exr_part_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.width;
        }
        return 0;
    }

    int height() const {
        ExrPartInfo info{};
        if (handle_ && exr_part_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.height;
        }
        return 0;
    }

    uint32_t channel_count() const {
        ExrPartInfo info{};
        if (handle_ && exr_part_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.num_channels;
        }
        return 0;
    }

    bool is_tiled() const {
        ExrPartInfo info{};
        if (handle_ && exr_part_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.part_type == EXR_PART_TILED || info.part_type == EXR_PART_DEEP_TILED;
        }
        return false;
    }

    bool is_deep() const {
        ExrPartInfo info{};
        if (handle_ && exr_part_get_info(handle_, &info) == EXR_SUCCESS) {
            return info.part_type == EXR_PART_DEEP_SCANLINE ||
                   info.part_type == EXR_PART_DEEP_TILED;
        }
        return false;
    }

    Result<ExrChannelInfo> get_channel(uint32_t index) const {
        if (!handle_) {
            return Result<ExrChannelInfo>::error(EXR_ERROR_INVALID_HANDLE);
        }
        ExrChannelInfo info{};
        ExrResult result = exr_part_get_channel(handle_, index, &info);
        if (result != EXR_SUCCESS) {
            return Result<ExrChannelInfo>::error(result);
        }
        return Result<ExrChannelInfo>::ok(info);
    }

    uint32_t chunk_count() const {
        uint32_t count = 0;
        if (handle_) {
            exr_part_get_chunk_count(handle_, &count);
        }
        return count;
    }

    // Tile iteration (implemented below)
    TileRange tiles(int level = 0) const;

    // Scanline iteration (implemented below)
    ScanlineRange scanlines() const;

    Image* image() const { return image_; }
    ExrPart get() const { return handle_; }
};

/* ============================================================================
 * Tile Coordinate
 * ============================================================================ */

struct TileCoord {
    int level_x = 0;
    int level_y = 0;
    int tile_x = 0;
    int tile_y = 0;

    bool operator==(const TileCoord& other) const {
        return level_x == other.level_x && level_y == other.level_y &&
               tile_x == other.tile_x && tile_y == other.tile_y;
    }

    bool operator!=(const TileCoord& other) const {
        return !(*this == other);
    }
};

/* ============================================================================
 * Tile Iterator
 * ============================================================================ */

class TileIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = TileCoord;
    using difference_type = std::ptrdiff_t;
    using pointer = const TileCoord*;
    using reference = const TileCoord&;

private:
    Part part_;
    int level_;
    int tile_x_ = 0;
    int tile_y_ = 0;
    int max_tile_x_ = 0;
    int max_tile_y_ = 0;
    TileCoord current_{};

    void update_current() {
        current_.level_x = level_;
        current_.level_y = level_;
        current_.tile_x = tile_x_;
        current_.tile_y = tile_y_;
    }

public:
    TileIterator() = default;

    TileIterator(Part part, int level, int tx, int ty, int max_tx, int max_ty)
        : part_(part), level_(level), tile_x_(tx), tile_y_(ty),
          max_tile_x_(max_tx), max_tile_y_(max_ty) {
        update_current();
    }

    reference operator*() const { return current_; }
    pointer operator->() const { return &current_; }

    TileIterator& operator++() {
        ++tile_x_;
        if (tile_x_ >= max_tile_x_) {
            tile_x_ = 0;
            ++tile_y_;
        }
        update_current();
        return *this;
    }

    TileIterator operator++(int) {
        TileIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const TileIterator& other) const {
        return tile_x_ == other.tile_x_ && tile_y_ == other.tile_y_;
    }

    bool operator!=(const TileIterator& other) const {
        return !(*this == other);
    }
};

/* ============================================================================
 * Tile Range
 * ============================================================================ */

class TileRange {
    Part part_;
    int level_;
    int num_tiles_x_ = 0;
    int num_tiles_y_ = 0;

public:
    TileRange(Part part, int level, int ntx, int nty)
        : part_(part), level_(level), num_tiles_x_(ntx), num_tiles_y_(nty) {}

    TileIterator begin() const {
        return TileIterator(part_, level_, 0, 0, num_tiles_x_, num_tiles_y_);
    }

    TileIterator end() const {
        return TileIterator(part_, level_, 0, num_tiles_y_, num_tiles_x_, num_tiles_y_);
    }

    size_t size() const {
        return static_cast<size_t>(num_tiles_x_) * num_tiles_y_;
    }

    bool empty() const { return size() == 0; }
};

/* ============================================================================
 * Scanline Iterator
 * ============================================================================ */

class ScanlineIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = const int*;
    using reference = const int&;

private:
    Part part_;
    int y_ = 0;
    int height_ = 0;
    int block_size_ = 1;

public:
    ScanlineIterator() = default;

    ScanlineIterator(Part part, int y, int height, int block_size)
        : part_(part), y_(y), height_(height), block_size_(block_size) {}

    reference operator*() const { return y_; }
    pointer operator->() const { return &y_; }

    ScanlineIterator& operator++() {
        y_ += block_size_;
        return *this;
    }

    ScanlineIterator operator++(int) {
        ScanlineIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const ScanlineIterator& other) const {
        return y_ == other.y_;
    }

    bool operator!=(const ScanlineIterator& other) const {
        return !(*this == other);
    }

    int lines_in_block() const {
        return std::min(block_size_, height_ - y_);
    }
};

/* ============================================================================
 * Scanline Range
 * ============================================================================ */

class ScanlineRange {
    Part part_;
    int height_ = 0;
    int block_size_ = 16;  // Default scanline block size

public:
    ScanlineRange(Part part, int height, int block_size = 16)
        : part_(part), height_(height), block_size_(block_size) {}

    ScanlineIterator begin() const {
        return ScanlineIterator(part_, 0, height_, block_size_);
    }

    ScanlineIterator end() const {
        int end_y = ((height_ + block_size_ - 1) / block_size_) * block_size_;
        return ScanlineIterator(part_, end_y, height_, block_size_);
    }

    size_t size() const {
        return (height_ + block_size_ - 1) / block_size_;
    }

    bool empty() const { return height_ == 0; }
};

/* ============================================================================
 * Inline Implementations
 * ============================================================================ */

inline Result<Image> Decoder::parse_header() {
    if (!handle_) {
        return Result<Image>::error(EXR_ERROR_INVALID_HANDLE, "Invalid decoder");
    }

    ExrImage img_handle = nullptr;
    ExrResult result = exr_decoder_parse_header(handle_, &img_handle);
    if (result != EXR_SUCCESS) {
        return Result<Image>::error(result, "Failed to parse header");
    }

    Image img;
    img.handle_ = img_handle;
    img.decoder_ = this;
    return Result<Image>::ok(std::move(img));
}

inline Result<Part> Image::get_part(uint32_t index) {
    if (!handle_) {
        return Result<Part>::error(EXR_ERROR_INVALID_HANDLE);
    }

    ExrPart part_handle = nullptr;
    ExrResult result = exr_image_get_part(handle_, index, &part_handle);
    if (result != EXR_SUCCESS) {
        return Result<Part>::error(result);
    }

    return Result<Part>::ok(Part(part_handle, this));
}

inline TileRange Part::tiles(int level) const {
    uint32_t x_tiles = 0, y_tiles = 0;
    if (handle_) {
        exr_part_get_tile_count(handle_, level, level, &x_tiles, &y_tiles);
    }
    return TileRange(*this, level, static_cast<int>(x_tiles), static_cast<int>(y_tiles));
}

inline ScanlineRange Part::scanlines() const {
    return ScanlineRange(*this, height());
}

/* ============================================================================
 * High-Level Image Data Structure
 * ============================================================================ */

/**
 * Loaded image data with pixel values.
 */
struct ImageData {
    int width = 0;
    int height = 0;
    int num_channels = 0;
    std::vector<float> rgba;  // RGBA float data (width * height * 4)

    struct ChannelInfo {
        std::string name;
        ExrPixelType pixel_type;
        int x_sampling;
        int y_sampling;
    };
    std::vector<ChannelInfo> channels;

    float* pixel_ptr(int x, int y) {
        return &rgba[(y * width + x) * 4];
    }

    const float* pixel_ptr(int x, int y) const {
        return &rgba[(y * width + x) * 4];
    }

    float get(int x, int y, int channel) const {
        return rgba[(y * width + x) * 4 + channel];
    }

    void set(int x, int y, int channel, float value) {
        rgba[(y * width + x) * 4 + channel] = value;
    }
};

/* ============================================================================
 * High-Level Convenience Functions
 * ============================================================================ */

/**
 * Load EXR image from memory.
 * Returns RGBA float data.
 *
 * Note: Full implementation requires header parsing and decompression
 * which are not yet complete in the C API.
 */
inline Result<ImageData> load(const uint8_t* data, size_t size) {
    auto ctx_result = Context::create();
    if (!ctx_result) {
        return Result<ImageData>::error(ctx_result.first_error());
    }

    auto decoder_result = Decoder::from_memory(ctx_result.value, data, size);
    if (!decoder_result) {
        return Result<ImageData>::error(decoder_result.first_error());
    }

    auto image_result = decoder_result.value.parse_header();
    if (!image_result) {
        return Result<ImageData>::error(image_result.first_error());
    }

    ImageData img_data;
    img_data.width = image_result.value.width();
    img_data.height = image_result.value.height();
    img_data.num_channels = image_result.value.channel_count();
    img_data.rgba.resize(img_data.width * img_data.height * 4, 0.0f);

    // TODO: Actually load pixel data once command buffer system is complete

    return Result<ImageData>::ok(std::move(img_data));
}

inline Result<ImageData> load(const std::vector<uint8_t>& data) {
    return load(data.data(), data.size());
}

/**
 * Load EXR image from file.
 */
inline Result<ImageData> load_file(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return Result<ImageData>::error(EXR_ERROR_IO, "Failed to open file");
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::vector<uint8_t> data(size);
    if (fread(data.data(), 1, size, fp) != size) {
        fclose(fp);
        return Result<ImageData>::error(EXR_ERROR_IO, "Failed to read file");
    }
    fclose(fp);

    return load(data);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get SIMD capability string.
 */
inline std::string get_simd_info() {
    return exr_get_simd_info();
}

/**
 * Get library version string.
 */
inline std::string get_version_string() {
    return exr_get_version_string();
}

/* ============================================================================
 * C++20 Coroutine Support (Optional)
 *
 * When compiled with C++20 and coroutines enabled, provides awaitable types
 * for async operations:
 *
 * Example usage:
 *   Task<Result<Image>> load_async(const std::string& path) {
 *       auto decoder = co_await create_decoder(path);
 *       auto image = co_await decoder->parse_header_async();
 *       // ...
 *   }
 *
 * To enable, define TINYEXR_V3_ENABLE_COROUTINES before including this header.
 * ============================================================================ */

#if defined(TINYEXR_V3_ENABLE_COROUTINES) && __cplusplus >= 202002L
#include <coroutine>

/**
 * Basic async task type for coroutines.
 * This is a minimal implementation - users may want to integrate with
 * their own async runtime (e.g., cppcoro, folly::coro, asio).
 */
template<typename T>
struct AsyncTask {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        T value;
        std::exception_ptr exception;

        AsyncTask get_return_object() {
            return AsyncTask(handle_type::from_promise(*this));
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() { exception = std::current_exception(); }
    };

    handle_type coro;

    explicit AsyncTask(handle_type h) : coro(h) {}
    AsyncTask(AsyncTask&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    ~AsyncTask() { if (coro) coro.destroy(); }

    T get() {
        if (coro.promise().exception)
            std::rethrow_exception(coro.promise().exception);
        return std::move(coro.promise().value);
    }

    bool done() const { return coro.done(); }
    void resume() { if (!coro.done()) coro.resume(); }
};

/**
 * Awaitable for async fetch operations.
 * When awaited, suspends the coroutine until the fetch completes.
 */
struct FetchAwaitable {
    ExrDecoder decoder;
    ExrSuspendState suspend_state = nullptr;
    ExrResult result = EXR_SUCCESS;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        /* Store the coroutine handle for later resumption */
        /* In a real implementation, this would register with an async runtime */
        (void)h;
        exr_decoder_get_suspend_state(decoder, &suspend_state);
    }

    ExrResult await_resume() noexcept {
        return result;
    }
};

#endif /* TINYEXR_V3_ENABLE_COROUTINES && C++20 */

} // namespace v3
} // namespace tinyexr

#endif // TINYEXR_V3_HH_
