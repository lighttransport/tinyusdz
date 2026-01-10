// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
#include <emscripten/bind.h>
#include <emscripten/console.h>
#include <emscripten/em_js.h>
#include <emscripten/fetch.h>
#include <emscripten/emscripten.h>

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>

//#include "external/fast_float/include/fast_float/bigint.h"
#include "tinyusdz.hh"
#include "pprinter.hh"
#include "typed-array.hh"
#include "value-types.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/material-serializer.hh"

#include "tydra/mcp-context.hh"
#include "tydra/mcp-resources.hh"
#include "tydra/mcp-tools.hh"
#include "usd-to-json.hh"
#include "json-to-usd.hh"
#include "sha256.hh"
#include "logger.hh"
#include "image-loader.hh"

// TinyEXR for EXR decoding
#if defined(TINYUSDZ_WITH_EXR)
#include "external/tinyexr.h"
#endif

// stb_image for HDR (Radiance RGBE) decoding
// Only compile HDR support to minimize code size
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// Handling Asset
// Due to the limitatrion of C++(synchronous) initiated async file(fetch) read,
// We decided to fetch asset in JavaScript layer.
//
// 1. First list up assets(textures, USD scenes(for composition)
// 2. Load(fetch) assets to memory in JavaScript layer
// 3. Set binary data to EMAssetResolutionResolver.
// 4. Use EMAssetResolutionResolver to load asset(simply lookup binary data by asset name)
//


using namespace emscripten;

// ============================================================================
// EM_JS: Synchronous JavaScript callbacks for progress reporting
// These functions are called from C++ during Tydra conversion to report
// progress to JavaScript in real-time without ASYNCIFY.
// ============================================================================

// Report mesh conversion progress
// Called for each mesh during Tydra conversion
EM_JS(void, reportTydraProgress, (int current, int total, const char* stage, const char* meshName, float progress), {
  if (typeof Module.onTydraProgress === 'function') {
    Module.onTydraProgress({
      meshCurrent: current,
      meshTotal: total,
      stage: UTF8ToString(stage),
      meshName: UTF8ToString(meshName),
      progress: progress
    });
  }
});

// Report conversion stage change
EM_JS(void, reportTydraStage, (const char* stage, const char* message), {
  if (typeof Module.onTydraStage === 'function') {
    Module.onTydraStage({
      stage: UTF8ToString(stage),
      message: UTF8ToString(message)
    });
  }
});

// Report conversion completion
EM_JS(void, reportTydraComplete, (int meshCount, int materialCount, int textureCount), {
  if (typeof Module.onTydraComplete === 'function') {
    Module.onTydraComplete({
      meshCount: meshCount,
      materialCount: materialCount,
      textureCount: textureCount
    });
  }
});

// ============================================================================
// C++20 Coroutine Support: Yield to JavaScript event loop
// ============================================================================
// This allows the browser to repaint between processing phases.
// Returns a Promise that resolves on the next animation frame.
//
// Enable with CMake option: -DTINYUSDZ_WASM_COROUTINE=ON (default)
// Disable with: -DTINYUSDZ_WASM_COROUTINE=OFF

#if defined(TINYUSDZ_USE_COROUTINE)

EM_JS(emscripten::EM_VAL, yieldToEventLoop_impl, (), {
  // Return a Promise that resolves on next animation frame
  // This gives the browser a chance to repaint
  return Emval.toHandle(new Promise(resolve => {
    if (typeof requestAnimationFrame === 'function') {
      requestAnimationFrame(() => resolve());
    } else {
      // Fallback for non-browser environments (Node.js)
      setTimeout(resolve, 0);
    }
  }));
});

// Wrapper for co_await usage
inline emscripten::val yieldToEventLoop() {
  return emscripten::val::take_ownership(yieldToEventLoop_impl());
}

// Helper to yield with a custom delay (milliseconds)
EM_JS(emscripten::EM_VAL, yieldWithDelay_impl, (int delayMs), {
  return Emval.toHandle(new Promise(resolve => {
    setTimeout(resolve, delayMs);
  }));
});

inline emscripten::val yieldWithDelay(int delayMs) {
  return emscripten::val::take_ownership(yieldWithDelay_impl(delayMs));
}

// Report that async operation is starting (for JS progress UI)
EM_JS(void, reportAsyncPhaseStart, (const char* phase, float progress), {
  if (typeof Module.onAsyncPhaseStart === 'function') {
    Module.onAsyncPhaseStart({
      phase: UTF8ToString(phase),
      progress: progress
    });
  }
});

#endif // TINYUSDZ_USE_COROUTINE

namespace detail {

std::array<double, 9> toArray(const tinyusdz::value::matrix3d &m) {
  std::array<double, 9> ret;

  ret[0] = m.m[0][0];
  ret[1] = m.m[0][1];
  ret[2] = m.m[0][2];

  ret[3] = m.m[1][0];
  ret[4] = m.m[1][1];
  ret[5] = m.m[1][2];

  ret[6] = m.m[2][0];
  ret[7] = m.m[2][1];
  ret[8] = m.m[2][2];

  return ret;
}

std::array<double, 16> toArray(const tinyusdz::value::matrix4d &m) {
  std::array<double, 16> ret;

  ret[0] = m.m[0][0];
  ret[1] = m.m[0][1];
  ret[2] = m.m[0][2];
  ret[3] = m.m[0][3];

  ret[4] = m.m[1][0];
  ret[5] = m.m[1][1];
  ret[6] = m.m[1][2];
  ret[7] = m.m[1][3];

  ret[8] = m.m[2][0];
  ret[9] = m.m[2][1];
  ret[10] = m.m[2][2];
  ret[11] = m.m[2][3];

  ret[12] = m.m[3][0];
  ret[13] = m.m[3][1];
  ret[14] = m.m[3][2];
  ret[15] = m.m[3][3];

  return ret;
}

// To RGBA
bool ToRGBA(const std::vector<uint8_t> &src, int channels,
            std::vector<uint8_t> &dst) {
  uint32_t npixels = src.size() / channels;
  dst.resize(npixels * 4);

  if (channels == 1) {  // grayscale
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[i];
      dst[4 * i + 1] = src[i];
      dst[4 * i + 2] = src[i];
      dst[4 * i + 3] = 1.0f;
    }
  } else if (channels == 2) {  // assume luminance + alpha
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[2 * i + 0];
      dst[4 * i + 1] = src[2 * i + 0];
      dst[4 * i + 2] = src[2 * i + 0];
      dst[4 * i + 3] = src[2 * i + 1];
    }
  } else if (channels == 3) {
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[3 * i + 0];
      dst[4 * i + 1] = src[3 * i + 1];
      dst[4 * i + 2] = src[3 * i + 2];
      dst[4 * i + 3] = 1.0f;
    }
  } else if (channels == 4) {
    dst = src;
  } else {
    return false;
  }

  return true;
}

bool uint8arrayToBuffer(const emscripten::val& u8, tinyusdz::TypedArray<uint8_t> &buf) {
  size_t n = u8["byteLength"].as<size_t>();
  buf.resize(n);

  // Copy JS typed array -> v (one memcpy under the hood)
  emscripten::val view = emscripten::val::global("Uint8Array").new_(u8["buffer"], u8["byteOffset"], n);
  emscripten::val heapView = emscripten::val(emscripten::typed_memory_view(n, buf.data()));
  heapView.call<void>("set", view);

  return true;
}


}  // namespace detail

// Simple UUID v4 generator
std::string generateUUID() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);
  static std::uniform_int_distribution<> dis2(8, 11);

  std::stringstream ss;
  ss << std::hex;
  
  // Generate 32 hex characters with hyphens at positions 8, 12, 16, 20
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      ss << "-";
    } else if (i == 14) {
      ss << "4";  // Version 4 UUID
    } else if (i == 19) {
      ss << dis2(gen);  // Variant bits
    } else {
      ss << dis(gen);
    }
  }
  
  return ss.str();
}

struct AssetCacheEntry {
  std::string sha256_hash;
  std::string binary;
  std::string uuid;
  
  AssetCacheEntry() : uuid(generateUUID()) {}
  AssetCacheEntry(const std::string& data) 
    : sha256_hash(tinyusdz::sha256(data.c_str(), data.size())),
      binary(data), 
      uuid(generateUUID()) {}
  AssetCacheEntry(std::string&& data) noexcept
    : sha256_hash(tinyusdz::sha256(data.c_str(), data.size())),
      binary(std::move(data)), 
      uuid(generateUUID()) {}
};

// Progress callback function type for streaming
using ProgressCallback = std::function<void(const std::string&, size_t, size_t)>;

// Streaming asset entry that builds incrementally
struct StreamingAssetEntry {
  std::string binary;
  size_t expected_size;
  size_t current_size;
  std::string sha256_hash;
  std::string uuid;
  ProgressCallback progress_callback;

  StreamingAssetEntry() : expected_size(0), current_size(0), uuid(generateUUID()) {}

  bool appendChunk(const std::string& chunk) {
    binary.append(chunk);
    current_size = binary.size();

    if (progress_callback && expected_size > 0) {
      progress_callback(sha256_hash, current_size, expected_size);
    }

    return current_size <= expected_size;
  }

  bool isComplete() const {
    return expected_size > 0 && current_size >= expected_size;
  }

  AssetCacheEntry finalize() {
    if (isComplete()) {
      sha256_hash = tinyusdz::sha256(binary.c_str(), binary.size());
      AssetCacheEntry entry;
      entry.sha256_hash = sha256_hash;
      entry.binary = std::move(binary);
      entry.uuid = uuid;  // Preserve the UUID from streaming
      return entry;
    }
    return AssetCacheEntry();
  }
};

///
/// Zero-copy streaming buffer for memory-efficient JS->WASM transfer
///
/// This allows JS to write directly into pre-allocated WASM memory,
/// avoiding the need to hold the entire file in JS memory.
/// The workflow is:
/// 1. JS calls allocateStreamingBuffer() to pre-allocate WASM memory
/// 2. JS gets the buffer pointer via getStreamingBufferPtr()
/// 3. JS writes chunks directly to WASM heap using HEAPU8.set(chunk, ptr + offset)
/// 4. JS can immediately free each chunk after writing
/// 5. JS calls markChunkWritten() to update progress
/// 6. JS calls finalizeStreamingBuffer() when complete
///
struct ZeroCopyStreamingBuffer {
  std::string buffer;           // Pre-allocated buffer
  size_t total_size{0};         // Total expected size
  size_t bytes_written{0};      // Bytes written so far
  std::string uuid;             // Unique identifier (key for buffer map)
  std::string asset_name;       // Asset path/name (key for cache when finalized)
  bool finalized{false};

  ZeroCopyStreamingBuffer() : uuid(generateUUID()) {}

  bool allocate(size_t size, const std::string &name = "") {
    if (size == 0) return false;
    try {
      buffer.resize(size);
      total_size = size;
      bytes_written = 0;
      finalized = false;
      asset_name = name;
      return true;
    } catch (const std::bad_alloc&) {
      return false;
    }
  }

  // Get raw pointer for direct memory access
  uintptr_t getBufferPtr() const {
    if (buffer.empty()) return 0;
    return reinterpret_cast<uintptr_t>(buffer.data());
  }

  // Get pointer at specific offset
  uintptr_t getBufferPtrAtOffset(size_t offset) const {
    if (buffer.empty() || offset >= total_size) return 0;
    return reinterpret_cast<uintptr_t>(buffer.data() + offset);
  }

  // Mark bytes as written (for progress tracking)
  bool markBytesWritten(size_t count) {
    if (bytes_written + count > total_size) {
      bytes_written = total_size;
      return false;  // Overflow
    }
    bytes_written += count;
    return true;
  }

  float getProgress() const {
    if (total_size == 0) return 0.0f;
    return static_cast<float>(bytes_written) / static_cast<float>(total_size);
  }

  bool isComplete() const {
    return bytes_written >= total_size;
  }

  AssetCacheEntry finalize() {
    if (!isComplete()) {
      return AssetCacheEntry();
    }
    finalized = true;
    std::string hash = tinyusdz::sha256(buffer.c_str(), buffer.size());
    AssetCacheEntry entry;
    entry.sha256_hash = hash;
    entry.binary = std::move(buffer);
    entry.uuid = uuid;
    return entry;
  }

  emscripten::val toJS() const {
    emscripten::val result = emscripten::val::object();
    result.set("uuid", uuid);
    result.set("assetName", asset_name);
    result.set("totalSize", double(total_size));
    result.set("bytesWritten", double(bytes_written));
    result.set("progress", getProgress());
    result.set("isComplete", isComplete());
    result.set("finalized", finalized);
    result.set("bufferPtr", double(getBufferPtr()));
    return result;
  }
};

struct EMAssetResolutionResolver {

  static int Resolve(const char *asset_name,
                     const std::vector<std::string> &search_paths,
                     std::string *resolved_asset_name, std::string *err,
                     void *userdata) {
    (void)err;
    (void)userdata;
    (void)search_paths;

    if (!asset_name) {
      return -2;  // err
    }

    if (!resolved_asset_name) {
      return -2;  // err
    }

    // TODO: searchpath
    (*resolved_asset_name) = asset_name;
    return 0;  // OK
  }

  // AssetResoltion handlers
  static int Size(const char *asset_name, uint64_t *nbytes, std::string *err,
                  void *userdata) {
    (void)userdata;

    if (!asset_name) {
      if (err) {
        (*err) += "asset_name arg is nullptr.\n";
      }
      return -1;
    }

    if (!nbytes) {
      if (err) {
        (*err) += "nbytes arg is nullptr.\n";
      }
      return -1;
    }

    EMAssetResolutionResolver *p = reinterpret_cast<EMAssetResolutionResolver *>(userdata);
    const AssetCacheEntry &entry = p->get(asset_name);

    //std::cout << asset_name << ".size " << entry.binary.size() << "\n";

    (*nbytes) = uint64_t(entry.binary.size());
    return 0;  // OK
  }

  static int Read(const char *asset_name, uint64_t req_nbytes, uint8_t *out_buf,
                  uint64_t *nbytes, std::string *err, void *userdata) {
    if (!asset_name) {
      if (err) {
        (*err) += "asset_name arg is nullptr.\n";
      }
      return -3;
    }

    if (!nbytes) {
      if (err) {
        (*err) += "nbytes arg is nullptr.\n";
      }
      return -3;
    }

    if (req_nbytes < 9) {  // at least 9 bytes(strlen("#usda 1.0")) or more
      return -2;
    }

    EMAssetResolutionResolver *p = reinterpret_cast<EMAssetResolutionResolver *>(userdata);

    if (p->has(asset_name)) {
      const AssetCacheEntry &entry = p->get(asset_name);
      if (entry.binary.size() > req_nbytes) {
        return -2;
      }
      memcpy(out_buf, entry.binary.data(), entry.binary.size());
      (*nbytes) = entry.binary.size();
      return 0; // ok
    }

    return -1;
  }

  // Assume content is loaded in JS layer.
  bool add(const std::string &asset_name, const std::string &binary) {
    bool overwritten = has(asset_name);
      
    cache[asset_name] = AssetCacheEntry(binary);
    
    return overwritten;
  }

  bool has(const std::string &asset_name) const {
    return cache.count(asset_name);
  }

  const AssetCacheEntry &get(const std::string &asset_name) const {
    if (!cache.count(asset_name)) {
      return empty_entry_;
    } 

    return cache.at(asset_name);
  }

  std::string getHash(const std::string &asset_name) const {
    if (!cache.count(asset_name)) {
      return std::string();
    }
    return cache.at(asset_name).sha256_hash;
  }

  bool verifyHash(const std::string &asset_name, const std::string &expected_hash) const {
    if (!cache.count(asset_name)) {
      return false;
    }
    return cache.at(asset_name).sha256_hash == expected_hash;
  }

  std::string getUUID(const std::string &asset_name) const {
    if (!cache.count(asset_name)) {
      return std::string();
    }
    return cache.at(asset_name).uuid;
  }

  std::string getStreamingUUID(const std::string &asset_name) const {
    if (!streaming_cache.count(asset_name)) {
      return std::string();
    }
    return streaming_cache.at(asset_name).uuid;
  }

  // Get all asset UUIDs
  emscripten::val getAssetUUIDs() const {
    emscripten::val uuids = emscripten::val::object();
    for (const auto &pair : cache) {
      uuids.set(pair.first, pair.second.uuid);
    }
    return uuids;
  }

  // Find asset by UUID
  std::string findAssetByUUID(const std::string &uuid) const {
    for (const auto &pair : cache) {
      if (pair.second.uuid == uuid) {
        return pair.first;
      }
    }
    return std::string();
  }

  // Get asset by UUID
  const AssetCacheEntry &getByUUID(const std::string &uuid) const {
    for (const auto &pair : cache) {
      if (pair.second.uuid == uuid) {
        return pair.second;
      }
    }
    return empty_entry_;
  }

  // Check if asset exists by UUID
  bool hasByUUID(const std::string &uuid) const {
    for (const auto &pair : cache) {
      if (pair.second.uuid == uuid) {
        return true;
      }
    }
    return false;
  }

  // Delete asset by name
  bool deleteAsset(const std::string &asset_name) {
    if (!cache.count(asset_name)) {
      return false;
    }
    cache.erase(asset_name);
    return true;
  }

  // Delete asset by UUID
  bool deleteAssetByUUID(const std::string &uuid) {
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (it->second.uuid == uuid) {
        cache.erase(it);
        return true;
      }
    }
    return false;
  }

  // Delete streaming asset if exists
  bool deleteStreamingAsset(const std::string &asset_name) {
    if (!streaming_cache.count(asset_name)) {
      return false;
    }
    streaming_cache.erase(asset_name);
    return true;
  }

  emscripten::val getCacheDataAsMemoryView(const std::string &asset_name) const {
    if (!cache.count(asset_name)) {
      return emscripten::val::undefined();
    }
    const AssetCacheEntry &entry = cache.at(asset_name);
    return emscripten::val(emscripten::typed_memory_view(entry.binary.size(), 
                                                         reinterpret_cast<const uint8_t*>(entry.binary.data())));
  }

  // Zero-copy method using raw pointers for direct Uint8Array access
  bool addFromRawPointer(const std::string &asset_name, uintptr_t dataPtr, size_t size) {
    if (size == 0) {
      return false;
    }
    
    // Direct access to the data without copying during read
    const uint8_t* data = reinterpret_cast<const uint8_t*>(dataPtr);
    
    // Only copy once into our storage format
    std::string binary;
    binary.reserve(size);
    binary.assign(reinterpret_cast<const char*>(data), size);
    
    bool overwritten = has(asset_name);
    cache[asset_name] = AssetCacheEntry(std::move(binary));
    
    return overwritten;
  }

  void clear() {
    cache.clear();
    streaming_cache.clear();
  }

  // Streaming asset methods
  bool startStreamingAsset(const std::string &asset_name, size_t expected_size) {
    streaming_cache[asset_name] = StreamingAssetEntry();
    streaming_cache[asset_name].expected_size = expected_size;
    streaming_cache[asset_name].current_size = 0;
    return true;
  }
  
  bool appendAssetChunk(const std::string &asset_name, const std::string &chunk) {
    if (!streaming_cache.count(asset_name)) {
      return false;
    }
    return streaming_cache[asset_name].appendChunk(chunk);
  }
  
  bool finalizeStreamingAsset(const std::string &asset_name) {
    if (!streaming_cache.count(asset_name)) {
      return false;
    }
    
    StreamingAssetEntry &entry = streaming_cache[asset_name];
    if (!entry.isComplete()) {
      return false;
    }
    
    cache[asset_name] = std::move(entry.finalize());
    streaming_cache.erase(asset_name);
    return true;
  }
  
  bool isStreamingAssetComplete(const std::string &asset_name) const {
    if (!streaming_cache.count(asset_name)) {
      return false;
    }
    return streaming_cache.at(asset_name).isComplete();
  }
  
  emscripten::val getStreamingProgress(const std::string &asset_name) const {
    emscripten::val progress = emscripten::val::object();
    
    if (!streaming_cache.count(asset_name)) {
      progress.set("exists", false);
      return progress;
    }
    
    const StreamingAssetEntry &entry = streaming_cache.at(asset_name);
    progress.set("exists", true);
    progress.set("current", double(entry.current_size));
    progress.set("total", double(entry.expected_size));
    progress.set("complete", entry.isComplete());
    progress.set("uuid", entry.uuid);
    if (entry.expected_size > 0) {
      progress.set("percentage", (double(entry.current_size) / double(entry.expected_size)) * 100.0);
    } else {
      progress.set("percentage", 0.0);
    }
    
    return progress;
  }

  //
  // Zero-copy streaming buffer methods
  //

  /// Allocate a zero-copy buffer for streaming transfer
  /// @param asset_name The asset path/name (used as cache key when finalized)
  /// @param size Buffer size in bytes
  /// Returns buffer info including UUID and pointer for direct memory access
  emscripten::val allocateZeroCopyBuffer(const std::string &asset_name, size_t size) {
    emscripten::val result = emscripten::val::object();

    if (size == 0) {
      result.set("success", false);
      result.set("error", "Size must be greater than 0");
      return result;
    }

    // Create a new buffer with unique UUID
    ZeroCopyStreamingBuffer buf;
    if (!buf.allocate(size, asset_name)) {
      result.set("success", false);
      result.set("error", "Failed to allocate buffer");
      return result;
    }

    std::string uuid = buf.uuid;
    zerocopy_buffers[uuid] = std::move(buf);

    result.set("success", true);
    result.set("uuid", uuid);
    result.set("assetName", asset_name);
    result.set("bufferPtr", double(zerocopy_buffers[uuid].getBufferPtr()));
    result.set("totalSize", double(size));
    return result;
  }

  /// Get buffer pointer for direct memory access
  /// @param uuid The buffer UUID returned from allocateZeroCopyBuffer
  double getZeroCopyBufferPtr(const std::string &uuid) {
    if (!zerocopy_buffers.count(uuid)) {
      return 0;
    }
    return double(zerocopy_buffers.at(uuid).getBufferPtr());
  }

  /// Get buffer pointer at specific offset
  /// @param uuid The buffer UUID
  double getZeroCopyBufferPtrAtOffset(const std::string &uuid, size_t offset) {
    if (!zerocopy_buffers.count(uuid)) {
      return 0;
    }
    return double(zerocopy_buffers.at(uuid).getBufferPtrAtOffset(offset));
  }

  /// Mark bytes as written and update progress
  /// @param uuid The buffer UUID
  bool markZeroCopyBytesWritten(const std::string &uuid, size_t count) {
    if (!zerocopy_buffers.count(uuid)) {
      return false;
    }
    return zerocopy_buffers[uuid].markBytesWritten(count);
  }

  /// Get current zero-copy buffer progress
  /// @param uuid The buffer UUID
  emscripten::val getZeroCopyProgress(const std::string &uuid) const {
    if (!zerocopy_buffers.count(uuid)) {
      emscripten::val result = emscripten::val::object();
      result.set("exists", false);
      return result;
    }

    emscripten::val result = zerocopy_buffers.at(uuid).toJS();
    result.set("exists", true);
    return result;
  }

  /// Finalize zero-copy buffer and move to asset cache
  /// Uses the asset_name stored in the buffer as the cache key
  /// @param uuid The buffer UUID
  bool finalizeZeroCopyBuffer(const std::string &uuid) {
    if (!zerocopy_buffers.count(uuid)) {
      return false;
    }

    ZeroCopyStreamingBuffer& buf = zerocopy_buffers[uuid];
    if (!buf.isComplete()) {
      return false;
    }

    // Use the stored asset_name as the cache key
    std::string cache_key = buf.asset_name.empty() ? uuid : buf.asset_name;
    cache[cache_key] = buf.finalize();
    zerocopy_buffers.erase(uuid);
    return true;
  }

  /// Cancel and free zero-copy buffer
  /// @param uuid The buffer UUID
  bool cancelZeroCopyBuffer(const std::string &uuid) {
    if (!zerocopy_buffers.count(uuid)) {
      return false;
    }
    zerocopy_buffers.erase(uuid);
    return true;
  }

  /// Get all active zero-copy buffers
  emscripten::val getActiveZeroCopyBuffers() const {
    emscripten::val result = emscripten::val::array();
    for (const auto& pair : zerocopy_buffers) {
      emscripten::val item = emscripten::val::object();
      item.set("uuid", pair.first);
      item.set("info", pair.second.toJS());
      result.call<void>("push", item);
    }
    return result;
  }

  // TODO: Use IndexDB?
  //
  // <uri, AssetCacheEntry>
  std::map<std::string, AssetCacheEntry> cache;
  std::map<std::string, StreamingAssetEntry> streaming_cache;
  std::map<std::string, ZeroCopyStreamingBuffer> zerocopy_buffers;
  AssetCacheEntry empty_entry_;
};

///
/// Parsing progress state for JS/WASM polling-based progress reporting
///
/// Since we cannot call async JS functions from C++ without Asyncify,
/// we use a polling approach where:
/// 1. C++ updates progress state via a callback
/// 2. JS can poll the progress state at any time
/// 3. JS can request cancellation which C++ checks at progress points
///
struct ParsingProgress {
  enum class Stage {
    Idle,
    Parsing,
    Converting,
    Complete,
    Error,
    Cancelled
  };

  float progress{0.0f};          // 0.0 to 1.0
  Stage stage{Stage::Idle};
  std::string stage_name{"idle"};
  std::string current_operation{""};
  std::atomic<bool> cancel_requested{false};
  std::string error_message{""};
  uint64_t bytes_processed{0};
  uint64_t total_bytes{0};

  // Detailed mesh/material progress (from Tydra converter)
  size_t meshes_processed{0};
  size_t meshes_total{0};
  std::string current_mesh_name{""};
  size_t materials_processed{0};
  size_t materials_total{0};
  std::string tydra_stage{""};  // Stage name from DetailedProgressInfo

  void reset() {
    progress = 0.0f;
    stage = Stage::Idle;
    stage_name = "idle";
    current_operation = "";
    cancel_requested.store(false);
    error_message = "";
    bytes_processed = 0;
    total_bytes = 0;
    meshes_processed = 0;
    meshes_total = 0;
    current_mesh_name = "";
    materials_processed = 0;
    materials_total = 0;
    tydra_stage = "";
  }

  void setStage(Stage s) {
    stage = s;
    switch (s) {
      case Stage::Idle: stage_name = "idle"; break;
      case Stage::Parsing: stage_name = "parsing"; break;
      case Stage::Converting: stage_name = "converting"; break;
      case Stage::Complete: stage_name = "complete"; break;
      case Stage::Error: stage_name = "error"; break;
      case Stage::Cancelled: stage_name = "cancelled"; break;
    }
  }

  bool shouldCancel() const {
    return cancel_requested.load();
  }

  emscripten::val toJS() const {
    emscripten::val result = emscripten::val::object();
    result.set("progress", progress);
    result.set("stage", stage_name);
    result.set("currentOperation", current_operation);
    result.set("cancelRequested", cancel_requested.load());
    result.set("errorMessage", error_message);
    result.set("bytesProcessed", double(bytes_processed));
    result.set("totalBytes", double(total_bytes));
    result.set("percentage", progress * 100.0f);

    // Detailed mesh/material progress
    result.set("meshesProcessed", double(meshes_processed));
    result.set("meshesTotal", double(meshes_total));
    result.set("currentMeshName", current_mesh_name);
    result.set("materialsProcessed", double(materials_processed));
    result.set("materialsTotal", double(materials_total));
    result.set("tydraStage", tydra_stage);

    return result;
  }
};

bool SetupEMAssetResolution(
    tinyusdz::AssetResolutionResolver &resolver,
    /* must be the persistent pointer address until usd load finishes */
    const EMAssetResolutionResolver *p) {
  if (!p) {
    return false;
  }

  tinyusdz::AssetResolutionHandler handler;
  handler.resolve_fun = EMAssetResolutionResolver::Resolve;
  handler.size_fun = EMAssetResolutionResolver::Size;
  handler.read_fun = EMAssetResolutionResolver::Read;
  handler.write_fun = nullptr;
  handler.userdata =
      reinterpret_cast<void *>(const_cast<EMAssetResolutionResolver *>(p));

  resolver.register_wildcard_asset_resolution_handler(handler);

  return true;
}

///
/// Simple C++ wrapper class for Emscripten
///
class TinyUSDZLoaderNative {
 public:
  struct CompositionFeatures {
    bool subLayers{true};
    bool inherits{true};
    bool variantSets{true};
    bool references{true};
    bool payload{true};  // Not 'payloads'
    bool specializes{true};
  };

  // Default constructor for async loading
  TinyUSDZLoaderNative() : loaded_(false) {}
  ~TinyUSDZLoaderNative() {}

#if 0
  ///
  /// `binary` is the buffer for TinyUSDZ binary(e.g. buffer read by
  /// fs.readFileSync) std::string can be used as UInt8Array in JS layer.
  ///
  TinyUSDZLoaderNative(const std::string &binary) {
    loadFromBinary(binary);
  }
#endif

  bool stageToRenderScene(const tinyusdz::Stage &stage, bool is_usdz, const std::string &binary) {

    tinyusdz::tydra::RenderSceneConverterEnv env(stage);

    // load texture in C++ image loader? default = false(Use JS to decode texture image)
    env.scene_config.load_texture_assets = loadTextureInNative_;

    env.material_config.preserve_texel_bitdepth = true;

    // Free GeomMesh data in stage after using it to save memory.
    env.mesh_config.lowmem = true;

    // Do not try to build indices(avoid temp memory consumption of vertex similarity search)
    //env.mesh_config.prefer_non_indexed = true;

    //env.mesh_config.build_index_method = 0; // simple

    // Bone reduction configuration
    env.mesh_config.enable_bone_reduction = enable_bone_reduction_;
    env.mesh_config.target_bone_count = target_bone_count_;

    if (is_usdz) {
      // TODO: Support USDZ + Composition
      // Setup AssetResolutionResolver to read a asset(file) from memory.
      bool asset_on_memory =
          false;  // duplicate asset data from USDZ(binary) to UDSZAsset struct.

      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
              reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
              asset_on_memory, &usdz_asset_, &warn_, &error_)) {
        std::cerr << "Failed to read USDZ assetInfo. \n";
        loaded_ = false;
        return false;
      }

      tinyusdz::AssetResolutionResolver arr;

      // NOTE: Pointer address of usdz_asset must be valid until the call of
      // RenderSceneConverter::ConvertToRenderScene.
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset_)) {
        std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
        loaded_ = false;
        return false;
      }

      env.asset_resolver = arr;
    } else {
      tinyusdz::AssetResolutionResolver arr;
      if (!SetupEMAssetResolution(arr, &em_resolver_)) {
        std::cerr << "Failed to setup FetchAssetResolution\n";
        loaded_ = false;
        return false;
      }

      env.asset_resolver = arr;
    }

    // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
    tinyusdz::tydra::RenderSceneConverter converter;

    // Set up detailed progress callback to update parsing_progress_ and call JS
    converter.SetDetailedProgressCallback(
        [](const tinyusdz::tydra::DetailedProgressInfo &info, void *userptr) -> bool {
          ParsingProgress *pp = static_cast<ParsingProgress *>(userptr);
          if (pp) {
            pp->meshes_processed = info.meshes_processed;
            pp->meshes_total = info.meshes_total;
            pp->current_mesh_name = info.current_mesh_name;
            pp->materials_processed = info.materials_processed;
            pp->materials_total = info.materials_total;
            pp->tydra_stage = info.GetStageName();
            pp->current_operation = info.message;
            // Update progress: parsing is 0-80%, conversion is 80-100%
            pp->progress = 0.8f + (info.progress * 0.2f);
          }

          // Call JavaScript synchronously via EM_JS
          reportTydraProgress(
            static_cast<int>(info.meshes_processed),
            static_cast<int>(info.meshes_total),
            info.GetStageName(),  // Already returns const char*
            info.current_mesh_name.c_str(),
            info.progress
          );

          return true;  // Continue conversion
        },
        &parsing_progress_);

    // Set timecode to startTimeCode if authored, so xformOps with TimeSamples
    // are evaluated at the start time (initial pose) for static viewers
    if (stage.metas().startTimeCode.authored()) {
      env.timecode = stage.metas().startTimeCode.get_value();
    }
    loaded_ = converter.ConvertToRenderScene(env, &render_scene_);

    // Capture warnings from converter (available via warn() method)
    if (!converter.GetWarning().empty()) {
      if (!warn_.empty()) warn_ += "\n";
      warn_ += converter.GetWarning();
      // Note: Not printing to cerr to avoid console error spam
    }

    if (!loaded_) {
      std::cerr << "Failed to convert USD Stage to RenderScene: \n"
                << converter.GetError() << "\n";
      error_ = converter.GetError();
      return false;
    }

    return true;
  }

  bool loadAsLayerFromBinary(const std::string &binary, const std::string &filename) {


    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;

    loaded_ = tinyusdz::LoadLayerFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &layer_, &warn_, &error_, options);

    if (!loaded_) {
      return false;
    }

    loaded_as_layer_ = true;
    filename_ = filename;

    return true;
  }


  bool loadFromBinary(const std::string &binary, const std::string &filename) {

    //if (enableComposition_) {
    //  return loadAndCompositeFromBinary(binary, filename);
    //}

    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;

    tinyusdz::Stage stage;
    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &stage, &warn_, &error_, options);

    if (!loaded_) {
      return false;
    }

    loaded_as_layer_ = false;
    filename_ = filename;

    std::cout << "loaded\n";
#if 0
    tinyusdz::tydra::RenderSceneConverterEnv env(stage);

    //
    // false = Load Texture in JS Layer
    //

    env.scene_config.load_texture_assets = loadTextureInNative_;

    env.material_config.preserve_texel_bitdepth = true;

    if (is_usdz) {
      // TODO: Support USDZ + Composition
      // Setup AssetResolutionResolver to read a asset(file) from memory.
      bool asset_on_memory =
          false;  // duplicate asset data from USDZ(binary) to UDSZAsset struct.

      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
              reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
              asset_on_memory, &usdz_asset_, &warn_, &error_)) {
        std::cerr << "Failed to read USDZ assetInfo. \n";
        loaded_ = false;
        return false;
      }

      tinyusdz::AssetResolutionResolver arr;

      // NOTE: Pointer address of usdz_asset must be valid until the call of
      // RenderSceneConverter::ConvertToRenderScene.
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset_)) {
        std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
        loaded_ = false;
        return false;
      }

      env.asset_resolver = arr;
    } else {
      tinyusdz::AssetResolutionResolver arr;
      if (!SetupFetchAssetResolution(arr, &em_resolver_)) {
        std::cerr << "Failed to setup FetchAssetResolution\n";
        loaded_ = false;
        return false;
      }

      env.asset_resolver = arr;
    }

    // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
    tinyusdz::tydra::RenderSceneConverter converter;

    // Set up detailed progress callback to update parsing_progress_ and call JS
    converter.SetDetailedProgressCallback(
        [](const tinyusdz::tydra::DetailedProgressInfo &info, void *userptr) -> bool {
          ParsingProgress *pp = static_cast<ParsingProgress *>(userptr);
          if (pp) {
            pp->meshes_processed = info.meshes_processed;
            pp->meshes_total = info.meshes_total;
            pp->current_mesh_name = info.current_mesh_name;
            pp->materials_processed = info.materials_processed;
            pp->materials_total = info.materials_total;
            pp->tydra_stage = info.GetStageName();
            pp->current_operation = info.message;
            // Update progress: parsing is 0-80%, conversion is 80-100%
            pp->progress = 0.8f + (info.progress * 0.2f);
          }

          // Call JavaScript synchronously via EM_JS
          reportTydraProgress(
            static_cast<int>(info.meshes_processed),
            static_cast<int>(info.meshes_total),
            info.GetStageName(),  // Already returns const char*
            info.current_mesh_name.c_str(),
            info.progress
          );

          return true;  // Continue conversion
        },
        &parsing_progress_);

    // Set timecode to startTimeCode if authored, so xformOps with TimeSamples
    // are evaluated at the start time (initial pose) for static viewers
    if (stage.metas().startTimeCode.authored()) {
      env.timecode = stage.metas().startTimeCode.get_value();
    }
    loaded_ = converter.ConvertToRenderScene(env, &render_scene_);

    // Capture warnings from converter (available via warn() method)
    if (!converter.GetWarning().empty()) {
      if (!warn_.empty()) warn_ += "\n";
      warn_ += converter.GetWarning();
      // Note: Not printing to cerr to avoid console error spam
    }

    if (!loaded_) {
      std::cerr << "Failed to convert USD Stage to RenderScene: \n"
                << converter.GetError() << "\n";
      error_ = converter.GetError();
      return false;
    }
#else
    return stageToRenderScene(stage, is_usdz, binary);
#endif

  }

  // ============================================================================
  // C++20 Coroutine-based Async Loading
  // ============================================================================
  // This method uses C++20 coroutines to yield to the JavaScript event loop
  // between processing phases, allowing the browser to repaint during loading.
  //
  // Enable with CMake option: -DTINYUSDZ_WASM_COROUTINE=ON (default)
  // Disable with: -DTINYUSDZ_WASM_COROUTINE=OFF
  //
  // Returns a Promise that resolves to a JS object: { success: bool, error?: string }
  //
#if defined(TINYUSDZ_USE_COROUTINE)
  emscripten::val loadFromBinaryAsync(std::string binary, std::string filename) {
    // IMPORTANT: Parameters are passed by VALUE (not by reference) to ensure
    // data remains valid across co_await suspension points. References would
    // become dangling after the coroutine yields to the event loop.

    // Phase 1: Initial setup and format detection
    reportAsyncPhaseStart("detecting", 0.0f);

    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());

    // Yield to allow UI to show "detecting" phase
    co_await yieldToEventLoop();

    // Phase 2: Parsing USD
    reportAsyncPhaseStart("parsing", 0.1f);

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;

    tinyusdz::Stage stage;
    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &stage, &warn_, &error_, options);

    if (!loaded_) {
      emscripten::val result = emscripten::val::object();
      result.set("success", false);
      result.set("error", error_);
      co_return result;
    }

    loaded_as_layer_ = false;
    filename_ = filename;

    // Yield after parsing to allow UI update
    co_await yieldToEventLoop();

    // Phase 3: Setup conversion environment
    reportAsyncPhaseStart("setup", 0.3f);

    tinyusdz::tydra::RenderSceneConverterEnv env(stage);
    env.scene_config.load_texture_assets = loadTextureInNative_;
    env.material_config.preserve_texel_bitdepth = true;
    env.mesh_config.lowmem = true;
    env.mesh_config.enable_bone_reduction = enable_bone_reduction_;
    env.mesh_config.target_bone_count = target_bone_count_;

    // Yield after setup
    co_await yieldToEventLoop();

    // Phase 4: Setup asset resolution
    reportAsyncPhaseStart("assets", 0.4f);

    if (is_usdz) {
      bool asset_on_memory = false;
      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
              reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
              asset_on_memory, &usdz_asset_, &warn_, &error_)) {
        emscripten::val result = emscripten::val::object();
        result.set("success", false);
        result.set("error", "Failed to read USDZ assetInfo");
        co_return result;
      }

      tinyusdz::AssetResolutionResolver arr;
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset_)) {
        emscripten::val result = emscripten::val::object();
        result.set("success", false);
        result.set("error", "Failed to setup AssetResolution for USDZ");
        co_return result;
      }
      env.asset_resolver = arr;
    } else {
      tinyusdz::AssetResolutionResolver arr;
      if (!SetupEMAssetResolution(arr, &em_resolver_)) {
        emscripten::val result = emscripten::val::object();
        result.set("success", false);
        result.set("error", "Failed to setup asset resolution");
        co_return result;
      }
      env.asset_resolver = arr;
    }

    // Yield after asset resolution setup
    co_await yieldToEventLoop();

    // Phase 5: Converting meshes (Tydra)
    reportAsyncPhaseStart("meshes", 0.5f);

    tinyusdz::tydra::RenderSceneConverter converter;

    // Set up progress callback that reports to JS
    converter.SetDetailedProgressCallback(
        [](const tinyusdz::tydra::DetailedProgressInfo &info, void *userptr) -> bool {
          // Report progress to JS synchronously
          reportTydraProgress(
            static_cast<int>(info.meshes_processed),
            static_cast<int>(info.meshes_total),
            info.GetStageName(),
            info.current_mesh_name.c_str(),
            info.progress
          );
          return true;
        },
        nullptr);

    if (stage.metas().startTimeCode.authored()) {
      env.timecode = stage.metas().startTimeCode.get_value();
    }

    // Yield before heavy conversion
    co_await yieldToEventLoop();

    loaded_ = converter.ConvertToRenderScene(env, &render_scene_);

    // Yield after conversion
    co_await yieldToEventLoop();

    if (!converter.GetWarning().empty()) {
      if (!warn_.empty()) warn_ += "\n";
      warn_ += converter.GetWarning();
    }

    if (!loaded_) {
      emscripten::val result = emscripten::val::object();
      result.set("success", false);
      result.set("error", converter.GetError());
      co_return result;
    }

    // Phase 6: Complete
    reportAsyncPhaseStart("complete", 1.0f);

    // Final yield to ensure UI updates
    co_await yieldToEventLoop();

    emscripten::val result = emscripten::val::object();
    result.set("success", true);
    result.set("meshCount", static_cast<int>(render_scene_.meshes.size()));
    result.set("materialCount", static_cast<int>(render_scene_.materials.size()));
    result.set("textureCount", static_cast<int>(render_scene_.textures.size()));
    co_return result;
  }
#endif // TINYUSDZ_USE_COROUTINE

  // u8 : Uint8Array object.
  bool loadTest(const std::string &filename, const emscripten::val &u8) {

    tinyusdz::TypedArray<uint8_t> binary;
    detail::uint8arrayToBuffer(u8, binary);
    std::cout << "binary.size = " << binary.size() << "\n";

    //bool is_usdz = tinyusdz::IsUSDZ(
    //    reinterpret_cast<const uint8_t *>(binary.data()), binary.size());

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;

#if 0
    tinyusdz::Stage stage;
    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.data()), binary.size(),
        filename, &stage, &warn_, &error_, options);

    if (!loaded_) {
      return false;
    }
#else
    std::cout << "layer\n";
    tinyusdz::Layer layer;
    loaded_ = tinyusdz::LoadLayerFromMemory(
        reinterpret_cast<const uint8_t *>(binary.data()), binary.size(),
        filename, &layer, &warn_, &error_, options);

    if (!loaded_) {
      return false;
    }
#endif

    loaded_as_layer_ = false;
    filename_ = filename;

    //std::cout << "loaded\n";

    return true;
  }

  /// Load USD from a cached asset (previously streamed via zero-copy transfer)
  /// @param asset_name The name/path used when the asset was cached
  /// @returns true on success
  bool loadFromCachedAsset(const std::string &asset_name) {
    if (!em_resolver_.has(asset_name)) {
      error_ = "Asset not found in cache: " + asset_name;
      return false;
    }

    const AssetCacheEntry &entry = em_resolver_.get(asset_name);
    if (entry.binary.empty()) {
      error_ = "Cached asset is empty: " + asset_name;
      return false;
    }

    // Delegate to loadFromBinary with the cached data
    return loadFromBinary(entry.binary, asset_name);
  }

  /// Load USD as Layer from a cached asset
  /// @param asset_name The name/path used when the asset was cached
  /// @returns true on success
  bool loadAsLayerFromCachedAsset(const std::string &asset_name) {
    if (!em_resolver_.has(asset_name)) {
      error_ = "Asset not found in cache: " + asset_name;
      return false;
    }

    const AssetCacheEntry &entry = em_resolver_.get(asset_name);
    if (entry.binary.empty()) {
      error_ = "Cached asset is empty: " + asset_name;
      return false;
    }

    // Delegate to loadAsLayerFromBinary with the cached data
    return loadAsLayerFromBinary(entry.binary, asset_name);
  }

  // Test function for value::Value memory usage estimation
  // arrayLength: optional parameter to specify the size of array tests (default: 10000)
  emscripten::val testValueMemoryUsage(emscripten::val arrayLengthVal) {
    emscripten::val result = emscripten::val::object();
    emscripten::val tests = emscripten::val::array();
    
    // Get array length from parameter or use default
    int arrayLength = 10000;
    if (!arrayLengthVal.isUndefined() && !arrayLengthVal.isNull()) {
      arrayLength = arrayLengthVal.as<int>();
    }
    
    // Test 1: Empty value
    {
      tinyusdz::value::Value v;
      size_t mem = v.estimate_memory_usage();
      emscripten::val test = emscripten::val::object();
      test.set("name", "Empty value");
      test.set("bytes", mem);
      tests.call<void>("push", test);
    }
    
    // Test 2: Simple types
    {
      tinyusdz::value::Value v1(42);  // int32
      emscripten::val test = emscripten::val::object();
      test.set("name", "int32(42)");
      test.set("bytes", v1.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    {
      tinyusdz::value::Value v2(3.14f);  // float
      emscripten::val test = emscripten::val::object();
      test.set("name", "float(3.14)");
      test.set("bytes", v2.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    {
      tinyusdz::value::Value v3(2.718);  // double
      emscripten::val test = emscripten::val::object();
      test.set("name", "double(2.718)");
      test.set("bytes", v3.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 3: Vector types
    {
      tinyusdz::value::float3 f3{1.0f, 2.0f, 3.0f};
      tinyusdz::value::Value v(f3);
      emscripten::val test = emscripten::val::object();
      test.set("name", "float3");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 4: Matrix types
    {
      tinyusdz::value::matrix4d m4d;
      tinyusdz::value::Value v(m4d);
      emscripten::val test = emscripten::val::object();
      test.set("name", "matrix4d");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 5: String type
    {
      std::string str = "Hello, World! This is a test string.";
      tinyusdz::value::Value v(str);
      emscripten::val test = emscripten::val::object();
      test.set("name", "string('" + str + "')");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 6: Token type
    {
      tinyusdz::value::token tok("myToken");
      tinyusdz::value::Value v(tok);
      emscripten::val test = emscripten::val::object();
      test.set("name", "token('myToken')");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 7: Array of floats
    {
      std::vector<float> floats = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
      tinyusdz::value::Value v(floats);
      emscripten::val test = emscripten::val::object();
      test.set("name", "float array (5 elements)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 8: Array of float3
    {
      std::vector<tinyusdz::value::float3> vec3s = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
      };
      tinyusdz::value::Value v(vec3s);
      emscripten::val test = emscripten::val::object();
      test.set("name", "float3 array (3 elements)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 9: Array of strings
    {
      std::vector<std::string> strings = {"one", "two", "three", "four"};
      tinyusdz::value::Value v(strings);
      emscripten::val test = emscripten::val::object();
      test.set("name", "string array (4 elements)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 10: Color types (role types)
    {
      tinyusdz::value::color3f c3f{1.0f, 0.5f, 0.0f};
      tinyusdz::value::Value v(c3f);
      emscripten::val test = emscripten::val::object();
      test.set("name", "color3f");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 11: Normal types (role types)
    {
      tinyusdz::value::normal3f n3f{0.0f, 1.0f, 0.0f};
      tinyusdz::value::Value v(n3f);
      emscripten::val test = emscripten::val::object();
      test.set("name", "normal3f");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 12: TimeSamples
    {
      tinyusdz::value::TimeSamples ts;
      ts.add_sample(0.0, tinyusdz::value::Value(1.0f));
      ts.add_sample(1.0, tinyusdz::value::Value(2.0f));
      ts.add_sample(2.0, tinyusdz::value::Value(3.0f));
      size_t mem = ts.estimate_memory_usage();
      emscripten::val test = emscripten::val::object();
      test.set("name", "TimeSamples (3 samples)");
      test.set("bytes", mem);
      tests.call<void>("push", test);
    }
    
    // Test 13: Large array test (using specified array length)
    {
      std::vector<float> large_array(arrayLength, 1.0f);
      tinyusdz::value::Value v(large_array);
      emscripten::val test = emscripten::val::object();
      test.set("name", "float array (" + std::to_string(arrayLength) + " elements)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 13b: Large float3 array test (using specified array length / 3)
    {
      int vec3Count = std::max(1, arrayLength / 3);
      std::vector<tinyusdz::value::float3> large_vec3_array;
      large_vec3_array.reserve(vec3Count);
      for (int i = 0; i < vec3Count; ++i) {
        large_vec3_array.push_back({static_cast<float>(i), static_cast<float>(i+1), static_cast<float>(i+2)});
      }
      tinyusdz::value::Value v(large_vec3_array);
      emscripten::val test = emscripten::val::object();
      test.set("name", "float3 array (" + std::to_string(vec3Count) + " elements)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 13c: Large int array test (using specified array length)
    {
      std::vector<int32_t> large_int_array(arrayLength, 42);
      tinyusdz::value::Value v(large_int_array);
      emscripten::val test = emscripten::val::object();
      test.set("name", "int32 array (" + std::to_string(arrayLength) + " elements)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 14: Half precision types
    {
      tinyusdz::value::half h(tinyusdz::value::float_to_half_full(1.5f));
      tinyusdz::value::Value v(h);
      emscripten::val test = emscripten::val::object();
      test.set("name", "half(1.5)");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Test 15: Quaternion types
    {
      tinyusdz::value::quatf q{0.0f, 0.0f, 0.0f, 1.0f};
      tinyusdz::value::Value v(q);
      emscripten::val test = emscripten::val::object();
      test.set("name", "quatf");
      test.set("bytes", v.estimate_memory_usage());
      tests.call<void>("push", test);
    }
    
    // Calculate total memory
    size_t totalMemory = 0;
    int numTests = tests["length"].as<int>();
    for (int i = 0; i < numTests; ++i) {
      emscripten::val test = tests[i];
      totalMemory += test["bytes"].as<size_t>();
    }
    
    result.set("tests", tests);
    result.set("success", true);
    result.set("totalTests", numTests);
    result.set("totalMemory", totalMemory);
    result.set("arrayLength", arrayLength);
    
    return result;
  }

  emscripten::val testLayer(emscripten::val arrayLengthVal) {
    
    // Get array length from parameter or use default
    int arrayLength = 10000;
    if (!arrayLengthVal.isUndefined() && !arrayLengthVal.isNull()) {
      arrayLength = arrayLengthVal.as<int>();
    }
    
    std::cout << "arrayLen " << arrayLength << "\n";
#if 1
    // create Attrib
    std::vector<tinyusdz::value::point3f> points(arrayLength);
    tinyusdz::Attribute attr;
    attr.set_value(std::move(points));

    std::cout << "Attr.memusage " << attr.estimate_memory_usage() << "\n";
    size_t totalMemory = 0; //attr.estimate_memory_usage();
#else
    tinyusdz::TypedArray<tinyusdz::value::point3f> points(arrayLength);
    tinyusdz::Attribute attr;
    //std::cout << "attr.set_value\n";
    //attr.set_value(std::move(points));
    
    tinyusdz::primvar::PrimVar var;
    std::cout << "pvar";
    var.set_value(std::move(points));

    //std::vector<tinyusdz::value::point3f> points(arrayLength);
    //tinyusdz::value::Value v(std::move(points));
    size_t totalMemory = points.size() * sizeof(tinyusdz::value::point3f);
    std::cout << "totalMemory " << totalMemory << "\n";
#endif
    
    
    emscripten::val result = emscripten::val::object();
    result.set("totalMemory", totalMemory);
    
    return result;
  }


#if 0 // TODO: Remove
  //
  //  Current limitation: can't specify usdz for USD to be composited(e.g. subLayer'ed, reference'ed)
  //  Toplevel USD can be USDZ.
  //
  bool loadAndCompositeFromBinary(const std::string &binary, const std::string &filename) {

    std::cout << "loadAndComposite " << std::endl;

    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());
    
    tinyusdz::Layer root_layer;
    bool ret = tinyusdz::LoadLayerFromMemory(reinterpret_cast<const uint8_t*>(binary.data()), binary.size(), filename, &root_layer, &warn_, &error_);

      if (!ret) {
        return false;
      }

      tinyusdz::Stage stage;
      stage.metas() = root_layer.metas();

      std::string warn;

      tinyusdz::AssetResolutionResolver resolver;
      if (!SetupEMAssetResolution(resolver, &em_resolver_)) {
        std::cerr << "Failed to setup FetchAssetResolution\n";
        return false;
      }
      const std::string base_dir = "./"; // FIXME
      resolver.set_current_working_path(base_dir);
      resolver.set_search_paths({base_dir});
    
      filename_ = filename;

      // TODO: Control composition feature flag from JS layer.
      CompositionFeatures comp_features;
      constexpr int kMaxIteration = 32; // Reduce iterations for web

      //
      // LIVRPS strength ordering
      // - [x] Local(subLayers)
      // - [x] Inherits
      // - [x] VariantSets
      // - [x] References
      // - [x] Payload
      // - [ ] Specializes
      //

      tinyusdz::Layer src_layer = root_layer;
      if (comp_features.subLayers) {
        tinyusdz::Layer composited_layer;
        if (!tinyusdz::CompositeSublayers(resolver, src_layer, &composited_layer, &warn_, &error_)) {
          //std::cerr << "Failed to composite subLayers: " << err << "\n";
          return false;
        }

        std::cout << "# `subLayers` composited\n";
        //std::cout << composited_layer << "\n";

        src_layer = std::move(composited_layer);
      }

      // TODO: Find more better way to Recursively resolve references/payload/variants
      for (int i = 0; i < kMaxIteration; i++) {

        bool has_unresolved = false;

        if (comp_features.references) {
          if (!src_layer.check_unresolved_references()) {
            std::cout << "# iter " << i << ": no unresolved references.\n";
          } else {
            has_unresolved = true;

            tinyusdz::Layer composited_layer;
            if (!tinyusdz::CompositeReferences(resolver, src_layer, &composited_layer, &warn_, &error_)) {
              return false;
            }


            src_layer = std::move(composited_layer);
          }
        }


        if (comp_features.payload) {
          if (!src_layer.check_unresolved_payload()) {
            std::cout << "# iter " << i << ": no unresolved payload.\n";
          } else {
            has_unresolved = true;

            tinyusdz::Layer composited_layer;
            if (!tinyusdz::CompositePayload(resolver, src_layer, &composited_layer, &warn_, &error_)) {
              return false;
            }

            src_layer = std::move(composited_layer);
          }
        }

        if (comp_features.inherits) {
          if (!src_layer.check_unresolved_inherits()) {
            std::cout << "# iter " << i << ": no unresolved inherits.\n";
          } else {
            has_unresolved = true;

            tinyusdz::Layer composited_layer;
            if (!tinyusdz::CompositeInherits(src_layer, &composited_layer, &warn_, &error_)) {
              return false;
            }

            src_layer = std::move(composited_layer);
          }
        }

        if (comp_features.variantSets) {
          if (!src_layer.check_unresolved_variant()) {
            std::cout << "# iter " << i << ": no unresolved variant.\n";
          } else {
            has_unresolved = true;

            tinyusdz::Layer composited_layer;
            if (!tinyusdz::CompositeVariant(src_layer, &composited_layer, &warn_, &error_)) {
              return false;
            }

            src_layer = std::move(composited_layer);
          }
        }


        std::cout << "# has_unresolved_references: " << src_layer.check_unresolved_references() << "\n";
        std::cout << "# all resolved? " << !has_unresolved << "\n";

        if (!has_unresolved) {
          std::cout << "# of composition iteration to resolve fully: " << (i + 1) << "\n";
          break;
        }

      }

      tinyusdz::Stage comp_stage;
      ret = LayerToStage(src_layer, &comp_stage, &warn_, &error_);

      if (!ret) {
        return false;
      }

      return stageToRenderScene(stage, is_usdz, binary);
  }
#endif


  int numMeshes() const { return render_scene_.meshes.size(); }

  int numMaterials() const { return render_scene_.materials.size(); }

  int numTextures() const { return render_scene_.textures.size(); }

  int numImages() const { return render_scene_.images.size(); }

  // Legacy method for backward compatibility
  emscripten::val getMaterial(int mat_id) const {
    // Default to JSON format for backward compatibility
    return getMaterial(mat_id, "json");
  }

  // New method that supports format parameter (json or xml)
  emscripten::val getMaterial(int mat_id, const std::string& format) const {
    emscripten::val result = emscripten::val::object();

    if (!loaded_) {
      result.set("error", "Scene not loaded");
      return result;
    }

    if (mat_id < 0 || mat_id >= render_scene_.materials.size()) {
      result.set("error", "Invalid material ID");
      return result;
    }

    const auto &material = render_scene_.materials[mat_id];

    // Determine serialization format
    tinyusdz::tydra::SerializationFormat serFormat;
    if (format == "xml") {
      serFormat = tinyusdz::tydra::SerializationFormat::XML;
    } else if (format == "json") {
      serFormat = tinyusdz::tydra::SerializationFormat::JSON;
    } else {
      // For backward compatibility, if format is not recognized,
      // return the old format
      if (format.empty() || format == "legacy") {
        // Return legacy format for backward compatibility
        emscripten::val mat = emscripten::val::object();

        // Check if material has UsdPreviewSurface
        if (!material.hasUsdPreviewSurface()) {
          mat.set("error", "Material does not have UsdPreviewSurface shader");
          return mat;
        }

        const auto &m = material;
        const auto &shader = *m.surfaceShader;

        mat.set("diffuseColor", shader.diffuseColor.value);
        if (shader.diffuseColor.is_texture()) {
          mat.set("diffuseColorTextureId", shader.diffuseColor.texture_id);
        }

        mat.set("emissiveColor", shader.emissiveColor.value);
        if (shader.emissiveColor.is_texture()) {
          mat.set("emissiveColorTextureId", shader.emissiveColor.texture_id);
        }

        mat.set("useSpecularWorkflow", shader.useSpecularWorkflow);
        if (shader.useSpecularWorkflow) {
          mat.set("specularColor", shader.specularColor.value);
          if (shader.specularColor.is_texture()) {
            mat.set("specularColorTextureId", shader.specularColor.texture_id);
          }
        } else {
          mat.set("metallic", shader.metallic.value);
          if (shader.metallic.is_texture()) {
            mat.set("metallicTextureId", shader.metallic.texture_id);
          }
        }

        mat.set("roughness", shader.roughness.value);
        if (shader.roughness.is_texture()) {
          mat.set("roughnessTextureId", shader.roughness.texture_id);
        }

        mat.set("clearcoat", shader.clearcoat.value);
        if (shader.clearcoat.is_texture()) {
          mat.set("clearcoatTextureId", shader.clearcoat.texture_id);
        }

        mat.set("clearcoatRoughness", shader.clearcoatRoughness.value);
        if (shader.clearcoatRoughness.is_texture()) {
          mat.set("clearcoatRoughnessTextureId", shader.clearcoatRoughness.texture_id);
        }

        mat.set("opacity", shader.opacity.value);
        if (shader.opacity.is_texture()) {
          mat.set("opacityTextureId", shader.opacity.texture_id);
        }

        mat.set("opacityThreshold", shader.opacityThreshold.value);
        if (shader.opacityThreshold.is_texture()) {
          mat.set("opacityThresholdTextureId", shader.opacityThreshold.texture_id);
        }

        mat.set("ior", shader.ior.value);
        if (shader.ior.is_texture()) {
          mat.set("iorTextureId", shader.ior.texture_id);
        }

        mat.set("normal", shader.normal.value);
        if (shader.normal.is_texture()) {
          mat.set("normalTextureId", shader.normal.texture_id);
        }

        mat.set("displacement", shader.displacement.value);
        if (shader.displacement.is_texture()) {
          mat.set("displacementTextureId", shader.displacement.texture_id);
        }

        mat.set("occlusion", shader.occlusion.value);
        if (shader.occlusion.is_texture()) {
          mat.set("occlusionTextureId", shader.occlusion.texture_id);
        }

        return mat;
      }

      result.set("error", "Unsupported format. Use 'json' or 'xml'");
      return result;
    }

    // Use the new serialization function with RenderScene for texture info
    auto serialized = tinyusdz::tydra::serializeMaterial(material, serFormat, &render_scene_);

    if (serialized.has_value()) {
      result.set("data", serialized.value());
      result.set("format", format);
    } else {
      result.set("error", serialized.error());
    }

    return result;
  }

  int numLights() const { return static_cast<int>(render_scene_.lights.size()); }

  // Get light as direct object with all properties
  emscripten::val getLight(int light_id) const {
    emscripten::val light = emscripten::val::object();

    if (!loaded_) {
      light.set("error", "Scene not loaded");
      return light;
    }

    if (light_id < 0 || light_id >= static_cast<int>(render_scene_.lights.size())) {
      light.set("error", "Invalid light ID");
      return light;
    }

    const auto &l = render_scene_.lights[static_cast<size_t>(light_id)];

    light.set("name", l.name);
    light.set("absPath", l.abs_path);
    light.set("displayName", l.display_name);

    // Light type as string
    std::string typeStr;
    switch (l.type) {
      case tinyusdz::tydra::RenderLight::Type::Point: typeStr = "point"; break;
      case tinyusdz::tydra::RenderLight::Type::Sphere: typeStr = "sphere"; break;
      case tinyusdz::tydra::RenderLight::Type::Disk: typeStr = "disk"; break;
      case tinyusdz::tydra::RenderLight::Type::Rect: typeStr = "rect"; break;
      case tinyusdz::tydra::RenderLight::Type::Cylinder: typeStr = "cylinder"; break;
      case tinyusdz::tydra::RenderLight::Type::Distant: typeStr = "distant"; break;
      case tinyusdz::tydra::RenderLight::Type::Dome: typeStr = "dome"; break;
      case tinyusdz::tydra::RenderLight::Type::Geometry: typeStr = "geometry"; break;
      case tinyusdz::tydra::RenderLight::Type::Portal: typeStr = "portal"; break;
    }
    light.set("type", typeStr);

    // Common light properties
    emscripten::val color = emscripten::val::array();
    color.call<void>("push", l.color[0]);
    color.call<void>("push", l.color[1]);
    color.call<void>("push", l.color[2]);
    light.set("color", color);

    light.set("intensity", l.intensity);
    light.set("exposure", l.exposure);
    light.set("diffuse", l.diffuse);
    light.set("specular", l.specular);
    light.set("normalize", l.normalize);

    // Color temperature
    light.set("enableColorTemperature", l.enableColorTemperature);
    light.set("colorTemperature", l.colorTemperature);

    // Transform
    emscripten::val transform = emscripten::val::array();
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        transform.call<void>("push", l.transform.m[i][j]);
      }
    }
    light.set("transform", transform);

    emscripten::val position = emscripten::val::array();
    position.call<void>("push", l.position[0]);
    position.call<void>("push", l.position[1]);
    position.call<void>("push", l.position[2]);
    light.set("position", position);

    emscripten::val direction = emscripten::val::array();
    direction.call<void>("push", l.direction[0]);
    direction.call<void>("push", l.direction[1]);
    direction.call<void>("push", l.direction[2]);
    light.set("direction", direction);

    // Type-specific parameters
    light.set("radius", l.radius);
    light.set("width", l.width);
    light.set("height", l.height);
    light.set("length", l.length);
    light.set("angle", l.angle);
    light.set("textureFile", l.textureFile);

    // Shaping (spotlight/IES)
    light.set("shapingConeAngle", l.shapingConeAngle);
    light.set("shapingConeSoftness", l.shapingConeSoftness);
    light.set("shapingFocus", l.shapingFocus);
    emscripten::val shapingFocusTint = emscripten::val::array();
    shapingFocusTint.call<void>("push", l.shapingFocusTint[0]);
    shapingFocusTint.call<void>("push", l.shapingFocusTint[1]);
    shapingFocusTint.call<void>("push", l.shapingFocusTint[2]);
    light.set("shapingFocusTint", shapingFocusTint);
    light.set("shapingIesFile", l.shapingIesFile);
    light.set("shapingIesAngleScale", l.shapingIesAngleScale);
    light.set("shapingIesNormalize", l.shapingIesNormalize);

    // Shadow
    light.set("shadowEnable", l.shadowEnable);
    emscripten::val shadowColor = emscripten::val::array();
    shadowColor.call<void>("push", l.shadowColor[0]);
    shadowColor.call<void>("push", l.shadowColor[1]);
    shadowColor.call<void>("push", l.shadowColor[2]);
    light.set("shadowColor", shadowColor);
    light.set("shadowDistance", l.shadowDistance);
    light.set("shadowFalloff", l.shadowFalloff);
    light.set("shadowFalloffGamma", l.shadowFalloffGamma);

    // DomeLight specific
    std::string domeTexFmtStr;
    switch (l.domeTextureFormat) {
      case tinyusdz::tydra::RenderLight::DomeTextureFormat::Automatic: domeTexFmtStr = "automatic"; break;
      case tinyusdz::tydra::RenderLight::DomeTextureFormat::Latlong: domeTexFmtStr = "latlong"; break;
      case tinyusdz::tydra::RenderLight::DomeTextureFormat::MirroredBall: domeTexFmtStr = "mirroredBall"; break;
      case tinyusdz::tydra::RenderLight::DomeTextureFormat::Angular: domeTexFmtStr = "angular"; break;
    }
    light.set("domeTextureFormat", domeTexFmtStr);
    light.set("guideRadius", l.guideRadius);
    light.set("envmapTextureId", l.envmap_texture_id);

    // GeometryLight specific
    light.set("geometryMeshId", l.geometry_mesh_id);
    light.set("materialSyncMode", l.material_sync_mode);

    // LTE SpectralAPI: Spectral emission
    if (l.hasSpectralEmission()) {
      emscripten::val spd = emscripten::val::object();
      const auto &emission = *l.spd_emission;

      // Samples as array of [wavelength, value] pairs
      emscripten::val samples = emscripten::val::array();
      for (const auto &s : emission.samples) {
        emscripten::val sample = emscripten::val::array();
        sample.call<void>("push", s[0]);
        sample.call<void>("push", s[1]);
        samples.call<void>("push", sample);
      }
      spd.set("samples", samples);

      // Interpolation method
      std::string interpStr;
      switch (emission.interpolation) {
        case tinyusdz::tydra::SpectralInterpolation::Linear: interpStr = "linear"; break;
        case tinyusdz::tydra::SpectralInterpolation::Held: interpStr = "held"; break;
        case tinyusdz::tydra::SpectralInterpolation::Cubic: interpStr = "cubic"; break;
        case tinyusdz::tydra::SpectralInterpolation::Sellmeier: interpStr = "sellmeier"; break;
      }
      spd.set("interpolation", interpStr);

      // Wavelength unit
      std::string unitStr = (emission.unit == tinyusdz::tydra::WavelengthUnit::Nanometers)
                            ? "nanometers" : "micrometers";
      spd.set("unit", unitStr);

      // Illuminant preset
      std::string presetStr;
      switch (emission.preset) {
        case tinyusdz::tydra::IlluminantPreset::None: presetStr = "none"; break;
        case tinyusdz::tydra::IlluminantPreset::A: presetStr = "a"; break;
        case tinyusdz::tydra::IlluminantPreset::D50: presetStr = "d50"; break;
        case tinyusdz::tydra::IlluminantPreset::D65: presetStr = "d65"; break;
        case tinyusdz::tydra::IlluminantPreset::E: presetStr = "e"; break;
        case tinyusdz::tydra::IlluminantPreset::F1: presetStr = "f1"; break;
        case tinyusdz::tydra::IlluminantPreset::F2: presetStr = "f2"; break;
        case tinyusdz::tydra::IlluminantPreset::F7: presetStr = "f7"; break;
        case tinyusdz::tydra::IlluminantPreset::F11: presetStr = "f11"; break;
      }
      spd.set("preset", presetStr);

      light.set("spectralEmission", spd);
    }

    return light;
  }

  // Get light with format parameter (json or xml) - serialized output
  emscripten::val getLightWithFormat(int light_id, const std::string& format) const {
    emscripten::val result = emscripten::val::object();

    if (!loaded_) {
      result.set("error", "Scene not loaded");
      return result;
    }

    if (light_id < 0 || light_id >= static_cast<int>(render_scene_.lights.size())) {
      result.set("error", "Invalid light ID");
      return result;
    }

    const auto &light = render_scene_.lights[static_cast<size_t>(light_id)];

    // Determine serialization format
    tinyusdz::tydra::SerializationFormat serFormat;
    if (format == "xml") {
      serFormat = tinyusdz::tydra::SerializationFormat::XML;
    } else if (format == "json") {
      serFormat = tinyusdz::tydra::SerializationFormat::JSON;
    } else {
      result.set("error", "Unsupported format. Use 'json' or 'xml'");
      return result;
    }

    // Use the serialization function with RenderScene for mesh info
    auto serialized = tinyusdz::tydra::serializeLight(light, serFormat, &render_scene_);

    if (serialized.has_value()) {
      result.set("data", serialized.value());
      result.set("format", format);
    } else {
      result.set("error", serialized.error());
    }

    return result;
  }

  emscripten::val getAllLights() const {
    emscripten::val lights = emscripten::val::array();

    if (!loaded_) {
      return lights;
    }

    for (int i = 0; i < static_cast<int>(render_scene_.lights.size()); i++) {
      lights.call<void>("push", getLight(i));
    }

    return lights;
  }

  emscripten::val getTexture(int tex_id) const {
    emscripten::val tex = emscripten::val::object();

    if (!loaded_) {
      return tex;
    }

    if (tex_id >= render_scene_.textures.size()) {
      return tex;
    }

    const auto &t = render_scene_.textures[tex_id];

    tex.set("textureImageId", int(t.texture_image_id));
    tex.set("wrapS", to_string(t.wrapS));
    tex.set("wrapT", to_string(t.wrapT));
    //  TOOD: bias, scale, rot/scale/trans, etc

    return tex;
  }

  emscripten::val getImage(int img_id) const {
    emscripten::val img = emscripten::val::object();

    if (!loaded_) {
      return img;
    }

    if (img_id >= render_scene_.images.size()) {
      return img;
    }

    const auto &i = render_scene_.images[img_id];

    img.set("width", int(i.width));
    img.set("height", int(i.height));
    img.set("channels", int(i.channels));
    img.set("uri", i.asset_identifier);
    img.set("decoded", bool(i.decoded));
    img.set("colorSpace", to_string(i.colorSpace));
    img.set("usdColorSpace", to_string(i.usdColorSpace));
    img.set("bufferId", int(i.buffer_id));

    if ((i.buffer_id >= 0) && (i.buffer_id < render_scene_.buffers.size())) {
      const auto &b = render_scene_.buffers[i.buffer_id];

      // TODO: Support HDR

      img.set("data",
              emscripten::typed_memory_view(b.data.size(), b.data.data()));
    }

    return img;
  }

  emscripten::val getMesh(int mesh_id) const {
    emscripten::val mesh = emscripten::val::object();

    if (!loaded_) {
      return mesh;
    }

    if (mesh_id >= render_scene_.meshes.size()) {
      return mesh;
    }

    const tinyusdz::tydra::RenderMesh &rmesh =
        render_scene_.meshes[size_t(mesh_id)];

    //if (rmesh.has_indices()) {
      const uint32_t *indices_ptr = rmesh.faceVertexIndices().data();
      mesh.set("faceVertexIndices",
               emscripten::typed_memory_view(rmesh.faceVertexIndices().size(),
                                             indices_ptr));
      const uint32_t *counts_ptr = rmesh.faceVertexCounts().data();
      mesh.set("faceVertexCounts",
               emscripten::typed_memory_view(rmesh.faceVertexCounts().size(),
                                             counts_ptr));
    //} else {
    //  // Assume all triangles and facevarying attributes.
    //  if (!rmesh.is_triangulated()) {
    //    TUSDZ_LOG_E("Mesh must be triangulated when the mesh doesn't have indices\n");
    //    return mesh;
    //  }
    //}

    // TODO: Use three.js scene description format?
    mesh.set("primName", rmesh.prim_name);
    mesh.set("displayName", rmesh.display_name);
    mesh.set("absPath", rmesh.abs_path);
    //mesh.set("hasIndices", rmesh.has_indices());


    const float *points_ptr =
        reinterpret_cast<const float *>(rmesh.points.data());
    // vec3
    mesh.set("points", emscripten::typed_memory_view(rmesh.points.size() * 3,
                                                     points_ptr));

    if (!rmesh.normals.empty()) {
      const float *normals_ptr =
          reinterpret_cast<const float *>(rmesh.normals.data.data());

      mesh.set("normals", emscripten::typed_memory_view(
                              rmesh.normals.vertex_count() * 3, normals_ptr));
    }

    {
      // Export all UV sets
      emscripten::val uvSets = emscripten::val::object();

      for (const auto& uv_pair : rmesh.texcoords) {
        uint32_t uvSlotId = uv_pair.first;
        const auto& uv_data = uv_pair.second;

        const float *uvs_ptr = reinterpret_cast<const float *>(uv_data.data.data());

        // Create UV set object with metadata
        emscripten::val uvSet = emscripten::val::object();
        uvSet.set("data", emscripten::typed_memory_view(
                     uv_data.vertex_count() * 2, uvs_ptr));
        uvSet.set("vertexCount", uv_data.vertex_count());
        uvSet.set("slotId", int(uvSlotId));

        // Add to UV sets collection
        std::string slotKey = "uv" + std::to_string(uvSlotId);
        uvSets.set(slotKey.c_str(), uvSet);
      }

      mesh.set("uvSets", uvSets);

      // Keep backward compatibility - slot 0 as "texcoords"
      if (rmesh.texcoords.count(0)) {
        const float *uvs_ptr = reinterpret_cast<const float *>(
            rmesh.texcoords.at(0).data.data());
        mesh.set("texcoords",
                 emscripten::typed_memory_view(
                     rmesh.texcoords.at(0).vertex_count() * 2, uvs_ptr));
      }
    }

    if (!rmesh.tangents.empty()) {
      const float *tangents_ptr =
          reinterpret_cast<const float *>(rmesh.tangents.data.data());

      mesh.set("tangents", emscripten::typed_memory_view(
                              rmesh.tangents.vertex_count() * 3, tangents_ptr));
    }

    mesh.set("materialId", rmesh.material_id);
    mesh.set("doubleSided", rmesh.doubleSided);

    // Export area light properties (MeshLightAPI)
    mesh.set("isAreaLight", rmesh.is_area_light);
    if (rmesh.is_area_light) {
      const float *light_color_ptr = rmesh.light_color.data();
      mesh.set("lightColor", emscripten::typed_memory_view(3, light_color_ptr));
      mesh.set("lightIntensity", rmesh.light_intensity);
      mesh.set("lightExposure", rmesh.light_exposure);
      mesh.set("lightNormalize", rmesh.light_normalize);
      mesh.set("lightMaterialSyncMode", emscripten::val(rmesh.light_material_sync_mode));
    }

    // Export skinning data (joint indices, joint weights)
    if (!rmesh.joint_and_weights.jointIndices.empty()) {
      const int *joint_indices_ptr = rmesh.joint_and_weights.jointIndices.data();
      mesh.set("jointIndices",
               emscripten::typed_memory_view(
                   rmesh.joint_and_weights.jointIndices.size(),
                   joint_indices_ptr));
    }

    if (!rmesh.joint_and_weights.jointWeights.empty()) {
      const float *joint_weights_ptr = rmesh.joint_and_weights.jointWeights.data();
      mesh.set("jointWeights",
               emscripten::typed_memory_view(
                   rmesh.joint_and_weights.jointWeights.size(),
                   joint_weights_ptr));
    }

    // Export element size (influences per vertex)
    mesh.set("elementSize", rmesh.joint_and_weights.elementSize);

    // Export skeleton ID
    if (rmesh.skel_id >= 0) {
      mesh.set("skel_id", rmesh.skel_id);
    }

    // Export geomBindTransform matrix (4x4 matrix as 16 doubles)
    const double *geom_bind_ptr =
        reinterpret_cast<const double *>(
            rmesh.joint_and_weights.geomBindTransform.m);
    mesh.set("geomBindTransform",
             emscripten::typed_memory_view(16, geom_bind_ptr));

    // Export GeomSubsets (per-face materials) as optimized submeshes
    // Reorder triangles by material so each material has exactly one contiguous group
    if (!rmesh.material_subsetMap.empty()) {
      // Step 1: Group face indices by material
      std::map<int, std::vector<int>> materialToFaces;
      size_t totalFaces = 0;

      for (const auto& subset_pair : rmesh.material_subsetMap) {
        const tinyusdz::tydra::MaterialSubset& subset = subset_pair.second;
        const std::vector<int>& faceIndices = subset.indices();

        int matId = subset.material_id;
        if (materialToFaces.find(matId) == materialToFaces.end()) {
          materialToFaces[matId] = std::vector<int>();
        }

        // Collect all face indices for this material
        materialToFaces[matId].insert(materialToFaces[matId].end(),
                                      faceIndices.begin(), faceIndices.end());
        totalFaces += faceIndices.size();
      }

      // Step 2: Build reordering map - new triangle index -> old triangle index
      // Group all triangles by material, creating contiguous ranges
      std::vector<int> reorderMap;
      reorderMap.reserve(totalFaces);

      emscripten::val submeshes = emscripten::val::array();
      int currentStart = 0;

      for (auto& mat_pair : materialToFaces) {
        int materialId = mat_pair.first;
        std::vector<int>& faceIndices = mat_pair.second;

        if (faceIndices.empty()) continue;

        // Sort face indices within this material group (optional, helps cache coherence)
        std::sort(faceIndices.begin(), faceIndices.end());

        // Add all faces for this material to the reorder map
        for (int faceIdx : faceIndices) {
          reorderMap.push_back(faceIdx);
        }

        // Create one submesh group for this material
        emscripten::val submesh = emscripten::val::object();
        submesh.set("start", currentStart * 3);  // Convert face index to vertex index
        submesh.set("count", static_cast<int>(faceIndices.size()) * 3);  // Number of vertices
        submesh.set("materialId", materialId);
        submeshes.call<void>("push", submesh);

        currentStart += static_cast<int>(faceIndices.size());
      }

      mesh.set("submeshes", submeshes);

      // Step 3: Reorder vertex attributes based on reorderMap
      // For facevarying attributes, each triangle has 3 vertices
      size_t numNewTriangles = reorderMap.size();

      // Reorder points (vec3)
      if (!rmesh.points.empty()) {
        std::vector<float> reorderedPoints(numNewTriangles * 3 * 3);  // numTris * 3 verts * 3 components
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {  // 3 vertices per triangle
            size_t oldVertIdx = static_cast<size_t>(oldTriIdx) * 3 + static_cast<size_t>(v);
            size_t newVertIdx = newTriIdx * 3 + static_cast<size_t>(v);
            if (oldVertIdx < rmesh.points.size()) {
              reorderedPoints[newVertIdx * 3 + 0] = rmesh.points[oldVertIdx][0];
              reorderedPoints[newVertIdx * 3 + 1] = rmesh.points[oldVertIdx][1];
              reorderedPoints[newVertIdx * 3 + 2] = rmesh.points[oldVertIdx][2];
            }
          }
        }
        // Store in cache and update mesh pointer
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.points = std::move(reorderedPoints);
        mesh.set("points", emscripten::typed_memory_view(cache.points.size(), cache.points.data()));
      }

      // Reorder normals (vec3) - data is raw uint8_t buffer, cast to float*
      if (!rmesh.normals.empty()) {
        const float* normalsData = reinterpret_cast<const float*>(rmesh.normals.data.data());
        std::vector<float> reorderedNormals(numNewTriangles * 3 * 3);
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {
            size_t oldVertIdx = static_cast<size_t>(oldTriIdx) * 3 + static_cast<size_t>(v);
            size_t newVertIdx = newTriIdx * 3 + static_cast<size_t>(v);
            if (oldVertIdx < rmesh.normals.vertex_count()) {
              reorderedNormals[newVertIdx * 3 + 0] = normalsData[oldVertIdx * 3 + 0];
              reorderedNormals[newVertIdx * 3 + 1] = normalsData[oldVertIdx * 3 + 1];
              reorderedNormals[newVertIdx * 3 + 2] = normalsData[oldVertIdx * 3 + 2];
            }
          }
        }
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.normals = std::move(reorderedNormals);
        mesh.set("normals", emscripten::typed_memory_view(cache.normals.size(), cache.normals.data()));
      }

      // Reorder texcoords (vec2) - slot 0
      if (rmesh.texcoords.count(0) && !rmesh.texcoords.at(0).data.empty()) {
        const auto& uvData = rmesh.texcoords.at(0);
        const float* uvDataPtr = reinterpret_cast<const float*>(uvData.data.data());
        std::vector<float> reorderedTexcoords(numNewTriangles * 3 * 2);
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {
            size_t oldVertIdx = static_cast<size_t>(oldTriIdx) * 3 + static_cast<size_t>(v);
            size_t newVertIdx = newTriIdx * 3 + static_cast<size_t>(v);
            if (oldVertIdx < uvData.vertex_count()) {
              reorderedTexcoords[newVertIdx * 2 + 0] = uvDataPtr[oldVertIdx * 2 + 0];
              reorderedTexcoords[newVertIdx * 2 + 1] = uvDataPtr[oldVertIdx * 2 + 1];
            }
          }
        }
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.texcoords = std::move(reorderedTexcoords);
        mesh.set("texcoords", emscripten::typed_memory_view(cache.texcoords.size(), cache.texcoords.data()));
      }

      // Reorder tangents (vec3)
      if (!rmesh.tangents.empty()) {
        const float* tangentsData = reinterpret_cast<const float*>(rmesh.tangents.data.data());
        std::vector<float> reorderedTangents(numNewTriangles * 3 * 3);
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {
            size_t oldVertIdx = static_cast<size_t>(oldTriIdx) * 3 + static_cast<size_t>(v);
            size_t newVertIdx = newTriIdx * 3 + static_cast<size_t>(v);
            if (oldVertIdx < rmesh.tangents.vertex_count()) {
              reorderedTangents[newVertIdx * 3 + 0] = tangentsData[oldVertIdx * 3 + 0];
              reorderedTangents[newVertIdx * 3 + 1] = tangentsData[oldVertIdx * 3 + 1];
              reorderedTangents[newVertIdx * 3 + 2] = tangentsData[oldVertIdx * 3 + 2];
            }
          }
        }
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.tangents = std::move(reorderedTangents);
        mesh.set("tangents", emscripten::typed_memory_view(cache.tangents.size(), cache.tangents.data()));
      }

      // Generate new sequential indices (0, 1, 2, 3, 4, 5, ...)
      // Since we reordered the vertex data, indices are now sequential
      std::vector<uint32_t> newIndices(numNewTriangles * 3);
      for (size_t i = 0; i < numNewTriangles * 3; i++) {
        newIndices[i] = static_cast<uint32_t>(i);
      }
      auto& cache = reordered_mesh_cache_[mesh_id];
      cache.faceVertexIndices = std::move(newIndices);
      mesh.set("faceVertexIndices", emscripten::typed_memory_view(
          cache.faceVertexIndices.size(), cache.faceVertexIndices.data()));
    }

    return mesh;
  }

  int getDefaultRootNodeId() { return render_scene_.default_root_node; }

  emscripten::val getDefaultRootNode() {
    return getRootNode(getDefaultRootNodeId());
  }

  emscripten::val getRootNode(int idx) {
    emscripten::val val = emscripten::val::object();

    if ((idx < 0) || (idx >= render_scene_.nodes.size())) {
      return val;
    }

    val = buildNodeRec(render_scene_.nodes[size_t(idx)]);
    return val;
  }

  int numRootNodes() { return render_scene_.nodes.size(); }

  // Get the upAxis from the RenderScene metadata
  std::string getUpAxis() const {
    if (!loaded_) {
      return "Y"; // Default
    }
    return render_scene_.meta.upAxis;
  }

  // Get the complete scene metadata as a JavaScript object
  emscripten::val getSceneMetadata() const {
    emscripten::val metadata = emscripten::val::object();

    if (!loaded_) {
      return metadata;
    }

    metadata.set("copyright", render_scene_.meta.copyright);
    metadata.set("comment", render_scene_.meta.comment);
    metadata.set("upAxis", render_scene_.meta.upAxis);
    metadata.set("metersPerUnit", render_scene_.meta.metersPerUnit);
    metadata.set("framesPerSecond", render_scene_.meta.framesPerSecond);
    metadata.set("timeCodesPerSecond", render_scene_.meta.timeCodesPerSecond);
    metadata.set("autoPlay", render_scene_.meta.autoPlay);

    if (render_scene_.meta.startTimeCode) {
      metadata.set("startTimeCode", render_scene_.meta.startTimeCode.value());
    } else {
      metadata.set("startTimeCode", emscripten::val::null());
    }

    if (render_scene_.meta.endTimeCode) {
      metadata.set("endTimeCode", render_scene_.meta.endTimeCode.value());
    } else {
      metadata.set("endTimeCode", emscripten::val::null());
    }

    return metadata;
  }

  // Animation data access methods
  int numAnimations() const { return render_scene_.animations.size(); }

  // Get a single animation clip as Three.js friendly JSON
  emscripten::val getAnimation(int anim_id) const {
    emscripten::val anim = emscripten::val::object();

    if (!loaded_) {
      return anim;
    }

    if (anim_id >= render_scene_.animations.size()) {
      return anim;
    }

    const auto &clip = render_scene_.animations[anim_id];

    // Basic animation metadata
    anim.set("name", clip.name.empty() ? "Animation" + std::to_string(anim_id) : clip.name);
    anim.set("primName", clip.prim_name);
    anim.set("absPath", clip.abs_path);
    anim.set("displayName", clip.display_name);
    anim.set("duration", clip.duration);

    // Convert samplers to Three.js KeyframeTrack format
    emscripten::val tracks = emscripten::val::array();

    for (const auto &channel : clip.channels) {
      if (!channel.is_valid() || channel.sampler >= clip.samplers.size()) {
        continue;
      }

      const auto &sampler = clip.samplers[channel.sampler];
      if (sampler.empty()) {
        continue;
      }

      emscripten::val track = emscripten::val::object();

      // Set track name based on target node and property
      if (channel.target_node >= 0 && channel.target_node < render_scene_.nodes.size()) {
        const auto &node = render_scene_.nodes[channel.target_node];
        std::string trackName = node.abs_path.empty() ? node.prim_name : node.abs_path;

        // Add property suffix for Three.js compatibility
        switch (channel.path) {
          case tinyusdz::tydra::AnimationPath::Translation:
            trackName += ".position";
            track.set("type", "vector3");
            break;
          case tinyusdz::tydra::AnimationPath::Rotation:
            trackName += ".quaternion";
            track.set("type", "quaternion");
            break;
          case tinyusdz::tydra::AnimationPath::Scale:
            trackName += ".scale";
            track.set("type", "vector3");
            break;
          case tinyusdz::tydra::AnimationPath::Weights:
            trackName += ".morphTargetInfluences";
            track.set("type", "number");
            break;
        }

        track.set("name", trackName);
        track.set("nodeName", node.prim_name);
        track.set("nodeIndex", channel.target_node);
      }

      // Set interpolation mode
      std::string interpolation;
      switch (sampler.interpolation) {
        case tinyusdz::tydra::AnimationInterpolation::Step:
          interpolation = "STEP";
          break;
        case tinyusdz::tydra::AnimationInterpolation::CubicSpline:
          interpolation = "CUBICSPLINE";
          break;
        case tinyusdz::tydra::AnimationInterpolation::Linear:
        default:
          interpolation = "LINEAR";
          break;
      }
      track.set("interpolation", interpolation);

      // Convert times and values to typed arrays for efficiency
      track.set("times", emscripten::typed_memory_view(sampler.times.size(), sampler.times.data()));
      track.set("values", emscripten::typed_memory_view(sampler.values.size(), sampler.values.data()));

      // Add property path for reference
      std::string pathStr;
      switch (channel.path) {
        case tinyusdz::tydra::AnimationPath::Translation:
          pathStr = "translation";
          break;
        case tinyusdz::tydra::AnimationPath::Rotation:
          pathStr = "rotation";
          break;
        case tinyusdz::tydra::AnimationPath::Scale:
          pathStr = "scale";
          break;
        case tinyusdz::tydra::AnimationPath::Weights:
          pathStr = "weights";
          break;
      }
      track.set("path", pathStr);

      tracks.call<void>("push", track);
    }

    anim.set("tracks", tracks);

    // Also expose raw channels and samplers arrays for advanced use (skeletal animation, etc.)
    emscripten::val channels = emscripten::val::array();
    for (const auto &channel : clip.channels) {
      emscripten::val ch = emscripten::val::object();
      ch.set("sampler", channel.sampler);
      ch.set("target_node", channel.target_node);
      ch.set("skeleton_id", channel.skeleton_id);
      ch.set("joint_id", channel.joint_id);

      // Set target_type string
      std::string targetTypeStr = (channel.target_type == tinyusdz::tydra::ChannelTargetType::SkeletonJoint)
        ? "SkeletonJoint" : "SceneNode";
      ch.set("target_type", targetTypeStr);

      // Set path string
      std::string pathStr;
      switch (channel.path) {
        case tinyusdz::tydra::AnimationPath::Translation:
          pathStr = "Translation";
          break;
        case tinyusdz::tydra::AnimationPath::Rotation:
          pathStr = "Rotation";
          break;
        case tinyusdz::tydra::AnimationPath::Scale:
          pathStr = "Scale";
          break;
        case tinyusdz::tydra::AnimationPath::Weights:
          pathStr = "Weights";
          break;
      }
      ch.set("path", pathStr);

      channels.call<void>("push", ch);
    }
    anim.set("channels", channels);

    // Expose samplers array
    emscripten::val samplers = emscripten::val::array();
    for (const auto &sampler : clip.samplers) {
      emscripten::val samp = emscripten::val::object();
      samp.set("times", emscripten::typed_memory_view(sampler.times.size(), sampler.times.data()));
      samp.set("values", emscripten::typed_memory_view(sampler.values.size(), sampler.values.data()));

      std::string interpolation;
      switch (sampler.interpolation) {
        case tinyusdz::tydra::AnimationInterpolation::Step:
          interpolation = "STEP";
          break;
        case tinyusdz::tydra::AnimationInterpolation::CubicSpline:
          interpolation = "CUBICSPLINE";
          break;
        case tinyusdz::tydra::AnimationInterpolation::Linear:
        default:
          interpolation = "LINEAR";
          break;
      }
      samp.set("interpolation", interpolation);

      samplers.call<void>("push", samp);
    }
    anim.set("samplers", samplers);

    return anim;
  }

  // Get all animations as an array
  emscripten::val getAllAnimations() const {
    emscripten::val animations = emscripten::val::array();

    if (!loaded_) {
      return animations;
    }

    for (int i = 0; i < render_scene_.animations.size(); ++i) {
      animations.call<void>("push", getAnimation(i));
    }

    return animations;
  }

  // Get animation summary info without full data (useful for listing)
  emscripten::val getAnimationInfo(int anim_id) const {
    emscripten::val info = emscripten::val::object();

    if (!loaded_ || anim_id >= render_scene_.animations.size()) {
      return info;
    }

    const auto &clip = render_scene_.animations[anim_id];

    info.set("id", anim_id);
    info.set("name", clip.name.empty() ? "Animation" + std::to_string(anim_id) : clip.name);
    info.set("duration", clip.duration);
    info.set("numTracks", int(clip.channels.size()));
    info.set("numSamplers", int(clip.samplers.size()));

    // Count unique target nodes
    std::set<int32_t> targetNodes;
    for (const auto &channel : clip.channels) {
      if (channel.target_node >= 0) {
        targetNodes.insert(channel.target_node);
      }
    }
    info.set("numTargetNodes", int(targetNodes.size()));

    return info;
  }

  // Get all animation summaries
  emscripten::val getAllAnimationInfos() const {
    emscripten::val infos = emscripten::val::array();

    if (!loaded_) {
      return infos;
    }

    for (int i = 0; i < render_scene_.animations.size(); ++i) {
      infos.call<void>("push", getAnimationInfo(i));
    }

    return infos;
  }

  void setEnableComposition(bool enabled) { enableComposition_ = enabled; }
  void setLoadTextureInNative(bool onoff) {
    loadTextureInNative_ = onoff;
  }

  void setMaxMemoryLimitMB(int32_t limit_mb) {
    max_memory_limit_mb_ = limit_mb;
  }

  int32_t getMaxMemoryLimitMB() const {
    return max_memory_limit_mb_;
  }

  // Bone reduction configuration
  void setEnableBoneReduction(bool enabled) {
    enable_bone_reduction_ = enabled;
  }

  bool getEnableBoneReduction() const {
    return enable_bone_reduction_;
  }

  void setTargetBoneCount(uint32_t count) {
    if (count > 0 && count <= 64) {  // Sanity check: 1-64 bones
      target_bone_count_ = count;
    }
  }

  uint32_t getTargetBoneCount() const {
    return target_bone_count_;
  }

  emscripten::val getAssetSearchPaths() const {
    emscripten::val arr = emscripten::val::array();
    for (size_t i = 0; i < search_paths_.size(); i++) {
     arr.call<void>("push", search_paths_[i]);
    }
    return arr;
  }

  void setBaseWorkingPath(const std::string &path) {
    base_dir_ = path;
  }

  std::string getBaseWorkingPath() const {
    return base_dir_;
  }

  void clearAssetSearchPaths() {
    search_paths_.clear();
  }

  void addAssetSearchPath(const std::string &path) {
    search_paths_.push_back(path);
  }

  // Return filename passed to loadFromBinary/loadAsLayerFromBinary.
  std::string getURI() const {
    return filename_;
  }

  emscripten::val extractSublayerAssetPaths() {
    emscripten::val arr = emscripten::val::array();

    
    std::vector<std::string> paths = tinyusdz::ExtractSublayerAssetPaths(layer_);
    for (size_t i = 0; i < paths.size(); i++) {
     arr.call<void>("push", paths[i]);
    }

    return arr;
  }

  emscripten::val extractReferencesAssetPaths() {
    emscripten::val arr = emscripten::val::array();

    
    std::vector<std::string> paths = tinyusdz::ExtractReferencesAssetPaths(layer_);
    for (size_t i = 0; i < paths.size(); i++) {
     arr.call<void>("push", paths[i]);
    }

    return arr;
  }

  emscripten::val extractPayloadAssetPaths() {
    emscripten::val arr = emscripten::val::array();

    
    std::vector<std::string> paths = tinyusdz::ExtractPayloadAssetPaths(layer_);
    for (size_t i = 0; i < paths.size(); i++) {
     arr.call<void>("push", paths[i]);
    }

    return arr;
  }

  bool hasSublayers() {
    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    return curr.metas().subLayers.size();
  }


  bool composeSublayers() {

    tinyusdz::AssetResolutionResolver resolver;
    if (!SetupEMAssetResolution(resolver, &em_resolver_)) {
      std::cerr << "Failed to setup EMAssetResolution\n";
      return false;
    }
    const std::string base_dir = "./"; // FIXME
    resolver.set_current_working_path(base_dir);
    resolver.set_search_paths({base_dir});

    if (composited_) {
      layer_ = std::move(composed_layer_);
    }

    if (!tinyusdz::CompositeSublayers(resolver, layer_, &composed_layer_, &warn_, &error_)) {
      std::cerr << "Failed to composite subLayers: \n";
      if (composited_) {
        // make 'layer_' and 'composed_layer_' invalid
        loaded_as_layer_ = false;
        composited_ = false;
      }
      return false;
    }

    composited_ = true;

    return true;
  }

  bool hasReferences() {
    return tinyusdz::HasReferences(composited_ ? composed_layer_ : layer_, /* force_check */true);
  }

  bool composeReferences() {

    tinyusdz::AssetResolutionResolver resolver;
    if (!SetupEMAssetResolution(resolver, &em_resolver_)) {
      std::cerr << "Failed to setup EMAssetResolution\n";
      return false;
    }
    const std::string base_dir = "./"; // FIXME
    resolver.set_current_working_path(base_dir);
    resolver.set_search_paths({base_dir});


    if (composited_) {
      layer_ = std::move(composed_layer_);
    }

    if (!tinyusdz::CompositeReferences(resolver, layer_, &composed_layer_, &warn_, &error_)) {
      std::cerr << "Failed to composite references: \n";
      if (composited_) {
        // make 'layer_' and 'composed_layer_' invalid
        loaded_as_layer_ = false;
        composited_ = false;
      }
      return false;
    }

    composited_ = true;

    return true;
  }

  bool hasPayload() {
    return tinyusdz::HasPayload(layer_, /* force_check */true);
  }

  bool composePayload() {

    tinyusdz::AssetResolutionResolver resolver;
    if (!SetupEMAssetResolution(resolver, &em_resolver_)) {
      std::cerr << "Failed to setup EMAssetResolution\n";
      return false;
    }
    const std::string base_dir = "./"; // FIXME
    resolver.set_current_working_path(base_dir);
    resolver.set_search_paths({base_dir});

    if (composited_) {
      layer_ = std::move(composed_layer_);
    }

    if (!tinyusdz::CompositePayload(resolver, layer_, &composed_layer_, &warn_, &error_)) {
      std::cerr << "Failed to composite payload: \n";
      if (composited_) {
        // make 'layer_' and 'composed_layer_' invalid
        loaded_as_layer_ = false;
        composited_ = false;
      }
      return false;
    }

    composited_ = true;

    return true;
  }


  bool hasInherits() {
    return tinyusdz::HasInherits(composited_ ? composed_layer_ : layer_ );
  }

  bool composeInherits() {

    if (composited_) {
      layer_ = std::move(composed_layer_);
    }

    if (!tinyusdz::CompositeInherits( layer_, &composed_layer_, &warn_, &error_)) {
      std::cerr << "Failed to composite inherits: \n";
      if (composited_) {
        // make 'layer_' and 'composed_layer_' invalid
        loaded_as_layer_ = false;
        composited_ = false;
      }
      return false;
    }

    composited_ = true;

    return true;
  }

  bool hasVariants() {
    return tinyusdz::HasVariants(composited_ ? composed_layer_ : layer_ );
  }

  bool composeVariants() {

    if (composited_) {
      layer_ = std::move(composed_layer_);
    }

    if (!tinyusdz::CompositeVariant( layer_, &composed_layer_, &warn_, &error_)) {
      std::cerr << "Failed to composite variant: \n";
      if (composited_) {
        // make 'layer_' and 'composed_layer_' invalid
        loaded_as_layer_ = false;
        composited_ = false;
      }
      return false;
    }

    composited_ = true;

    return true;
  }
  
  bool layerToRenderScene() {

    if (!loaded_as_layer_) {
      std::cerr << "not loaded as layer\n";
      return false;
    }

    tinyusdz::Stage stage;

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;

    // LayerToStage expects an rvalue reference, so make a copy
    tinyusdz::Layer layer_copy = curr;

    if (!tinyusdz::LayerToStage(std::move(layer_copy), &stage, &warn_, &error_)) {
      std::cerr << "Failed to LayerToStage \n";
      return false;
    }

    std::string empty;
    return stageToRenderScene(stage, /* TODO: is_usdz*/false, empty);

  }

  std::string layerToString() const {
    if (!loaded_) {
      return std::string();
    }
    if (!loaded_as_layer_) {
      return std::string();
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;

    return tinyusdz::to_string(curr);
  }

  void clearAssets() {
    em_resolver_.clear();
  }

  /// Reset all state - clears render scene, assets, and all cached data
  /// Call this before loading a new USD file to free memory
  void reset() {
    // Clear loaded flag
    loaded_ = false;
    loaded_as_layer_ = false;
    composited_ = false;

    // Clear strings
    filename_.clear();
    warn_.clear();
    error_.clear();

    // Clear render scene (meshes, materials, textures, buffers, etc.)
    render_scene_ = tinyusdz::tydra::RenderScene();

    // Clear layers
    layer_ = tinyusdz::Layer();
    composed_layer_ = tinyusdz::Layer();

    // Clear USDZ asset
    usdz_asset_ = tinyusdz::USDZAsset();

    // Clear asset resolver cache
    em_resolver_.clear();

    // Clear reordered mesh cache
    reordered_mesh_cache_.clear();

    // Reset parsing progress
    parsing_progress_.reset();
  }

  /// Get memory usage statistics
  emscripten::val getMemoryStats() const {
    emscripten::val stats = emscripten::val::object();

    // Count meshes
    stats.set("numMeshes", static_cast<int>(render_scene_.meshes.size()));
    stats.set("numMaterials", static_cast<int>(render_scene_.materials.size()));
    stats.set("numTextures", static_cast<int>(render_scene_.textures.size()));
    stats.set("numImages", static_cast<int>(render_scene_.images.size()));
    stats.set("numBuffers", static_cast<int>(render_scene_.buffers.size()));
    stats.set("numNodes", static_cast<int>(render_scene_.nodes.size()));
    stats.set("numLights", static_cast<int>(render_scene_.lights.size()));

    // Estimate buffer memory
    size_t bufferMemory = 0;
    for (const auto &buf : render_scene_.buffers) {
      bufferMemory += buf.data.size();
    }
    stats.set("bufferMemoryBytes", static_cast<double>(bufferMemory));
    stats.set("bufferMemoryMB", static_cast<double>(bufferMemory) / (1024.0 * 1024.0));

    // Asset cache count
    stats.set("assetCacheCount", static_cast<int>(em_resolver_.cache.size()));

    // Reordered mesh cache count
    stats.set("reorderedMeshCacheCount", static_cast<int>(reordered_mesh_cache_.size()));

    return stats;
  }

  void setAsset(const std::string &name, const std::string &binary) {
    em_resolver_.add(name, binary);
  }

  // Streaming asset methods
  bool startStreamingAsset(const std::string &name, size_t expected_size) {
    return em_resolver_.startStreamingAsset(name, expected_size);
  }
  
  bool appendAssetChunk(const std::string &name, const std::string &chunk) {
    return em_resolver_.appendAssetChunk(name, chunk);
  }
  
  bool finalizeStreamingAsset(const std::string &name) {
    return em_resolver_.finalizeStreamingAsset(name);
  }
  
  bool isStreamingAssetComplete(const std::string &name) const {
    return em_resolver_.isStreamingAssetComplete(name);
  }
  
  emscripten::val getStreamingProgress(const std::string &name) const {
    return em_resolver_.getStreamingProgress(name);
  }

  //
  // Zero-copy streaming buffer methods for memory-efficient transfer
  //

  /// Allocate a zero-copy buffer for streaming transfer from JS
  /// Returns object with {success, uuid, bufferPtr, totalSize} or {success: false, error}
  emscripten::val allocateZeroCopyBuffer(const std::string &name, size_t size) {
    return em_resolver_.allocateZeroCopyBuffer(name, size);
  }

  /// Get the buffer pointer for direct memory writes
  double getZeroCopyBufferPtr(const std::string &name) {
    return em_resolver_.getZeroCopyBufferPtr(name);
  }

  /// Get buffer pointer at specific offset for chunked writes
  double getZeroCopyBufferPtrAtOffset(const std::string &name, size_t offset) {
    return em_resolver_.getZeroCopyBufferPtrAtOffset(name, offset);
  }

  /// Mark bytes as written (call after each chunk write)
  bool markZeroCopyBytesWritten(const std::string &name, size_t count) {
    return em_resolver_.markZeroCopyBytesWritten(name, count);
  }

  /// Get zero-copy buffer progress
  emscripten::val getZeroCopyProgress(const std::string &name) const {
    return em_resolver_.getZeroCopyProgress(name);
  }

  /// Finalize the zero-copy buffer and move to asset cache
  bool finalizeZeroCopyBuffer(const std::string &name) {
    return em_resolver_.finalizeZeroCopyBuffer(name);
  }

  /// Cancel and free zero-copy buffer
  bool cancelZeroCopyBuffer(const std::string &name) {
    return em_resolver_.cancelZeroCopyBuffer(name);
  }

  /// Get all active zero-copy buffers
  emscripten::val getActiveZeroCopyBuffers() const {
    return em_resolver_.getActiveZeroCopyBuffers();
  }

  bool hasAsset(const std::string &name) const {
    return em_resolver_.has(name);
  }

  std::string getAssetHash(const std::string &name) const {
    return em_resolver_.getHash(name);
  }

  bool verifyAssetHash(const std::string &name, const std::string &expected_hash) const {
    return em_resolver_.verifyHash(name, expected_hash);
  }

  emscripten::val getAsset(const std::string &name) const {
    emscripten::val val = emscripten::val::object();
    if (em_resolver_.has(name)) {
      const AssetCacheEntry &entry = em_resolver_.get(name);
      val.set("name", name);
      val.set("data", emscripten::typed_memory_view(entry.binary.size(), entry.binary.data()));
      val.set("sha256", entry.sha256_hash);
      val.set("uuid", entry.uuid);
    }
    return val;
  }

  std::string getAssetUUID(const std::string &name) const {
    return em_resolver_.getUUID(name);
  }

  std::string getStreamingAssetUUID(const std::string &name) const {
    return em_resolver_.getStreamingUUID(name);
  }

  emscripten::val getAllAssetUUIDs() const {
    return em_resolver_.getAssetUUIDs();
  }

  std::string findAssetByUUID(const std::string &uuid) const {
    return em_resolver_.findAssetByUUID(uuid);
  }

  // Get asset by UUID instead of name
  emscripten::val getAssetByUUID(const std::string &uuid) const {
    emscripten::val val = emscripten::val::object();
    
    if (!em_resolver_.hasByUUID(uuid)) {
      val.set("error", "Asset not found with UUID: " + uuid);
      return val;
    }
    
    const AssetCacheEntry &entry = em_resolver_.getByUUID(uuid);
    const std::string name = em_resolver_.findAssetByUUID(uuid);
    
    val.set("name", name);
    val.set("data", emscripten::typed_memory_view(entry.binary.size(), 
                                                   reinterpret_cast<const uint8_t*>(entry.binary.data())));
    val.set("sha256", entry.sha256_hash);
    val.set("uuid", entry.uuid);
    
    return val;
  }

  // Delete asset by name or UUID
  bool deleteAsset(const std::string &nameOrUuid) {
    // First try to delete by name
    if (em_resolver_.deleteAsset(nameOrUuid)) {
      return true;
    }
    
    // If not found by name, try to delete by UUID
    return em_resolver_.deleteAssetByUUID(nameOrUuid);
  }

  // Delete asset specifically by UUID
  bool deleteAssetByUUID(const std::string &uuid) {
    return em_resolver_.deleteAssetByUUID(uuid);
  }

  // Delete asset specifically by name
  bool deleteAssetByName(const std::string &name) {
    return em_resolver_.deleteAsset(name);
  }

  // Get number of cached assets
  size_t getAssetCount() const {
    return em_resolver_.cache.size();
  }

  // Check if asset exists (by name or UUID)
  bool assetExists(const std::string &nameOrUuid) const {
    return em_resolver_.has(nameOrUuid) || em_resolver_.hasByUUID(nameOrUuid);
  }

  emscripten::val getAssetCacheDataAsMemoryView(const std::string &name) const {
    return em_resolver_.getCacheDataAsMemoryView(name);
  }

  bool setAssetFromRawPointer(const std::string &name, uintptr_t dataPtr, size_t size) {
    return em_resolver_.addFromRawPointer(name, dataPtr, size);
  }

  emscripten::val extractUnresolvedTexturePaths() const {
    emscripten::val val;

    // Assume USD is converted to RenderScene before calling this function.
    
    for (const tinyusdz::tydra::TextureImage &texImg : render_scene_.images) {
      // 
      if (texImg.buffer_id == -1) {
        std::string path = texImg.asset_identifier;
        val.call<void>("push", path);
      }
    }

    return val;
  }

  bool mcpCreateContext(const std::string &session_id) {
    
    if (mcp_ctx_.count(session_id)) {
      // Context already exists
      return false;
    }

    mcp_ctx_[session_id] = tinyusdz::tydra::mcp::Context();
    mcp_session_id_ = session_id;

    return true;
  }

  bool mcpSelectContext(const std::string &session_id) {
    
    if (!mcp_ctx_.count(session_id)) {
      // Context does not exist
      return false;
    }

    mcp_session_id_ = session_id;

    return true;
  }


  // return JSON string
  std::string mcpToolsList() {

    if (!mcp_ctx_.count(mcp_session_id_)) {
      // TODO: better error message
      return "{ \"error\": \"invalid session_id\"}";
    }

    //Context &ctx = mcp_ctx_.at(mcp_session_id_);
    tinyusdz::tydra::mcp::Context &ctx = mcp_global_ctx_;

    nlohmann::json result;
    if (!tinyusdz::tydra::mcp::GetToolsList(ctx, result)) {
      std::cerr << "[tydra:mcp:GetToolsList] failed." << "\n";
      // TODO: Report error more nice way.
      result = nlohmann::json::object();
      result["isError"] = true;
      result["content"] = nlohmann::json::array();
    }

    std::string s_result = result.dump();

    return s_result;
  }

  // args: JSON string
  // return JSON string
  std::string mcpToolsCall(const std::string &tool_name, const std::string &args) {

    if (!mcp_ctx_.count(mcp_session_id_)) {
      // TODO: better error message
      return "{ \"error\": \"invalid session_id\"}";
    }

    nlohmann::json j_args = nlohmann::json::parse(args);

    //Context &ctx = mcp_ctx_.at(mcp_session_id_);
    auto &ctx = mcp_global_ctx_;

    nlohmann::json result;

    std::string err;
    if (!tinyusdz::tydra::mcp::CallTool(ctx, tool_name, j_args, result, err)) {
      // TODO: Report error more nice way.
      std::cerr << "[tydra:mcp:CallTool]" << err << "\n";
      result = nlohmann::json::object();
      result["isError"] = true;
      result["content"] = nlohmann::json::array();

      nlohmann::json e;
      e["type"] = "text";

      nlohmann::json msg;
      msg["error"] = err;
      e["text"] = msg.dump();

      result["content"].push_back(e);
    }
    
    std::string s_result = result.dump();

    return s_result;
  }

  std::string mcpResourcesList() {

    if (!mcp_ctx_.count(mcp_session_id_)) {
      // TODO: better error message
      return "{ \"error\": \"invalid session_id\"}";
    }

    //Context &ctx = mcp_ctx_.at(mcp_session_id_);
    auto &ctx = mcp_global_ctx_;

    nlohmann::json result;

    if (!tinyusdz::tydra::mcp::GetResourcesList(ctx, result)) {
      // TODO: Report error more nice way.
      std::cerr << "[tydra:mcp:ListResources] failed\n";
      result = nlohmann::json::object();
      result["isError"] = true;
      //result["content"] = nlohmann::json::array();
    }
    
    std::string s_result = result.dump();

    return s_result;
  }

  std::string mcpResourcesRead(const std::string &uri) {

    if (!mcp_ctx_.count(mcp_session_id_)) {
      // TODO: better error message
      return "{ \"error\": \"invalid session_id\"}";
    }

    //Context &ctx = mcp_ctx_.at(mcp_session_id_);
    auto &ctx = mcp_global_ctx_;

    nlohmann::json content;

    if (!tinyusdz::tydra::mcp::ReadResource(ctx, uri, content)) {
      // TODO: Report error more nice way.
      std::cerr << "[tydra:mcp:ReadResources] failed\n";
      content = nlohmann::json::object();
      content["isError"] = true;
      //content["content"] = nlohmann::json::array();
    }
    
    std::string s_content = content.dump();

    return s_content;
  }

  // JSON <-> USD Layer conversion methods
  std::string layerToJSON() const {
    if (!loaded_as_layer_) {
      return "{\"error\": \"No layer loaded\"}";
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    
    nlohmann::json json_obj = tinyusdz::ToJSON(curr);
    return json_obj.dump(2); // Pretty print with 2 spaces
  }

  std::string layerToJSONWithOptions(bool embedBuffers, const std::string& arrayMode) const {
    if (!loaded_as_layer_) {
      return "{\"error\": \"No layer loaded\"}";
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    
    tinyusdz::USDToJSONOptions options;
    options.embedBuffers = embedBuffers;
    
    if (arrayMode == "buffer") {
      options.arrayMode = tinyusdz::ArraySerializationMode::Buffer;
    } else {
      options.arrayMode = tinyusdz::ArraySerializationMode::Base64;
    }

    std::string json_str, warn, err;
    bool success = tinyusdz::to_json_string(curr, options, &json_str, &warn, &err);
    
    if (!success) {
      return "{\"error\": \"Failed to convert layer to JSON: " + err + "\"}";
    }
    
    return json_str;
  }

  bool loadLayerFromJSON(const std::string& json_string) {
    std::string warn, err;
    
    bool success = tinyusdz::JSONToLayer(json_string, &layer_, &warn, &err);
    
    if (success) {
      loaded_ = true;
      loaded_as_layer_ = true;
      composited_ = false;
      warn_ = warn;
      error_.clear();
      filename_ = "from_json.usd";
    } else {
      loaded_ = false;
      loaded_as_layer_ = false;
      composited_ = false;
      warn_ = warn;
      error_ = err;
    }
    
    return success;
  }

  //
  // Progress reporting methods for polling-based async progress
  //

  /// Get current parsing progress as a JS object
  emscripten::val getProgress() const {
    return parsing_progress_.toJS();
  }

  /// Request cancellation of current parsing operation
  void cancelParsing() {
    parsing_progress_.cancel_requested.store(true);
  }

  /// Check if parsing was cancelled
  bool wasCancelled() const {
    return parsing_progress_.stage == ParsingProgress::Stage::Cancelled;
  }

  /// Check if parsing is currently in progress
  bool isParsingInProgress() const {
    return parsing_progress_.stage == ParsingProgress::Stage::Parsing ||
           parsing_progress_.stage == ParsingProgress::Stage::Converting;
  }

  /// Reset progress state (call before starting a new parse)
  void resetProgress() {
    parsing_progress_.reset();
  }

  /// Load from binary with progress reporting
  /// Returns immediately, progress can be polled via getProgress()
  bool loadFromBinaryWithProgress(const std::string &binary, const std::string &filename) {
    // Reset progress state
    parsing_progress_.reset();
    parsing_progress_.setStage(ParsingProgress::Stage::Parsing);
    parsing_progress_.total_bytes = binary.size();
    parsing_progress_.current_operation = "Loading USD file";

    bool is_usdz = tinyusdz::IsUSDZ(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size());

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;

    // Set up progress callback
    options.progress_callback = [](float progress, void *userptr) -> bool {
      ParsingProgress *pp = static_cast<ParsingProgress *>(userptr);
      pp->progress = progress * 0.8f;  // Parsing is 80% of total work
      pp->bytes_processed = static_cast<uint64_t>(progress * pp->total_bytes);
      // Return false to cancel, true to continue
      return !pp->shouldCancel();
    };
    options.progress_userptr = &parsing_progress_;

    tinyusdz::Stage stage;
    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &stage, &warn_, &error_, options);

    if (!loaded_) {
      if (parsing_progress_.shouldCancel()) {
        parsing_progress_.setStage(ParsingProgress::Stage::Cancelled);
        parsing_progress_.error_message = "Parsing cancelled by user";
      } else {
        parsing_progress_.setStage(ParsingProgress::Stage::Error);
        parsing_progress_.error_message = error_;
      }
      return false;
    }

    loaded_as_layer_ = false;
    filename_ = filename;

    // Now convert to render scene
    parsing_progress_.setStage(ParsingProgress::Stage::Converting);
    parsing_progress_.current_operation = "Converting to render scene";
    parsing_progress_.progress = 0.8f;

    bool render_ok = stageToRenderScene(stage, is_usdz, binary);

    if (!render_ok) {
      parsing_progress_.setStage(ParsingProgress::Stage::Error);
      parsing_progress_.error_message = error_;
      return false;
    }

    parsing_progress_.progress = 1.0f;
    parsing_progress_.setStage(ParsingProgress::Stage::Complete);
    parsing_progress_.current_operation = "Complete";

    return true;
  }

  /// Load as layer with progress reporting
  bool loadAsLayerFromBinaryWithProgress(const std::string &binary, const std::string &filename) {
    // Reset progress state
    parsing_progress_.reset();
    parsing_progress_.setStage(ParsingProgress::Stage::Parsing);
    parsing_progress_.total_bytes = binary.size();
    parsing_progress_.current_operation = "Loading USD layer";

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;

    // Set up progress callback
    options.progress_callback = [](float progress, void *userptr) -> bool {
      ParsingProgress *pp = static_cast<ParsingProgress *>(userptr);
      pp->progress = progress;
      pp->bytes_processed = static_cast<uint64_t>(progress * pp->total_bytes);
      return !pp->shouldCancel();
    };
    options.progress_userptr = &parsing_progress_;

    loaded_ = tinyusdz::LoadLayerFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &layer_, &warn_, &error_, options);

    if (!loaded_) {
      if (parsing_progress_.shouldCancel()) {
        parsing_progress_.setStage(ParsingProgress::Stage::Cancelled);
        parsing_progress_.error_message = "Parsing cancelled by user";
      } else {
        parsing_progress_.setStage(ParsingProgress::Stage::Error);
        parsing_progress_.error_message = error_;
      }
      return false;
    }

    loaded_as_layer_ = true;
    filename_ = filename;

    parsing_progress_.progress = 1.0f;
    parsing_progress_.setStage(ParsingProgress::Stage::Complete);
    parsing_progress_.current_operation = "Complete";

    return true;
  }

  // TODO: Deprecate
  bool ok() const { return loaded_; }

  const std::string &error() const { return error_; }
  const std::string &warn() const { return warn_; }

 private:


  // Simple glTF-like Node
  emscripten::val buildNodeRec(const tinyusdz::tydra::Node &rnode) {
    emscripten::val node = emscripten::val::object();

    node.set("primName", rnode.prim_name);
    node.set("displayName", rnode.display_name);
    node.set("absPath", rnode.abs_path);

    std::string nodeCategoryStr = to_string(rnode.category);
    node.set("nodeCategory", nodeCategoryStr);

    std::string nodeTypeStr = to_string(rnode.nodeType);
    node.set("nodeType", nodeTypeStr);

    node.set("contentId",
             rnode.id);  // e.g. index to Mesh if nodeType == 'mesh'

    std::array<double, 16> localMatrix = detail::toArray(rnode.local_matrix);
    std::array<double, 16> globalMatrix = detail::toArray(rnode.global_matrix);

    node.set("localMatrix", localMatrix);
    node.set("globalMatrix", globalMatrix);
    node.set("hasResetXform", rnode.has_resetXform);

    emscripten::val children = emscripten::val::array();

    for (const tinyusdz::tydra::Node &child : rnode.children) {
      emscripten::val child_val = buildNodeRec(child);

      children.call<void>("push", child_val);
    }

    node.set("children", children);

    return node;
  }


  bool loaded_{false};
  bool loaded_as_layer_{false};
  bool enableComposition_{false};
  bool loadTextureInNative_{false}; // true: Let JavaScript to decode texture image.

  // Set appropriate default memory limits based on WASM architecture
#ifdef TINYUSDZ_WASM_MEMORY64
  int32_t max_memory_limit_mb_{8192}; // 8GB for MEMORY64
#else
  int32_t max_memory_limit_mb_{2048}; // 2GB for 32-bit WASM
#endif

  // Bone reduction configuration (disabled by default for backward compatibility)
  bool enable_bone_reduction_{false};
  uint32_t target_bone_count_{4};  // Default to 4 bones (standard for WebGL/Three.js)

  std::string filename_;
  std::string warn_;
  std::string error_;

  tinyusdz::Layer layer_;
  tinyusdz::Layer composed_layer_;
  bool composited_{false};
  std::vector<std::string> search_paths_;
  std::string base_dir_{"./"};

  tinyusdz::tydra::RenderScene render_scene_;
  tinyusdz::USDZAsset usdz_asset_;
  EMAssetResolutionResolver em_resolver_;

  // Cache for reordered mesh data (triangles sorted by material for optimal submesh grouping)
  struct ReorderedMeshCache {
    std::vector<float> points;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<float> tangents;
    std::vector<uint32_t> faceVertexIndices;
  };
  mutable std::unordered_map<int, ReorderedMeshCache> reordered_mesh_cache_;

  // key = session_id
  std::unordered_map<std::string, tinyusdz::tydra::mcp::Context> mcp_ctx_;
  std::string mcp_session_id_;

  tinyusdz::tydra::mcp::Context mcp_global_ctx_;

  // Progress tracking for polling-based progress reporting
  ParsingProgress parsing_progress_;
};

///
/// USD composition
///
class TinyUSDZComposerNative {
 public:
  // Default constructor for async loading
  TinyUSDZComposerNative() : loaded_(false) {}

  bool loaded() const { return loaded_; }
  const std::string &error() const { return error_; }

 private:
  bool loaded_{false};
  std::string warn_;
  std::string error_;

  tinyusdz::Layer root_layer_;
};

#if 0
// Helper to register std::array
namespace emscripten {
    namespace internal {
        template<typename T, size_t N>
        struct TypeID<std::array<T, N>> {
            static constexpr TYPEID get() {
                return TypeID<val>::get();
            }
        };
    }
}

// Convert std::array<float, 3> to/from JavaScript array
namespace emscripten {
    namespace internal {
        template<>
        struct BindingType<std::array<float, 3>> {
            typedef std::array<float, 3> WireType;
            static WireType toWireType(const std::array<float, 3>& arr) {
                return arr;
            }
            static std::array<float, 3> fromWireType(const WireType& arr) {
                return arr;
            }
        };
    }
}
#endif

// TODO: quaternion type.

// =============================================================================
// HDR/EXR Image Decoding Functions with FP16 Support
// =============================================================================

namespace {

// IEEE 754 half-precision float conversion utilities
// Based on public domain code from OpenEXR/TinyEXR

union FP32 {
  uint32_t u;
  float f;
  struct {
    unsigned int Mantissa : 23;
    unsigned int Exponent : 8;
    unsigned int Sign : 1;
  } s;
};

union FP16 {
  uint16_t u;
  struct {
    unsigned int Mantissa : 10;
    unsigned int Exponent : 5;
    unsigned int Sign : 1;
  } s;
};

/// Convert float32 to float16 (IEEE 754 half-precision)
inline uint16_t float32ToFloat16(float value) {
  FP32 f;
  f.f = value;
  FP16 o = {0};

  if (f.s.Exponent == 0) {
    // Signed zero/denormal (will underflow)
    o.s.Exponent = 0;
  } else if (f.s.Exponent == 255) {
    // Inf or NaN
    o.s.Exponent = 31;
    o.s.Mantissa = f.s.Mantissa ? 0x200 : 0;  // NaN->qNaN, Inf->Inf
  } else {
    // Normalized number
    int newexp = f.s.Exponent - 127 + 15;
    if (newexp >= 31) {
      // Overflow -> infinity
      o.s.Exponent = 31;
    } else if (newexp <= 0) {
      // Underflow
      if ((14 - newexp) <= 24) {
        unsigned int mant = f.s.Mantissa | 0x800000;  // Hidden 1 bit
        o.s.Mantissa = mant >> (14 - newexp);
        if ((mant >> (13 - newexp)) & 1)
          o.u++;  // Round
      }
    } else {
      o.s.Exponent = static_cast<unsigned int>(newexp);
      o.s.Mantissa = f.s.Mantissa >> 13;
      if (f.s.Mantissa & 0x1000)
        o.u++;  // Round
    }
  }
  o.s.Sign = f.s.Sign;
  return o.u;
}

/// Convert float16 to float32
inline float float16ToFloat32(uint16_t h) {
  static const FP32 magic = {113 << 23};
  static const unsigned int shifted_exp = 0x7c00 << 13;
  FP32 o;
  FP16 hp;
  hp.u = h;

  o.u = (hp.u & 0x7fffU) << 13U;
  unsigned int exp_ = shifted_exp & o.u;
  o.u += (127 - 15) << 23;

  if (exp_ == shifted_exp)
    o.u += (128 - 16) << 23;
  else if (exp_ == 0) {
    o.u += 1 << 23;
    o.f -= magic.f;
  }

  o.u |= (hp.u & 0x8000U) << 16U;
  return o.f;
}

/// Convert int32 to float16 (with normalization)
inline uint16_t int32ToFloat16(int32_t value, float scale = 1.0f / 2147483647.0f) {
  float normalized = static_cast<float>(value) * scale;
  return float32ToFloat16(normalized);
}

/// Convert uint32 to float16 (with normalization)
inline uint16_t uint32ToFloat16(uint32_t value, float scale = 1.0f / 4294967295.0f) {
  float normalized = static_cast<float>(value) * scale;
  return float32ToFloat16(normalized);
}

/// Convert float32 array to float16 array
void convertFloat32ToFloat16(const float* src, uint16_t* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    dst[i] = float32ToFloat16(src[i]);
  }
}

/// Copy buffer from JS Uint8Array
void copyFromJSBuffer(const emscripten::val& data, std::vector<uint8_t>& buffer) {
  size_t size = data["byteLength"].as<size_t>();
  buffer.resize(size);
  emscripten::val view = emscripten::val::global("Uint8Array").new_(
      data["buffer"], data["byteOffset"], size);
  emscripten::val heapView = emscripten::val(
      emscripten::typed_memory_view(size, buffer.data()));
  heapView.call<void>("set", view);
}

}  // namespace

#if defined(TINYUSDZ_WITH_EXR)
///
/// Decode EXR image with output format options
///
/// @param data Uint8Array containing EXR file data
/// @param outputFormat Output format: "float32", "float16", or "auto" (default)
///   - "float32": Always output as Float32Array (default, preserves precision)
///   - "float16": Convert to Uint16Array (IEEE 754 half-float, saves 50% memory)
///   - "auto": Use native format if fp16, otherwise float32
///
emscripten::val decodeEXR(const emscripten::val& data,
                          const std::string& outputFormat = "float32") {
  emscripten::val result = emscripten::val::object();

  std::vector<uint8_t> buffer;
  copyFromJSBuffer(data, buffer);

  if (IsEXRFromMemory(buffer.data(), buffer.size()) != TINYEXR_SUCCESS) {
    result.set("success", false);
    result.set("error", std::string("Not a valid EXR file"));
    return result;
  }

  float* rgba = nullptr;
  int width = 0;
  int height = 0;
  const char* err = nullptr;

  // LoadEXRFromMemory always returns float32 RGBA
  int ret = LoadEXRFromMemory(&rgba, &width, &height,
                               buffer.data(), buffer.size(), &err);

  if (ret != TINYEXR_SUCCESS) {
    result.set("success", false);
    if (err) {
      result.set("error", std::string(err));
      FreeEXRErrorMessage(err);
    } else {
      result.set("error", std::string("Failed to decode EXR"));
    }
    return result;
  }

  size_t pixelCount = size_t(width) * size_t(height) * 4;

  if (outputFormat == "float16") {
    // Convert to float16 and return as Uint16Array
    std::vector<uint16_t> fp16Data(pixelCount);
    convertFloat32ToFloat16(rgba, fp16Data.data(), pixelCount);
    free(rgba);

    emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
    emscripten::val pixelData = Uint16Array.new_(pixelCount);
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, fp16Data.data()));
    pixelData.call<void>("set", jsHeap);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("float16"));
    result.set("bitsPerChannel", 16);
  } else {
    // Return as Float32Array (default)
    emscripten::val Float32Array = emscripten::val::global("Float32Array");
    emscripten::val pixelData = Float32Array.new_(pixelCount);
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, rgba));
    pixelData.call<void>("set", jsHeap);
    free(rgba);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("float32"));
    result.set("bitsPerChannel", 32);
  }

  result.set("success", true);
  result.set("width", width);
  result.set("height", height);
  result.set("channels", 4);

  return result;
}

/// Check if data is a valid EXR file
bool isEXR(const emscripten::val& data) {
  size_t size = data["byteLength"].as<size_t>();
  if (size < 8) return false;

  std::vector<uint8_t> buffer;
  copyFromJSBuffer(data, buffer);
  return IsEXRFromMemory(buffer.data(), buffer.size()) == TINYEXR_SUCCESS;
}
#endif

///
/// Decode HDR (Radiance RGBE) image with output format options
/// Uses stb_image's stbi_loadf_from_memory for HDR decoding
///
/// @param data Uint8Array containing HDR file data
/// @param outputFormat Output format: "float16" (default) or "float32"
///   - "float16": Returns Uint16Array with IEEE 754 half-float (default, saves memory)
///   - "float32": Returns Float32Array (full precision)
///
emscripten::val decodeHDR(const emscripten::val& data,
                          const std::string& outputFormat = "float16") {
  emscripten::val result = emscripten::val::object();

  std::vector<uint8_t> buffer;
  copyFromJSBuffer(data, buffer);

  int width = 0, height = 0, channels = 0;

  // Use stbi_loadf_from_memory which returns float32 RGBA data
  // Request 4 channels (RGBA) for consistency
  float* floatData = stbi_loadf_from_memory(
      buffer.data(), static_cast<int>(buffer.size()),
      &width, &height, &channels, 4);

  if (!floatData) {
    result.set("success", false);
    result.set("error", std::string("Failed to decode HDR: ") + stbi_failure_reason());
    return result;
  }

  // Always output 4 channels (RGBA)
  const int outputChannels = 4;
  size_t pixelCount = size_t(width) * size_t(height) * size_t(outputChannels);

  if (outputFormat == "float32") {
    // Return as Float32Array
    emscripten::val Float32Array = emscripten::val::global("Float32Array");
    emscripten::val pixelData = Float32Array.new_(pixelCount);
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, floatData));
    pixelData.call<void>("set", jsHeap);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("float32"));
    result.set("bitsPerChannel", 32);
  } else {
    // Convert float32 to float16 and return as Uint16Array (default)
    std::vector<uint16_t> fp16Data(pixelCount);
    convertFloat32ToFloat16(floatData, fp16Data.data(), pixelCount);

    emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
    emscripten::val pixelData = Uint16Array.new_(pixelCount);
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, fp16Data.data()));
    pixelData.call<void>("set", jsHeap);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("float16"));
    result.set("bitsPerChannel", 16);
  }

  stbi_image_free(floatData);

  result.set("success", true);
  result.set("width", width);
  result.set("height", height);
  result.set("channels", outputChannels);

  return result;
}

///
/// Generic image decoder with output format options
///
/// @param data Uint8Array containing image file data
/// @param hint Filename hint for format detection (e.g., "image.exr")
/// @param outputFormat Output format: "auto", "float32", "float16", "uint16", "uint8"
///   - "auto": Use native format (default)
///   - "float32": Convert HDR/EXR to float32
///   - "float16": Convert HDR/EXR to float16 (Uint16Array with IEEE 754 half-float)
///   - "uint16": Keep 16-bit data as Uint16Array
///   - "uint8": Keep 8-bit data as Uint8Array
///
emscripten::val decodeImage(const emscripten::val& data,
                            const std::string& hint = "",
                            const std::string& outputFormat = "auto") {
  emscripten::val result = emscripten::val::object();

  std::vector<uint8_t> buffer;
  copyFromJSBuffer(data, buffer);

#if defined(TINYUSDZ_WITH_EXR)
  // Check for EXR first
  if (IsEXRFromMemory(buffer.data(), buffer.size()) == TINYEXR_SUCCESS) {
    std::string exrFormat = (outputFormat == "auto") ? "float32" : outputFormat;
    return decodeEXR(data, exrFormat);
  }
#endif

  // Use generic image loader for other formats (HDR, PNG, JPEG, etc.)
  auto loadResult = tinyusdz::image::LoadImageFromMemory(
      buffer.data(), buffer.size(), hint);

  if (!loadResult) {
    result.set("success", false);
    result.set("error", loadResult.error());
    return result;
  }

  const auto& img = loadResult.value().image;
  size_t pixelCount = size_t(img.width) * size_t(img.height) * size_t(img.channels);
  size_t dataSize = img.data.size();

  // Determine actual output format
  std::string actualFormat = outputFormat;
  if (actualFormat == "auto") {
    if (img.format == tinyusdz::Image::PixelFormat::Float) {
      actualFormat = "float32";
    } else if (img.bpp == 16) {
      actualFormat = "uint16";
    } else {
      actualFormat = "uint8";
    }
  }

  // Handle float data
  if (img.format == tinyusdz::Image::PixelFormat::Float) {
    const float* srcData = reinterpret_cast<const float*>(img.data.data());

    if (actualFormat == "float16") {
      // Downcast float32 to float16
      std::vector<uint16_t> fp16Data(pixelCount);
      convertFloat32ToFloat16(srcData, fp16Data.data(), pixelCount);

      emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
      emscripten::val pixelData = Uint16Array.new_(pixelCount);
      emscripten::val jsHeap = emscripten::val(
          emscripten::typed_memory_view(pixelCount, fp16Data.data()));
      pixelData.call<void>("set", jsHeap);

      result.set("data", pixelData);
      result.set("pixelFormat", std::string("float16"));
      result.set("bitsPerChannel", 16);
    } else {
      // Keep as float32
      emscripten::val Float32Array = emscripten::val::global("Float32Array");
      emscripten::val pixelData = Float32Array.new_(pixelCount);
      emscripten::val jsHeap = emscripten::val(
          emscripten::typed_memory_view(pixelCount, srcData));
      pixelData.call<void>("set", jsHeap);

      result.set("data", pixelData);
      result.set("pixelFormat", std::string("float32"));
      result.set("bitsPerChannel", 32);
    }
  }
  // Handle 16-bit integer data (e.g., 16-bit PNG)
  else if (img.bpp == 16) {
    const uint16_t* srcData = reinterpret_cast<const uint16_t*>(img.data.data());

    // Return as Uint16Array (native format)
    emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
    emscripten::val pixelData = Uint16Array.new_(pixelCount);
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, srcData));
    pixelData.call<void>("set", jsHeap);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("uint16"));
    result.set("bitsPerChannel", 16);
  }
  // Handle 8-bit data
  else {
    emscripten::val Uint8Array = emscripten::val::global("Uint8Array");
    emscripten::val pixelData = Uint8Array.new_(dataSize);
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(dataSize, img.data.data()));
    pixelData.call<void>("set", jsHeap);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("uint8"));
    result.set("bitsPerChannel", 8);
  }

  result.set("success", true);
  result.set("width", img.width);
  result.set("height", img.height);
  result.set("channels", img.channels);

  return result;
}

///
/// Convert Float32Array to Float16 Uint16Array
/// Utility function for post-processing or manual conversion
///
emscripten::val convertFloat32ToFloat16Array(const emscripten::val& float32Data) {
  size_t count = float32Data["length"].as<size_t>();

  // Copy float32 data from JS
  std::vector<float> srcData(count);
  emscripten::val srcView = emscripten::val(
      emscripten::typed_memory_view(count, srcData.data()));
  srcView.call<void>("set", float32Data);

  // Convert to float16
  std::vector<uint16_t> fp16Data(count);
  convertFloat32ToFloat16(srcData.data(), fp16Data.data(), count);

  // Return as Uint16Array
  emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
  emscripten::val result = Uint16Array.new_(count);
  emscripten::val jsHeap = emscripten::val(
      emscripten::typed_memory_view(count, fp16Data.data()));
  result.call<void>("set", jsHeap);

  return result;
}

///
/// Convert Float16 Uint16Array to Float32Array
/// Utility function for reading back float16 data
///
emscripten::val convertFloat16ToFloat32Array(const emscripten::val& uint16Data) {
  size_t count = uint16Data["length"].as<size_t>();

  // Copy uint16 data from JS
  std::vector<uint16_t> srcData(count);
  emscripten::val srcView = emscripten::val(
      emscripten::typed_memory_view(count, srcData.data()));
  srcView.call<void>("set", uint16Data);

  // Convert to float32
  std::vector<float> fp32Data(count);
  for (size_t i = 0; i < count; ++i) {
    fp32Data[i] = float16ToFloat32(srcData[i]);
  }

  // Return as Float32Array
  emscripten::val Float32Array = emscripten::val::global("Float32Array");
  emscripten::val result = Float32Array.new_(count);
  emscripten::val jsHeap = emscripten::val(
      emscripten::typed_memory_view(count, fp32Data.data()));
  result.call<void>("set", jsHeap);

  return result;
}

// Register STL
EMSCRIPTEN_BINDINGS(stl_wrappters) {
  register_vector<float>("VectorFloat");
  register_vector<int16_t>("VectorInt16");
  register_vector<uint16_t>("VectorUInt16");
  register_vector<uint32_t>("VectorUInt");
  register_vector<int>("VectorInt");
  register_vector<std::string>("VectorString");
}

// Register the array type
EMSCRIPTEN_BINDINGS(array_bindings) {
  value_array<std::array<int16_t, 2>>("Short2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<int16_t, 3>>("Short3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<int16_t, 4>>("Short4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<uint16_t, 2>>("UShort2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<uint16_t, 3>>("UShort3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<uint16_t, 4>>("UShort4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<int, 2>>("Int2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<int, 3>>("Int3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<int, 4>>("Int4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<uint32_t, 2>>("UInt2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<uint32_t, 3>>("UInt3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<uint32_t, 4>>("UInt4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  value_array<std::array<float, 2>>("Float2Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>());
  value_array<std::array<float, 3>>("Float3Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>());
  value_array<std::array<float, 4>>("Float4Array")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>());

  //  for mat33
  value_array<std::array<float, 9>>("Mat33")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>());

  value_array<std::array<double, 9>>("DMat33")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>());

  //  for mat44
  value_array<std::array<float, 16>>("Mat44")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>())
      .element(emscripten::index<9>())
      .element(emscripten::index<10>())
      .element(emscripten::index<11>())
      .element(emscripten::index<12>())
      .element(emscripten::index<13>())
      .element(emscripten::index<14>())
      .element(emscripten::index<15>());

  value_array<std::array<double, 16>>("DMat44")
      .element(emscripten::index<0>())
      .element(emscripten::index<1>())
      .element(emscripten::index<2>())
      .element(emscripten::index<3>())
      .element(emscripten::index<4>())
      .element(emscripten::index<5>())
      .element(emscripten::index<6>())
      .element(emscripten::index<7>())
      .element(emscripten::index<8>())
      .element(emscripten::index<9>())
      .element(emscripten::index<10>())
      .element(emscripten::index<11>())
      .element(emscripten::index<12>())
      .element(emscripten::index<13>())
      .element(emscripten::index<14>())
      .element(emscripten::index<15>());
}

EMSCRIPTEN_BINDINGS(tinyusdz_module) {
  class_<TinyUSDZLoaderNative>("TinyUSDZLoaderNative")
      .constructor<>()  // Default constructor for async loading
  //.constructor<const std::string &>()  // Keep original for compatibility
#if defined(TINYUSDZ_WASM_ASYNCIFY)
      .function("loadAsync", &TinyUSDZLoaderNative::loadAsync)
#endif
      .function("loadAsLayerFromBinary", &TinyUSDZLoaderNative::loadAsLayerFromBinary)
      .function("loadFromBinary", &TinyUSDZLoaderNative::loadFromBinary)
#if defined(TINYUSDZ_USE_COROUTINE)
      .function("loadFromBinaryAsync", &TinyUSDZLoaderNative::loadFromBinaryAsync)  // C++20 coroutine async version
#endif
      .function("loadTest", &TinyUSDZLoaderNative::loadTest)
      .function("loadFromCachedAsset", &TinyUSDZLoaderNative::loadFromCachedAsset)
      .function("loadAsLayerFromCachedAsset", &TinyUSDZLoaderNative::loadAsLayerFromCachedAsset)
      .function("testValueMemoryUsage", &TinyUSDZLoaderNative::testValueMemoryUsage)
      .function("testLayer", &TinyUSDZLoaderNative::testLayer)
      //.function("loadAndCompositeFromBinary", &TinyUSDZLoaderNative::loadFromBinary)
      
      // For Stage 
      .function("extractUnresolvedTexturePaths", &TinyUSDZLoaderNative::extractUnresolvedTexturePaths)
      .function("getURI", &TinyUSDZLoaderNative::getURI)
      .function("getMesh", &TinyUSDZLoaderNative::getMesh)
      .function("numMeshes", &TinyUSDZLoaderNative::numMeshes)
      .function("getMaterial", select_overload<emscripten::val(int) const>(&TinyUSDZLoaderNative::getMaterial))
      .function("getMaterialWithFormat", select_overload<emscripten::val(int, const std::string&) const>(&TinyUSDZLoaderNative::getMaterial))
      .function("numMaterials", &TinyUSDZLoaderNative::numMaterials)
      .function("getLight", &TinyUSDZLoaderNative::getLight)
      .function("getLightWithFormat", &TinyUSDZLoaderNative::getLightWithFormat)
      .function("getAllLights", &TinyUSDZLoaderNative::getAllLights)
      .function("numLights", &TinyUSDZLoaderNative::numLights)
      .function("getTexture", &TinyUSDZLoaderNative::getTexture)
      .function("numTextures", &TinyUSDZLoaderNative::numTextures)
      .function("getImage", &TinyUSDZLoaderNative::getImage)
      .function("numImages", &TinyUSDZLoaderNative::numImages)
      .function("getDefaultRootNodeId",
                &TinyUSDZLoaderNative::getDefaultRootNodeId)
      .function("getRootNode", &TinyUSDZLoaderNative::getRootNode)
      .function("getDefaultRootNode", &TinyUSDZLoaderNative::getDefaultRootNode)
      .function("numRootNodes", &TinyUSDZLoaderNative::numRootNodes)

      // Metadata access
      .function("getUpAxis", &TinyUSDZLoaderNative::getUpAxis)
      .function("getSceneMetadata", &TinyUSDZLoaderNative::getSceneMetadata)

      // Animation methods
      .function("numAnimations", &TinyUSDZLoaderNative::numAnimations)
      .function("getAnimation", &TinyUSDZLoaderNative::getAnimation)
      .function("getAllAnimations", &TinyUSDZLoaderNative::getAllAnimations)
      .function("getAnimationInfo", &TinyUSDZLoaderNative::getAnimationInfo)
      .function("getAllAnimationInfos", &TinyUSDZLoaderNative::getAllAnimationInfos)

      .function("setLoadTextureInNative",
                &TinyUSDZLoaderNative::setLoadTextureInNative)

      .function("setMaxMemoryLimitMB",
                &TinyUSDZLoaderNative::setMaxMemoryLimitMB)
      .function("getMaxMemoryLimitMB",
                &TinyUSDZLoaderNative::getMaxMemoryLimitMB)

      // Bone reduction configuration
      .function("setEnableBoneReduction",
                &TinyUSDZLoaderNative::setEnableBoneReduction)
      .function("getEnableBoneReduction",
                &TinyUSDZLoaderNative::getEnableBoneReduction)
      .function("setTargetBoneCount",
                &TinyUSDZLoaderNative::setTargetBoneCount)
      .function("getTargetBoneCount",
                &TinyUSDZLoaderNative::getTargetBoneCount)

      .function("setEnableComposition",
                &TinyUSDZLoaderNative::setEnableComposition)
      .function("extractSublayerAssetPaths",
                &TinyUSDZLoaderNative::extractSublayerAssetPaths)
      .function("extractReferencesAssetPaths",
                &TinyUSDZLoaderNative::extractReferencesAssetPaths)
      .function("extractPayloadAssetPaths",
                &TinyUSDZLoaderNative::extractPayloadAssetPaths)

      .function("composeSublayers",
                &TinyUSDZLoaderNative::composeSublayers)

      .function("hasReferences",
                &TinyUSDZLoaderNative::hasReferences)

      .function("composeReferences",
                &TinyUSDZLoaderNative::composeReferences)

      .function("hasPayload",
                &TinyUSDZLoaderNative::hasPayload)

      .function("composePayload",
                &TinyUSDZLoaderNative::composePayload)

      .function("hasInherits",
                &TinyUSDZLoaderNative::hasInherits)

      .function("composeInherits",
                &TinyUSDZLoaderNative::composeInherits)

      // TODO: nested variants
      .function("hasVariants",
                &TinyUSDZLoaderNative::hasInherits)

      .function("composeVariants",
                &TinyUSDZLoaderNative::composeVariants)

      .function("layerToRenderScene",
                &TinyUSDZLoaderNative::layerToRenderScene)
    
    
      .function("setAsset",
                &TinyUSDZLoaderNative::setAsset)
      .function("startStreamingAsset",
                &TinyUSDZLoaderNative::startStreamingAsset)
      .function("appendAssetChunk",
                &TinyUSDZLoaderNative::appendAssetChunk)
      .function("finalizeStreamingAsset",
                &TinyUSDZLoaderNative::finalizeStreamingAsset)
      .function("isStreamingAssetComplete",
                &TinyUSDZLoaderNative::isStreamingAssetComplete)
      .function("getStreamingProgress",
                &TinyUSDZLoaderNative::getStreamingProgress)

      // Zero-copy streaming buffer methods
      .function("allocateZeroCopyBuffer",
                &TinyUSDZLoaderNative::allocateZeroCopyBuffer)
      .function("getZeroCopyBufferPtr",
                &TinyUSDZLoaderNative::getZeroCopyBufferPtr)
      .function("getZeroCopyBufferPtrAtOffset",
                &TinyUSDZLoaderNative::getZeroCopyBufferPtrAtOffset)
      .function("markZeroCopyBytesWritten",
                &TinyUSDZLoaderNative::markZeroCopyBytesWritten)
      .function("getZeroCopyProgress",
                &TinyUSDZLoaderNative::getZeroCopyProgress)
      .function("finalizeZeroCopyBuffer",
                &TinyUSDZLoaderNative::finalizeZeroCopyBuffer)
      .function("cancelZeroCopyBuffer",
                &TinyUSDZLoaderNative::cancelZeroCopyBuffer)
      .function("getActiveZeroCopyBuffers",
                &TinyUSDZLoaderNative::getActiveZeroCopyBuffers)

      .function("hasAsset",
                &TinyUSDZLoaderNative::hasAsset)
      .function("getAsset",
                &TinyUSDZLoaderNative::getAsset)
      .function("getAssetCacheDataAsMemoryView",
                &TinyUSDZLoaderNative::getAssetCacheDataAsMemoryView)
      .function("setAssetFromRawPointer",
                &TinyUSDZLoaderNative::setAssetFromRawPointer, emscripten::allow_raw_pointers())
      .function("getAssetHash",
                &TinyUSDZLoaderNative::getAssetHash)
      .function("verifyAssetHash",
                &TinyUSDZLoaderNative::verifyAssetHash)
      .function("getAssetUUID",
                &TinyUSDZLoaderNative::getAssetUUID)
      .function("getStreamingAssetUUID",
                &TinyUSDZLoaderNative::getStreamingAssetUUID)
      .function("getAllAssetUUIDs",
                &TinyUSDZLoaderNative::getAllAssetUUIDs)
      .function("findAssetByUUID",
                &TinyUSDZLoaderNative::findAssetByUUID)
      .function("getAssetByUUID",
                &TinyUSDZLoaderNative::getAssetByUUID)
      .function("deleteAsset",
                &TinyUSDZLoaderNative::deleteAsset)
      .function("deleteAssetByUUID",
                &TinyUSDZLoaderNative::deleteAssetByUUID)
      .function("deleteAssetByName",
                &TinyUSDZLoaderNative::deleteAssetByName)
      .function("getAssetCount",
                &TinyUSDZLoaderNative::getAssetCount)
      .function("assetExists",
                &TinyUSDZLoaderNative::assetExists)
      .function("clearAssets",
                &TinyUSDZLoaderNative::clearAssets)
      .function("reset",
                &TinyUSDZLoaderNative::reset)
      .function("getMemoryStats",
                &TinyUSDZLoaderNative::getMemoryStats)

      .function("layerToString",
                &TinyUSDZLoaderNative::layerToString)
      
      // JSON conversion methods
      .function("layerToJSON",
                &TinyUSDZLoaderNative::layerToJSON)
      .function("layerToJSONWithOptions",
                &TinyUSDZLoaderNative::layerToJSONWithOptions)
      .function("loadLayerFromJSON",
                &TinyUSDZLoaderNative::loadLayerFromJSON)

      .function("setBaseWorkingPath", &TinyUSDZLoaderNative::setBaseWorkingPath)
      .function("getBaseWorkingPath", &TinyUSDZLoaderNative::getBaseWorkingPath)
      .function("clearAssetSearchPaths", &TinyUSDZLoaderNative::clearAssetSearchPaths)
      .function("addAssetSearchPath", &TinyUSDZLoaderNative::addAssetSearchPath)
      .function("getAssetSearchPaths", &TinyUSDZLoaderNative::getAssetSearchPaths)


      // MCP
      .function("mcpCreateContext", &TinyUSDZLoaderNative::mcpCreateContext)
      .function("mcpSelectContext", &TinyUSDZLoaderNative::mcpSelectContext)
      .function("mcpResourcesList", &TinyUSDZLoaderNative::mcpResourcesList)
      .function("mcpResourcesRead", &TinyUSDZLoaderNative::mcpResourcesRead)
      .function("mcpToolsList", &TinyUSDZLoaderNative::mcpToolsList)
      .function("mcpToolsCall", &TinyUSDZLoaderNative::mcpToolsCall)

      // Progress reporting for async parsing
      .function("getProgress", &TinyUSDZLoaderNative::getProgress)
      .function("cancelParsing", &TinyUSDZLoaderNative::cancelParsing)
      .function("wasCancelled", &TinyUSDZLoaderNative::wasCancelled)
      .function("isParsingInProgress", &TinyUSDZLoaderNative::isParsingInProgress)
      .function("resetProgress", &TinyUSDZLoaderNative::resetProgress)
      .function("loadFromBinaryWithProgress", &TinyUSDZLoaderNative::loadFromBinaryWithProgress)
      .function("loadAsLayerFromBinaryWithProgress", &TinyUSDZLoaderNative::loadAsLayerFromBinaryWithProgress)

      .function("ok", &TinyUSDZLoaderNative::ok)
      .function("error", &TinyUSDZLoaderNative::error)
      .function("warn", &TinyUSDZLoaderNative::warn);

  class_<TinyUSDZComposerNative>("TinyUSDZComposerNative")
      .constructor<>()  // Default constructor for async loading
      .function("ok", &TinyUSDZComposerNative::loaded)
      .function("error", &TinyUSDZComposerNative::error);
}

// =============================================================================
// Image Decoding Bindings (EXR, HDR, PNG, JPEG, etc.)
// =============================================================================

// Wrapper functions for default parameters
#if defined(TINYUSDZ_WITH_EXR)
static emscripten::val decodeEXR_default(const emscripten::val& data) {
  return decodeEXR(data, "float32");
}
#endif

static emscripten::val decodeHDR_default(const emscripten::val& data) {
  return decodeHDR(data, "float16");
}

static emscripten::val decodeImage_default(const emscripten::val& data) {
  return decodeImage(data, "", "auto");
}

static emscripten::val decodeImage_hint(const emscripten::val& data, const std::string& hint) {
  return decodeImage(data, hint, "auto");
}

EMSCRIPTEN_BINDINGS(image_module) {
#if defined(TINYUSDZ_WITH_EXR)
  // EXR decoding
  // decodeEXR(data) - returns float32 by default
  // decodeEXR(data, "float16") - returns Uint16Array with IEEE 754 half-float
  function("decodeEXR", &decodeEXR);
  function("decodeEXRDefault", &decodeEXR_default);
  function("isEXR", &isEXR);
#endif

  // HDR (Radiance RGBE) decoding
  // decodeHDR(data) - returns float32 by default
  // decodeHDR(data, "float16") - returns Uint16Array with IEEE 754 half-float
  function("decodeHDR", &decodeHDR);
  function("decodeHDRDefault", &decodeHDR_default);

  // Generic image decoder (auto-detects EXR, HDR, PNG, JPEG, etc.)
  // decodeImage(data) - auto format
  // decodeImage(data, hint) - with filename hint
  // decodeImage(data, hint, "float16") - with format specification
  function("decodeImage", &decodeImage);
  function("decodeImageDefault", &decodeImage_default);
  function("decodeImageHint", &decodeImage_hint);

  // Float16 <-> Float32 conversion utilities
  function("convertFloat32ToFloat16Array", &convertFloat32ToFloat16Array);
  function("convertFloat16ToFloat32Array", &convertFloat16ToFloat32Array);
}

