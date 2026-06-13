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
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

//#include "external/fast_float/include/fast_float/bigint.h"
#include "tinyusdz.hh"
#include "pprinter.hh"
#include "tsd/tinysubdiv.hh"
#include "typed-array-core.hh"
#include "value-types.hh"

// next: low-memory lazy-ValueRep flatten pipeline (src/next/).
#include "next/pipeline/flatten.hh"
#include "next/reader/usdc-reader.hh"
#include "next/stage/stage.hh"
#include "next/types/value.hh"
#include "next/schema/geom-mesh.hh"
#include "next/schema/usd-shade.hh"
#include "tydra/render-data.hh"
#include "tydra/tangent-quantize.hh"
#include "tydra/scene-access.hh"
#include "tydra/material-serializer.hh"
#include "tydra/diff-and-compare.hh"

// js-script.hh must precede mcp-context.hh: tydra::mcp::Context holds a
// std::unique_ptr<JSEngineState> and relies on its implicit destructor, which
// requires the complete JSEngineState type (forward-declared in mcp-context.hh,
// defined in js-script.hh).
#include "tydra/js-script.hh"
#include "tydra/mcp-context.hh"
// mcp-context.hh's Context holds a unique_ptr<JSEngineState> (forward-declared
// there); js-script.hh provides the complete type so Context's destructor can
// be instantiated here (matches mcp-server.cc / mcp-js-bridge.cc).
#include "tydra/js-script.hh"
#include "tydra/mcp-resources.hh"
#include "tydra/mcp-tools.hh"
#include "tydra/urdf-to-usd.hh"
#include "minijson.hh"
#include "usd-to-json.hh"
#include "json-to-usd.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"
#include "crate-writer.hh"
#include "image-writer.hh"
#include "imageio/png-stream.hh"  // streaming scanline PNG codec
#include "imageproc/simd.hh"      // SIMD row kernels (channel pack)
#include "usdGeom.hh"
#include "usd-validation.hh"
#include "usdPhysics.hh"
#include "mjcPhysics.hh"
#include "usdShade.hh"
#include "pprint-enum.hh"
#include "stage.hh"
#include "sha256.hh"
#include "logger.hh"
#include "image-loader.hh"
#include "image-types.hh"
#include "safe-arithmetic.hh"
#include "tydra/texture-util.hh"
#include "usdz-convert.hh"

// EXR detection here is backend-agnostic (a magic-number test). Decoding goes
// through tinyusdz::image::LoadImageFromMemory, which selects the active EXR
// backend (pure-C11 v3 C by default), so binding.cc no longer depends on a
// specific tinyexr API.
static inline bool IsEXRMagic(const uint8_t *p, size_t n) {
  // EXR magic 20000630 == 0x01312F76, stored little-endian.
  return n >= 4 && p[0] == 0x76 && p[1] == 0x2f && p[2] == 0x31 && p[3] == 0x01;
}

// stb_image for HDR (Radiance RGBE) decoding
// Only compile HDR support to minimize code size
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#include "external/stb_image.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

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

namespace {

tinyusdz::ValidationOptions ParseValidationOptionsJSONForWeb(
    const std::string &options_json) {
  tinyusdz::ValidationOptions opts;
  if (options_json.empty()) {
    return opts;
  }

  nlohmann::json args = nlohmann::json::parse(options_json, nullptr, false);
  if (args.is_discarded() || !args.is_object() || !args.contains("groups") ||
      !args["groups"].is_array()) {
    return opts;
  }

  opts.core = false;
  opts.geom = false;
  opts.shade = false;
  opts.lux = false;
  opts.physics = false;
  opts.crate = false;
  for (const auto &group : args["groups"]) {
    if (!group.is_string()) {
      continue;
    }
    const std::string name = group.get<std::string>();
    if (name == "core") {
      opts.core = true;
    } else if (name == "geom") {
      opts.geom = true;
    } else if (name == "shade") {
      opts.shade = true;
    } else if (name == "lux") {
      opts.lux = true;
    } else if (name == "physics") {
      opts.physics = true;
    } else if (name == "crate") {
      opts.crate = true;
    } else if (name == "all") {
      opts = tinyusdz::MakeValidateAllOptions();
    }
  }

  if (!opts.core && !opts.geom && !opts.shade && !opts.lux &&
      !opts.physics && !opts.crate) {
    opts.core = true;
  }
  return opts;
}

const char *ValidationSeverityString(tinyusdz::USDValidationSeverity severity) {
  return severity == tinyusdz::USDValidationSeverity::Error ? "error"
                                                            : "warning";
}

nlohmann::json ValidationGroupsToJSON(
    const tinyusdz::ValidationOptions &options) {
  nlohmann::json groups = nlohmann::json::array();
  for (const std::string &name : tinyusdz::GetValidationGroupNames(options)) {
    groups.push_back(name);
  }
  return groups;
}

nlohmann::json ValidationResultToJSON(
    const tinyusdz::USDValidationResult &validation) {
  nlohmann::json result;
  result["parse_ok"] = true;
  result["ok"] = validation.ok();
  result["error_count"] = validation.error_count();
  result["warning_count"] = validation.warning_count();
  result["spec_version"] = tinyusdz::GetAOUSDCoreSpecVersionString();
  result["checked_groups"] =
      ValidationGroupsToJSON(validation.checked_groups);

  nlohmann::json issues = nlohmann::json::array();
  for (const tinyusdz::USDValidationIssue *issue :
       tinyusdz::GetOrderedValidationIssues(validation)) {
    nlohmann::json item;
    item["severity"] = ValidationSeverityString(issue->severity);
    item["rule_id"] = issue->rule_id;
    item["location"] = issue->location;
    item["message"] = issue->message;
    issues.push_back(item);
  }
  result["issues"] = issues;
  return result;
}

}  // namespace

// Fix degenerate tangent: when a tangent vector is zero, near-zero, NaN, or
// Inf, generate a fallback perpendicular to the normal.  Also handles
// degenerate/NaN normals.  Modifies tx/ty/tz in place.
static inline void FixupZeroTangent(float &tx, float &ty, float &tz,
                                     float nx, float ny, float nz) {
  // Check if tangent is valid (finite and non-trivially long)
  if (std::isfinite(tx) && std::isfinite(ty) && std::isfinite(tz)) {
    float len2 = tx * tx + ty * ty + tz * tz;
    if (std::isfinite(len2) && len2 > 1.0e-12f) {
      return;  // tangent is fine
    }
  }

  // Ensure normal is usable (finite and non-zero)
  if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) {
    nx = 0.0f; ny = 1.0f; nz = 0.0f;  // arbitrary up
  } else {
    float nlen2 = nx * nx + ny * ny + nz * nz;
    if (!std::isfinite(nlen2) || nlen2 < 1.0e-12f) {
      nx = 0.0f; ny = 1.0f; nz = 0.0f;
    } else {
      float inv = 1.0f / std::sqrt(nlen2);
      nx *= inv; ny *= inv; nz *= inv;
    }
  }

  // Generate perpendicular to normal via cross with a reference axis
  float rx, ry, rz;
  if (std::fabs(ny) < 0.9f) {
    rx = 0.0f; ry = 1.0f; rz = 0.0f;
  } else {
    rx = 1.0f; ry = 0.0f; rz = 0.0f;
  }
  // cross(N, ref)
  tx = ny * rz - nz * ry;
  ty = nz * rx - nx * rz;
  tz = nx * ry - ny * rx;
  float len2 = tx * tx + ty * ty + tz * tz;
  if (std::isfinite(len2) && len2 > 1.0e-20f) {
    float inv = 1.0f / std::sqrt(len2);
    tx *= inv; ty *= inv; tz *= inv;
  } else {
    // Last resort: normal was along both reference axes (shouldn't happen)
    tx = 1.0f; ty = 0.0f; tz = 0.0f;
  }
}

// ============================================================================
// EM_JS: Synchronous JavaScript callbacks for progress reporting
// These functions are called from C++ during Tydra conversion to report
// progress to JavaScript in real-time without ASYNCIFY.
// ============================================================================

// Report mesh conversion progress
// Called for each mesh during Tydra conversion
// NOTE: const char* params are BigInt in MEMORY64 mode, but UTF8ToString
// expects Number. Using Number() is a no-op for regular numbers (32-bit)
// and converts BigInt→Number (64-bit), so it works for both modes.
EM_JS(void, reportTydraProgress, (int current, int total, const char* stage, const char* meshName, int materialsCurrent, int materialsTotal, const char* materialName, float progress), {
  if (typeof Module.onTydraProgress === 'function') {
    Module.onTydraProgress({
      meshCurrent: current,
      meshTotal: total,
      stage: UTF8ToString(Number(stage)),
      meshName: UTF8ToString(Number(meshName)),
      materialsCurrent,
      materialsTotal,
      materialName: UTF8ToString(Number(materialName)),
      progress: progress
    });
  }
});

EM_JS(double, getWasmHeapByteLengthForDebug, (), {
  return HEAPU8.buffer.byteLength;
});

EM_JS(void, reportTinyUSDZDebug, (const char* phase, const char* detail, double heapBytes, double inputBytes, int isUsdz, int materialsCurrent, int materialsTotal, const char* materialName), {
  const event = {
    phase: UTF8ToString(Number(phase)),
    detail: UTF8ToString(Number(detail)),
    heapBytes,
    inputBytes,
    isUsdz: !!isUsdz,
    materialsCurrent,
    materialsTotal,
    materialName: UTF8ToString(Number(materialName))
  };
  if (typeof Module.onTinyUSDZDebug === 'function') {
    Module.onTinyUSDZDebug(event);
  }
});

static inline double GetWasmHeapByteLengthForDebug() {
  return getWasmHeapByteLengthForDebug();
}

// Cheap test for whether a JS debug listener is attached. Lets hot paths skip
// building debug strings / querying the heap size when nobody is listening.
EM_JS(int, isTinyUSDZDebugEnabled, (), {
  return (typeof Module.onTinyUSDZDebug === 'function') ? 1 : 0;
});

static inline bool IsTinyUSDZDebugEnabled() {
  return isTinyUSDZDebugEnabled() != 0;
}

static inline void ReportTinyUSDZDebugEvent(
    const char *phase, const std::string &detail, size_t input_bytes = 0,
    bool is_usdz = false, size_t materials_current = 0,
    size_t materials_total = 0, const std::string &material_name = "") {
  // No JS listener => skip the heap-size query and the JS event construction
  // entirely. This keeps debug instrumentation off the cost path in the common
  // (no-listener) case.
  if (!IsTinyUSDZDebugEnabled()) {
    return;
  }
  reportTinyUSDZDebug(
      phase, detail.c_str(), GetWasmHeapByteLengthForDebug(),
      static_cast<double>(input_bytes), is_usdz ? 1 : 0,
      static_cast<int>(materials_current), static_cast<int>(materials_total),
      material_name.c_str());
}

// Report conversion stage change
EM_JS(void, reportTydraStage, (const char* stage, const char* message), {
  if (typeof Module.onTydraStage === 'function') {
    Module.onTydraStage({
      stage: UTF8ToString(Number(stage)),
      message: UTF8ToString(Number(message))
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

// NOTE: EM_VAL is a pointer type (struct _EM_VAL*). In MEMORY64 mode,
// pointers are i64 and must be returned as BigInt from JS→WASM imports.
// Emval.toHandle() returns a Number, so we wrap with BigInt() for MEMORY64.
#if defined(TINYUSDZ_WASM_MEMORY64)
EM_JS(emscripten::EM_VAL, yieldToEventLoop_impl, (), {
  return BigInt(Emval.toHandle(new Promise(resolve => {
    if (typeof requestAnimationFrame === 'function') {
      requestAnimationFrame(() => resolve());
    } else {
      setTimeout(resolve, 0);
    }
  })));
});
#else
EM_JS(emscripten::EM_VAL, yieldToEventLoop_impl, (), {
  return Emval.toHandle(new Promise(resolve => {
    if (typeof requestAnimationFrame === 'function') {
      requestAnimationFrame(() => resolve());
    } else {
      setTimeout(resolve, 0);
    }
  }));
});
#endif

// Wrapper for co_await usage
inline emscripten::val yieldToEventLoop() {
  return emscripten::val::take_ownership(yieldToEventLoop_impl());
}

// Helper to yield with a custom delay (milliseconds)
#if defined(TINYUSDZ_WASM_MEMORY64)
EM_JS(emscripten::EM_VAL, yieldWithDelay_impl, (int delayMs), {
  return BigInt(Emval.toHandle(new Promise(resolve => {
    setTimeout(resolve, delayMs);
  })));
});
#else
EM_JS(emscripten::EM_VAL, yieldWithDelay_impl, (int delayMs), {
  return Emval.toHandle(new Promise(resolve => {
    setTimeout(resolve, delayMs);
  }));
});
#endif

inline emscripten::val yieldWithDelay(int delayMs) {
  return emscripten::val::take_ownership(yieldWithDelay_impl(delayMs));
}

// Report that async operation is starting (for JS progress UI)
EM_JS(void, reportAsyncPhaseStart, (const char* phase, float progress), {
  if (typeof Module.onAsyncPhaseStart === 'function') {
    Module.onAsyncPhaseStart({
      phase: UTF8ToString(Number(phase)),
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
  if (channels <= 0 || channels > 4) return false;
  size_t npixels = src.size() / static_cast<size_t>(channels);
  if (npixels > SIZE_MAX / 4) return false;
  dst.resize(npixels * 4);

  if (channels == 1) {  // grayscale
    for (size_t i = 0; i < npixels; i++) {
      dst[4 * i + 0] = src[i];
      dst[4 * i + 1] = src[i];
      dst[4 * i + 2] = src[i];
      dst[4 * i + 3] = 255;
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
      dst[4 * i + 3] = 255;
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
  // Cap allocation to avoid OOM from untrusted JS typed arrays.
  constexpr size_t kMaxUint8ArrayBytes = size_t(1) << 30;  // 1 GiB
  if (n == 0 || n > kMaxUint8ArrayBytes) {
    return false;
  }
  try {
    buf.resize(n);
  } catch (const std::bad_alloc&) {
    return false;
  }

  // Copy JS typed array -> v (one memcpy under the hood). Length must be a JS
  // Number (double): a C++ size_t marshals to a BigInt under wasm64 and
  // `new Uint8Array(buffer, byteOffset, bigint)` throws.
  emscripten::val view = emscripten::val::global("Uint8Array").new_(
      u8["buffer"], u8["byteOffset"], emscripten::val(static_cast<double>(n)));
  emscripten::val heapView = emscripten::val(emscripten::typed_memory_view(n, buf.data()));
  heapView.call<void>("set", view);

  return true;
}

template <typename T>
void copyTypedArray(const emscripten::val &data, std::vector<T> &buffer,
                    const char *array_ctor) {
  if (data.isUndefined() || data.isNull()) {
    buffer.clear();
    return;
  }
  const size_t length = data["length"].as<size_t>();
  const size_t byteOffset = data["byteOffset"].as<size_t>();
  const size_t byteLength = data["buffer"]["byteLength"].as<size_t>();
  constexpr size_t kMaxArraySize = size_t(1) << 28;  // 256M elements
  if (length > kMaxArraySize) {
    buffer.clear();
    return;
  }
  // Validate that the requested range fits within the backing buffer.
  // Each element is sizeof(T) bytes; compute total bytes needed.
  size_t needed_bytes;
  if (tinyusdz::safe::mul(
          size_t(length), size_t(sizeof(T)), &needed_bytes)) {
    if (byteOffset > byteLength ||
        needed_bytes > byteLength - byteOffset) {
      buffer.clear();
      return;
    }
  } else {
    buffer.clear();
    return;
  }
  buffer.resize(length);
  if (length == 0) {
    return;
  }
  // byteOffset/length as JS Numbers (double): size_t -> BigInt under wasm64
  // breaks the typed-array constructor.
  emscripten::val view = emscripten::val::global(array_ctor).new_(
      data["buffer"], emscripten::val(static_cast<double>(byteOffset)),
      emscripten::val(static_cast<double>(length)));
  emscripten::val heapView =
      emscripten::val(emscripten::typed_memory_view(length, buffer.data()));
  heapView.call<void>("set", view);
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

class JSUint8ArrayOutputStream
    : public tinyusdz::experimental::IOutputStream {
 public:
  JSUint8ArrayOutputStream(const emscripten::val &buffer, size_t capacity)
      : buffer_(buffer), capacity_(capacity) {}

  bool Open(std::string *err) override {
    if (buffer_.isNull() || buffer_.isUndefined() || capacity_ == 0) {
      if (err) {
        *err = "JS output buffer is empty.";
      }
      return false;
    }
    pos_ = 0;
    max_pos_ = 0;
    open_ = true;
    error_.clear();
    return true;
  }

  void Close() override { open_ = false; }

  bool IsOpen() const override { return open_; }

  int64_t Tell() override { return static_cast<int64_t>(pos_); }

  bool Seek(int64_t pos) override {
    if (pos < 0) {
      error_ = "Negative seek in JS output buffer.";
      return false;
    }
    const size_t next = static_cast<size_t>(pos);
    if (next > capacity_) {
      error_ = "Seek exceeds JS output buffer capacity.";
      return false;
    }
    pos_ = next;
    return true;
  }

  bool Write(const void *data, size_t size) override {
    if (!open_) {
      error_ = "JS output buffer is not open.";
      return false;
    }
    if (size == 0) {
      return true;
    }
    if (!data) {
      error_ = "Null write data for JS output buffer.";
      return false;
    }
    if (pos_ > capacity_ || size > capacity_ - pos_) {
      error_ = "JS output buffer too small for USDC export.";
      return false;
    }

    emscripten::val src = emscripten::val(emscripten::typed_memory_view(
        size, reinterpret_cast<const uint8_t *>(data)));
    buffer_.call<void>("set", src, emscripten::val(static_cast<double>(pos_)));
    pos_ += size;
    if (pos_ > max_pos_) {
      max_pos_ = pos_;
    }
    return true;
  }

  bool Flush() override { return true; }

  size_t written() const { return max_pos_; }
  const std::string &error() const { return error_; }

 private:
  emscripten::val buffer_;
  size_t capacity_{0};
  size_t pos_{0};
  size_t max_pos_{0};
  bool open_{false};
  std::string error_;
};

struct EMAssetResolutionResolver {

  // Lexically collapse '.' and '<seg>/..' in a relative path, preserving any
  // leading '..' (those are resolved against a base directory elsewhere). Pure
  // string arithmetic — no filesystem access (FILESYSTEM=0 in this build).
  static std::string LexicalNormalizePath(const std::string &path) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= path.size()) {
      size_t j = path.find('/', i);
      std::string seg =
          path.substr(i, j == std::string::npos ? std::string::npos : j - i);
      if (!seg.empty() && seg != ".") {
        if (seg == "..") {
          if (!parts.empty() && parts.back() != "..") {
            parts.pop_back();
          } else {
            parts.push_back("..");
          }
        } else {
          parts.push_back(std::move(seg));
        }
      }
      if (j == std::string::npos) {
        break;
      }
      i = j + 1;
    }
    std::string out;
    for (size_t k = 0; k < parts.size(); k++) {
      if (k) {
        out.push_back('/');
      }
      out += parts[k];
    }
    return out;
  }

  static int Resolve(const char *asset_name,
                     const std::vector<std::string> &search_paths,
                     std::string *resolved_asset_name, std::string *err,
                     void *userdata) {
    (void)err;

    if (!asset_name) {
      return -2;  // err
    }

    if (!resolved_asset_name) {
      return -2;  // err
    }

    EMAssetResolutionResolver *p =
        reinterpret_cast<EMAssetResolutionResolver *>(userdata);

    // Without a cache to consult, echo the name back (legacy behavior).
    if (!p) {
      (*resolved_asset_name) = asset_name;
      return 0;
    }

    // 1) Direct hit: the name is already a cache key.
    if (p->has(asset_name)) {
      (*resolved_asset_name) = asset_name;
      return 0;
    }

    // 2) Honor search paths. A nested reference path is authored relative to the
    //    referencing layer's directory; composition pushes that directory into
    //    `search_paths`. Try "<search_path>/<asset_name>" against the cache.
    const std::string name(asset_name);
    for (const std::string &sp : search_paths) {
      if (sp.empty() || sp == "." || sp == "./") {
        continue;
      }
      std::string base = sp;
      while (!base.empty() && base.back() == '/') {
        base.pop_back();
      }
      const std::string cand = base + "/" + name;
      if (p->has(cand)) {
        (*resolved_asset_name) = cand;
        return 0;
      }
    }

    // 3) Fallback: match by trailing path segment, so a relative ref resolves to
    //    a uniquely-named cached asset regardless of its subdirectory.
    const std::string suffix = "/" + name;
    for (const auto &kv : p->cache) {
      const std::string &key = kv.first;
      if (key.size() >= suffix.size() &&
          key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
        (*resolved_asset_name) = key;
        return 0;
      }
    }

    // 4) Resolve '..'/'.' nicely: collapse the request (and each
    //    "<search_path>/<name>") lexically, then retry the cache. Lets a
    //    parent-relative ref such as `../common/foo.usd` match a cached key that
    //    differs only by collapsible segments, regardless of how composition
    //    pushed the working directory.
    {
      const std::string norm_name = LexicalNormalizePath(name);
      if (norm_name != name && p->has(norm_name)) {
        (*resolved_asset_name) = norm_name;
        return 0;
      }
      for (const std::string &sp : search_paths) {
        if (sp.empty() || sp == "." || sp == "./") {
          continue;
        }
        std::string base = sp;
        while (!base.empty() && base.back() == '/') {
          base.pop_back();
        }
        const std::string cand = LexicalNormalizePath(base + "/" + name);
        if (p->has(cand)) {
          (*resolved_asset_name) = cand;
          return 0;
        }
      }
      if (!norm_name.empty() && norm_name != name) {
        const std::string nsuffix = "/" + norm_name;
        for (const auto &kv : p->cache) {
          const std::string &key = kv.first;
          if (key.size() >= nsuffix.size() &&
              key.compare(key.size() - nsuffix.size(), nsuffix.size(),
                          nsuffix) == 0) {
            (*resolved_asset_name) = key;
            return 0;
          }
        }
      }
    }

    // Not found in cache: echo the name so Size/Read report a clean miss.
    (*resolved_asset_name) = asset_name;
    return 0;
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
    if (!p || !p->has(asset_name)) {
      if (err) {
        (*err) += "Asset not found in cache: " + std::string(asset_name) + "\n";
      }
      return -1;  // not found
    }
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

    // Enforce cache size limit before adding
    if (max_cache_size_bytes_ > 0 && !overwritten) {
      size_t new_size = getCacheSizeBytes() + asset_name.size() + binary.size();
      if (new_size > max_cache_size_bytes_) {
        evictToFitBytes(max_cache_size_bytes_ - std::min(max_cache_size_bytes_,
                        asset_name.size() + binary.size()));
      }
    }

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

  // Explicit zero-copy accessor: returns a typed_memory_view directly into the
  // cached bytes. WARNING: the returned Uint8Array aliases WASM heap memory
  // owned by this cache and becomes a dangling reference once the asset is
  // evicted or deleted. Callers must consume it before any such mutation.
  // Prefer the copying getAsset()/getAssetByUUID() unless you manage lifetime.
  emscripten::val getCacheDataAsMemoryView(const std::string &asset_name) const {
    if (!cache.count(asset_name)) {
      return emscripten::val::undefined();
    }
    const AssetCacheEntry &entry = cache.at(asset_name);
    return emscripten::val(emscripten::typed_memory_view(entry.binary.size(),
                                                         reinterpret_cast<const uint8_t*>(entry.binary.data())));
  }

  // Zero-copy ingest using a raw WASM-heap pointer from JS.
  // Rejects null pointer and absurd sizes; copies data into our own storage.
  bool addFromRawPointer(const std::string &asset_name, uintptr_t dataPtr, size_t size) {
    constexpr size_t kMaxRawAssetBytes = size_t(1) << 30;  // 1 GiB
    if ((size == 0) || (size > kMaxRawAssetBytes)) {
      return false;
    }
    if (dataPtr == 0) {
      return false;
    }
    // Overflow guard: reject if pointer + size wraps around.
    if (dataPtr + size < dataPtr) {
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
    
    cache[asset_name] = entry.finalize();
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
  /// @param max_bytes Caller-supplied upper bound for a single buffer (0 = use
  ///        the 512 MiB default). Lets a geometry-heavy root USDC whose single
  ///        layer exceeds the default stream in instead of falling back to the
  ///        high-memory in-heap path (raise via the CLI --max-mem-mb arg).
  /// Returns buffer info including UUID and pointer for direct memory access
  emscripten::val allocateZeroCopyBuffer(const std::string &asset_name, size_t size,
                                         size_t max_bytes) {
    emscripten::val result = emscripten::val::object();

    if (size == 0) {
      result.set("success", false);
      result.set("error", "Size must be greater than 0");
      return result;
    }

    // Cap single buffer allocation to avoid OOM in WASM's ~2GB linear memory.
    // Default 512 MiB; the caller may raise it (still bounded by the heap).
    constexpr size_t kDefaultMaxZeroCopyBufferBytes = size_t(1) << 29;  // 512 MiB
    const size_t cap = max_bytes ? max_bytes : kDefaultMaxZeroCopyBufferBytes;
    if (size > cap) {
      result.set("success", false);
      result.set("error",
                 "Buffer size exceeds " + std::to_string(cap >> 20) + " MiB limit");
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

  /// Move the raw bytes out of a zero-copy buffer (and erase it), for callers
  /// that want to adopt the streamed input directly (e.g. the next flatten
  /// pipeline) instead of caching it as an asset. Returns empty on unknown uuid.
  std::string takeZeroCopyBufferString(const std::string &uuid) {
    auto it = zerocopy_buffers.find(uuid);
    if (it == zerocopy_buffers.end()) return std::string();
    std::string s = std::move(it->second.buffer);
    zerocopy_buffers.erase(it);
    return s;
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

  /// Get total cache memory usage in bytes (all caches combined).
  size_t getCacheSizeBytes() const {
    size_t total = 0;
    for (const auto &pair : cache) {
      total += pair.first.size() + pair.second.binary.size();
    }
    for (const auto &pair : streaming_cache) {
      total += pair.first.size() + pair.second.current_size;
    }
    for (const auto &pair : zerocopy_buffers) {
      total += pair.first.size() + pair.second.total_size;
    }
    return total;
  }

  /// Set maximum cache size in bytes. 0 = unlimited (default).
  /// When adding assets that would exceed this limit, the oldest
  /// finalized assets are evicted first.
  void setMaxCacheSizeBytes(size_t max_bytes) {
    max_cache_size_bytes_ = max_bytes;
  }

  size_t getMaxCacheSizeBytes() const { return max_cache_size_bytes_; }

  /// Evict oldest finalized cache entries until total size <= target.
  /// Returns number of entries evicted.
  size_t evictToFitBytes(size_t target_bytes) {
    size_t evicted = 0;
    while (getCacheSizeBytes() > target_bytes && !cache.empty()) {
      // std::map is sorted by key; evict first entry as simple policy
      cache.erase(cache.begin());
      evicted++;
    }
    return evicted;
  }

  // TODO: Use IndexDB?
  //
  // <uri, AssetCacheEntry>
  std::map<std::string, AssetCacheEntry> cache;
  std::map<std::string, StreamingAssetEntry> streaming_cache;
  std::map<std::string, ZeroCopyStreamingBuffer> zerocopy_buffers;
  AssetCacheEntry empty_entry_;
  size_t max_cache_size_bytes_{0};  // 0 = unlimited
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

namespace {

using json = nlohmann::json;

std::string AxisName(const tinyusdz::Axis axis) {
  switch (axis) {
    case tinyusdz::Axis::X:
      return "X";
    case tinyusdz::Axis::Y:
      return "Y";
    case tinyusdz::Axis::Z:
    default:
      return "Z";
  }
}

json Vec3Json(const tinyusdz::value::point3f &v) {
  return json::array({v[0], v[1], v[2]});
}

json Vec3Json(const tinyusdz::value::float3 &v) {
  return json::array({v[0], v[1], v[2]});
}

json Vec3Json(const tinyusdz::value::vector3f &v) {
  return json::array({v[0], v[1], v[2]});
}

json QuatJson(const tinyusdz::value::quatf &v) {
  return json::array({v.real, v.imag[0], v.imag[1], v.imag[2]});
}

json Matrix4Json(const tinyusdz::value::matrix4d &m) {
  json a = json::array();
  for (size_t r = 0; r < 4; r++) {
    for (size_t c = 0; c < 4; c++) {
      a.push_back(m.m[r][c]);
    }
  }
  return a;
}

std::string PathName(const tinyusdz::Path &path) {
  return path.full_path_name();
}

json RelationshipTargetsJson(const tinyusdz::RelationshipProperty &rel) {
  json targets = json::array();
  for (const auto &path : rel.get_targetPaths()) {
    targets.push_back(PathName(path));
  }
  return targets;
}

template <typename T>
bool AddTypedAttr(json &props, const std::string &name,
                  const tinyusdz::TypedAttribute<T> &attr) {
  auto v = attr.get_value();
  if (!v) {
    return false;
  }
  props[name] = v.value();
  return true;
}

bool AddTypedAttr(json &props, const std::string &name,
                  const tinyusdz::TypedAttribute<tinyusdz::value::token> &attr) {
  auto v = attr.get_value();
  if (!v) {
    return false;
  }
  props[name] = v.value().str();
  return true;
}

bool AddTypedAttr(json &props, const std::string &name,
                  const tinyusdz::TypedAttribute<tinyusdz::value::point3f> &attr) {
  auto v = attr.get_value();
  if (!v) {
    return false;
  }
  props[name] = Vec3Json(v.value());
  return true;
}


bool AddTypedAttr(json &props, const std::string &name,
                  const tinyusdz::TypedAttribute<tinyusdz::value::vector3f> &attr) {
  auto v = attr.get_value();
  if (!v) {
    return false;
  }
  props[name] = Vec3Json(v.value());
  return true;
}

bool AddTypedAttr(json &props, const std::string &name,
                  const tinyusdz::TypedAttribute<tinyusdz::value::quatf> &attr) {
  auto v = attr.get_value();
  if (!v) {
    return false;
  }
  props[name] = QuatJson(v.value());
  return true;
}

template <typename T>
bool AddFallbackAttr(json &props, const std::string &name,
                     const tinyusdz::TypedAttributeWithFallback<T> &attr) {
  if (!attr.authored()) {
    return false;
  }
  props[name] = attr.get_value();
  return true;
}

bool AddFallbackAttr(json &props, const std::string &name,
                     const tinyusdz::TypedAttributeWithFallback<tinyusdz::value::token> &attr) {
  if (!attr.authored()) {
    return false;
  }
  props[name] = attr.get_value().str();
  return true;
}

template <typename T>
bool AddAnimatableFallbackAttr(
    json &props, const std::string &name,
    const tinyusdz::TypedAttributeWithFallback<tinyusdz::Animatable<T>> &attr) {
  if (!attr.authored()) {
    return false;
  }
  T value{};
  if (!attr.get_value().get(tinyusdz::value::TimeCode::Default(), &value)) {
    return false;
  }
  props[name] = value;
  return true;
}

json AttributeValueJson(const tinyusdz::Attribute &attr) {
  if (attr.has_connections()) {
    json paths = json::array();
    for (const auto &path : attr.connections()) {
      paths.push_back(PathName(path));
    }
    return {{"connections", paths}};
  }
  if (!attr.has_value()) {
    return nullptr;
  }

  if (auto v = attr.get_value<bool>()) return v.value();
  if (auto v = attr.get_value<int>()) return v.value();
  if (auto v = attr.get_value<int32_t>()) return v.value();
  if (auto v = attr.get_value<uint32_t>()) return v.value();
  if (auto v = attr.get_value<float>()) return v.value();
  if (auto v = attr.get_value<double>()) return v.value();
  if (auto v = attr.get_value<std::string>()) return v.value();
  if (auto v = attr.get_value<tinyusdz::value::StringData>()) return v.value().value;
  if (auto v = attr.get_value<tinyusdz::value::token>()) return v.value().str();
  if (auto v = attr.get_value<tinyusdz::value::AssetPath>()) return v.value().GetAssetPath();
  if (auto v = attr.get_value<tinyusdz::value::point3f>()) return Vec3Json(v.value());
  if (auto v = attr.get_value<tinyusdz::value::float3>()) return Vec3Json(v.value());
  if (auto v = attr.get_value<tinyusdz::value::vector3f>()) return Vec3Json(v.value());
  if (auto v = attr.get_value<tinyusdz::value::quatf>()) return QuatJson(v.value());
  if (auto v = attr.get_value<std::vector<int32_t>>()) return v.value();
  if (auto v = attr.get_value<std::vector<float>>()) return v.value();
  if (auto v = attr.get_value<std::vector<double>>()) return v.value();
  if (auto v = attr.get_value<std::vector<tinyusdz::value::token>>()) {
    json arr = json::array();
    for (const auto &tok : v.value()) {
      arr.push_back(tok.str());
    }
    return arr;
  }
  if (auto v = attr.get_value<std::vector<tinyusdz::value::point3f>>()) {
    json arr = json::array();
    for (const auto &p : v.value()) {
      arr.push_back(Vec3Json(p));
    }
    return arr;
  }

  return {{"unsupportedType", attr.type_name()}};
}

void AddPropertyMap(json &props, json &rels,
                    const std::map<std::string, tinyusdz::Property> &map) {
  for (const auto &kv : map) {
    if (const tinyusdz::Attribute *attr = kv.second.get_attribute_or_null()) {
      props[kv.first] = AttributeValueJson(*attr);
    } else if (kv.second.is_relationship()) {
      json targets = json::array();
      for (const auto &path : kv.second.get_relationTargets()) {
        targets.push_back(PathName(path));
      }
      rels[kv.first] = targets;
    }
  }
}

// Emit `purpose` and `visibility` token attrs for any GPrim-derived
// geometry prim (Mesh, Cube, Sphere, Cylinder, Capsule, Plane, …).
// Only writes the keys when non-default (Purpose::Default /
// Visibility::Inherited are the USD-spec defaults; omit them so the
// JSON output stays compact). Called from AppendPhysicsPrimJson so
// downstream consumers (e.g. web/sim's `usd-physics.js`) can filter
// `purpose == "guide"` collision meshes from default renders without
// duplicating the schema walk. See doc/usd.md "Mesh + collider
// convention" in github.com/lighttransport/lightgeom for the motivating
// use case.
template <typename GPrimT>
void AddPurposeVisibilityJson(json &prim_json, const GPrimT &gprim) {
  if (gprim.purpose.authored()) {
    tinyusdz::Purpose p_val = gprim.purpose.get_value();
    if (p_val != tinyusdz::Purpose::Default) {
      prim_json["purpose"] = tinyusdz::to_string(p_val);
    }
  }
  if (gprim.visibility.authored()) {
    const auto &v_anim = gprim.visibility.get_value();
    if (v_anim.has_default()) {
      tinyusdz::Visibility v_val;
      if (v_anim.get_default(&v_val)
          && v_val != tinyusdz::Visibility::Inherited) {
        prim_json["visibility"] = tinyusdz::to_string(v_val);
      }
    }
  }
}

void AddAPISchemasJson(json &prim_json, const tinyusdz::Prim &prim) {
  json schemas = json::array();
  const tinyusdz::APISchemas api = prim.metas().get_apiSchemas();
  for (const auto &schema : api.names) {
    std::string name = tinyusdz::to_string(schema.first);
    if (!schema.second.empty()) {
      name += ":" + schema.second;
    }
    schemas.push_back(name);
  }
  for (const auto &schema : api.unknownSchemas) {
    std::string name = schema.first;
    if (!schema.second.empty()) {
      name += ":" + schema.second;
    }
    schemas.push_back(name);
  }
  prim_json["apiSchemas"] = std::move(schemas);
}

void AddXformableJson(json &prim_json, const tinyusdz::Xformable &xformable) {
  bool reset = false;
  auto m = xformable.GetLocalMatrix(
      tinyusdz::value::TimeCode::Default(),
      tinyusdz::value::TimeSampleInterpolationType::Linear, &reset);
  if (m) {
    prim_json["matrix"] = Matrix4Json(m.value());
    prim_json["resetXformStack"] = reset;
  }
}

void AddJointBaseJson(json &props, json &rels,
                      const tinyusdz::PhysicsJointBase &joint) {
  rels["physics:body0"] = RelationshipTargetsJson(joint.body0);
  rels["physics:body1"] = RelationshipTargetsJson(joint.body1);
  AddTypedAttr(props, "physics:localPos0", joint.localPos0);
  AddTypedAttr(props, "physics:localPos1", joint.localPos1);
  AddTypedAttr(props, "physics:localRot0", joint.localRot0);
  AddTypedAttr(props, "physics:localRot1", joint.localRot1);
  AddTypedAttr(props, "physics:jointEnabled", joint.jointEnabled);
  AddTypedAttr(props, "physics:collisionEnabled", joint.collisionEnabled);
  AddTypedAttr(props, "physics:breakForce", joint.breakForce);
  AddTypedAttr(props, "physics:breakTorque", joint.breakTorque);
  AddTypedAttr(props, "physics:excludeFromArticulation",
               joint.excludeFromArticulation);
  // mjc:* attributes are consumed by the reconstruct path into the typed
  // MjcJointAPI struct (see prim-reconstruct-physics.cc); they no longer
  // appear in joint.props, so re-emit them here from the struct.
  // physxJoint:* / physxLimit:* / state:* are *not* consumed into any
  // typed struct — they remain in joint.props and arrive through the
  // AddPropertyMap(props, rels, joint->props) call below in each
  // PhysicsRevoluteJoint / PhysicsPrismaticJoint case.
  if (joint.mjcJoint) {
    AddFallbackAttr(props, "mjc:group", joint.mjcJoint.value().group);
    AddFallbackAttr(props, "mjc:stiffness", joint.mjcJoint.value().stiffness);
    AddFallbackAttr(props, "mjc:damping", joint.mjcJoint.value().damping);
    AddFallbackAttr(props, "mjc:armature", joint.mjcJoint.value().armature);
    AddFallbackAttr(props, "mjc:frictionloss",
                    joint.mjcJoint.value().frictionloss);
    AddTypedAttr(props, "mjc:springdamper",
                 joint.mjcJoint.value().springdamper);
    AddFallbackAttr(props, "mjc:springref", joint.mjcJoint.value().springref);
    AddFallbackAttr(props, "mjc:ref", joint.mjcJoint.value().ref);
    AddFallbackAttr(props, "mjc:margin", joint.mjcJoint.value().margin);
    AddFallbackAttr(props, "mjc:actuatorfrcrange:min",
                    joint.mjcJoint.value().actuatorfrcrange_min);
    AddFallbackAttr(props, "mjc:actuatorfrcrange:max",
                    joint.mjcJoint.value().actuatorfrcrange_max);
    AddFallbackAttr(props, "mjc:actuatorgravcomp",
                    joint.mjcJoint.value().actuatorgravcomp);
  }
  if (joint.newtonMimic) {
    AddFallbackAttr(props, "newton:mimicEnabled",
                    joint.newtonMimic.value().mimicEnabled);
    rels["newton:mimicJoint"] =
        RelationshipTargetsJson(joint.newtonMimic.value().mimicJoint);
    AddFallbackAttr(props, "newton:mimicCoef0",
                    joint.newtonMimic.value().mimicCoef0);
    AddFallbackAttr(props, "newton:mimicCoef1",
                    joint.newtonMimic.value().mimicCoef1);
  }
}

void AddSceneJson(json &props, const tinyusdz::PhysicsScene &scene) {
  AddTypedAttr(props, "physics:gravityDirection", scene.gravityDirection);
  AddTypedAttr(props, "physics:gravityMagnitude", scene.gravityMagnitude);
  if (scene.mjcScene) {
    AddFallbackAttr(props, "mjc:timestep", scene.mjcScene.value().timestep);
    AddFallbackAttr(props, "mjc:iterations",
                    scene.mjcScene.value().iterations);
    AddFallbackAttr(props, "mjc:integrator",
                    scene.mjcScene.value().integrator);
  }
  if (scene.newtonScene) {
    AddFallbackAttr(props, "newton:maxSolverIterations",
                    scene.newtonScene.value().maxSolverIterations);
    AddFallbackAttr(props, "newton:timeStepsPerSecond",
                    scene.newtonScene.value().timeStepsPerSecond);
    AddFallbackAttr(props, "newton:gravityEnabled",
                    scene.newtonScene.value().gravityEnabled);
  }
  if (scene.newtonXpbdScene) {
    AddFallbackAttr(props, "newton:xpbd:softBodyRelaxation",
                    scene.newtonXpbdScene.value().softBodyRelaxation);
    AddFallbackAttr(props, "newton:xpbd:softContactRelaxation",
                    scene.newtonXpbdScene.value().softContactRelaxation);
    AddFallbackAttr(props, "newton:xpbd:jointLinearRelaxation",
                    scene.newtonXpbdScene.value().jointLinearRelaxation);
    AddFallbackAttr(props, "newton:xpbd:jointAngularRelaxation",
                    scene.newtonXpbdScene.value().jointAngularRelaxation);
    AddFallbackAttr(props, "newton:xpbd:jointLinearCompliance",
                    scene.newtonXpbdScene.value().jointLinearCompliance);
    AddFallbackAttr(props, "newton:xpbd:jointAngularCompliance",
                    scene.newtonXpbdScene.value().jointAngularCompliance);
    AddFallbackAttr(props, "newton:xpbd:rigidContactRelaxation",
                    scene.newtonXpbdScene.value().rigidContactRelaxation);
    AddFallbackAttr(props, "newton:xpbd:rigidContactConWeighting",
                    scene.newtonXpbdScene.value().rigidContactConWeighting);
    AddFallbackAttr(props, "newton:xpbd:angularDamping",
                    scene.newtonXpbdScene.value().angularDamping);
    AddFallbackAttr(props, "newton:xpbd:restitutionEnabled",
                    scene.newtonXpbdScene.value().restitutionEnabled);
  }
  if (scene.newtonKaminoScene) {
    AddFallbackAttr(props, "newton:kamino:padmm:primalTolerance",
                    scene.newtonKaminoScene.value().padmmPrimalTolerance);
    AddFallbackAttr(props, "newton:kamino:padmm:dualTolerance",
                    scene.newtonKaminoScene.value().padmmDualTolerance);
    AddFallbackAttr(props, "newton:kamino:padmm:complementarityTolerance",
                    scene.newtonKaminoScene.value().padmmComplementarityTolerance);
    AddFallbackAttr(props, "newton:kamino:padmm:warmstarting",
                    scene.newtonKaminoScene.value().padmmWarmstarting);
    AddFallbackAttr(props, "newton:kamino:padmm:useAcceleration",
                    scene.newtonKaminoScene.value().padmmUseAcceleration);
    AddFallbackAttr(props, "newton:kamino:constraints:usePreconditioning",
                    scene.newtonKaminoScene.value().constraintsUsePreconditioning);
    AddFallbackAttr(props, "newton:kamino:constraints:alpha",
                    scene.newtonKaminoScene.value().constraintsAlpha);
    AddFallbackAttr(props, "newton:kamino:constraints:beta",
                    scene.newtonKaminoScene.value().constraintsBeta);
    AddFallbackAttr(props, "newton:kamino:constraints:gamma",
                    scene.newtonKaminoScene.value().constraintsGamma);
    AddFallbackAttr(props, "newton:kamino:jointCorrection",
                    scene.newtonKaminoScene.value().jointCorrection);
  }
}

void AddGeometryJson(json &prim_json, json &props,
                     const tinyusdz::GeomMesh &mesh) {
  AddXformableJson(prim_json, mesh);
  json geom;
  geom["type"] = "mesh";
  geom["pointCount"] = mesh.get_points().size();
  geom["faceCount"] = mesh.get_faceVertexCounts().size();
  prim_json["geometry"] = std::move(geom);
  AddPropertyMap(props, prim_json["relationships"], mesh.props);
}

void AddGeometryJson(json &prim_json, json &props,
                     const tinyusdz::GeomCube &cube) {
  AddXformableJson(prim_json, cube);
  json geom;
  // Use the schema-canonical "cube" name (matches the USD type "Cube"
  // and the lowercase-prim-name convention used by every other geom
  // emitter above). USD's GeomCube size is a single scalar (full edge
  // length, default 2.0); preserve that shape rather than fanning out
  // to a vec3 of identical values.
  geom["type"] = "cube";
  double size = 2.0;
  if (cube.size.authored()) {
    cube.size.get_value().get(tinyusdz::value::TimeCode::Default(), &size);
  }
  geom["size"] = size;
  prim_json["geometry"] = std::move(geom);
  AddPropertyMap(props, prim_json["relationships"], cube.props);
}

void AddGeometryJson(json &prim_json, json &props,
                     const tinyusdz::GeomSphere &sphere) {
  AddXformableJson(prim_json, sphere);
  json geom;
  geom["type"] = "sphere";
  AddAnimatableFallbackAttr(geom, "radius", sphere.radius);
  prim_json["geometry"] = std::move(geom);
  AddPropertyMap(props, prim_json["relationships"], sphere.props);
}

void AddGeometryJson(json &prim_json, json &props,
                     const tinyusdz::GeomCylinder &cylinder) {
  AddXformableJson(prim_json, cylinder);
  json geom;
  geom["type"] = "cylinder";
  AddAnimatableFallbackAttr(geom, "radius", cylinder.radius);
  AddAnimatableFallbackAttr(geom, "length", cylinder.height);
  if (cylinder.axis.authored()) {
    geom["axis"] = AxisName(cylinder.axis.get_value());
  }
  prim_json["geometry"] = std::move(geom);
  AddPropertyMap(props, prim_json["relationships"], cylinder.props);
}

void AddGeometryJson(json &prim_json, json &props,
                     const tinyusdz::GeomCapsule &capsule) {
  AddXformableJson(prim_json, capsule);
  json geom;
  geom["type"] = "capsule";
  AddAnimatableFallbackAttr(geom, "radius", capsule.radius);
  AddAnimatableFallbackAttr(geom, "length", capsule.height);
  if (capsule.axis.authored()) {
    geom["axis"] = AxisName(capsule.axis.get_value());
  }
  prim_json["geometry"] = std::move(geom);
  AddPropertyMap(props, prim_json["relationships"], capsule.props);
}

void AddGeometryJson(json &prim_json, json &props,
                     const tinyusdz::GeomPlane &plane) {
  AddXformableJson(prim_json, plane);
  json geom;
  geom["type"] = "plane";
  AddAnimatableFallbackAttr(geom, "width", plane.width);
  AddAnimatableFallbackAttr(geom, "length", plane.length);
  if (plane.axis.authored()) {
    geom["axis"] = AxisName(plane.axis.get_value());
  }
  prim_json["geometry"] = std::move(geom);
  AddPropertyMap(props, prim_json["relationships"], plane.props);
}

void AppendPhysicsPrimJson(const tinyusdz::Prim &prim, const std::string &path,
                           json &prims, int depth = 0) {
  // Guard against stack overflow from deeply nested USD stages.
  if (depth > 1024) return;

  json item;
  item["path"] = path;
  item["name"] = prim.element_name();
  item["type"] = prim.type_name();
  item["properties"] = json::object();
  item["relationships"] = json::object();
  AddAPISchemasJson(item, prim);

  json &props = item["properties"];
  json &rels = item["relationships"];

  if (const auto *xform = prim.as<tinyusdz::Xform>()) {
    AddXformableJson(item, *xform);
    AddPropertyMap(props, rels, xform->props);
  } else if (const auto *mesh = prim.as<tinyusdz::GeomMesh>()) {
    AddGeometryJson(item, props, *mesh);
    AddPurposeVisibilityJson(item, *mesh);
  } else if (const auto *cube = prim.as<tinyusdz::GeomCube>()) {
    AddGeometryJson(item, props, *cube);
    AddPurposeVisibilityJson(item, *cube);
  } else if (const auto *sphere = prim.as<tinyusdz::GeomSphere>()) {
    AddGeometryJson(item, props, *sphere);
    AddPurposeVisibilityJson(item, *sphere);
  } else if (const auto *cylinder = prim.as<tinyusdz::GeomCylinder>()) {
    AddGeometryJson(item, props, *cylinder);
    AddPurposeVisibilityJson(item, *cylinder);
  } else if (const auto *capsule = prim.as<tinyusdz::GeomCapsule>()) {
    AddGeometryJson(item, props, *capsule);
    AddPurposeVisibilityJson(item, *capsule);
  } else if (const auto *plane = prim.as<tinyusdz::GeomPlane>()) {
    AddGeometryJson(item, props, *plane);
    AddPurposeVisibilityJson(item, *plane);
  } else if (const auto *scene = prim.as<tinyusdz::PhysicsScene>()) {
    AddSceneJson(props, *scene);
    AddPropertyMap(props, rels, scene->props);
  } else if (const auto *joint = prim.as<tinyusdz::PhysicsRevoluteJoint>()) {
    AddJointBaseJson(props, rels, *joint);
    AddTypedAttr(props, "physics:axis", joint->axis);
    AddTypedAttr(props, "physics:lowerLimit", joint->lowerLimit);
    AddTypedAttr(props, "physics:upperLimit", joint->upperLimit);
    AddPropertyMap(props, rels, joint->props);
  } else if (const auto *joint = prim.as<tinyusdz::PhysicsPrismaticJoint>()) {
    AddJointBaseJson(props, rels, *joint);
    AddTypedAttr(props, "physics:axis", joint->axis);
    AddTypedAttr(props, "physics:lowerLimit", joint->lowerLimit);
    AddTypedAttr(props, "physics:upperLimit", joint->upperLimit);
    AddPropertyMap(props, rels, joint->props);
  } else if (const auto *joint = prim.as<tinyusdz::PhysicsSphericalJoint>()) {
    AddJointBaseJson(props, rels, *joint);
    AddTypedAttr(props, "physics:axis", joint->axis);
    AddTypedAttr(props, "physics:coneAngle0Limit", joint->coneAngle0Limit);
    AddTypedAttr(props, "physics:coneAngle1Limit", joint->coneAngle1Limit);
    AddPropertyMap(props, rels, joint->props);
  } else if (const auto *joint = prim.as<tinyusdz::PhysicsFixedJoint>()) {
    AddJointBaseJson(props, rels, *joint);
    AddPropertyMap(props, rels, joint->props);
  } else if (const auto *joint = prim.as<tinyusdz::PhysicsJoint>()) {
    AddJointBaseJson(props, rels, *joint);
    AddPropertyMap(props, rels, joint->props);
  } else if (const auto *act = prim.as<tinyusdz::NewtonActuator>()) {
    rels["newton:targets"] = RelationshipTargetsJson(act->targets);
    AddFallbackAttr(props, "newton:delaySteps", act->delaySteps);
    AddFallbackAttr(props, "newton:constEffort", act->constEffort);
    AddFallbackAttr(props, "newton:kp", act->kp);
    AddFallbackAttr(props, "newton:kd", act->kd);
    AddFallbackAttr(props, "newton:ki", act->ki);
    AddFallbackAttr(props, "newton:integralMax", act->integralMax);
    AddFallbackAttr(props, "newton:maxEffort", act->maxEffort);
    AddFallbackAttr(props, "newton:maxMotorEffort", act->maxMotorEffort);
    AddFallbackAttr(props, "newton:saturationEffort", act->saturationEffort);
    AddFallbackAttr(props, "newton:velocityLimit", act->velocityLimit);
    AddTypedAttr(props, "newton:lookupPositions", act->lookupPositions);
    AddTypedAttr(props, "newton:lookupEfforts", act->lookupEfforts);
    AddPropertyMap(props, rels, act->props);
  }

  prims.push_back(std::move(item));

  for (const auto &child : prim.children()) {
    AppendPhysicsPrimJson(child, path + "/" + child.element_name(), prims, depth + 1);
  }
}

}  // namespace

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
    ReportTinyUSDZDebugEvent(
        "renderScene.begin",
        "filename=" + filename_ + " inputBytes=" + std::to_string(binary.size()),
        binary.size(), is_usdz);

    tinyusdz::tydra::RenderSceneConverterEnv env(stage);

    // load texture in C++ image loader? default = false(Use JS to decode texture image)
    env.scene_config.load_texture_assets = loadTextureInNative_;

    env.material_config.preserve_texel_bitdepth = true;

    // UDIM: combine tiles into a single atlas, or keep them sparse for editing.
    env.material_config.combine_udim_tiles = combineUDIMTiles_;

    // Free GeomMesh data in stage after using it to save memory.
    env.mesh_config.lowmem = true;

    // Defer tangent computation to save memory and time during initial load.
    // Tangents will be computed on demand via computeMeshTangents().
    env.mesh_config.defer_tangent_computation = defer_tangent_computation_;

    // Only compute tangents for meshes with normal map textures.
    env.mesh_config.compute_tangents_only_with_normal_map = true;

    // Do not try to build indices(avoid temp memory consumption of vertex similarity search)
    //env.mesh_config.prefer_non_indexed = true;

    //env.mesh_config.build_index_method = 0; // simple

    // Sphere tessellation
    env.mesh_config.sphere_subdivisions = sphere_subdivisions_;

    // Bone reduction configuration
    env.mesh_config.enable_bone_reduction = enable_bone_reduction_;
    env.mesh_config.target_bone_count = target_bone_count_;
    env.mesh_config.round_bone_count = round_bone_count_;

    if (is_usdz) {
      // TODO: Support USDZ + Composition
      // Setup AssetResolutionResolver to read a asset(file) from memory.
      bool asset_on_memory =
          false;  // duplicate asset data from USDZ(binary) to UDSZAsset struct.

      ReportTinyUSDZDebugEvent(
          "usdzAssetInfo.begin",
          "asset_on_memory=false filename=" + filename_, binary.size(),
          is_usdz);
      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
              reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
              asset_on_memory, &usdz_asset_, &warn_, &error_)) {
        std::cerr << "Failed to read USDZ assetInfo. \n";
        ReportTinyUSDZDebugEvent(
            "usdzAssetInfo.failed", error_, binary.size(), is_usdz);
        loaded_ = false;
        return false;
      }
      ReportTinyUSDZDebugEvent(
          "usdzAssetInfo.end",
          "entries=" + std::to_string(usdz_asset_.asset_map.size()) +
              " copiedBytes=" + std::to_string(usdz_asset_.data.size()) +
              " backingBytes=" + std::to_string(usdz_asset_.size),
          binary.size(), is_usdz);

      tinyusdz::AssetResolutionResolver arr;

      // NOTE: Pointer address of usdz_asset must be valid until the call of
      // RenderSceneConverter::ConvertToRenderScene.
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset_)) {
        std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
        ReportTinyUSDZDebugEvent(
            "usdzAssetResolution.failed", "SetupUSDZAssetResolution failed",
            binary.size(), is_usdz);
        loaded_ = false;
        return false;
      }
      ReportTinyUSDZDebugEvent(
          "usdzAssetResolution.end",
          "entries=" + std::to_string(usdz_asset_.asset_map.size()),
          binary.size(), is_usdz);

      env.asset_resolver = arr;
    } else {
      tinyusdz::AssetResolutionResolver arr;
      if (!SetupEMAssetResolution(arr, &em_resolver_)) {
        std::cerr << "Failed to setup FetchAssetResolution\n";
        ReportTinyUSDZDebugEvent(
            "emAssetResolution.failed", "SetupEMAssetResolution failed",
            binary.size(), is_usdz);
        loaded_ = false;
        return false;
      }
      ReportTinyUSDZDebugEvent(
          "emAssetResolution.end",
          "cacheEntries=" + std::to_string(em_resolver_.cache.size()),
          binary.size(), is_usdz);

      env.asset_resolver = arr;
    }

    // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
    tinyusdz::tydra::RenderSceneConverter converter;

    // Set up detailed progress callback to update parsing_progress_ and call JS
    converter.SetDetailedProgressCallback(
        [input_size = binary.size(), is_usdz, debug_enabled = IsTinyUSDZDebugEnabled()](const tinyusdz::tydra::DetailedProgressInfo &info, void *userptr) -> bool {
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

          // Build the (per-tick) debug detail string only when a JS listener is
          // attached; otherwise this hot callback pays nothing for debugging.
          if (debug_enabled) {
            std::ostringstream detail;
            detail << "stage=" << info.GetStageName()
                   << " message=" << info.message
                   << " mesh=" << info.meshes_processed << "/"
                   << info.meshes_total
                   << " material=" << info.materials_processed << "/"
                   << info.materials_total
                   << " currentMaterial=" << info.current_material_name;
            ReportTinyUSDZDebugEvent(
                "renderScene.progress", detail.str(), input_size, is_usdz,
                info.materials_processed, info.materials_total,
                info.current_material_name);
          }

          // Call JavaScript synchronously via EM_JS
          reportTydraProgress(
            static_cast<int>(info.meshes_processed),
            static_cast<int>(info.meshes_total),
            info.GetStageName(),  // Already returns const char*
            info.current_mesh_name.c_str(),
            static_cast<int>(info.materials_processed),
            static_cast<int>(info.materials_total),
            info.current_material_name.c_str(),
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
    env.scene_config.enable_value_clips = enable_value_clips_;
    env.scene_config.value_clip_sample_rate = value_clip_sample_rate_;
    env.scene_config.value_clip_use_time_range =
        value_clip_use_time_range_;
    env.scene_config.value_clip_start_time = value_clip_start_time_;
    env.scene_config.value_clip_end_time = value_clip_end_time_;
    ReportTinyUSDZDebugEvent(
        "convertToRenderScene.begin",
        "loadTextureInNative=" + std::to_string(loadTextureInNative_ ? 1 : 0) +
            " combineUDIMTiles=" + std::to_string(combineUDIMTiles_ ? 1 : 0) +
            " deferTangents=" +
            std::to_string(defer_tangent_computation_ ? 1 : 0),
        binary.size(), is_usdz);
    loaded_ = converter.ConvertToRenderScene(env, &render_scene_);
    ReportTinyUSDZDebugEvent(
        loaded_ ? "convertToRenderScene.end" : "convertToRenderScene.failed",
        loaded_ ? "success" : converter.GetError(), binary.size(), is_usdz);

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
    ReportTinyUSDZDebugEvent(
        "loadFromBinary.begin",
        "filename=" + filename + " bytes=" + std::to_string(binary.size()),
        binary.size(), is_usdz);

    tinyusdz::USDLoadOptions options;
    options.max_memory_limit_in_mb = max_memory_limit_mb_;
    options.mmap_zero_copy = mmap_zero_copy_;

    tinyusdz::Stage stage;
    loaded_ = tinyusdz::LoadUSDFromMemory(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, &stage, &warn_, &error_, options);

    if (!loaded_) {
      ReportTinyUSDZDebugEvent(
          "loadFromBinary.parseFailed", error_, binary.size(), is_usdz);
      return false;
    }
    ReportTinyUSDZDebugEvent(
        "loadFromBinary.parsed",
        "warnBytes=" + std::to_string(warn_.size()) +
            " maxMemoryLimitMB=" + std::to_string(max_memory_limit_mb_) +
            " mmapZeroCopy=" + std::to_string(mmap_zero_copy_ ? 1 : 0),
        binary.size(), is_usdz);

    loaded_as_layer_ = false;
    filename_ = filename;
    export_stage_ = stage;
    has_stage_ = true;

    //std::cout << "[tusd:loadFromBinary] loaded << " filename << "\n";
#if 0
    tinyusdz::tydra::RenderSceneConverterEnv env(stage);

    //
    // false = Load Texture in JS Layer
    //

    env.scene_config.load_texture_assets = loadTextureInNative_;

    env.material_config.preserve_texel_bitdepth = true;

    // UDIM: combine tiles into a single atlas, or keep them sparse for editing.
    env.material_config.combine_udim_tiles = combineUDIMTiles_;

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
            static_cast<int>(info.materials_processed),
            static_cast<int>(info.materials_total),
            info.current_material_name.c_str(),
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
    env.scene_config.enable_value_clips = enable_value_clips_;
    env.scene_config.value_clip_sample_rate = value_clip_sample_rate_;
    env.scene_config.value_clip_use_time_range =
        value_clip_use_time_range_;
    env.scene_config.value_clip_start_time = value_clip_start_time_;
    env.scene_config.value_clip_end_time = value_clip_end_time_;
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
    options.mmap_zero_copy = mmap_zero_copy_;

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

    // UDIM: combine tiles into a single atlas, or keep them sparse for editing.
    env.material_config.combine_udim_tiles = combineUDIMTiles_;
    env.mesh_config.lowmem = true;
    env.mesh_config.defer_tangent_computation = defer_tangent_computation_;
    env.mesh_config.compute_tangents_only_with_normal_map = true;
    env.mesh_config.sphere_subdivisions = sphere_subdivisions_;
    env.mesh_config.enable_bone_reduction = enable_bone_reduction_;
    env.mesh_config.target_bone_count = target_bone_count_;
    env.mesh_config.round_bone_count = round_bone_count_;

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
            static_cast<int>(info.materials_processed),
            static_cast<int>(info.materials_total),
            info.current_material_name.c_str(),
            info.progress
          );
          return true;
        },
        nullptr);

    if (stage.metas().startTimeCode.authored()) {
      env.timecode = stage.metas().startTimeCode.get_value();
    }
    env.scene_config.enable_value_clips = enable_value_clips_;
    env.scene_config.value_clip_sample_rate = value_clip_sample_rate_;
    env.scene_config.value_clip_use_time_range =
        value_clip_use_time_range_;
    env.scene_config.value_clip_start_time = value_clip_start_time_;
    env.scene_config.value_clip_end_time = value_clip_end_time_;

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
      tinyusdz::value::quatf q{{0.0f, 0.0f, 0.0f}, 1.0f};
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

      // Allow parent-relative/drive-prefixed asset paths (UE exports); the
      // sandboxed in-memory resolver bounds what is reachable.
      tinyusdz::SublayersCompositionOptions sublayer_options;
      sublayer_options.allow_parent_relative_paths = true;
      tinyusdz::ReferencesCompositionOptions references_options;
      references_options.allow_parent_relative_paths = true;
      tinyusdz::PayloadCompositionOptions payload_options;
      payload_options.allow_parent_relative_paths = true;

      tinyusdz::Layer src_layer = root_layer;
      if (comp_features.subLayers) {
        tinyusdz::Layer composited_layer;
        if (!tinyusdz::CompositeSublayers(resolver, src_layer, &composited_layer, &warn_, &error_, sublayer_options)) {
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
            if (!tinyusdz::CompositeReferences(resolver, src_layer, &composited_layer, &warn_, &error_, references_options)) {
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
            if (!tinyusdz::CompositePayload(resolver, src_layer, &composited_layer, &warn_, &error_, payload_options)) {
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

  // ---- Instance support (AOUSD Spec 11.3.3) ----

  int numInstances() const {
    return static_cast<int>(render_scene_.instances.size());
  }

  emscripten::val getInstance(int instance_id) const {
    if (instance_id < 0 ||
        static_cast<size_t>(instance_id) >= render_scene_.instances.size()) {
      return emscripten::val::null();
    }
    const auto &inst = render_scene_.instances[static_cast<size_t>(instance_id)];
    emscripten::val obj = emscripten::val::object();
    obj.set("primName", inst.prim_name);
    obj.set("absPath", inst.abs_path);
    obj.set("displayName", inst.display_name);
    obj.set("prototypeIndex", inst.prototype_index);
    obj.set("meshId", inst.mesh_id);
    obj.set("materialId", inst.material_id);
    obj.set("localMatrix", detail::toArray(inst.local_matrix));
    obj.set("globalMatrix", detail::toArray(inst.global_matrix));
    obj.set("visible", inst.visible);
    return obj;
  }

  emscripten::val getInstancesForMesh(int mesh_id) const {
    emscripten::val arr = emscripten::val::array();
    for (size_t i = 0; i < render_scene_.instances.size(); i++) {
      if (render_scene_.instances[i].mesh_id == mesh_id) {
        arr.call<void>("push", static_cast<int>(i));
      }
    }
    return arr;
  }

  // ---- End instance support ----

  /**
   * Generate bone data texture for GPU skinning with high bone counts.
   *
   * The texture stores bone indices and weights in RGBA format:
   * - R: bone index 0, G: weight 0, B: bone index 1, A: weight 1
   * - Each texel contains 2 bone influences
   *
   * @param mesh_id Mesh index
   * @param max_influences Maximum influences per vertex (0 = use mesh's elementSize)
   * @return Object with textureData, dimensions, and metadata
   */
  emscripten::val generateBoneTexture(int mesh_id, int max_influences = 0) const {
    emscripten::val result = emscripten::val::object();

    if (!loaded_ || mesh_id < 0 || mesh_id >= static_cast<int>(render_scene_.meshes.size())) {
      result.set("error", "Invalid mesh ID or scene not loaded");
      return result;
    }

    const auto &rmesh = render_scene_.meshes[size_t(mesh_id)];
    const auto &jw = rmesh.joint_and_weights;

    if (jw.jointIndices.empty() || jw.jointWeights.empty()) {
      result.set("error", "Mesh has no skinning data");
      return result;
    }

    int elementSize = jw.elementSize;
    if (elementSize <= 0) {
      result.set("error", "Invalid skinning data (elementSize <= 0)");
      return result;
    }
    int vertexCount = static_cast<int>(jw.jointIndices.size()) / elementSize;

    // Determine max influences for texture
    int maxInfl = (max_influences > 0) ? max_influences : elementSize;

    // Round up to standard GPU skinning values if needed
    auto roundUp = [](int count) -> int {
      const int standardCounts[] = {4, 8, 16, 32, 48, 64, 80, 96, 128};
      for (int stdCount : standardCounts) {
        if (count <= stdCount) return stdCount;
      }
      return 128;
    };
    maxInfl = roundUp(maxInfl);

    // Calculate texture dimensions
    // Each texel stores 2 influences (boneIdx0, weight0, boneIdx1, weight1)
    int influencesPerTexel = 2;
    int texelsPerVertex = (maxInfl + influencesPerTexel - 1) / influencesPerTexel;
    size_t totalTexels = static_cast<size_t>(vertexCount) * static_cast<size_t>(texelsPerVertex);

    // Find optimal texture dimensions (prefer power of 2)
    size_t texWidth = 1;
    while (texWidth * texWidth < totalTexels && texWidth < 4096) {
      texWidth *= 2;
    }
    size_t texHeight = (totalTexels + texWidth - 1) / texWidth;
    size_t texDataSize = texWidth * texHeight * 4;

    // Guard against excessive allocation.
    constexpr size_t kMaxTexels = size_t(1) << 30;  // 1 billion texels
    if (totalTexels > kMaxTexels || texWidth > 4096 || texHeight > 4096) {
      result.set("error", "Bone texture dimensions too large");
      return result;
    }

    // Allocate texture data (RGBA float)
    std::vector<float> textureData(texDataSize, 0.0f);

    // Fill texture with bone data
    for (int v = 0; v < vertexCount; v++) {
      int texelOffset = v * texelsPerVertex;

      // Collect influences for this vertex, sorted by weight (descending)
      std::vector<std::pair<int, float>> influences;
      for (int j = 0; j < elementSize && j < maxInfl; j++) {
        int srcIdx = v * elementSize + j;
        if (srcIdx < static_cast<int>(jw.jointIndices.size())) {
          int boneIdx = jw.jointIndices[srcIdx];
          float weight = jw.jointWeights[srcIdx];
          if (weight > 0.0f) {
            influences.push_back({boneIdx, weight});
          }
        }
      }

      // Sort by weight descending for potential early termination in shader
      std::sort(influences.begin(), influences.end(),
                [](const auto &a, const auto &b) { return a.second > b.second; });

      // Write to texture (2 influences per texel)
      for (int t = 0; t < texelsPerVertex; t++) {
        int texelIdx = (texelOffset + t) * 4;
        if (texelIdx + 3 >= static_cast<int>(textureData.size())) break;

        // First influence in texel (RG)
        int infIdx0 = t * 2;
        if (infIdx0 < static_cast<int>(influences.size())) {
          textureData[texelIdx + 0] = static_cast<float>(influences[infIdx0].first);  // R: bone index
          textureData[texelIdx + 1] = influences[infIdx0].second;  // G: weight
        } else {
          textureData[texelIdx + 0] = -1.0f;  // Invalid bone index
          textureData[texelIdx + 1] = 0.0f;
        }

        // Second influence in texel (BA)
        int infIdx1 = t * 2 + 1;
        if (infIdx1 < static_cast<int>(influences.size())) {
          textureData[texelIdx + 2] = static_cast<float>(influences[infIdx1].first);  // B: bone index
          textureData[texelIdx + 3] = influences[infIdx1].second;  // A: weight
        } else {
          textureData[texelIdx + 2] = -1.0f;  // Invalid bone index
          textureData[texelIdx + 3] = 0.0f;
        }
      }
    }

    // Generate vertex offset array (where each vertex's data starts in texture)
    std::vector<float> vertexOffsets(vertexCount);
    for (int v = 0; v < vertexCount; v++) {
      vertexOffsets[v] = static_cast<float>(v * texelsPerVertex);
    }

    // Return result
    result.set("textureWidth", texWidth);
    result.set("textureHeight", texHeight);
    result.set("texelsPerVertex", texelsPerVertex);
    result.set("maxInfluences", maxInfl);
    result.set("vertexCount", vertexCount);
    result.set("originalElementSize", elementSize);

    // Store in member first, then create views (avoids dangling pointers).
    bone_texture_data_ = std::move(textureData);
    bone_vertex_offsets_ = std::move(vertexOffsets);

    result.set("textureData", emscripten::val(emscripten::typed_memory_view(
        bone_texture_data_.size(), bone_texture_data_.data())));
    result.set("vertexOffsets", emscripten::val(emscripten::typed_memory_view(
        bone_vertex_offsets_.size(), bone_vertex_offsets_.data())));

    // Re-set with valid pointers
    result.set("textureData", emscripten::val(emscripten::typed_memory_view(
        bone_texture_data_.size(), bone_texture_data_.data())));
    result.set("vertexOffsets", emscripten::val(emscripten::typed_memory_view(
        bone_vertex_offsets_.size(), bone_vertex_offsets_.data())));

    return result;
  }

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

    if (mat_id < 0 || mat_id >= static_cast<int>(render_scene_.materials.size())) {
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

  int numCameras() const { return static_cast<int>(render_scene_.cameras.size()); }

  emscripten::val getCamera(int camera_id) const {
    emscripten::val cam = emscripten::val::object();

    if (!loaded_) {
      cam.set("error", "Scene not loaded");
      return cam;
    }

    if (camera_id < 0 || camera_id >= static_cast<int>(render_scene_.cameras.size())) {
      cam.set("error", "Invalid camera ID");
      return cam;
    }

    const auto &c = render_scene_.cameras[static_cast<size_t>(camera_id)];

    cam.set("name", c.name);
    cam.set("absPath", c.abs_path);
    cam.set("displayName", c.display_name);
    cam.set("focalLength", c.focalLength);
    cam.set("verticalAperture", c.verticalAperture);
    cam.set("horizontalAperture", c.horizontalAperture);
    cam.set("znear", c.znear);
    cam.set("zfar", c.zfar);

    // Compute FOV in radians
    cam.set("yfov", 2.0f * std::atan(0.5f * c.verticalAperture / c.focalLength));
    cam.set("xfov", 2.0f * std::atan(0.5f * c.horizontalAperture / c.focalLength));
    cam.set("aspectRatio", c.horizontalAperture / c.verticalAperture);

    // Projection type
    std::string projStr;
    switch (c.projection) {
      case tinyusdz::GeomCamera::Projection::Perspective: projStr = "perspective"; break;
      case tinyusdz::GeomCamera::Projection::Orthographic: projStr = "orthographic"; break;
    }
    cam.set("projection", projStr);

    return cam;
  }

  emscripten::val getTexture(int tex_id) const {
    emscripten::val tex = emscripten::val::object();

    if (!loaded_) {
      return tex;
    }

    if (tex_id < 0 || static_cast<size_t>(tex_id) >= render_scene_.textures.size()) {
      return tex;
    }

    const auto &t = render_scene_.textures[tex_id];

    tex.set("textureImageId", int(t.texture_image_id));
    tex.set("wrapS", to_string(t.wrapS));
    tex.set("wrapT", to_string(t.wrapT));
    //  TOOD: bias, scale, rot/scale/trans, etc

    // UDIM: expose remap (combined atlas) or sparse-tile linkage.
    tex.set("isUDIM", bool(t.is_udim));
    if (t.is_udim) {
      tex.set("udimTextureId", int(t.udim_texture_id));
      tex.set("udimUvScaleU", float(t.udim_uv_scale[0]));
      tex.set("udimUvScaleV", float(t.udim_uv_scale[1]));
      tex.set("udimUvOffsetU", float(t.udim_uv_offset[0]));
      tex.set("udimUvOffsetV", float(t.udim_uv_offset[1]));
    }

    return tex;
  }

  int numUDIMTextures() const { return render_scene_.udim_textures.size(); }

  // Return a sparse (keep-as-is) UDIM texture: its `<UDIM>` asset identifier
  // and the list of resolved tiles { udim, u, v, imageId }. Each tile image can
  // be fetched with getImage(imageId).
  emscripten::val getUDIMTexture(int udim_id) const {
    emscripten::val out = emscripten::val::object();

    if (!loaded_) {
      return out;
    }

    if (udim_id < 0 ||
        static_cast<size_t>(udim_id) >= render_scene_.udim_textures.size()) {
      return out;
    }

    const auto &u = render_scene_.udim_textures[size_t(udim_id)];

    out.set("primName", u.prim_name);
    out.set("absPath", u.abs_path);
    out.set("displayName", u.display_name);
    out.set("assetIdentifier", u.asset_identifier);

    emscripten::val tiles = emscripten::val::array();
    int idx = 0;
    for (const auto &kv : u.imageTileIds) {
      const uint32_t tile_id = kv.first;
      emscripten::val tile = emscripten::val::object();
      tile.set("udim", int(tile_id));
      tile.set("u", int((tile_id - 1001u) % 10u));
      tile.set("v", int((tile_id - 1001u) / 10u));
      tile.set("imageId", int(kv.second));
      tiles.set(idx++, tile);
    }
    out.set("tiles", tiles);

    return out;
  }

  emscripten::val getImage(int img_id) const {
    warnDeprecated_("getImage", "getImagePtr()/getImageCopy()");
    return buildImageVal_(img_id);
  }

  emscripten::val buildImageVal_(int img_id) const {
    emscripten::val img = emscripten::val::object();

    if (!loaded_) {
      return img;
    }

    if (img_id < 0 || static_cast<size_t>(img_id) >= render_scene_.images.size()) {
      return img;
    }

    const auto &i = render_scene_.images[size_t(img_id)];

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

  // ---------------------------------------------------------------------------
  // Id-based, OpenGL-style heap accessors (zero-copy + explicit copy)
  //
  // The scene owns mesh/image data in the WASM heap, addressed by id. Transfer
  // to the GPU lazily, when needed:
  //
  //   getMeshPtr(i) / getImagePtr(i)  -> per-attribute {ptr,length,comps,dtype,
  //     byteLength} descriptors (NO TypedArrays). Build a view on the *live*
  //     Module.HEAPU8.buffer at the instant of gl.bufferData/texImage2D, then
  //     keep only the GL object (like an OpenGL name). `ptr` is the byte offset
  //     into linear memory; it survives heap growth (a TypedArray view would
  //     NOT — growth detaches it), as long as the loader isn't deleted/reloaded.
  //
  //   getMeshCopy(i) / getImageCopy(i) -> the SAME shape as the deprecated
  //     getMesh()/getImage() (a drop-in replacement), but every heap-backed
  //     TypedArray is an owned (JS-heap) copy — safe to retain, hand to
  //     THREE.BufferAttribute, or process on the CPU (e.g. UDIM atlas assembly).
  //
  // getMesh()/getImage() remain (deprecated) for backward compatibility.
  // ---------------------------------------------------------------------------

  static size_t dtypeByteSize_(const char *dtype) {
    std::string d(dtype);
    if (d == "f32" || d == "u32") return 4;
    if (d == "snorm16") return 2;
    return 1;  // snorm8 / u8
  }

  // Build one zero-copy attribute descriptor: {ptr, length, comps, count,
  // dtype, byteLength}. `length` is the total scalar count (vertices * comps).
  static emscripten::val heapAttr_(const void *p, size_t length, int comps,
                                   const char *dtype) {
    emscripten::val a = emscripten::val::object();
    a.set("length", emscripten::val(static_cast<double>(length)));
    a.set("comps", comps);
    a.set("dtype", std::string(dtype));
    a.set("count", emscripten::val(static_cast<double>(comps ? length / comps : length)));
    a.set("ptr", emscripten::val(static_cast<double>(reinterpret_cast<uintptr_t>(p))));
    a.set("byteLength", emscripten::val(static_cast<double>(length * dtypeByteSize_(dtype))));
    return a;
  }

  // Replace every (possibly nested) TypedArray in `v` with an owned JS-heap
  // copy (`.slice()`), turning a heap-aliasing getMesh()/getImage() result into
  // a retain-safe one without duplicating those builders.
  static void deepCopyTypedArrays_(emscripten::val v) {
    emscripten::val keys = emscripten::val::global("Object").call<emscripten::val>("keys", v);
    const size_t n = keys["length"].as<size_t>();
    for (size_t i = 0; i < n; i++) {
      const std::string k = keys[i].as<std::string>();
      emscripten::val child = v[k];
      if (child.isNull() || child.isUndefined()) continue;
      if (!child["BYTES_PER_ELEMENT"].isUndefined()) {
        v.set(k, child.call<emscripten::val>("slice"));  // TypedArray -> owned copy
      } else if (child.typeOf().as<std::string>() == "object") {
        deepCopyTypedArrays_(child);  // recurse (e.g. uvSets.uvN.data)
      }
    }
  }

  void warnDeprecated_(const char *fn, const char *repl) const {
    if (deprecation_warned_.insert(fn).second) {
      emscripten::val::global("console").call<void>(
          "warn", std::string("[tinyusdz] ") + fn +
                      "() is deprecated; prefer " + repl +
                      ". (Heap views from the old API alias WASM memory and can"
                      " dangle; the *Ptr/*Copy accessors make the contract"
                      " explicit.)");
    }
  }

  // Zero-copy mesh descriptor: per-attribute {ptr,length,comps,count,dtype,
  // byteLength}. Subset needed for GPU rendering (points/indices/normals/uv0).
  emscripten::val getMeshPtr(int mesh_id) const {
    emscripten::val out = emscripten::val::object();
    if (!loaded_ || mesh_id < 0 ||
        static_cast<size_t>(mesh_id) >= render_scene_.meshes.size()) {
      return out;
    }
    using tinyusdz::tydra::VertexAttributeFormat;
    const tinyusdz::tydra::RenderMesh &rmesh =
        render_scene_.meshes[size_t(mesh_id)];

    const size_t vtx = rmesh.points.size();
    out.set("vertexCount", emscripten::val(static_cast<double>(vtx)));
    out.set("materialId", rmesh.material_id);
    out.set("doubleSided", rmesh.doubleSided);
    out.set("primName", rmesh.prim_name);

    out.set("points",
            heapAttr_(reinterpret_cast<const float *>(rmesh.points.data()),
                      vtx * 3, 3, "f32"));

    const auto &idx = rmesh.faceVertexIndices();
    const auto &cnt = rmesh.faceVertexCounts();
    if (!idx.empty()) {
      out.set("indices", heapAttr_(idx.data(), idx.size(), 1, "u32"));
    }
    bool triangulated = !cnt.empty();
    for (uint32_t c : cnt) {
      if (c != 3) { triangulated = false; break; }
    }
    if (!cnt.empty()) {
      out.set("faceVertexCounts", heapAttr_(cnt.data(), cnt.size(), 1, "u32"));
    }
    out.set("triangulated", triangulated);

    // normals (snorm8 / snorm16 / f32; 1010102 unpacked to a stable f32 cache)
    if (!rmesh.normals.empty()) {
      const size_t nv = rmesh.normals.vertex_count();
      if (rmesh.normals.format == VertexAttributeFormat::Char3) {
        out.set("normals", heapAttr_(rmesh.normals.data.data(), nv * 3, 3, "snorm8"));
      } else if (rmesh.normals.format == VertexAttributeFormat::Short3) {
        out.set("normals", heapAttr_(rmesh.normals.data.data(), nv * 3, 3, "snorm16"));
      } else if (rmesh.normals.format == VertexAttributeFormat::Uint) {
        auto &cache = normals_cache_[mesh_id];
        if (cache.size() != nv * 3) {
          cache.resize(nv * 3);
          const uint32_t *P =
              reinterpret_cast<const uint32_t *>(rmesh.normals.data.data());
          for (size_t i = 0; i < nv; i++) {
            tinyusdz::tydra::tangent_quantize::unpack_normal_1010102(
                P[i], cache[i * 3 + 0], cache[i * 3 + 1], cache[i * 3 + 2]);
          }
        }
        out.set("normals", heapAttr_(cache.data(), nv * 3, 3, "f32"));
      } else {
        out.set("normals",
                heapAttr_(reinterpret_cast<const float *>(rmesh.normals.data.data()),
                          nv * 3, 3, "f32"));
      }
    }

    auto uvit = rmesh.texcoords.find(0);
    if (uvit != rmesh.texcoords.end()) {
      const size_t uvn = uvit->second.vertex_count();
      out.set("uv0",
              heapAttr_(reinterpret_cast<const float *>(uvit->second.data.data()),
                        uvn * 2, 2, "f32"));
    }
    return out;
  }

  // Owned, retain-safe drop-in for getMesh(): identical shape, copied arrays.
  emscripten::val getMeshCopy(int mesh_id) const {
    emscripten::val m = buildMeshVal_(mesh_id);
    deepCopyTypedArrays_(m);
    return m;
  }

  // Zero-copy image descriptor: {width,height,channels,decoded,colorSpace,
  // usdColorSpace,uri,bufferId, ptr,byteLength}.
  emscripten::val getImagePtr(int img_id) const {
    emscripten::val out = imageMeta_(img_id);
    if (out.isUndefined()) return emscripten::val::object();
    const auto &i = render_scene_.images[size_t(img_id)];
    if (i.buffer_id >= 0 &&
        static_cast<size_t>(i.buffer_id) < render_scene_.buffers.size()) {
      const auto &b = render_scene_.buffers[size_t(i.buffer_id)];
      out.set("ptr", emscripten::val(static_cast<double>(
                         reinterpret_cast<uintptr_t>(b.data.data()))));
      out.set("byteLength", emscripten::val(static_cast<double>(b.data.size())));
    }
    return out;
  }

  // Owned, retain-safe drop-in for getImage(): identical shape, copied data.
  emscripten::val getImageCopy(int img_id) const {
    emscripten::val m = buildImageVal_(img_id);
    deepCopyTypedArrays_(m);
    return m;
  }

  // Image metadata common to getImage/getImagePtr/getImageCopy (no pixel data).
  emscripten::val imageMeta_(int img_id) const {
    if (!loaded_ || img_id < 0 ||
        static_cast<size_t>(img_id) >= render_scene_.images.size()) {
      return emscripten::val::undefined();
    }
    const auto &i = render_scene_.images[size_t(img_id)];
    emscripten::val out = emscripten::val::object();
    out.set("width", int(i.width));
    out.set("height", int(i.height));
    out.set("channels", int(i.channels));
    out.set("decoded", bool(i.decoded));
    out.set("colorSpace", to_string(i.colorSpace));
    out.set("usdColorSpace", to_string(i.usdColorSpace));
    out.set("uri", i.asset_identifier);
    out.set("bufferId", int(i.buffer_id));
    return out;
  }

  emscripten::val getMesh(int mesh_id) const {
    warnDeprecated_("getMesh", "getMeshPtr()/getMeshCopy()");
    return buildMeshVal_(mesh_id);
  }

  emscripten::val buildMeshVal_(int mesh_id) const {
    emscripten::val mesh = emscripten::val::object();

    if (!loaded_) {
      return mesh;
    }

    if (mesh_id < 0 || static_cast<size_t>(mesh_id) >= render_scene_.meshes.size()) {
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
      using tinyusdz::tydra::VertexAttributeFormat;
      if (rmesh.normals.format == VertexAttributeFormat::Char3) {
        // SNorm8x3 — pass as Int8Array; Three.js uses normalized=true
        const int8_t *normals_ptr =
            reinterpret_cast<const int8_t *>(rmesh.normals.data.data());
        mesh.set("normals", emscripten::typed_memory_view(
                                rmesh.normals.vertex_count() * 3, normals_ptr));
        mesh.set("normalsFormat", std::string("snorm8"));
      } else if (rmesh.normals.format == VertexAttributeFormat::Short3) {
        // SNorm16x3 — pass as Int16Array; Three.js uses normalized=true
        const int16_t *normals_ptr =
            reinterpret_cast<const int16_t *>(rmesh.normals.data.data());
        mesh.set("normals", emscripten::typed_memory_view(
                                rmesh.normals.vertex_count() * 3, normals_ptr));
        mesh.set("normalsFormat", std::string("snorm16"));
      } else if (rmesh.normals.format == VertexAttributeFormat::Uint) {
        // Packed 1010102 — Three.js can't use this; unpack to float3 cache
        using namespace tinyusdz::tydra::tangent_quantize;
        size_t nv = rmesh.normals.vertex_count();
        auto &cache = normals_cache_[mesh_id];
        cache.resize(nv * 3);
        const uint32_t *P =
            reinterpret_cast<const uint32_t *>(rmesh.normals.data.data());
        for (size_t i = 0; i < nv; i++) {
          unpack_normal_1010102(P[i], cache[i*3+0], cache[i*3+1], cache[i*3+2]);
        }
        mesh.set("normals", emscripten::typed_memory_view(nv * 3, cache.data()));
        mesh.set("normalsFormat", std::string("float32"));
      } else {
        // Float3 (Vec3) — pass as Float32Array
        const float *normals_ptr =
            reinterpret_cast<const float *>(rmesh.normals.data.data());
        mesh.set("normals", emscripten::typed_memory_view(
                                rmesh.normals.vertex_count() * 3, normals_ptr));
        mesh.set("normalsFormat", std::string("float32"));
      }
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

    // Expose tangents as vec4 float (xyz=tangent direction, w=handedness sign).
    // Three.js expects vec4 tangent where w = sign(dot(cross(N, T), B)).
    // Supports both packed formats (10_10_10_2, SNorm8, Fp16) and legacy Vec3.
    if (!rmesh.tangents.empty()) {
      using namespace tinyusdz::tydra;
      size_t nv = rmesh.tangents.vertex_count();
      auto &cache = tangents4_cache_[mesh_id];
      cache.resize(nv * 4);

      if (rmesh.tangents.format == VertexAttributeFormat::Uint) {
        // Packed INT_2_10_10_10_REV — unpack to vec4 float
        const tangent_quantize::PackedTangent1010102 *P =
            reinterpret_cast<const tangent_quantize::PackedTangent1010102 *>(
                rmesh.tangents.data.data());
        for (size_t i = 0; i < nv; i++) {
          tangent_quantize::unpack_tangent_1010102(
              P[i], cache[i*4+0], cache[i*4+1], cache[i*4+2], cache[i*4+3]);
        }
      } else if (rmesh.tangents.format == VertexAttributeFormat::Char4) {
        // Packed SNorm8x4 — unpack to vec4 float
        const tangent_quantize::PackedTangentSNorm8x4 *P =
            reinterpret_cast<const tangent_quantize::PackedTangentSNorm8x4 *>(
                rmesh.tangents.data.data());
        for (size_t i = 0; i < nv; i++) {
          tangent_quantize::unpack_tangent_snorm8(
              P[i], cache[i*4+0], cache[i*4+1], cache[i*4+2], cache[i*4+3]);
        }
      } else if (rmesh.tangents.format == VertexAttributeFormat::Half4) {
        // Packed FP16x4 — unpack to vec4 float
        const tangent_quantize::PackedTangentFp16x4 *P =
            reinterpret_cast<const tangent_quantize::PackedTangentFp16x4 *>(
                rmesh.tangents.data.data());
        for (size_t i = 0; i < nv; i++) {
          tangent_quantize::unpack_tangent_fp16(
              P[i], cache[i*4+0], cache[i*4+1], cache[i*4+2], cache[i*4+3]);
        }
      } else if (rmesh.tangents.format == VertexAttributeFormat::Vec3 &&
                 !rmesh.normals.empty() && !rmesh.binormals.empty()) {
        // Legacy Vec3 float tangent + binormal — compute sign from cross product.
        // Normals may be packed; use unpacked cache if available.
        const float *T = reinterpret_cast<const float *>(rmesh.tangents.data.data());
        const float *N = nullptr;
        if (rmesh.normals.format == VertexAttributeFormat::Uint) {
          // Packed normals — ensure cache is populated
          auto &nc = normals3_cache_[mesh_id];
          if (nc.empty()) {
            nc.resize(nv * 3);
            const uint32_t *packed = reinterpret_cast<const uint32_t *>(
                rmesh.normals.data.data());
            for (size_t j = 0; j < nv; j++) {
              tangent_quantize::unpack_normal_1010102(
                  packed[j], nc[j*3+0], nc[j*3+1], nc[j*3+2]);
            }
          }
          N = nc.data();
        } else {
          N = reinterpret_cast<const float *>(rmesh.normals.data.data());
        }
        const float *B = reinterpret_cast<const float *>(rmesh.binormals.data.data());
        for (size_t i = 0; i < nv; i++) {
          float tx = T[i*3+0], ty = T[i*3+1], tz = T[i*3+2];
          FixupZeroTangent(tx, ty, tz, N[i*3+0], N[i*3+1], N[i*3+2]);
          cache[i*4+0] = tx;
          cache[i*4+1] = ty;
          cache[i*4+2] = tz;
          float cx = N[i*3+1]*tz - N[i*3+2]*ty;
          float cy = N[i*3+2]*tx - N[i*3+0]*tz;
          float cz = N[i*3+0]*ty - N[i*3+1]*tx;
          float d = cx*B[i*3+0] + cy*B[i*3+1] + cz*B[i*3+2];
          cache[i*4+3] = (std::isfinite(d) && d < 0.0f) ? -1.0f : 1.0f;
        }
      } else if (rmesh.tangents.format == VertexAttributeFormat::Vec3) {
        // Vec3 float tangent, no binormals — assume w=1
        const float *T = reinterpret_cast<const float *>(rmesh.tangents.data.data());
        const float *N = nullptr;
        if (!rmesh.normals.empty()) {
          if (rmesh.normals.format == VertexAttributeFormat::Uint) {
            auto &nc = normals3_cache_[mesh_id];
            if (nc.empty()) {
              nc.resize(nv * 3);
              const uint32_t *packed = reinterpret_cast<const uint32_t *>(
                  rmesh.normals.data.data());
              for (size_t j = 0; j < nv; j++) {
                tangent_quantize::unpack_normal_1010102(
                    packed[j], nc[j*3+0], nc[j*3+1], nc[j*3+2]);
              }
            }
            N = nc.data();
          } else {
            N = reinterpret_cast<const float *>(rmesh.normals.data.data());
          }
        }
        for (size_t i = 0; i < nv; i++) {
          float tx = T[i*3+0], ty = T[i*3+1], tz = T[i*3+2];
          if (N) FixupZeroTangent(tx, ty, tz, N[i*3+0], N[i*3+1], N[i*3+2]);
          cache[i*4+0] = tx;
          cache[i*4+1] = ty;
          cache[i*4+2] = tz;
          cache[i*4+3] = 1.0f;
        }
      }
      mesh.set("tangents", emscripten::typed_memory_view(cache.size(), cache.data()));

      // Also expose raw packed tangent buffer for direct WebGL2 upload
      if (rmesh.tangents.format == VertexAttributeFormat::Uint) {
        // Uint32Array for GL_INT_2_10_10_10_REV
        const uint32_t *raw = reinterpret_cast<const uint32_t *>(
            rmesh.tangents.data.data());
        mesh.set("tangentsPacked", emscripten::typed_memory_view(nv, raw));
        mesh.set("tangentsPackedFormat", emscripten::val("INT_2_10_10_10_REV"));
      }
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
    // If not authored in USD, defaults to identity matrix
    const double *geom_bind_ptr =
        reinterpret_cast<const double *>(
            rmesh.joint_and_weights.geomBindTransform.m);
    mesh.set("geomBindTransform",
             emscripten::typed_memory_view(16, geom_bind_ptr));
    // Flag indicating whether geomBindTransform was explicitly authored in USD
    // If false, the identity matrix is being used as a fallback
    mesh.set("hasGeomBindTransform", rmesh.joint_and_weights.hasGeomBindTransform);

    // Export GeomSubsets (per-face materials) as optimized submeshes
    // Reorder triangles by material so each material has exactly one contiguous group
    if (!rmesh.material_subsetMap.empty()) {
      // Step 1: Group face indices by material
      std::map<int, std::vector<int>> materialToFaces;
      size_t totalFaces = 0;

      // Track which faces are covered by GeomSubsets
      std::unordered_set<int> coveredFaces;

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

        for (int fi : faceIndices) {
          coveredFaces.insert(fi);
        }
      }

      // Include faces not covered by any GeomSubset — assign mesh-level material_id
      {
        size_t numMeshFaces = rmesh.faceVertexCounts().size();
        std::vector<int> uncoveredFaces;
        for (size_t i = 0; i < numMeshFaces; i++) {
          if (coveredFaces.find(static_cast<int>(i)) == coveredFaces.end()) {
            uncoveredFaces.push_back(static_cast<int>(i));
          }
        }
        if (!uncoveredFaces.empty()) {
          int fallbackMatId = rmesh.material_id;
          materialToFaces[fallbackMatId].insert(materialToFaces[fallbackMatId].end(),
                                                uncoveredFaces.begin(), uncoveredFaces.end());
          totalFaces += uncoveredFaces.size();
        }
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
      // Each entry in reorderMap maps a new triangle index to an old triangle index.
      // The output is facevarying (3 vertices per triangle, sequential indices).
      //
      // IMPORTANT: rmesh.points is ALWAYS per-vertex (shared vertices with index
      // buffer). When is_single_indexable, normals/texcoords/tangents are also
      // per-vertex. When NOT single_indexable, they may be facevarying.
      // Per-vertex attributes must be looked up via faceVertexIndices[triIdx*3+v],
      // while facevarying attributes are accessed directly at triIdx*3+v.
      size_t numNewTriangles = reorderMap.size();
      const auto& fvIndices = rmesh.faceVertexIndices();
      const bool singleIndexable = rmesh.is_single_indexable;

      // Reorder points (vec3) — ALWAYS per-vertex, must go through index buffer
      if (!rmesh.points.empty()) {
        std::vector<float> reorderedPoints(numNewTriangles * 3 * 3);  // numTris * 3 verts * 3 components
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {  // 3 vertices per triangle
            size_t oldFaceVertIdx = static_cast<size_t>(oldTriIdx) * 3 + static_cast<size_t>(v);
            size_t newVertIdx = newTriIdx * 3 + static_cast<size_t>(v);
            if (oldFaceVertIdx < fvIndices.size()) {
              uint32_t vertIdx = fvIndices[oldFaceVertIdx];
              if (vertIdx < rmesh.points.size()) {
                reorderedPoints[newVertIdx * 3 + 0] = rmesh.points[vertIdx][0];
                reorderedPoints[newVertIdx * 3 + 1] = rmesh.points[vertIdx][1];
                reorderedPoints[newVertIdx * 3 + 2] = rmesh.points[vertIdx][2];
              }
            }
          }
        }
        // Store in cache and update mesh pointer
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.points = std::move(reorderedPoints);
        mesh.set("points", emscripten::typed_memory_view(cache.points.size(), cache.points.data()));
      }

      // Reorder normals - per-vertex if single_indexable, facevarying otherwise
      // Handles SNorm8x3 (Char3), SNorm16x3 (Short3), and float3 (Vec3) formats.
      if (!rmesh.normals.empty()) {
        using tinyusdz::tydra::VertexAttributeFormat;
        const bool isSnorm8 = (rmesh.normals.format == VertexAttributeFormat::Char3);
        const bool isSnorm16 = (rmesh.normals.format == VertexAttributeFormat::Short3);
        const size_t totalVerts = numNewTriangles * 3;

        if (isSnorm16) {
          const int16_t* src = reinterpret_cast<const int16_t*>(rmesh.normals.data.data());
          std::vector<int16_t> reordered(totalVerts * 3, 0);
          for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
            int oldTriIdx = reorderMap[newTriIdx];
            for (int v = 0; v < 3; v++) {
              size_t oldFV = size_t(oldTriIdx) * 3 + size_t(v);
              size_t newV = newTriIdx * 3 + size_t(v);
              uint32_t vi = singleIndexable
                  ? (oldFV < fvIndices.size() ? fvIndices[oldFV] : 0)
                  : uint32_t(oldFV);
              if (vi < rmesh.normals.vertex_count()) {
                reordered[newV*3+0] = src[vi*3+0];
                reordered[newV*3+1] = src[vi*3+1];
                reordered[newV*3+2] = src[vi*3+2];
              }
            }
          }
          auto& cache = reordered_mesh_cache_[mesh_id];
          cache.normals_i16 = std::move(reordered);
          mesh.set("normals", emscripten::typed_memory_view(
              cache.normals_i16.size(), cache.normals_i16.data()));
          mesh.set("normalsFormat", std::string("snorm16"));
        } else if (isSnorm8) {
          const int8_t* src = reinterpret_cast<const int8_t*>(rmesh.normals.data.data());
          std::vector<int8_t> reordered(totalVerts * 3, 0);
          for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
            int oldTriIdx = reorderMap[newTriIdx];
            for (int v = 0; v < 3; v++) {
              size_t oldFV = size_t(oldTriIdx) * 3 + size_t(v);
              size_t newV = newTriIdx * 3 + size_t(v);
              uint32_t vi = singleIndexable
                  ? (oldFV < fvIndices.size() ? fvIndices[oldFV] : 0)
                  : uint32_t(oldFV);
              if (vi < rmesh.normals.vertex_count()) {
                reordered[newV*3+0] = src[vi*3+0];
                reordered[newV*3+1] = src[vi*3+1];
                reordered[newV*3+2] = src[vi*3+2];
              }
            }
          }
          auto& cache = reordered_mesh_cache_[mesh_id];
          cache.normals_i8 = std::move(reordered);
          mesh.set("normals", emscripten::typed_memory_view(
              cache.normals_i8.size(), cache.normals_i8.data()));
          mesh.set("normalsFormat", std::string("snorm8"));
        } else {
          // Float3 (Vec3) or unpacked from 1010102
          const float* src;
          if (rmesh.normals.format == VertexAttributeFormat::Uint) {
            // 1010102 was already unpacked to normals_cache_ by the primary export above
            if (normals_cache_.count(mesh_id)) {
              src = normals_cache_[mesh_id].data();
            } else {
              src = reinterpret_cast<const float*>(rmesh.normals.data.data());
            }
          } else {
            src = reinterpret_cast<const float*>(rmesh.normals.data.data());
          }
          std::vector<float> reordered(totalVerts * 3, 0.0f);
          for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
            int oldTriIdx = reorderMap[newTriIdx];
            for (int v = 0; v < 3; v++) {
              size_t oldFV = size_t(oldTriIdx) * 3 + size_t(v);
              size_t newV = newTriIdx * 3 + size_t(v);
              uint32_t vi = singleIndexable
                  ? (oldFV < fvIndices.size() ? fvIndices[oldFV] : 0)
                  : uint32_t(oldFV);
              if (vi < rmesh.normals.vertex_count()) {
                reordered[newV*3+0] = src[vi*3+0];
                reordered[newV*3+1] = src[vi*3+1];
                reordered[newV*3+2] = src[vi*3+2];
              }
            }
          }
          auto& cache = reordered_mesh_cache_[mesh_id];
          cache.normals = std::move(reordered);
          mesh.set("normals", emscripten::typed_memory_view(
              cache.normals.size(), cache.normals.data()));
          mesh.set("normalsFormat", std::string("float32"));
        }
      }

      // Reorder texcoords (vec2) - slot 0; per-vertex if single_indexable
      if (rmesh.texcoords.count(0) && !rmesh.texcoords.at(0).data.empty()) {
        const auto& uvData = rmesh.texcoords.at(0);
        const float* uvDataPtr = reinterpret_cast<const float*>(uvData.data.data());
        std::vector<float> reorderedTexcoords(numNewTriangles * 3 * 2);
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {
            size_t oldFaceVertIdx = static_cast<size_t>(oldTriIdx) * 3 + static_cast<size_t>(v);
            size_t newVertIdx = newTriIdx * 3 + static_cast<size_t>(v);
            if (singleIndexable) {
              if (oldFaceVertIdx < fvIndices.size()) {
                uint32_t vertIdx = fvIndices[oldFaceVertIdx];
                if (vertIdx < uvData.vertex_count()) {
                  reorderedTexcoords[newVertIdx * 2 + 0] = uvDataPtr[vertIdx * 2 + 0];
                  reorderedTexcoords[newVertIdx * 2 + 1] = uvDataPtr[vertIdx * 2 + 1];
                }
              }
            } else {
              if (oldFaceVertIdx < uvData.vertex_count()) {
                reorderedTexcoords[newVertIdx * 2 + 0] = uvDataPtr[oldFaceVertIdx * 2 + 0];
                reorderedTexcoords[newVertIdx * 2 + 1] = uvDataPtr[oldFaceVertIdx * 2 + 1];
              }
            }
          }
        }
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.texcoords = std::move(reorderedTexcoords);
        mesh.set("texcoords", emscripten::typed_memory_view(cache.texcoords.size(), cache.texcoords.data()));
      }

      // Reorder tangents as vec4 — use tangents4_cache_ (already unpacked from any
      // packed format by the non-reorder tangent export path above).
      if (tangents4_cache_.count(mesh_id) && !tangents4_cache_[mesh_id].empty()) {
        const float* t4 = tangents4_cache_[mesh_id].data();
        size_t tangentVertCount = tangents4_cache_[mesh_id].size() / 4;
        std::vector<float> reorderedTangents(numNewTriangles * 3 * 4);
        for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
          int oldTriIdx = reorderMap[newTriIdx];
          for (int v = 0; v < 3; v++) {
            size_t oldFV = size_t(oldTriIdx) * 3 + size_t(v);
            size_t newV = newTriIdx * 3 + size_t(v);
            uint32_t vi = singleIndexable
                ? (oldFV < fvIndices.size() ? fvIndices[oldFV] : 0)
                : uint32_t(oldFV);
            if (vi < tangentVertCount) {
              reorderedTangents[newV*4+0] = t4[vi*4+0];
              reorderedTangents[newV*4+1] = t4[vi*4+1];
              reorderedTangents[newV*4+2] = t4[vi*4+2];
              reorderedTangents[newV*4+3] = t4[vi*4+3];
            } else {
              reorderedTangents[newV*4+3] = 1.0f;  // default w=1
            }
          }
        }
        auto& cache = reordered_mesh_cache_[mesh_id];
        cache.tangents = std::move(reorderedTangents);
        mesh.set("tangents", emscripten::typed_memory_view(cache.tangents.size(), cache.tangents.data()));
      }

      // Reorder vertex skinning data. Joint indices/weights are authored per
      // original mesh point, while this path expands points to one vertex per
      // triangle corner for material grouping.
      if (!rmesh.joint_and_weights.jointIndices.empty() &&
          !rmesh.joint_and_weights.jointWeights.empty() &&
          rmesh.joint_and_weights.elementSize > 0) {
        const int elementSize = rmesh.joint_and_weights.elementSize;
        const size_t skinIndexCount = rmesh.joint_and_weights.jointIndices.size();
        const size_t skinWeightCount = rmesh.joint_and_weights.jointWeights.size();
        const size_t sourceSkinVertexCount =
            std::min(skinIndexCount, skinWeightCount) / size_t(elementSize);
        const bool skinIsPerPoint = sourceSkinVertexCount == rmesh.points.size();
        const bool skinIsFaceVarying = sourceSkinVertexCount == fvIndices.size();
        const size_t totalVerts = numNewTriangles * 3;

        if (skinIsPerPoint || skinIsFaceVarying) {
          auto& cache = reordered_mesh_cache_[mesh_id];
          cache.jointIndices.assign(totalVerts * size_t(elementSize), 0);
          cache.jointWeights.assign(totalVerts * size_t(elementSize), 0.0f);

          for (size_t newTriIdx = 0; newTriIdx < numNewTriangles; newTriIdx++) {
            int oldTriIdx = reorderMap[newTriIdx];
            for (int v = 0; v < 3; v++) {
              size_t oldFV = size_t(oldTriIdx) * 3 + size_t(v);
              size_t newV = newTriIdx * 3 + size_t(v);
              size_t srcVertex = oldFV;
              if (skinIsPerPoint) {
                if (oldFV >= fvIndices.size()) {
                  continue;
                }
                srcVertex = size_t(fvIndices[oldFV]);
              }
              if (srcVertex >= sourceSkinVertexCount) {
                continue;
              }

              const size_t srcBase = srcVertex * size_t(elementSize);
              const size_t dstBase = newV * size_t(elementSize);
              for (int j = 0; j < elementSize; j++) {
                const size_t srcIdx = srcBase + size_t(j);
                const size_t dstIdx = dstBase + size_t(j);
                if (srcIdx < skinIndexCount && srcIdx < skinWeightCount) {
                  cache.jointIndices[dstIdx] =
                      rmesh.joint_and_weights.jointIndices[srcIdx];
                  cache.jointWeights[dstIdx] =
                      rmesh.joint_and_weights.jointWeights[srcIdx];
                }
              }
            }
          }

          mesh.set("jointIndices", emscripten::typed_memory_view(
              cache.jointIndices.size(), cache.jointIndices.data()));
          mesh.set("jointWeights", emscripten::typed_memory_view(
              cache.jointWeights.size(), cache.jointWeights.data()));
        }
      }

      // Generate new sequential indices (0, 1, 2, 3, 4, 5, ...)
      // Since we reordered the vertex data to facevarying, indices are sequential
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

    if ((idx < 0) || (idx >= static_cast<int>(render_scene_.nodes.size()))) {
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

    if (anim_id < 0 || static_cast<size_t>(anim_id) >= render_scene_.animations.size()) {
      return anim;
    }

    const auto &clip = render_scene_.animations[size_t(anim_id)];

    // Basic animation metadata
    anim.set("name", clip.name.empty() ? "Animation" + std::to_string(anim_id) : clip.name);
    anim.set("primName", clip.prim_name);
    anim.set("absPath", clip.abs_path);
    anim.set("displayName", clip.display_name);
    anim.set("duration", clip.duration);

    // Source type metadata
    {
      std::string sourceTypeStr = "Unknown";
      switch (clip.source_type) {
        case tinyusdz::tydra::AnimationSourceType::XformOp: sourceTypeStr = "XformOp"; break;
        case tinyusdz::tydra::AnimationSourceType::SkelAnimation: sourceTypeStr = "SkelAnimation"; break;
        case tinyusdz::tydra::AnimationSourceType::BlendShape: sourceTypeStr = "BlendShape"; break;
        default: break;
      }
    anim.set("sourceType", sourceTypeStr);
      anim.set("numAnimatedJoints", clip.num_animated_joints);
      anim.set("numAnimatedNodes", clip.num_animated_nodes);
      anim.set("hasValueClip", clip.has_value_clip);
      anim.set("valueClipBaked", clip.value_clip_baked);
      anim.set("valueClipStartTime", clip.value_clip_start_time);
      anim.set("valueClipEndTime", clip.value_clip_end_time);
      anim.set("valueClipSampleRate", clip.value_clip_sample_rate);
      emscripten::val clipAssetPaths = emscripten::val::array();
      for (const auto &path : clip.clip_asset_paths) {
        clipAssetPaths.call<void>("push", path);
      }
      anim.set("clipAssetPaths", clipAssetPaths);
    }

    // Convert samplers to Three.js KeyframeTrack format
    emscripten::val tracks = emscripten::val::array();

    for (const auto &channel : clip.channels) {
      if (!channel.is_valid() || channel.sampler >= static_cast<int32_t>(clip.samplers.size())) {
        continue;
      }

      const auto &sampler = clip.samplers[channel.sampler];
      if (sampler.empty()) {
        continue;
      }

      emscripten::val track = emscripten::val::object();

      // Set track name based on target node and property
      if (channel.target_node >= 0 && channel.target_node < static_cast<int32_t>(render_scene_.nodes.size())) {
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
          case tinyusdz::tydra::AnimationPath::CustomProperty: {
            std::string type = "number";
            if (!sampler.times.empty() && !sampler.values.empty() &&
                (sampler.values.size() % sampler.times.size() == 0u)) {
              const size_t comp_count = sampler.values.size() / sampler.times.size();
              if (comp_count == 2) {
                type = "vector2";
              } else if (comp_count == 3) {
                type = "vector3";
              } else if (comp_count == 4) {
                type = "vector4";
              }
            }
            trackName += "." + (channel.property_name.empty() ? "value" : channel.property_name);
            track.set("type", type);
            break;
          }
        }

        track.set("name", trackName);
        track.set("isCustomProperty", channel.is_custom_property);
        if (channel.is_custom_property) {
          track.set("propertyName", channel.property_name);
        }
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
        case tinyusdz::tydra::AnimationPath::CustomProperty:
          pathStr = "custom";
          break;
        default:
          pathStr = "unknown";
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
        case tinyusdz::tydra::AnimationPath::CustomProperty:
          pathStr = "CustomProperty";
          break;
        default:
          pathStr = "Unknown";
          break;
      }
      ch.set("path", pathStr);
      ch.set("isCustomProperty", channel.is_custom_property);
      if (channel.is_custom_property && !channel.property_name.empty()) {
        ch.set("propertyName", channel.property_name);
      }

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

    for (int i = 0; i < static_cast<int>(render_scene_.animations.size()); ++i) {
      animations.call<void>("push", getAnimation(i));
    }

    return animations;
  }

  // Get animation summary info without full data (useful for listing)
  emscripten::val getAnimationInfo(int anim_id) const {
    emscripten::val info = emscripten::val::object();

    if (!loaded_ || anim_id >= static_cast<int>(render_scene_.animations.size())) {
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

    // Source type metadata
    {
      std::string sourceTypeStr = "Unknown";
      switch (clip.source_type) {
        case tinyusdz::tydra::AnimationSourceType::XformOp: sourceTypeStr = "XformOp"; break;
        case tinyusdz::tydra::AnimationSourceType::SkelAnimation: sourceTypeStr = "SkelAnimation"; break;
        case tinyusdz::tydra::AnimationSourceType::BlendShape: sourceTypeStr = "BlendShape"; break;
        default: break;
      }
      info.set("sourceType", sourceTypeStr);
      info.set("numAnimatedJoints", clip.num_animated_joints);
      info.set("numAnimatedNodes", clip.num_animated_nodes);
      info.set("hasValueClip", clip.has_value_clip);
      info.set("valueClipBaked", clip.value_clip_baked);
      info.set("valueClipStartTime", clip.value_clip_start_time);
      info.set("valueClipEndTime", clip.value_clip_end_time);
      info.set("valueClipSampleRate", clip.value_clip_sample_rate);
      emscripten::val infoClipAssetPaths = emscripten::val::array();
      for (const auto &path : clip.clip_asset_paths) {
        infoClipAssetPaths.call<void>("push", path);
      }
      info.set("clipAssetPaths", infoClipAssetPaths);
      info.set("numClipAssetPaths", int(clip.clip_asset_paths.size()));
    }

    return info;
  }

  // Get all animation summaries
  emscripten::val getAllAnimationInfos() const {
    emscripten::val infos = emscripten::val::array();

    if (!loaded_) {
      return infos;
    }

    for (int i = 0; i < static_cast<int>(render_scene_.animations.size()); ++i) {
      infos.call<void>("push", getAnimationInfo(i));
    }

    return infos;
  }

  // ========================================================================
  // Skeleton hierarchy methods
  // ========================================================================

  int numSkeletons() const {
    if (!loaded_) return 0;
    return static_cast<int>(render_scene_.skeletons.size());
  }

  // Convert SkelNode to JS object recursively
  emscripten::val skelNodeToJS(const tinyusdz::tydra::SkelNode& node) const {
    emscripten::val obj = emscripten::val::object();

    obj.set("joint_path", node.joint_path);
    obj.set("joint_name", node.joint_name);
    obj.set("joint_id", node.joint_id);

    // Export bind and rest transforms - must copy data, not use typed_memory_view
    // (typed_memory_view would point to stack memory that becomes invalid)
    std::array<double, 16> bind_mat = detail::toArray(node.bind_transform);
    std::array<double, 16> rest_mat = detail::toArray(node.rest_transform);

    emscripten::val bind_arr = emscripten::val::array();
    emscripten::val rest_arr = emscripten::val::array();
    for (int i = 0; i < 16; i++) {
      bind_arr.call<void>("push", bind_mat[i]);
      rest_arr.call<void>("push", rest_mat[i]);
    }
    obj.set("bind_transform", bind_arr);
    obj.set("rest_transform", rest_arr);

    // Recursively convert children
    emscripten::val children = emscripten::val::array();
    for (const auto& child : node.children) {
      children.call<void>("push", skelNodeToJS(child));
    }
    obj.set("children", children);

    return obj;
  }

  emscripten::val getSkeleton(int skel_id) const {
    emscripten::val result = emscripten::val::object();

    if (!loaded_) {
      result.set("error", "Scene not loaded");
      return result;
    }

    if (skel_id < 0 || skel_id >= static_cast<int>(render_scene_.skeletons.size())) {
      result.set("error", "Invalid skeleton ID");
      return result;
    }

    const auto& skel = render_scene_.skeletons[skel_id];

    result.set("id", skel_id);
    result.set("prim_name", skel.prim_name);
    result.set("abs_path", skel.abs_path);
    result.set("display_name", skel.display_name);
    result.set("anim_id", skel.anim_id);

    // Convert root node and hierarchy
    result.set("root_node", skelNodeToJS(skel.root_node));

    return result;
  }

  emscripten::val getAllSkeletons() const {
    emscripten::val skeletons = emscripten::val::array();

    if (!loaded_) {
      return skeletons;
    }

    for (int i = 0; i < static_cast<int>(render_scene_.skeletons.size()); ++i) {
      skeletons.call<void>("push", getSkeleton(i));
    }

    return skeletons;
  }

  // Get skeleton joints as flat array (useful for Three.js)
  emscripten::val getSkeletonJointsFlat(int skel_id) const {
    emscripten::val result = emscripten::val::object();

    if (!loaded_) {
      result.set("error", "Scene not loaded");
      return result;
    }

    if (skel_id < 0 || skel_id >= static_cast<int>(render_scene_.skeletons.size())) {
      result.set("error", "Invalid skeleton ID");
      return result;
    }

    const auto& skel = render_scene_.skeletons[skel_id];

    // Flatten skeleton hierarchy into arrays
    std::vector<std::string> joint_names;
    std::vector<std::string> joint_paths;
    std::vector<int> joint_ids;
    std::vector<int> parent_indices;
    std::vector<double> bind_matrices;
    std::vector<double> rest_matrices;

    // Recursive function to traverse skeleton hierarchy
    std::function<void(const tinyusdz::tydra::SkelNode&, int)> traverseNode;
    traverseNode = [&](const tinyusdz::tydra::SkelNode& node, int parent_idx) {
      int current_idx = static_cast<int>(joint_names.size());

      joint_names.push_back(node.joint_name);
      joint_paths.push_back(node.joint_path);
      joint_ids.push_back(node.joint_id);
      parent_indices.push_back(parent_idx);

      // Add bind transform (16 doubles)
      const auto& bind = node.bind_transform;
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
          bind_matrices.push_back(bind.m[row][col]);
        }
      }

      // Add rest transform (16 doubles)
      const auto& rest = node.rest_transform;
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
          rest_matrices.push_back(rest.m[row][col]);
        }
      }

      // Traverse children
      for (const auto& child : node.children) {
        traverseNode(child, current_idx);
      }
    };

    // Start traversal from root (root has no parent, so parent_idx = -1)
    traverseNode(skel.root_node, -1);

    // Convert to JS arrays
    emscripten::val js_joint_names = emscripten::val::array();
    for (const auto& name : joint_names) {
      js_joint_names.call<void>("push", name);
    }

    emscripten::val js_joint_paths = emscripten::val::array();
    for (const auto& path : joint_paths) {
      js_joint_paths.call<void>("push", path);
    }

    result.set("joint_names", js_joint_names);
    result.set("joint_paths", js_joint_paths);
    // Use JS arrays (not typed_memory_view) to avoid use-after-free when
    // local vectors are destroyed on function return.
    emscripten::val js_joint_ids = emscripten::val::array();
    for (auto id : joint_ids) js_joint_ids.call<void>("push", id);
    result.set("joint_ids", js_joint_ids);

    emscripten::val js_parent_indices = emscripten::val::array();
    for (auto idx : parent_indices) js_parent_indices.call<void>("push", idx);
    result.set("parent_indices", js_parent_indices);

    emscripten::val js_bind_matrices = emscripten::val::array();
    for (auto m : bind_matrices) js_bind_matrices.call<void>("push", m);
    result.set("bind_matrices", js_bind_matrices);

    emscripten::val js_rest_matrices = emscripten::val::array();
    for (auto m : rest_matrices) js_rest_matrices.call<void>("push", m);
    result.set("rest_matrices", js_rest_matrices);
    result.set("num_joints", static_cast<int>(joint_names.size()));

    return result;
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

  // Sphere tessellation
  void setSphereSubdivisions(int subdivisions) {
    if (subdivisions >= 0 && subdivisions <= 6) {
      sphere_subdivisions_ = subdivisions;
    }
  }

  int getSphereSubdivisions() const {
    return sphere_subdivisions_;
  }

  // Bone reduction configuration
  void setEnableBoneReduction(bool enabled) {
    enable_bone_reduction_ = enabled;
  }

  bool getEnableBoneReduction() const {
    return enable_bone_reduction_;
  }

  void setEnableValueClips(bool enabled) {
    enable_value_clips_ = enabled;
  }

  bool getEnableValueClips() const {
    return enable_value_clips_;
  }

  void setValueClipSampleRate(float sample_rate) {
    value_clip_sample_rate_ = sample_rate;
  }

  float getValueClipSampleRate() const {
    return value_clip_sample_rate_;
  }

  void setValueClipUseTimeRange(bool enabled) {
    value_clip_use_time_range_ = enabled;
  }

  bool getValueClipUseTimeRange() const {
    return value_clip_use_time_range_;
  }

  void setValueClipTimeRange(double start_time, double end_time) {
    value_clip_start_time_ = start_time;
    value_clip_end_time_ = end_time;
  }

  double getValueClipStartTime() const {
    return value_clip_start_time_;
  }

  double getValueClipEndTime() const {
    return value_clip_end_time_;
  }

  void setTargetBoneCount(uint32_t count) {
    if (count > 0 && count <= 128) {  // Sanity check: 1-128 bones
      target_bone_count_ = count;
    }
  }

  uint32_t getTargetBoneCount() const {
    return target_bone_count_;
  }

  void setRoundBoneCount(bool enabled) {
    round_bone_count_ = enabled;
  }

  bool getRoundBoneCount() const {
    return round_bone_count_;
  }

  // Deferred tangent computation
  void setDeferTangentComputation(bool enabled) {
    defer_tangent_computation_ = enabled;
  }

  bool getDeferTangentComputation() const {
    return defer_tangent_computation_;
  }

  // UDIM: combine tiles into a single atlas (true, default) or keep them
  // sparse for per-tile editing (false).
  void setCombineUDIMTiles(bool enabled) {
    combineUDIMTiles_ = enabled;
  }

  bool getCombineUDIMTiles() const {
    return combineUDIMTiles_;
  }

  // Allow parent-directory ('..') segments in composition asset paths
  // (references/payloads/sublayers). Resolution of the surviving '..' is
  // delegated to the (sandboxed) EM asset resolver, so this is safe in the
  // browser, where USD's legitimate `../foo.usd` references must work. Default
  // on for the WASM build (FILESYSTEM=0 — there is no real filesystem to escape).
  void setAllowParentRelativeAssetPaths(bool enabled) {
    allow_parent_relative_asset_paths_ = enabled;
  }

  bool getAllowParentRelativeAssetPaths() const {
    return allow_parent_relative_asset_paths_;
  }

  // MMap zero-copy configuration
  void setMMapZeroCopy(bool enabled) {
    mmap_zero_copy_ = enabled;
  }

  bool getMMapZeroCopy() const {
    return mmap_zero_copy_;
  }

  // Compute tangents for a specific mesh on demand (lazy tangent computation).
  // Returns true on success. Call this before accessing tangent data for meshes
  // that had tangent computation deferred.
  bool computeMeshTangents(int mesh_index) {
    if (mesh_index < 0 || mesh_index >= static_cast<int>(render_scene_.meshes.size())) {
      return false;
    }

    auto &mesh = render_scene_.meshes[size_t(mesh_index)];
    if (!mesh.tangent_computation_deferred) {
      // Already computed or not deferred
      return true;
    }

    std::string err;
    // Use Lengyel (default) for deferred computation — fast and lightweight for WASM.
    // Use Packed1010102 for WASM (WebGL2 native, 4 bytes/vertex).
    bool ok = tinyusdz::tydra::RenderSceneConverter::ComputeDeferredTangents(
        &mesh,
        tinyusdz::tydra::MeshConverterConfig::TangentComputationMethod::Lengyel,
        tinyusdz::tydra::MeshConverterConfig::TangentStorageFormat::Packed1010102,
        &err);
    if (!ok) {
      std::cerr << "computeMeshTangents failed for mesh " << mesh_index << ": " << err << "\n";
    }

    // Invalidate caches for this mesh since we just computed new data
    tangents4_cache_.erase(mesh_index);
    normals_cache_.erase(mesh_index);
    reordered_mesh_cache_.erase(mesh_index);

    return ok;
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

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    std::vector<std::string> paths = tinyusdz::ExtractSublayerAssetPaths(curr);
    for (size_t i = 0; i < paths.size(); i++) {
     arr.call<void>("push", paths[i]);
    }

    return arr;
  }

  emscripten::val extractReferencesAssetPaths() {
    emscripten::val arr = emscripten::val::array();

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    std::vector<std::string> paths = tinyusdz::ExtractReferencesAssetPaths(curr);
    for (size_t i = 0; i < paths.size(); i++) {
     arr.call<void>("push", paths[i]);
    }

    return arr;
  }

  emscripten::val extractPayloadAssetPaths() {
    emscripten::val arr = emscripten::val::array();

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    std::vector<std::string> paths = tinyusdz::ExtractPayloadAssetPaths(curr);
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

    tinyusdz::SublayersCompositionOptions sublayer_options;
    sublayer_options.allow_parent_relative_paths = allow_parent_relative_asset_paths_;
    if (!tinyusdz::CompositeSublayers(resolver, layer_, &composed_layer_, &warn_, &error_, sublayer_options)) {
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

    tinyusdz::ReferencesCompositionOptions references_options;
    references_options.allow_parent_relative_paths = allow_parent_relative_asset_paths_;
    if (!tinyusdz::CompositeReferences(resolver, layer_, &composed_layer_, &warn_, &error_, references_options)) {
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
    return tinyusdz::HasPayload(composited_ ? composed_layer_ : layer_, /* force_check */true);
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

    tinyusdz::PayloadCompositionOptions payload_options;
    payload_options.allow_parent_relative_paths = allow_parent_relative_asset_paths_;
    if (!tinyusdz::CompositePayload(resolver, layer_, &composed_layer_, &warn_, &error_, payload_options)) {
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

  std::string validateLoadedLayer(const std::string &options_json) const {
    nlohmann::json result;
    if (!loaded_ || !loaded_as_layer_) {
      result["parse_ok"] = false;
      result["ok"] = false;
      result["error"] = "No Layer is loaded. Use loadAsLayerFromBinary first.";
      return result.dump();
    }

    const tinyusdz::ValidationOptions options =
        ParseValidationOptionsJSONForWeb(options_json);
    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    result = ValidationResultToJSON(
        tinyusdz::ValidateLayerAgainstAOUSDCore(curr, options));
    if (!warn_.empty()) {
      result["warn"] = warn_;
    }
    return result.dump();
  }

  std::string validateFromBinary(const std::string &binary,
                                 const std::string &filename,
                                 const std::string &options_json) {
    warn_.clear();
    error_.clear();

    tinyusdz::USDLoadOptions load_options;
    load_options.max_memory_limit_in_mb = max_memory_limit_mb_;

    nlohmann::json result;
    const tinyusdz::ValidationOptions options =
        ParseValidationOptionsJSONForWeb(options_json);
    tinyusdz::USDValidationResult validation;
    const bool loaded = tinyusdz::ValidateUSDFromMemoryAgainstAOUSDCore(
        reinterpret_cast<const uint8_t *>(binary.c_str()), binary.size(),
        filename, options, load_options, &validation, &warn_, &error_);
    if (!loaded) {
      result["parse_ok"] = false;
      result["ok"] = false;
      result["error"] = error_;
      if (!warn_.empty()) {
        result["warn"] = warn_;
      }
      return result.dump();
    }

    result = ValidationResultToJSON(validation);
    if (!warn_.empty()) {
      result["warn"] = warn_;
    }
    return result.dump();
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
    normals_cache_.clear();

    // Reset parsing progress
    parsing_progress_.reset();

    // Clear export state
    export_stage_ = tinyusdz::Stage();
    has_stage_ = false;
    // (USDC export no longer retains a wasm-side buffer; it copies straight to a
    // JS-owned Uint8Array — see toOwnedUint8Array().)
    usdz_export_buf_.clear();
    image_export_buf_.clear();
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

    // Asset cache
    stats.set("assetCacheCount", static_cast<int>(em_resolver_.cache.size()));
    stats.set("assetCacheSizeBytes", static_cast<double>(em_resolver_.getCacheSizeBytes()));
    stats.set("assetCacheMaxBytes", static_cast<double>(em_resolver_.getMaxCacheSizeBytes()));

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
  emscripten::val allocateZeroCopyBuffer(const std::string &name, size_t size,
                                         size_t max_bytes) {
    return em_resolver_.allocateZeroCopyBuffer(name, size, max_bytes);
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

  // Returns { name, data, sha256, uuid }. `data` is a JS-owned *copy* of the
  // asset bytes. We intentionally copy rather than return a
  // typed_memory_view into the cached std::string: such a view would dangle
  // (use-after-free in JS) if the asset is later evicted or deleted. Callers
  // that want a zero-copy view and that manage lifetime themselves can use
  // getAssetCacheDataAsMemoryView().
  emscripten::val getAsset(const std::string &name) const {
    emscripten::val val = emscripten::val::object();
    if (em_resolver_.has(name)) {
      const AssetCacheEntry &entry = em_resolver_.get(name);
      val.set("name", name);
      emscripten::val u8 =
          emscripten::val::global("Uint8Array").new_(emscripten::val(static_cast<double>(entry.binary.size())));
      u8.call<void>("set",
                    emscripten::val(emscripten::typed_memory_view(
                        entry.binary.size(),
                        reinterpret_cast<const uint8_t *>(entry.binary.data()))));
      val.set("data", u8);
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

  // Get asset by UUID instead of name. Like getAsset(), `data` is a JS-owned
  // *copy* to avoid a dangling view after eviction/deletion.
  emscripten::val getAssetByUUID(const std::string &uuid) const {
    emscripten::val val = emscripten::val::object();

    if (!em_resolver_.hasByUUID(uuid)) {
      val.set("error", "Asset not found with UUID: " + uuid);
      return val;
    }

    const AssetCacheEntry &entry = em_resolver_.getByUUID(uuid);
    const std::string name = em_resolver_.findAssetByUUID(uuid);

    val.set("name", name);
    emscripten::val u8 =
        emscripten::val::global("Uint8Array").new_(emscripten::val(static_cast<double>(entry.binary.size())));
    u8.call<void>("set",
                  emscripten::val(emscripten::typed_memory_view(
                      entry.binary.size(),
                      reinterpret_cast<const uint8_t *>(entry.binary.data()))));
    val.set("data", u8);
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

  // Cache size management
  size_t getAssetCacheSizeBytes() const {
    return em_resolver_.getCacheSizeBytes();
  }

  void setAssetCacheMaxSizeBytes(size_t max_bytes) {
    em_resolver_.setMaxCacheSizeBytes(max_bytes);
  }

  size_t getAssetCacheMaxSizeBytes() const {
    return em_resolver_.getMaxCacheSizeBytes();
  }

  // Check if asset exists (by name or UUID)
  bool assetExists(const std::string &nameOrUuid) const {
    return em_resolver_.has(nameOrUuid) || em_resolver_.hasByUUID(nameOrUuid);
  }

  // Explicit zero-copy view into the cached bytes. See the warning on
  // EMAssetResolutionResolver::getCacheDataAsMemoryView(): the returned
  // Uint8Array dangles after the asset is evicted/deleted. Prefer getAsset().
  emscripten::val getAssetCacheDataAsMemoryView(const std::string &name) const {
    return em_resolver_.getCacheDataAsMemoryView(name);
  }

  bool setAssetFromRawPointer(const std::string &name, uintptr_t dataPtr, size_t size) {
    return em_resolver_.addFromRawPointer(name, dataPtr, size);
  }

  emscripten::val extractUnresolvedTexturePaths() const {
    // Must be an Array: a default-constructed val is `undefined`, on which
    // `.push()` throws. Call this AFTER layerToRenderScene()/loadFromBinary().
    emscripten::val val = emscripten::val::array();

    for (const tinyusdz::tydra::TextureImage &texImg : render_scene_.images) {
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

    // Per-session context (see note in mcpToolsCall). Guarded above.
    tinyusdz::tydra::mcp::Context &ctx = mcp_ctx_.at(mcp_session_id_);

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

    nlohmann::json j_args;
    try {
      j_args = nlohmann::json::parse(args);
    } catch (const std::exception& e) {
      return std::string("{\"error\": \"Invalid JSON: ") + e.what() + "\"}";
    }

    // Per-session context: isolated so one session cannot read/overwrite
    // another session's assets/layers/screenshots. Guarded by the
    // mcp_ctx_.count(mcp_session_id_) check above, so .at() never throws.
    auto &ctx = mcp_ctx_.at(mcp_session_id_);

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

    // Per-session context: isolated so one session cannot read/overwrite
    // another session's assets/layers/screenshots. Guarded by the
    // mcp_ctx_.count(mcp_session_id_) check above, so .at() never throws.
    auto &ctx = mcp_ctx_.at(mcp_session_id_);

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

    // Per-session context: isolated so one session cannot read/overwrite
    // another session's assets/layers/screenshots. Guarded by the
    // mcp_ctx_.count(mcp_session_id_) check above, so .at() never throws.
    auto &ctx = mcp_ctx_.at(mcp_session_id_);

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
    
    tinyusdz::USDToJSONContext context;
    tinyusdz::minijson::Value json_obj = tinyusdz::ToJSONValue(curr, context);
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

  // =========================================================================
  // USD Export Methods
  // =========================================================================

  /// Helper: return a detach-safe, JS-owned Uint8Array COPY of `bytes`.
  ///
  /// A bare `typed_memory_view` aliases the WASM heap's ArrayBuffer; if the
  /// caller holds it across any later embind call that grows the heap (e.g.
  /// `delete()` then `new TinyUSDZLoaderNative()`), the view's ArrayBuffer is
  /// detached and reads return garbage. Buffer exporters whose result a caller
  /// may retain must therefore hand back an independent JS-owned copy. Same
  /// idiom as getAsset()/getAssetByUUID().
  static emscripten::val toOwnedUint8Array(const std::vector<uint8_t> &bytes) {
    // Pass the length as a double (JS Number), not size_t: under wasm64
    // (MEMORY64) size_t marshals to a BigInt and `new Uint8Array(bigint)`
    // throws "Cannot convert a BigInt value to a number". double is exact for
    // these sizes and works on both wasm32 and wasm64.
    emscripten::val u8 = emscripten::val::global("Uint8Array").new_(
        static_cast<double>(bytes.size()));
    if (!bytes.empty()) {
      u8.call<void>("set", emscripten::val(emscripten::typed_memory_view(
                               bytes.size(), bytes.data())));
    }
    return u8;
  }

  // ============================================================
  // next: low-memory lazy-ValueRep flatten pipeline
  // ============================================================

  /// Flatten a USDC buffer via the next lazy pipeline: numeric arrays are kept
  /// as lazy references into a single moved-in source buffer, composed
  /// structurally (no array copy), and written back by copying unchanged
  /// compressed blocks verbatim. This avoids the eager path's 5-10x heap
  /// blow-up. Returns {success, data?:Uint8Array, error?, inputBytes,
  /// outputBytes, primCount, arraysPassedThrough, arraysReencoded}.
  // Shared: flatten an owned USDC buffer and build the JS result object.
  emscripten::val nextFlattenOwned(std::string &&input, bool lazyArrays) {
    emscripten::val result = emscripten::val::object();
    std::vector<uint8_t> out;
    tinyusdz::next::pipeline::FlattenOptions opts;
    opts.read.lazy_arrays = lazyArrays;  // false => eager decode (A/B baseline)
    tinyusdz::next::pipeline::FlattenStats stats;
    std::string err;
    bool ok = tinyusdz::next::pipeline::FlattenUSDCToUSDCOwned(
        std::move(input), out, opts, &stats, &err);

    result.set("success", ok);
    if (!ok) {
      result.set("error", err);
      return result;
    }
    result.set("data", toOwnedUint8Array(out));
    result.set("inputBytes", static_cast<double>(stats.input_bytes));
    result.set("outputBytes", static_cast<double>(stats.output_bytes));
    result.set("primCount", static_cast<double>(stats.prim_count));
    result.set("arraysPassedThrough",
               static_cast<double>(stats.arrays_passed_through));
    result.set("arraysReencoded", static_cast<double>(stats.arrays_reencoded));
    return result;
  }

  emscripten::val nextFlattenUSDC(emscripten::val data, bool lazyArrays) {
    // One JS->WASM copy into an owned std::string; the pipeline then MOVES it
    // into the retained crate buffer (the single in-heap copy of the input).
    size_t size = data["byteLength"].as<size_t>();
    std::string input;
    input.resize(size);
    if (size > 0) {
      // Pass length as a JS Number (double): under wasm64 size_t marshals to a
      // BigInt and `new Uint8Array(buffer, byteOffset, bigint)` throws.
      emscripten::val view = emscripten::val::global("Uint8Array").new_(
          data["buffer"], data["byteOffset"],
          emscripten::val(static_cast<double>(size)));
      emscripten::val heapView = emscripten::val(emscripten::typed_memory_view(
          size, reinterpret_cast<uint8_t *>(&input[0])));
      heapView.call<void>("set", view);
    }
    return nextFlattenOwned(std::move(input), lazyArrays);
  }

  /// Streaming-input flatten: the caller first allocates a buffer via
  /// allocateZeroCopyBuffer(name, size), fills it directly through module.HEAPU8
  /// (in chunks, re-grabbing HEAPU8 after the alloc), then calls this. The
  /// buffer's bytes are MOVED straight into the retained crate buffer — no
  /// embind marshalling and no second copy. The buffer is consumed (erased).
  emscripten::val nextFlattenBuffer(const std::string &uuid, bool lazyArrays) {
    std::string input = em_resolver_.takeZeroCopyBufferString(uuid);
    if (input.empty()) {
      emscripten::val result = emscripten::val::object();
      result.set("success", false);
      result.set("error", "Unknown or empty zero-copy buffer: " + uuid);
      return result;
    }
    return nextFlattenOwned(std::move(input), lazyArrays);
  }

  /// Streaming-output variant of nextFlattenBuffer: the flattened crate is
  /// emitted to `chunkCb(view)` in file order, so the full output crate is never
  /// materialized in the wasm heap (peak stays ~= retained input + small
  /// structural sections). `chunkCb` receives a Uint8Array VIEW into the wasm
  /// heap valid ONLY for the duration of the call — JS must copy it out
  /// synchronously and must not retain it (a later wasm growth can detach it).
  /// chunkCb may return false to abort. Returns stats only (no `data`).
  emscripten::val nextFlattenBufferToSink(const std::string &uuid, bool lazyArrays,
                                          emscripten::val chunkCb) {
    emscripten::val result = emscripten::val::object();
    std::string input = em_resolver_.takeZeroCopyBufferString(uuid);
    if (input.empty()) {
      result.set("success", false);
      result.set("error", "Unknown or empty zero-copy buffer: " + uuid);
      return result;
    }
    tinyusdz::next::pipeline::FlattenOptions opts;
    opts.read.lazy_arrays = lazyArrays;
    opts.write.streaming = true;
    tinyusdz::next::pipeline::FlattenStats stats;
    std::string err;
    bool aborted = false;
    tinyusdz::next::CrateWriteSink sink =
        [&](const uint8_t *data, size_t size) -> bool {
      emscripten::val view(emscripten::typed_memory_view(size, data));
      emscripten::val r = chunkCb(view);
      if (r.isFalse()) {  // strictly false aborts; undefined/anything else continues
        aborted = true;
        return false;
      }
      return true;
    };
    bool ok = tinyusdz::next::pipeline::FlattenUSDCToUSDCOwnedToSink(
        std::move(input), sink, opts, &stats, &err);
    result.set("success", ok);
    if (!ok) {
      result.set("error", aborted ? "aborted by sink" : err);
      return result;
    }
    result.set("inputBytes", static_cast<double>(stats.input_bytes));
    result.set("outputBytes", static_cast<double>(stats.output_bytes));
    result.set("primCount", static_cast<double>(stats.prim_count));
    result.set("arraysPassedThrough",
               static_cast<double>(stats.arrays_passed_through));
    result.set("arraysReencoded", static_cast<double>(stats.arrays_reencoded));
    return result;
  }

  /// Helper: convert current loaded layer to a Stage
  bool getStageFromLayer(tinyusdz::Stage &stage) {
    if (!loaded_) {
      error_ = "No scene loaded";
      return false;
    }

    if (has_stage_) {
      stage = export_stage_;
      return true;
    }

    if (!loaded_as_layer_) {
      error_ = "Scene not loaded as layer";
      return false;
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    tinyusdz::Layer layer_copy = curr;

    if (!tinyusdz::LayerToStage(std::move(layer_copy), &stage, &warn_, &error_)) {
      error_ = "Failed to convert Layer to Stage: " + error_;
      return false;
    }

    return true;
  }

  /// Extract a compact JSON view of UsdPhysics/MuJoCo prims and geometry.
  /// This is intentionally shaped for JS-side URDF conversion and testing.
  std::string extractPhysicsSceneJSON() {
    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      return std::string();
    }

    json root;
    root["upAxis"] = AxisName(stage.metas().upAxis.get_value());
    root["prims"] = json::array();

    for (const auto &prim : stage.root_prims()) {
      AppendPhysicsPrimJson(prim, "/" + prim.element_name(), root["prims"], 0);
    }

    return root.dump();
  }

  /// Export loaded scene as USDA (ASCII) string
  std::string exportAsUSDA() {
    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      return std::string();
    }

    std::string output;
    std::string warn, err;
    if (!tinyusdz::usda::ExportToUSDAString(stage, &output, &warn, &err)) {
      error_ = "USDA export failed: " + err;
      warn_ = warn;
      return std::string();
    }

    warn_ = warn;
    return output;
  }

  /// Export loaded scene as USDC (binary Crate) — returns Uint8Array
  /// Override the USDC writer resource limits for subsequent exportAsUSDC()
  /// calls. Megabytes; pass 0 to keep the (conservative) built-in WASM default.
  /// Use to allow large exports for mesh-dense scenes / roundtrip testing.
  void setUSDCExportLimitMB(int file_size_mb, int memory_mb) {
    usdc_max_file_size_bytes_ =
        file_size_mb > 0 ? static_cast<int64_t>(file_size_mb) * 1024 * 1024 : 0;
    usdc_max_memory_bytes_ =
        memory_mb > 0 ? static_cast<int64_t>(memory_mb) * 1024 * 1024 : 0;
  }

  emscripten::val debugLogMemory(const std::string &label) {
    ReportTinyUSDZDebugEvent("manual", label);
    emscripten::val result = emscripten::val::object();
    result.set("label", label);
    result.set("heapBytes", GetWasmHeapByteLengthForDebug());
    return result;
  }

  emscripten::val exportAsUSDC() {
    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      return emscripten::val::null();
    }

    std::vector<uint8_t> output;
    std::string warn, err;
    if (!tinyusdz::usdc::SaveAsUSDCToMemory(stage, &output, &warn, &err,
                                            usdc_max_file_size_bytes_,
                                            usdc_max_memory_bytes_)) {
      error_ = "USDC export failed: " + err;
      warn_ = warn;
      return emscripten::val::null();
    }

    warn_ = warn;

    // Return a JS-owned COPY, not a heap view: callers commonly hold the result
    // across delete()/new-loader calls that grow the WASM heap and would detach
    // a typed_memory_view (yielding a garbage buffer). Copy straight from the
    // local `output` so the USDC bytes are freed wasm-side on return rather than
    // retained in a member. See toOwnedUint8Array().
    return toOwnedUint8Array(output);
  }

  /// Export the current layer as USDC (binary Crate) - returns Uint8Array.
  /// This avoids Stage reconstruction and USDZ packaging, and is used by the
  /// JS low-heap USDZ repacker to rewrite only the archive root layer.
  emscripten::val exportLayerAsUSDCWithOptions(emscripten::val options) {
    (void)options;
    if (!loaded_ || !loaded_as_layer_) {
      error_ = "No layer loaded";
      return emscripten::val::null();
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    std::vector<uint8_t> output;
    std::string warn, err;
    if (!tinyusdz::usdc::SaveAsUSDCToMemory(curr, &output, &warn, &err,
                                            usdc_max_file_size_bytes_,
                                            usdc_max_memory_bytes_)) {
      error_ = "USDC export failed: " + err;
      warn_ = warn;
      return emscripten::val::null();
    }

    warn_ = warn;
    // JS-owned copy (not a heap view) — detach-safe if the caller retains it
    // across heap-growing WASM calls; copied from the local so nothing is
    // retained wasm-side after return. See toOwnedUint8Array().
    return toOwnedUint8Array(output);
  }

  /// Export the current layer as USDC directly into a JS Uint8Array.
  /// This avoids retaining the whole written crate in a WASM-side vector.
  emscripten::val exportLayerAsUSDCToBufferWithOptions(
      emscripten::val buffer, emscripten::val options) {
    (void)options;
    emscripten::val result = emscripten::val::object();
    result.set("success", false);
    result.set("size", 0.0);

    if (!loaded_ || !loaded_as_layer_) {
      error_ = "No layer loaded";
      result.set("error", error_);
      return result;
    }
    if (buffer.isNull() || buffer.isUndefined()) {
      error_ = "USDC export output buffer is null.";
      result.set("error", error_);
      return result;
    }

    size_t capacity = 0;
    try {
      capacity = buffer["byteLength"].as<size_t>();
    } catch (...) {
      error_ = "USDC export output must be a Uint8Array.";
      result.set("error", error_);
      return result;
    }
    if (capacity == 0) {
      error_ = "USDC export output buffer is empty.";
      result.set("error", error_);
      return result;
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;

    auto js_stream = std::unique_ptr<JSUint8ArrayOutputStream>(
        new JSUint8ArrayOutputStream(buffer, capacity));
    JSUint8ArrayOutputStream *stream_ptr = js_stream.get();
    std::unique_ptr<tinyusdz::experimental::IOutputStream> out_stream(
        std::move(js_stream));
    tinyusdz::experimental::CrateWriter writer(std::move(out_stream));

    tinyusdz::experimental::CrateWriter::Options opts;
    opts.version_major = 0;
    opts.version_minor = 8;
    opts.version_patch = 0;
    opts.enable_compression = true;
    opts.enable_deduplication = true;
    if (usdc_max_file_size_bytes_ > 0) {
      opts.max_file_size_bytes = usdc_max_file_size_bytes_;
    }
    if (usdc_max_memory_bytes_ > 0) {
      opts.max_memory_bytes = usdc_max_memory_bytes_;
    }
    writer.SetOptions(opts);

    std::string open_err;
    if (!writer.Open(&open_err)) {
      error_ = "Failed to open CrateWriter: " + open_err;
      result.set("error", error_);
      return result;
    }

    std::string convert_err;
    if (!writer.ConvertLayerToSpecs(curr, &convert_err)) {
      writer.Close();
      error_ = "Failed to convert Layer to USDC: " + convert_err;
      result.set("error", error_);
      return result;
    }

    std::string finalize_err;
    if (!writer.Finalize(&finalize_err)) {
      writer.Close();
      error_ = "Failed to finalize USDC: " + finalize_err;
      if (!stream_ptr->error().empty()) {
        error_ += " " + stream_ptr->error();
      }
      result.set("error", error_);
      return result;
    }

    writer.Close();
    warn_.clear();
    result.set("success", true);
    result.set("size", static_cast<double>(stream_ptr->written()));
    result.set("warn", warn_);
    return result;
  }

  /// Export the current layer's COMPOSED STAGE (typed-Prim reconstruction, the
  /// same path as exportAsUSDC()/exportAsUSDZWithOptions()) as USDC directly
  /// into a JS Uint8Array. Low-heap counterpart of the in-heap stage export:
  /// the finalized crate streams into `buffer` instead of a WASM-side vector,
  /// so large scenes that overflow the 2 GB wasm32 heap when the whole USDZ is
  /// built in-heap can be repacked from JS (keep textures + JS-side zip).
  emscripten::val exportStageAsUSDCToBufferWithOptions(
      emscripten::val buffer, emscripten::val options) {
    (void)options;
    emscripten::val result = emscripten::val::object();
    result.set("success", false);
    result.set("size", 0.0);

    if (buffer.isNull() || buffer.isUndefined()) {
      error_ = "USDC export output buffer is null.";
      result.set("error", error_);
      return result;
    }
    size_t capacity = 0;
    try {
      capacity = buffer["byteLength"].as<size_t>();
    } catch (...) {
      error_ = "USDC export output must be a Uint8Array.";
      result.set("error", error_);
      return result;
    }
    if (capacity == 0) {
      error_ = "USDC export output buffer is empty.";
      result.set("error", error_);
      return result;
    }

    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      result.set("error", error_);
      return result;
    }

    auto js_stream = std::unique_ptr<JSUint8ArrayOutputStream>(
        new JSUint8ArrayOutputStream(buffer, capacity));
    JSUint8ArrayOutputStream *stream_ptr = js_stream.get();
    std::unique_ptr<tinyusdz::experimental::IOutputStream> out_stream(
        std::move(js_stream));
    tinyusdz::experimental::CrateWriter writer(std::move(out_stream));

    tinyusdz::experimental::CrateWriter::Options opts;
    opts.version_major = 0;
    opts.version_minor = 8;
    opts.version_patch = 0;
    opts.enable_compression = true;
    opts.enable_deduplication = true;
    if (usdc_max_file_size_bytes_ > 0) {
      opts.max_file_size_bytes = usdc_max_file_size_bytes_;
    }
    if (usdc_max_memory_bytes_ > 0) {
      opts.max_memory_bytes = usdc_max_memory_bytes_;
    }
    writer.SetOptions(opts);

    std::string open_err;
    if (!writer.Open(&open_err)) {
      error_ = "Failed to open CrateWriter: " + open_err;
      result.set("error", error_);
      return result;
    }

    std::string convert_err;
    if (!writer.ConvertStageToSpecs(stage, &convert_err)) {
      writer.Close();
      error_ = "Failed to convert Stage to USDC: " + convert_err;
      result.set("error", error_);
      return result;
    }

    std::string finalize_err;
    if (!writer.Finalize(&finalize_err)) {
      writer.Close();
      error_ = "Failed to finalize USDC: " + finalize_err;
      if (!stream_ptr->error().empty()) {
        error_ += " " + stream_ptr->error();
      }
      result.set("error", error_);
      return result;
    }

    writer.Close();
    warn_.clear();
    result.set("success", true);
    result.set("size", static_cast<double>(stream_ptr->written()));
    result.set("warn", warn_);
    return result;
  }

  /// Flatten the loaded layer at the LAYER level: compose
  /// sublayers/references/payload/inherits/variants into a single Layer and
  /// store it as composed_layer_ (composited_=true). Non-consuming, so the
  /// caller writes the result with exportLayerAsUSDCToBufferWithOptions (which
  /// is retriable for buffer growth). This is the non-Stage flatten entry: it
  /// avoids the Layer->Stage typed reconstruction AND the layer copy
  /// getStageFromLayer makes, so it is much lighter on the wasm heap and
  /// faithful (PrimSpecs written as-authored, no typed-input drop).
  bool flattenLayer() {
    if (!loaded_ || !loaded_as_layer_) {
      error_ = "No layer loaded";
      return false;
    }

    tinyusdz::AssetResolutionResolver resolver;
    if (!SetupEMAssetResolution(resolver, &em_resolver_)) {
      error_ = "Failed to setup asset resolution for flatten.";
      return false;
    }
    resolver.set_current_working_path("./");
    resolver.set_search_paths({"./"});

    // Move the current layer out — flatten in place, never duplicating it.
    tinyusdz::Layer src_layer =
        composited_ ? std::move(composed_layer_) : std::move(layer_);

    // Parent-relative ('../') and drive-prefixed asset paths are legitimate in
    // UE-exported scenes; the sandboxed in-memory resolver bounds what is
    // reachable, and the resolver suffix-fallback rebases escaping paths onto
    // the uploaded folder root.
    tinyusdz::SublayersCompositionOptions sublayer_options;
    sublayer_options.allow_parent_relative_paths =
        allow_parent_relative_asset_paths_;
    tinyusdz::ReferencesCompositionOptions references_options;
    references_options.allow_parent_relative_paths =
        allow_parent_relative_asset_paths_;
    tinyusdz::PayloadCompositionOptions payload_options;
    payload_options.allow_parent_relative_paths =
        allow_parent_relative_asset_paths_;

    // LIVRPS flatten: subLayers, then references/payload/inherits/variants to a
    // fixed point. Each Composite* moves the prior layer into the next.
    {
      tinyusdz::Layer tmp;
      if (!tinyusdz::CompositeSublayers(resolver, src_layer, &tmp, &warn_, &error_, sublayer_options)) {
        error_ = "Failed to composite subLayers: " + error_;
        return false;
      }
      src_layer = std::move(tmp);
    }
    constexpr int kMaxFlattenIter = 32;
    for (int i = 0; i < kMaxFlattenIter; i++) {
      bool unresolved = false;
      if (src_layer.check_unresolved_references()) {
        tinyusdz::Layer tmp;
        if (!tinyusdz::CompositeReferences(resolver, src_layer, &tmp, &warn_, &error_, references_options)) return false;
        src_layer = std::move(tmp); unresolved = true;
      }
      if (src_layer.check_unresolved_payload()) {
        tinyusdz::Layer tmp;
        if (!tinyusdz::CompositePayload(resolver, src_layer, &tmp, &warn_, &error_, payload_options)) return false;
        src_layer = std::move(tmp); unresolved = true;
      }
      if (src_layer.check_unresolved_inherits()) {
        tinyusdz::Layer tmp;
        if (!tinyusdz::CompositeInherits(src_layer, &tmp, &warn_, &error_)) return false;
        src_layer = std::move(tmp); unresolved = true;
      }
      if (src_layer.check_unresolved_variant()) {
        tinyusdz::Layer tmp;
        if (!tinyusdz::CompositeVariant(src_layer, &tmp, &warn_, &error_)) return false;
        src_layer = std::move(tmp); unresolved = true;
      }
      if (!unresolved) break;
    }

    // Store the flattened layer; exportLayerAsUSDCToBufferWithOptions writes it
    // (and can be retried with a larger buffer without re-flattening).
    composed_layer_ = std::move(src_layer);
    composited_ = true;
    loaded_as_layer_ = true;
    return true;
  }

  /// Export loaded scene as USDZ (ZIP package with packed assets) — returns Uint8Array
  /// Assets are collected from the em_resolver_ cache.
  ///
  /// CONTRACT: the four exportAsUSDZ*/exportLayerAsUSDZ* methods return a
  /// `typed_memory_view` aliasing the WASM heap (cheap, no copy — these buffers
  /// can be large). The caller MUST copy the result (e.g. `new Uint8Array(v)`)
  /// before invoking any other method that can grow the heap, or the view is
  /// detached and reads garbage. The in-tree callers (usdzconvert.js exportUSDZ)
  /// copy immediately. If you may retain the result across WASM calls, prefer a
  /// USDC exporter (those return JS-owned copies via toOwnedUint8Array).
  emscripten::val exportAsUSDZ() {
    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      return emscripten::val::null();
    }

    // Collect assets from resolver cache
    std::map<std::string, std::vector<uint8_t>> assets;
    for (const auto &kv : em_resolver_.cache) {
      const std::string &name = kv.first;
      const std::string &binary = kv.second.binary;
      std::string ext;
      {
        auto dot = name.rfind('.');
        if (dot != std::string::npos) {
          ext = name.substr(dot);
          // lowercase
          for (auto &c : ext) c = static_cast<char>(std::tolower(c));
        }
      }
      // The stage is flattened (getStageFromLayer composed all sublayers /
      // references / payloads in), so .usd/.usda/.usdc dependency layers are
      // already inlined into the root — packing them would duplicate the
      // geometry. Pack only the image/audio assets the flattened root still
      // references.
      if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".exr" ||
          ext == ".avif" || ext == ".m4a" || ext == ".mp3" || ext == ".wav") {
        assets[name] = std::vector<uint8_t>(binary.begin(), binary.end());
      }
    }

    tinyusdz::USDZWriteOptions write_options;
    write_options.max_file_size_bytes = usdc_max_file_size_bytes_;
    write_options.max_memory_bytes = usdc_max_memory_bytes_;

    std::vector<uint8_t> output;
    std::string warn, err;
    if (!tinyusdz::SaveAsUSDZToMemory(stage, assets, &output, write_options,
                                      &warn, &err)) {
      error_ = "USDZ export failed: " + err;
      warn_ = warn;
      return emscripten::val::null();
    }

    warn_ = warn;

    // Copy to JS Uint8Array
    usdz_export_buf_ = std::move(output);
    return emscripten::val(emscripten::typed_memory_view(
        usdz_export_buf_.size(), usdz_export_buf_.data()));
  }

  /// Like exportAsUSDZ(), but first rewrites UsdUVTexture `inputs:file` asset
  /// paths according to `remap` ({oldName: newName}). Use when textures are
  /// renamed (e.g. transcoded PNG -> JPG) so references follow.
  emscripten::val exportAsUSDZWithRemap(emscripten::val remap) {
    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      return emscripten::val::null();
    }

    // Build the remap map from the JS object.
    std::map<std::string, std::string> remap_map;
    emscripten::val keys =
        emscripten::val::global("Object").call<emscripten::val>("keys", remap);
    const size_t nkeys = keys["length"].as<size_t>();
    for (size_t i = 0; i < nkeys; i++) {
      std::string k = keys[i].as<std::string>();
      remap_map[k] = remap[k].as<std::string>();
    }
    tinyusdz::usdz::RemapTextureAssetPaths(stage, remap_map);

    // The stage is flattened, so .usd/.usda/.usdc dependency layers are already
    // inlined into the root; pack only the image/audio assets it references
    // (packing the inlined layers would duplicate the geometry).
    std::map<std::string, std::vector<uint8_t>> assets;
    for (const auto &kv : em_resolver_.cache) {
      const std::string &name = kv.first;
      const std::string &binary = kv.second.binary;
      std::string ext;
      auto dot = name.rfind('.');
      if (dot != std::string::npos) {
        ext = name.substr(dot);
        for (auto &c : ext) c = static_cast<char>(std::tolower(c));
      }
      if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".exr" ||
          ext == ".avif" || ext == ".m4a" || ext == ".mp3" || ext == ".wav") {
        assets[name] = std::vector<uint8_t>(binary.begin(), binary.end());
      }
    }

    tinyusdz::USDZWriteOptions write_options;
    write_options.max_file_size_bytes = usdc_max_file_size_bytes_;
    write_options.max_memory_bytes = usdc_max_memory_bytes_;

    std::vector<uint8_t> output;
    std::string warn, err;
    if (!tinyusdz::SaveAsUSDZToMemory(stage, assets, &output, write_options,
                                      &warn, &err)) {
      error_ = "USDZ export failed: " + err;
      warn_ = warn;
      return emscripten::val::null();
    }
    warn_ = warn;
    usdz_export_buf_ = std::move(output);
    return emscripten::val(emscripten::typed_memory_view(
        usdz_export_buf_.size(), usdz_export_buf_.data()));
  }

  /// Export USDZ with optional texture remap and write options.
  /// options: { rootLayerFormat?: "usdc"|"usda", arkitCompatible?: bool }
  emscripten::val exportAsUSDZWithOptions(emscripten::val remap,
                                          emscripten::val options) {
    tinyusdz::Stage stage;
    if (!getStageFromLayer(stage)) {
      return emscripten::val::null();
    }

    std::map<std::string, std::string> remap_map;
    if (!remap.isUndefined() && !remap.isNull()) {
      emscripten::val keys =
          emscripten::val::global("Object").call<emscripten::val>("keys", remap);
      const size_t nkeys = keys["length"].as<size_t>();
      for (size_t i = 0; i < nkeys; i++) {
        std::string k = keys[i].as<std::string>();
        remap_map[k] = remap[k].as<std::string>();
      }
    }
    if (!remap_map.empty()) {
      tinyusdz::usdz::RemapTextureAssetPaths(stage, remap_map);
    }

    tinyusdz::USDZWriteOptions write_options;
    write_options.max_file_size_bytes = usdc_max_file_size_bytes_;
    write_options.max_memory_bytes = usdc_max_memory_bytes_;
    bool arkit_compatible = false;
    if (!options.isUndefined() && !options.isNull()) {
      emscripten::val arkit_val = options["arkitCompatible"];
      if (!arkit_val.isUndefined() && !arkit_val.isNull()) {
        arkit_compatible = arkit_val.as<bool>();
      }
      emscripten::val root_format_val = options["rootLayerFormat"];
      if (!root_format_val.isUndefined() && !root_format_val.isNull()) {
        std::string root_format = root_format_val.as<std::string>();
        for (auto &c : root_format) c = static_cast<char>(std::tolower(c));
        if (root_format == "usda") {
          write_options.root_layer_format = tinyusdz::USDZRootLayerFormat::USDA;
        }
      }
    }
    if (arkit_compatible) {
      stage.metas().upAxis.set_value(tinyusdz::Axis::Y);
      write_options.root_layer_format = tinyusdz::USDZRootLayerFormat::USDC;
    }

    // The stage is flattened, so .usd/.usda/.usdc dependency layers are already
    // inlined into the root; pack only the image/audio assets it references
    // (packing the inlined layers would duplicate the geometry).
    std::map<std::string, std::vector<uint8_t>> assets;
    for (const auto &kv : em_resolver_.cache) {
      const std::string &name = kv.first;
      const std::string &binary = kv.second.binary;
      std::string ext;
      auto dot = name.rfind('.');
      if (dot != std::string::npos) {
        ext = name.substr(dot);
        for (auto &c : ext) c = static_cast<char>(std::tolower(c));
      }
      if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".exr" ||
          ext == ".avif" || ext == ".m4a" || ext == ".mp3" || ext == ".wav") {
        assets[name] = std::vector<uint8_t>(binary.begin(), binary.end());
      }
    }

    std::vector<uint8_t> output;
    std::string warn, err;
    if (!tinyusdz::SaveAsUSDZToMemory(stage, assets, &output, write_options,
                                      &warn, &err)) {
      error_ = "USDZ export failed: " + err;
      warn_ = warn;
      return emscripten::val::null();
    }
    warn_ = warn;
    usdz_export_buf_ = std::move(output);
    return emscripten::val(emscripten::typed_memory_view(
        usdz_export_buf_.size(), usdz_export_buf_.data()));
  }

  /// Export the current layer as the USDZ root layer. This is used for
  /// non-flattened packaging so composition arcs stay authored in the root.
  emscripten::val exportLayerAsUSDZWithOptions(emscripten::val options) {
    if (!loaded_ || !loaded_as_layer_) {
      error_ = "No layer loaded";
      return emscripten::val::null();
    }

    tinyusdz::USDZWriteOptions write_options;
    write_options.root_layer_format = tinyusdz::USDZRootLayerFormat::USDA;
    write_options.max_file_size_bytes = usdc_max_file_size_bytes_;
    write_options.max_memory_bytes = usdc_max_memory_bytes_;
    if (!options.isUndefined() && !options.isNull()) {
      emscripten::val root_format_val = options["rootLayerFormat"];
      if (!root_format_val.isUndefined() && !root_format_val.isNull()) {
        std::string root_format = root_format_val.as<std::string>();
        for (auto &c : root_format) c = static_cast<char>(std::tolower(c));
        if (root_format == "usdc") {
          write_options.root_layer_format = tinyusdz::USDZRootLayerFormat::USDC;
        }
      }
    }

    // When the layer was composed (composited_), `curr` below is the composed
    // layer with all sublayers/references/payloads resolved in, so the
    // .usd/.usda/.usdc dependency layers are already inlined and packing them
    // would duplicate the geometry. When the raw layer is exported (not
    // composited), composition arcs stay authored in the root, so those
    // dependency layers are still required.
    std::map<std::string, std::vector<uint8_t>> assets;
    for (const auto &kv : em_resolver_.cache) {
      const std::string &name = kv.first;
      const std::string &binary = kv.second.binary;
      std::string ext;
      auto dot = name.rfind('.');
      if (dot != std::string::npos) {
        ext = name.substr(dot);
        for (auto &c : ext) c = static_cast<char>(std::tolower(c));
      }
      const bool is_usd_layer =
          (ext == ".usd" || ext == ".usda" || ext == ".usdc");
      const bool is_media =
          (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".exr" ||
           ext == ".avif" || ext == ".m4a" || ext == ".mp3" || ext == ".wav");
      if (is_media || (is_usd_layer && !composited_)) {
        assets[name] = std::vector<uint8_t>(binary.begin(), binary.end());
      }
    }

    const tinyusdz::Layer &curr = composited_ ? composed_layer_ : layer_;
    std::vector<uint8_t> output;
    std::string warn, err;
    if (!tinyusdz::SaveAsUSDZToMemory(curr, assets, &output, write_options,
                                      &warn, &err)) {
      error_ = "USDZ export failed: " + err;
      warn_ = warn;
      return emscripten::val::null();
    }
    warn_ = warn;
    usdz_export_buf_ = std::move(output);
    return emscripten::val(emscripten::typed_memory_view(
        usdz_export_buf_.size(), usdz_export_buf_.data()));
  }

  /// Create a sample scene with a textured quad (checkerboard).
  /// The texture PNG must be set from JS via setAsset("textures/checkerboard.png", pngBytes)
  /// BEFORE calling exportAsUSDZ.
  bool createSampleScene() {
    // Build stage
    tinyusdz::Stage stage;
    stage.metas().defaultPrim = tinyusdz::value::token("root");
    stage.metas().upAxis = tinyusdz::Axis::Y;

    // -- Xform root --
    tinyusdz::Xform xform;
    xform.name = "root";

    // -- GeomMesh quad --
    tinyusdz::GeomMesh mesh;
    mesh.name = "quad";
    {
      std::vector<tinyusdz::value::point3f> pts;
      pts.push_back({-0.5f, 0.0f, -0.5f});
      pts.push_back({ 0.5f, 0.0f, -0.5f});
      pts.push_back({ 0.5f, 0.0f,  0.5f});
      pts.push_back({-0.5f, 0.0f,  0.5f});
      mesh.points.set_value(std::move(pts));

      std::vector<tinyusdz::value::normal3f> normals;
      normals.push_back({0.0f, 1.0f, 0.0f});
      normals.push_back({0.0f, 1.0f, 0.0f});
      normals.push_back({0.0f, 1.0f, 0.0f});
      normals.push_back({0.0f, 1.0f, 0.0f});
      mesh.normals.set_value(std::move(normals));
      mesh.normals.metas().set_interpolation_enum(tinyusdz::Interpolation::Vertex);

      std::vector<int> counts = {3, 3};
      mesh.faceVertexCounts.set_value(std::move(counts));

      std::vector<int> indices = {0, 1, 2, 0, 2, 3};
      mesh.faceVertexIndices.set_value(std::move(indices));

      // UV primvar
      tinyusdz::Attribute uvAttr;
      std::vector<tinyusdz::value::texcoord2f> uvs;
      uvs.push_back({0.0f, 0.0f});
      uvs.push_back({1.0f, 0.0f});
      uvs.push_back({1.0f, 1.0f});
      uvs.push_back({0.0f, 1.0f});
      uvAttr.set_value(std::move(uvs));
      uvAttr.metas().set_interpolation_enum(tinyusdz::Interpolation::Vertex);
      mesh.props.emplace("primvars:st", tinyusdz::Property(uvAttr, false));

      // Material binding
      tinyusdz::Relationship materialBinding;
      materialBinding.set(tinyusdz::Path("/root/mat", ""));
      mesh.materialBinding = materialBinding;
    }

    // -- Material --
    tinyusdz::Material mat;
    mat.name = "mat";
    mat.surface.set(tinyusdz::Path("/root/mat/PBRShader", "outputs:surface"));

    // -- UsdPreviewSurface shader --
    tinyusdz::Shader pbrShader;
    pbrShader.name = "PBRShader";
    pbrShader.info_id = tinyusdz::kUsdPreviewSurface;
    {
      tinyusdz::UsdPreviewSurface surf;
      surf.outputsSurface.set_authored(true);
      surf.metallic.set_value(0.0f);
      surf.roughness.set_value(0.5f);

      // Connect diffuseColor to texture
      surf.diffuseColor.set_connection(
          tinyusdz::Path("/root/mat/diffuseTexture", "outputs:rgb"));
      surf.diffuseColor.set_value_empty();

      pbrShader.value = std::move(surf);
    }

    // -- UsdPrimvarReader_float2 shader --
    tinyusdz::Shader stReaderShader;
    stReaderShader.name = "stReader";
    stReaderShader.info_id = tinyusdz::kUsdPrimvarReader_float2;
    {
      tinyusdz::UsdPrimvarReader_float2 reader;

      tinyusdz::Animatable<std::string> varname;
      varname.set_default(std::string("st"));
      reader.varname.set_value(varname);

      reader.result.set_authored(true);

      stReaderShader.value = std::move(reader);
    }

    // -- UsdUVTexture shader --
    tinyusdz::Shader texShader;
    texShader.name = "diffuseTexture";
    texShader.info_id = tinyusdz::kUsdUVTexture;
    {
      tinyusdz::UsdUVTexture tex;
      tex.file = tinyusdz::value::AssetPath("textures/checkerboard.png");

      // Connect st input to primvar reader
      tex.st.set_connection(
          tinyusdz::Path("/root/mat/stReader", "outputs:result"));
      tex.st.set_value_empty();

      tex.outputsRGB.set_authored(true);

      texShader.value = std::move(tex);
    }

    // Assemble scene hierarchy
    tinyusdz::Prim matPrim(mat);
    {
      std::string err;
      matPrim.add_child(tinyusdz::Prim(pbrShader), true, &err);
      matPrim.add_child(tinyusdz::Prim(stReaderShader), true, &err);
      matPrim.add_child(tinyusdz::Prim(texShader), true, &err);
    }

    tinyusdz::Prim xformPrim(xform);
    {
      std::string err;
      xformPrim.add_child(tinyusdz::Prim(mesh), true, &err);
      xformPrim.add_child(std::move(matPrim), true, &err);
    }

    stage.add_root_prim(std::move(xformPrim));

    // Store stage for export
    export_stage_ = std::move(stage);
    has_stage_ = true;
    loaded_ = true;

    return true;
  }

  void clearURDFMeshBuffers() {
    urdf_mesh_buffers_.clear();
  }

  bool setVisualMesh(const std::string &name, const emscripten::val &positions,
                     const emscripten::val &normals,
                     const emscripten::val &uvs,
                     const emscripten::val &indices) {
    return setURDFMeshBuffer(name, positions, normals, uvs, indices);
  }

  bool setCollisionMesh(const std::string &name, const emscripten::val &positions,
                        const emscripten::val &normals,
                        const emscripten::val &uvs,
                        const emscripten::val &indices) {
    return setURDFMeshBuffer(name, positions, normals, uvs, indices);
  }

  /// Build an exportable USD Physics + MuJoCo stage from a compact JSON
  /// description generated by web/js/urdf.js. Geometry is expected to be
  /// already baked to triangle meshes in link-local space.
  bool createURDFPhysicsScene(const std::string &robot_json) {
    tinyusdz::Stage stage;
    std::string warn;
    std::string err;
    if (!tinyusdz::tydra::ConvertURDFJsonToUSDStage(
            robot_json, &urdf_mesh_buffers_, &stage, &warn, &err)) {
      warn_ = std::move(warn);
      error_ = std::move(err);
      return false;
    }

    export_stage_ = std::move(stage);
    warn_ = std::move(warn);
    error_.clear();
    has_stage_ = true;
    loaded_ = true;
    loaded_as_layer_ = false;
    return true;
  }

  bool setURDFMeshBuffer(const std::string &name,
                         const emscripten::val &positions,
                         const emscripten::val &normals,
                         const emscripten::val &uvs,
                         const emscripten::val &indices) {
    if (name.empty()) {
      error_ = "setVisualMesh/setCollisionMesh requires a non-empty mesh name";
      return false;
    }

    tinyusdz::tydra::URDFMeshBuffer buffer;
    detail::copyTypedArray<float>(positions, buffer.positions, "Float32Array");
    detail::copyTypedArray<float>(normals, buffer.normals, "Float32Array");
    detail::copyTypedArray<float>(uvs, buffer.uvs, "Float32Array");
    detail::copyTypedArray<int32_t>(indices, buffer.indices, "Int32Array");

    if (buffer.positions.size() < 9 || (buffer.positions.size() % 3) != 0) {
      error_ = "setVisualMesh/setCollisionMesh `" + name +
               "` requires positions as Float32Array triples";
      return false;
    }
    if (!buffer.normals.empty() && buffer.normals.size() != buffer.positions.size()) {
      error_ = "setVisualMesh/setCollisionMesh `" + name +
               "` normals length must match positions length";
      return false;
    }
    if (!buffer.uvs.empty() && buffer.uvs.size() != (buffer.positions.size() / 3) * 2) {
      error_ = "setVisualMesh/setCollisionMesh `" + name +
               "` uvs length must be vertex count * 2";
      return false;
    }
    if (!buffer.indices.empty() && (buffer.indices.size() % 3) != 0) {
      error_ = "setVisualMesh/setCollisionMesh `" + name +
               "` indices must be triangle indices";
      return false;
    }

    urdf_mesh_buffers_[name] = std::move(buffer);
    error_.clear();
    return true;
  }

  /// Encode raw pixel data to image format using native writer (for EXR/TIFF/DNG only).
  /// For PNG/JPEG, use browser Canvas API instead.
  /// format: "exr", "tiff", "dng", "bmp", "png" (fallback)
  emscripten::val encodeImageNative(const std::string &pixelData, int width, int height, int channels, const std::string &format) {
    // Validate dimensions at WASM boundary.
    constexpr int kMaxDimension = 65536;
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 ||
        width > kMaxDimension || height > kMaxDimension) {
      emscripten::val err = emscripten::val::object();
      err.set("success", false);
      err.set("error", "Invalid image dimensions.");
      return err;
    }
    tinyusdz::Image img;
    img.width = width;
    img.height = height;
    img.channels = channels;
    img.bpp = 8;
    img.format = tinyusdz::Image::PixelFormat::UInt;
    img.data.assign(reinterpret_cast<const uint8_t*>(pixelData.data()),
                    reinterpret_cast<const uint8_t*>(pixelData.data()) + pixelData.size());

    tinyusdz::image::WriteOption opt;
    if (format == "exr") {
      opt.format = tinyusdz::image::WriteImageFormat::EXR;
    } else if (format == "tiff") {
      opt.format = tinyusdz::image::WriteImageFormat::TIFF;
    } else if (format == "dng") {
      opt.format = tinyusdz::image::WriteImageFormat::DNG;
    } else if (format == "bmp") {
      opt.format = tinyusdz::image::WriteImageFormat::BMP;
    } else if (format == "png") {
      opt.format = tinyusdz::image::WriteImageFormat::PNG;
    } else {
      error_ = "Unsupported image format: " + format;
      return emscripten::val::null();
    }

    auto result = tinyusdz::image::WriteImageToMemory(img, opt);
    if (!result) {
      error_ = "Image encoding failed: " + result.error();
      return emscripten::val::null();
    }

    image_export_buf_ = std::move(result.value());
    return emscripten::val(emscripten::typed_memory_view(
        image_export_buf_.size(), image_export_buf_.data()));
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
    options.mmap_zero_copy = mmap_zero_copy_;

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

    // Instance support (AOUSD Spec 11.3.3)
    node.set("isInstance", rnode.is_instance);
    node.set("prototypeIndex", rnode.prototype_index);
    node.set("instanceId", rnode.instance_id);

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

  // Allow '..' parent-dir segments in composition asset paths. Default on for
  // WASM: the EM resolver is a sandboxed cache (FILESYSTEM=0), so there is no
  // real directory to traverse out of, and USD `../foo.usd` refs are common.
  bool allow_parent_relative_asset_paths_{true};

  // UDIM: when false, keep UDIM tiles separate (sparse tydra::UDIMTexture)
  // for editing tiles in the web RenderScene. When true (default), combine
  // tiles into a single atlas texture.
  bool combineUDIMTiles_{true};

  // Set appropriate default memory limits based on WASM architecture
#ifdef TINYUSDZ_WASM_MEMORY64
  int32_t max_memory_limit_mb_{8192}; // 8GB for MEMORY64
#else
  int32_t max_memory_limit_mb_{2048}; // 2GB for 32-bit WASM
#endif

  // Defer tangent computation until explicitly requested via computeMeshTangents()
  bool defer_tangent_computation_{true};  // default true for WASM to save memory

  // MMap zero-copy: record mmap offsets during USDC parsing so Tydra can read
  // large float/double arrays directly from the input buffer, skipping the
  // EvaluateTypedAnimatableAttribute copy.  Default off; will be enabled after
  // more testing.  The input binary buffer must stay alive while the Stage is
  // in use (guaranteed by loadFromBinary / loadFromBinaryAsync call flow).
  bool mmap_zero_copy_{false};

  // Sphere tessellation
  int sphere_subdivisions_{4};  // Default to 4 subdivisions

  // Bone reduction configuration (disabled by default for backward compatibility)
  bool enable_bone_reduction_{false};
  uint32_t target_bone_count_{4};  // Default to 4 bones (standard for WebGL/Three.js)
  bool round_bone_count_{false};   // Round up to standard GPU skinning values (4,8,16,32,48,64,80,96,128)

  bool enable_value_clips_{true};
  float value_clip_sample_rate_{0.0f};
  bool value_clip_use_time_range_{false};
  double value_clip_start_time_{0.0};
  double value_clip_end_time_{0.0};

  // Bone texture data cache (mutable for const member function)
  mutable std::vector<float> bone_texture_data_;
  mutable std::vector<float> bone_vertex_offsets_;

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

  // Export state
  tinyusdz::Stage export_stage_;
  bool has_stage_{false};
  std::vector<uint8_t> usdz_export_buf_;
  std::vector<uint8_t> image_export_buf_;
  // Optional USDC writer resource-limit overrides (bytes; 0 = built-in default).
  // Settable from JS via setUSDCExportLimitMB() to allow large exports
  // (e.g. mesh-dense robots) past the conservative WASM defaults.
  int64_t usdc_max_file_size_bytes_{0};
  int64_t usdc_max_memory_bytes_{0};
  std::map<std::string, tinyusdz::tydra::URDFMeshBuffer> urdf_mesh_buffers_;

  // Cache for reordered mesh data (triangles sorted by material for optimal submesh grouping)
  struct ReorderedMeshCache {
    std::vector<float> points;
    std::vector<float> normals;        // float3 normals
    std::vector<int8_t> normals_i8;    // SNorm8x3 normals
    std::vector<int16_t> normals_i16;  // SNorm16x3 normals
    std::vector<float> texcoords;
    std::vector<float> tangents;
    std::vector<int> jointIndices;
    std::vector<float> jointWeights;
    std::vector<uint32_t> faceVertexIndices;
  };
  mutable std::unordered_map<int, ReorderedMeshCache> reordered_mesh_cache_;

  // Cache for unpacked float3 normals (used when format is Uint/1010102)
  mutable std::unordered_map<int, std::vector<float>> normals_cache_;

  // Cache for vec4 tangents (xyz=tangent, w=handedness) in the non-reordered path
  mutable std::unordered_map<int, std::vector<float>> tangents4_cache_;
  mutable std::unordered_map<int, std::vector<float>> normals3_cache_;

  // Deprecated-method names already warned about (warn once per name).
  mutable std::set<std::string> deprecation_warned_;

  // Per-session MCP contexts. key = session_id. Each session gets its own
  // isolated Context so tools cannot read/overwrite another session's state.
  std::unordered_map<std::string, tinyusdz::tydra::mcp::Context> mcp_ctx_;
  std::string mcp_session_id_;

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

/// Convert float32 array to float16 array
void convertFloat32ToFloat16(const float* src, uint16_t* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    dst[i] = float32ToFloat16(src[i]);
  }
}

/// Copy buffer from JS Uint8Array
void copyFromJSBuffer(const emscripten::val& data, std::vector<uint8_t>& buffer) {
  size_t size = data["byteLength"].as<size_t>();
  constexpr size_t kMaxBufferSize = size_t(1) << 30;  // 1 GiB
  if (size > kMaxBufferSize) {
    buffer.clear();
    return;
  }
  buffer.resize(size);
  emscripten::val view = emscripten::val::global("Uint8Array").new_(
      data["buffer"], data["byteOffset"],
      emscripten::val(static_cast<double>(size)));  // double: wasm64 BigInt-safe
  emscripten::val heapView = emscripten::val(
      emscripten::typed_memory_view(size, buffer.data()));
  heapView.call<void>("set", view);
}

/// Validate decoded image dimensions and compute the total component count
/// (width * height * channels) with overflow checking. Image dimensions come
/// from decoded (untrusted) headers, so this guards against integer overflow
/// that could otherwise lead to under-allocation and out-of-bounds access.
/// Note: on wasm32 `size_t` is 32-bit, so the fits-in-size_t check below also
/// bounds the resulting allocation. Returns false (and leaves *out_count
/// untouched) when the dimensions are non-positive, exceed sane limits, or the
/// product does not fit in size_t.
bool ComputeImageComponentCount(int width, int height, int channels,
                                size_t* out_count) {
  // Generous per-side limit; also keeps width*height comfortably within 64-bit.
  constexpr int kMaxImageDim = 65536;
  constexpr int kMaxImageChannels = 16;
  if ((width <= 0) || (height <= 0) || (channels <= 0)) {
    return false;
  }
  if ((width > kMaxImageDim) || (height > kMaxImageDim) ||
      (channels > kMaxImageChannels)) {
    return false;
  }
  const uint64_t total = uint64_t(uint32_t(width)) *
                         uint64_t(uint32_t(height)) *
                         uint64_t(uint32_t(channels));
  if (total > uint64_t((std::numeric_limits<size_t>::max)())) {
    return false;
  }
  (*out_count) = size_t(total);
  return true;
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

  if (!IsEXRMagic(buffer.data(), buffer.size())) {
    result.set("success", false);
    result.set("error", std::string("Not a valid EXR file"));
    return result;
  }

  // Decode via the backend-agnostic image loader (EXR -> fp32 RGBA).
  auto loaded = tinyusdz::image::LoadImageFromMemory(buffer.data(),
                                                     buffer.size(), "decodeEXR");
  if (!loaded) {
    result.set("success", false);
    result.set("error", loaded.error());
    return result;
  }
  tinyusdz::Image& im = loaded.value().image;
  const int width = im.width;
  const int height = im.height;
  float* rgba = reinterpret_cast<float*>(im.data.data());

  size_t pixelCount = 0;
  if (!ComputeImageComponentCount(width, height, 4, &pixelCount)) {
    result.set("success", false);
    result.set("error",
               std::string("EXR image dimensions are invalid or too large."));
    return result;
  }

  if (outputFormat == "float16") {
    // Convert to float16 and return as Uint16Array
    std::vector<uint16_t> fp16Data(pixelCount);
    convertFloat32ToFloat16(rgba, fp16Data.data(), pixelCount);

    emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
    emscripten::val pixelData = Uint16Array.new_(emscripten::val(static_cast<double>(pixelCount)));
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, fp16Data.data()));
    pixelData.call<void>("set", jsHeap);

    result.set("data", pixelData);
    result.set("pixelFormat", std::string("float16"));
    result.set("bitsPerChannel", 16);
  } else {
    // Return as Float32Array (default)
    emscripten::val Float32Array = emscripten::val::global("Float32Array");
    emscripten::val pixelData = Float32Array.new_(emscripten::val(static_cast<double>(pixelCount)));
    emscripten::val jsHeap = emscripten::val(
        emscripten::typed_memory_view(pixelCount, rgba));
    pixelData.call<void>("set", jsHeap);

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
  return IsEXRMagic(buffer.data(), buffer.size());
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
  if (buffer.size() > static_cast<size_t>(INT_MAX)) {
    result.set("success", false);
    result.set("error", "HDR file too large to decode.");
    return result;
  }
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
  size_t pixelCount = 0;
  if (!ComputeImageComponentCount(width, height, outputChannels, &pixelCount)) {
    stbi_image_free(floatData);
    result.set("success", false);
    result.set("error",
               std::string("HDR image dimensions are invalid or too large."));
    return result;
  }

  if (outputFormat == "float32") {
    // Return as Float32Array
    emscripten::val Float32Array = emscripten::val::global("Float32Array");
    emscripten::val pixelData = Float32Array.new_(emscripten::val(static_cast<double>(pixelCount)));
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
    emscripten::val pixelData = Uint16Array.new_(emscripten::val(static_cast<double>(pixelCount)));
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
  if (IsEXRMagic(buffer.data(), buffer.size())) {
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
  size_t pixelCount = 0;
  if (!ComputeImageComponentCount(img.width, img.height, img.channels,
                                  &pixelCount)) {
    result.set("success", false);
    result.set("error",
               std::string("Decoded image dimensions are invalid or too large."));
    return result;
  }
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
    // Guard the reinterpret/read against a buffer that is smaller than the
    // reported dimensions imply (truncated/malformed image).
    if (pixelCount > (dataSize / sizeof(float))) {
      result.set("success", false);
      result.set("error", std::string("Decoded float image buffer is smaller "
                                       "than reported dimensions."));
      return result;
    }
    const float* srcData = reinterpret_cast<const float*>(img.data.data());

    if (actualFormat == "float16") {
      // Downcast float32 to float16
      std::vector<uint16_t> fp16Data(pixelCount);
      convertFloat32ToFloat16(srcData, fp16Data.data(), pixelCount);

      emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
      emscripten::val pixelData = Uint16Array.new_(emscripten::val(static_cast<double>(pixelCount)));
      emscripten::val jsHeap = emscripten::val(
          emscripten::typed_memory_view(pixelCount, fp16Data.data()));
      pixelData.call<void>("set", jsHeap);

      result.set("data", pixelData);
      result.set("pixelFormat", std::string("float16"));
      result.set("bitsPerChannel", 16);
    } else {
      // Keep as float32
      emscripten::val Float32Array = emscripten::val::global("Float32Array");
      emscripten::val pixelData = Float32Array.new_(emscripten::val(static_cast<double>(pixelCount)));
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
    // Guard the reinterpret/read against a truncated/malformed buffer.
    if (pixelCount > (dataSize / sizeof(uint16_t))) {
      result.set("success", false);
      result.set("error", std::string("Decoded 16-bit image buffer is smaller "
                                       "than reported dimensions."));
      return result;
    }
    const uint16_t* srcData = reinterpret_cast<const uint16_t*>(img.data.data());

    // Return as Uint16Array (native format)
    emscripten::val Uint16Array = emscripten::val::global("Uint16Array");
    emscripten::val pixelData = Uint16Array.new_(emscripten::val(static_cast<double>(pixelCount)));
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
    emscripten::val pixelData = Uint8Array.new_(emscripten::val(static_cast<double>(dataSize)));
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
  emscripten::val result = Uint16Array.new_(emscripten::val(static_cast<double>(count)));
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
  emscripten::val result = Float32Array.new_(emscripten::val(static_cast<double>(count)));
  emscripten::val jsHeap = emscripten::val(
      emscripten::typed_memory_view(count, fp32Data.data()));
  result.call<void>("set", jsHeap);

  return result;
}

// ============================================================================
// RenderStream — incremental, low-memory render-data extraction.
//
// Loads ONLY the root USDC crate (the caller extracts it from the .usdz in JS
// and keeps the texture entries there, off the WASM heap) into the next pipeline
// with LAZY arrays, so the crate sits in the heap exactly once (~= input size).
// getMesh(i) then materializes a SINGLE mesh's geometry on demand into a reused
// scratch and returns zero-copy descriptors; the next getMesh(i) overwrites the
// scratch, so at most one mesh's geometry is decoded at a time. Geometry arrays
// are read through a *copy* of the lazy Value (Value tmp = *v) so the Stage's own
// property stays lazy and the per-mesh decode never accumulates across meshes.
//
// Peak WASM heap = crate + one mesh's largest array (vs. the eager
// loadFromBinary() path which builds the whole typed Stage + RenderScene at once,
// peaking at ~5-10x input). Material parameters resolve to UsdPreviewSurface
// values + texture ASSET PATHS; the JS caller maps a path to its in-archive
// texture entry (which it already holds) and uploads it to the GPU.
// ============================================================================
class RenderStream {
 public:
  RenderStream() = default;

  // Adopt the root crate bytes by move and load lazily.
  emscripten::val beginOwned(std::string &&crate) {
    emscripten::val r = emscripten::val::object();
    end();
    error_.clear();
    tinyusdz::next::USDCLoadResult res =
        tinyusdz::next::LoadUSDCFromMemoryOwned(std::move(crate));
    if (!res.success) {
      error_ = res.error_summary.empty() ? std::string("USDC load failed")
                                         : res.error_summary;
      r.set("success", false);
      r.set("error", error_);
      return r;
    }
    stage_ = std::move(res.stage);
    meshes_ = tinyusdz::next::GetAllMeshes(stage_);
    loaded_ = true;
    r.set("success", true);
    r.set("meshCount", static_cast<int>(meshes_.size()));
    return r;
  }

  // Begin from a JS Uint8Array (one copy into the WASM heap, then adopted).
  emscripten::val begin(emscripten::val bytes) {
    const size_t size = bytes["byteLength"].as<size_t>();
    std::string s;
    s.resize(size);
    if (size > 0) {
      emscripten::val view = emscripten::val::global("Uint8Array").new_(
          bytes["buffer"], bytes["byteOffset"],
          emscripten::val(static_cast<double>(size)));
      emscripten::val heapView = emscripten::val(emscripten::typed_memory_view(
          size, reinterpret_cast<uint8_t *>(&s[0])));
      heapView.call<void>("set", view);
    }
    return beginOwned(std::move(s));
  }

  int meshCount() const { return loaded_ ? static_cast<int>(meshes_.size()) : 0; }
  std::string error() const { return error_; }

  // Materialize mesh i's geometry into the scratch and return zero-copy
  // descriptors {points,indices,normals,uv0} + resolved material. Valid until the
  // next getMesh()/end(); the JS caller must upload before calling getMesh again.
  emscripten::val getMesh(int i) {
    emscripten::val out = emscripten::val::object();
    if (!loaded_ || i < 0 || i >= static_cast<int>(meshes_.size())) {
      out.set("error", std::string("invalid mesh index"));
      return out;
    }
    const tinyusdz::next::UsdPrim &prim = meshes_[static_cast<size_t>(i)].GetPrim();

    const bool soup = buildRenderMesh_(prim);  // indexed, or non-indexed soup

    out.set("vertexCount", static_cast<double>(s_points_.size() / 3));
    out.set("primName", prim.GetName());
    out.set("points", heapF_(s_points_, 3));
    if (!soup && !s_indices_.empty()) out.set("indices", heapU32_(s_indices_));
    if (!s_normals_.empty()) out.set("normals", heapF_(s_normals_, 3));
    if (!s_uv_.empty()) out.set("uv0", heapF_(s_uv_, 2));
    out.set("material", resolveMaterial_(prim));
    return out;
  }

  // Free the stage, mesh list and scratch (returns the heap to the allocator).
  void end() {
    loaded_ = false;
    meshes_.clear();
    meshes_.shrink_to_fit();
    stage_ = tinyusdz::next::Stage();
    freeVec_(s_points_);
    freeVec_(s_normals_);
    freeVec_(s_uv_);
    freeVec_(s_indices_);
  }

 private:
  template <typename T>
  static void freeVec_(std::vector<T> &v) { std::vector<T>().swap(v); }

  // Read an array property through a COPY of the lazy Value, so the Stage's own
  // property stays lazy (per-mesh decode does not accumulate across meshes).
  std::vector<float> matFloat_(const tinyusdz::next::UsdPrim &prim, const char *name) {
    const tinyusdz::next::Value *v = prim.GetPropertyValue(name);
    if (!v) return {};
    tinyusdz::next::Value tmp = *v;
    const std::vector<float> *a = tmp.as_float_array();
    return a ? *a : std::vector<float>{};
  }
  std::vector<int32_t> matInt_(const tinyusdz::next::UsdPrim &prim, const char *name) {
    const tinyusdz::next::Value *v = prim.GetPropertyValue(name);
    if (!v) return {};
    tinyusdz::next::Value tmp = *v;
    const std::vector<int32_t> *a = tmp.as_int_array();
    return a ? *a : std::vector<int32_t>{};
  }

  // Build render geometry for one mesh into the scratch (s_points_/s_normals_/
  // s_uv_/s_indices_). Returns true if the result is a NON-INDEXED triangle soup
  // (drawn with drawArrays), false if INDEXED.
  //   - all primvars per-vertex  -> keep the indexed form directly (compact);
  //   - indexed UVs / per-vertex UV with face-varying normals -> de-index AND
  //     WELD inline (one vertex per distinct pos/uv/normal tuple), recovering
  //     vertex sharing while keeping correct attributes at seams;
  //   - PURE face-varying UVs (no st indices) -> emit the non-indexed soup, the
  //     minimal form when corners are mostly unique (welding would only add
  //     index + hash-map overhead).
  // The full soup is never materialized in the welded path; at most one mesh is
  // resident at a time either way.
  bool buildRenderMesh_(const tinyusdz::next::UsdPrim &prim) {
    std::vector<float> P = matFloat_(prim, "points");
    std::vector<int32_t> fvc = matInt_(prim, "faceVertexCounts");
    std::vector<int32_t> fvi = matInt_(prim, "faceVertexIndices");
    std::vector<float> N = matFloat_(prim, "normals");
    std::vector<float> UV = matFloat_(prim, "primvars:st");
    if (UV.empty()) UV = matFloat_(prim, "primvars:st0");
    if (UV.empty()) UV = matFloat_(prim, "st");
    std::vector<int32_t> stIdx = matInt_(prim, "primvars:st:indices");

    const size_t vtxCount = P.size() / 3;
    const size_t faceVtx = fvi.size();
    const size_t uvCount = UV.size() / 2;
    const size_t nCount = N.size() / 3;

    const bool uvFaceVarying = !UV.empty() && uvCount != vtxCount &&
                               (uvCount == faceVtx || !stIdx.empty());
    const bool nFaceVarying = !N.empty() && nCount != vtxCount && nCount == faceVtx;
    const bool needExpand = uvFaceVarying || nFaceVarying || !stIdx.empty();

    s_points_.clear(); s_normals_.clear(); s_uv_.clear(); s_indices_.clear();

    if (!needExpand) {
      s_points_ = std::move(P);
      triangulate_(fvi, fvc, s_indices_);
      if (nCount == vtxCount) s_normals_ = std::move(N);
      else computeNormals_(s_points_, s_indices_, s_normals_);
      if (uvCount == vtxCount) s_uv_ = std::move(UV);
      return false;
    }

    const bool haveN = (nCount == vtxCount) || nFaceVarying;

    // Decide weld vs soup by POSITION sharing, not interpolation type: a welded
    // mesh has at least vtxCount vertices, so it can only beat the (index-free)
    // soup when positions are heavily shared (vtxCount well below the triangle-
    // corner count). Face-varying UVs still weld well when positions share — what
    // matters is the expansion factor. When vtxCount is already close to the
    // corner count, the soup is minimal, so skip welding and keep it.
    size_t triCount = 0;
    for (int32_t nn : fvc) if (nn >= 3) triCount += static_cast<size_t>(nn - 2);
    const size_t cornerCount = triCount * 3;
    const bool doWeld = vtxCount > 0 && vtxCount * 3 < cornerCount;

    if (!doWeld) {
      // Non-indexed triangle soup (the minimal form for unique-per-corner UVs).
      std::vector<uint32_t> slots;
      size_t b = 0;
      for (int32_t n : fvc) {
        if (n >= 3 && b + static_cast<size_t>(n) <= faceVtx) {
          for (int32_t k = 2; k < n; ++k) {
            slots.push_back(static_cast<uint32_t>(b));
            slots.push_back(static_cast<uint32_t>(b + static_cast<size_t>(k) - 1));
            slots.push_back(static_cast<uint32_t>(b + static_cast<size_t>(k)));
          }
        }
        b += static_cast<size_t>(n < 0 ? 0 : n);
      }
      const size_t corners = slots.size();
      s_points_.resize(corners * 3);
      if (!UV.empty()) s_uv_.assign(corners * 2, 0.0f);
      if (haveN) s_normals_.resize(corners * 3);
      for (size_t c = 0; c < corners; ++c) {
        const uint32_t slot = slots[c];
        const uint32_t vi = (slot < faceVtx) ? static_cast<uint32_t>(fvi[slot]) : 0u;
        if (static_cast<size_t>(vi) * 3 + 2 < P.size()) {
          s_points_[c * 3] = P[vi * 3]; s_points_[c * 3 + 1] = P[vi * 3 + 1]; s_points_[c * 3 + 2] = P[vi * 3 + 2];
        }
        if (!UV.empty()) {
          const uint32_t ui = uvFaceVarying ? slot : vi;  // st:indices is empty here
          if (static_cast<size_t>(ui) * 2 + 1 < UV.size()) { s_uv_[c * 2] = UV[ui * 2]; s_uv_[c * 2 + 1] = UV[ui * 2 + 1]; }
        }
        if (haveN) {
          const uint32_t ni = nFaceVarying ? slot : vi;
          if (static_cast<size_t>(ni) * 3 + 2 < N.size()) {
            s_normals_[c * 3] = N[ni * 3]; s_normals_[c * 3 + 1] = N[ni * 3 + 1]; s_normals_[c * 3 + 2] = N[ni * 3 + 2];
          }
        }
      }
      if (s_normals_.empty()) computeNormals_(s_points_, s_indices_, s_normals_);  // empty idx -> flat per-tri
      return true;  // non-indexed soup
    }

    // De-index + weld: emit one welded vertex per unique (pos[,uv][,normal])
    // corner, producing an INDEXED mesh. Built inline as faces are walked, so the
    // full per-corner soup never exists — peak ~= welded verts + index buffer.
    struct WeldKey {
      uint32_t b[8];
      bool operator==(const WeldKey &o) const { return std::memcmp(b, o.b, sizeof(b)) == 0; }
    };
    struct WeldHash {
      size_t operator()(const WeldKey &k) const {
        uint64_t h = 1469598103934665603ull;  // FNV-1a (folded to size_t for wasm32)
        for (uint32_t w : k.b) { h ^= w; h *= 1099511628211ull; }
        return static_cast<size_t>(h ^ (h >> 32));
      }
    };
    std::unordered_map<WeldKey, uint32_t, WeldHash> weld;
    // Welded vertices are bounded below by the point count; reserve to cut
    // rehash spikes (which transiently inflate the peak).
    weld.reserve(vtxCount ? vtxCount * 2 : 1024);

    auto emit = [&](uint32_t slot) {
      const uint32_t vi = (slot < faceVtx) ? static_cast<uint32_t>(fvi[slot]) : 0u;
      float px = 0, py = 0, pz = 0, u = 0, v = 0, nx = 0, ny = 0, nz = 0;
      if (static_cast<size_t>(vi) * 3 + 2 < P.size()) { px = P[vi * 3]; py = P[vi * 3 + 1]; pz = P[vi * 3 + 2]; }
      if (!UV.empty()) {
        const uint32_t ui = (!stIdx.empty() && slot < stIdx.size())
                                ? static_cast<uint32_t>(stIdx[slot])
                                : (uvFaceVarying ? slot : vi);
        if (static_cast<size_t>(ui) * 2 + 1 < UV.size()) { u = UV[ui * 2]; v = UV[ui * 2 + 1]; }
      }
      if (haveN) {
        const uint32_t ni = nFaceVarying ? slot : vi;
        if (static_cast<size_t>(ni) * 3 + 2 < N.size()) { nx = N[ni * 3]; ny = N[ni * 3 + 1]; nz = N[ni * 3 + 2]; }
      }
      WeldKey key;
      std::memcpy(&key.b[0], &px, 4); std::memcpy(&key.b[1], &py, 4); std::memcpy(&key.b[2], &pz, 4);
      std::memcpy(&key.b[3], &u, 4); std::memcpy(&key.b[4], &v, 4);
      std::memcpy(&key.b[5], &nx, 4); std::memcpy(&key.b[6], &ny, 4); std::memcpy(&key.b[7], &nz, 4);
      auto it = weld.find(key);
      if (it != weld.end()) { s_indices_.push_back(it->second); return; }
      const uint32_t idx = static_cast<uint32_t>(s_points_.size() / 3);
      s_points_.push_back(px); s_points_.push_back(py); s_points_.push_back(pz);
      if (!UV.empty()) { s_uv_.push_back(u); s_uv_.push_back(v); }
      if (haveN) { s_normals_.push_back(nx); s_normals_.push_back(ny); s_normals_.push_back(nz); }
      weld.emplace(key, idx);
      s_indices_.push_back(idx);
    };

    size_t base = 0;
    for (int32_t n : fvc) {
      if (n >= 3 && base + static_cast<size_t>(n) <= faceVtx) {
        for (int32_t k = 2; k < n; ++k) {
          emit(static_cast<uint32_t>(base));
          emit(static_cast<uint32_t>(base + static_cast<size_t>(k) - 1));
          emit(static_cast<uint32_t>(base + static_cast<size_t>(k)));
        }
      }
      base += static_cast<size_t>(n < 0 ? 0 : n);
    }
    // Normals not authored -> smooth normals on the welded indexed mesh.
    if (!haveN) computeNormals_(s_points_, s_indices_, s_normals_);
    return false;  // welded result is INDEXED
  }

  // Resolve a UsdUVTexture connection path ("/.../Tex.outputs:rgb") to its
  // inputs:file asset path, which the JS caller maps to an archive texture entry.
  std::string texFile_(const std::string &connPath) {
    if (connPath.empty()) return "";
    const size_t slash = connPath.rfind('/');
    const size_t dot = connPath.find('.', slash == std::string::npos ? 0 : slash);
    const std::string primPath = (dot == std::string::npos) ? connPath : connPath.substr(0, dot);
    tinyusdz::next::UsdPrim tex = stage_.GetPrimAtPath(primPath);
    if (!tex.IsValid()) return "";
    const tinyusdz::next::Value *v = tex.GetPropertyValue("inputs:file");
    if (!v) return "";
    if (const std::string *a = v->as_asset_path()) return *a;
    if (const std::string *s = v->as_string()) return *s;
    return "";
  }

  // Fan-triangulate faceVertexIndices grouped by faceVertexCounts.
  static void triangulate_(const std::vector<int32_t> &fvi,
                           const std::vector<int32_t> &fvc,
                           std::vector<uint32_t> &out) {
    out.clear();
    if (fvi.empty()) return;
    if (fvc.empty()) {  // assume an already-triangulated index list
      out.reserve(fvi.size());
      for (int32_t v : fvi) out.push_back(static_cast<uint32_t>(v));
      return;
    }
    size_t base = 0;
    for (int32_t n : fvc) {
      if (n < 3 || base + static_cast<size_t>(n) > fvi.size()) { base += static_cast<size_t>(n < 0 ? 0 : n); continue; }
      for (int32_t k = 2; k < n; ++k) {
        out.push_back(static_cast<uint32_t>(fvi[base]));
        out.push_back(static_cast<uint32_t>(fvi[base + static_cast<size_t>(k) - 1]));
        out.push_back(static_cast<uint32_t>(fvi[base + static_cast<size_t>(k)]));
      }
      base += static_cast<size_t>(n);
    }
  }

  // Area-weighted vertex normals from the triangulated indices.
  static void computeNormals_(const std::vector<float> &pos,
                              const std::vector<uint32_t> &idx,
                              std::vector<float> &out) {
    out.assign(pos.size(), 0.0f);
    const size_t nv = pos.size() / 3;
    auto addTri = [&](uint32_t a, uint32_t b, uint32_t c) {
      if (a >= nv || b >= nv || c >= nv) return;
      const float ex1 = pos[b * 3] - pos[a * 3], ey1 = pos[b * 3 + 1] - pos[a * 3 + 1], ez1 = pos[b * 3 + 2] - pos[a * 3 + 2];
      const float ex2 = pos[c * 3] - pos[a * 3], ey2 = pos[c * 3 + 1] - pos[a * 3 + 1], ez2 = pos[c * 3 + 2] - pos[a * 3 + 2];
      const float nx = ey1 * ez2 - ez1 * ey2, ny = ez1 * ex2 - ex1 * ez2, nz = ex1 * ey2 - ey1 * ex2;
      for (uint32_t vi : {a, b, c}) { out[vi * 3] += nx; out[vi * 3 + 1] += ny; out[vi * 3 + 2] += nz; }
    };
    if (!idx.empty()) {
      for (size_t t = 0; t + 2 < idx.size(); t += 3) addTri(idx[t], idx[t + 1], idx[t + 2]);
    } else {
      for (uint32_t v = 0; v + 2 < nv; v += 3) addTri(v, v + 1, v + 2);
    }
    for (size_t i = 0; i < nv; ++i) {
      float x = out[i * 3], y = out[i * 3 + 1], z = out[i * 3 + 2];
      float l = std::sqrt(x * x + y * y + z * z);
      if (l > 0) { out[i * 3] = x / l; out[i * 3 + 1] = y / l; out[i * 3 + 2] = z / l; }
      else { out[i * 3 + 2] = 1.0f; }
    }
  }

  emscripten::val heapF_(const std::vector<float> &v, int comps) const {
    emscripten::val d = emscripten::val::object();
    d.set("ptr", static_cast<double>(reinterpret_cast<uintptr_t>(v.data())));
    d.set("length", static_cast<double>(v.size()));
    d.set("comps", comps);
    d.set("dtype", std::string("f32"));
    d.set("byteLength", static_cast<double>(v.size() * sizeof(float)));
    return d;
  }
  emscripten::val heapU32_(const std::vector<uint32_t> &v) const {
    emscripten::val d = emscripten::val::object();
    d.set("ptr", static_cast<double>(reinterpret_cast<uintptr_t>(v.data())));
    d.set("length", static_cast<double>(v.size()));
    d.set("comps", 1);
    d.set("dtype", std::string("u32"));
    d.set("byteLength", static_cast<double>(v.size() * sizeof(uint32_t)));
    return d;
  }
  static emscripten::val arr3_(const float *c) {
    emscripten::val a = emscripten::val::array();
    a.call<void>("push", c[0]);
    a.call<void>("push", c[1]);
    a.call<void>("push", c[2]);
    return a;
  }

  // Resolve the mesh's bound material to UsdPreviewSurface values + texture
  // asset paths (resolved to GPU textures by the JS caller from the archive).
  emscripten::val resolveMaterial_(const tinyusdz::next::UsdPrim &prim) {
    emscripten::val m = emscripten::val::object();
    tinyusdz::next::UsdPrim mat = tinyusdz::next::GetBoundMaterial(stage_, prim);
    if (!mat.IsValid()) return m;
    // Resolve the surface shader: prefer the material's outputs:surface (a
    // connection), but fall back to the first UsdPreviewSurface child shader —
    // the common case and robust when the output connection is not resolved.
    tinyusdz::next::UsdPrim shader;
    const std::string shaderPath = tinyusdz::next::GetSurfaceShader(stage_, mat);
    if (!shaderPath.empty()) shader = stage_.GetPrimAtPath(shaderPath);
    if (!shader.IsValid()) {
      for (const auto &ch : mat.GetChildren()) {
        if (tinyusdz::next::IsPreviewSurface(ch)) { shader = ch; break; }
      }
    }
    if (!shader.IsValid()) return m;
    tinyusdz::next::PreviewSurfaceData ps;
    if (!tinyusdz::next::GetPreviewSurfaceData(stage_, shader, &ps)) return m;
    m.set("baseColor", arr3_(ps.diffuse_color));
    m.set("metallic", ps.metallic);
    m.set("roughness", ps.roughness);
    m.set("opacity", ps.opacity);
    m.set("occlusion", ps.occlusion);
    m.set("emissive", arr3_(ps.emissive_color));
    if (ps.opacity_threshold > 0.0f) m.set("opacityThreshold", ps.opacity_threshold);
    // PreviewSurfaceData texture fields are connection paths to the UsdUVTexture
    // shader; resolve each to its inputs:file asset path for the JS caller.
    auto setTex = [&](const char *key, const std::string &connPath) {
      const std::string file = texFile_(connPath);
      if (!file.empty()) m.set(key, file);
    };
    setTex("baseColorTexture", ps.diffuse_texture);
    setTex("normalTexture", ps.normal_texture);
    setTex("roughnessTexture", ps.roughness_texture);
    setTex("metallicTexture", ps.metallic_texture);
    setTex("occlusionTexture", ps.occlusion_texture);
    setTex("emissiveTexture", ps.emissive_texture);
    return m;
  }

  tinyusdz::next::Stage stage_;
  std::vector<tinyusdz::next::UsdGeomMesh> meshes_;
  bool loaded_ = false;
  std::string error_;
  std::vector<float> s_points_, s_normals_, s_uv_;
  std::vector<uint32_t> s_indices_;
};

EMSCRIPTEN_BINDINGS(render_stream_module) {
  emscripten::class_<RenderStream>("RenderStream")
      .constructor<>()
      .function("begin", &RenderStream::begin)
      .function("meshCount", &RenderStream::meshCount)
      .function("getMesh", &RenderStream::getMesh)
      .function("error", &RenderStream::error)
      .function("end", &RenderStream::end);
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
      .function("nextFlattenUSDC", &TinyUSDZLoaderNative::nextFlattenUSDC)
      .function("nextFlattenBuffer", &TinyUSDZLoaderNative::nextFlattenBuffer)
      .function("nextFlattenBufferToSink",
                &TinyUSDZLoaderNative::nextFlattenBufferToSink)
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
      .function("getMesh", &TinyUSDZLoaderNative::getMesh)  // deprecated: use getMeshPtr/getMeshCopy
      .function("getMeshPtr", &TinyUSDZLoaderNative::getMeshPtr)
      .function("getMeshCopy", &TinyUSDZLoaderNative::getMeshCopy)
      .function("numMeshes", &TinyUSDZLoaderNative::numMeshes)
      .function("numInstances", &TinyUSDZLoaderNative::numInstances)
      .function("getInstance", &TinyUSDZLoaderNative::getInstance)
      .function("getInstancesForMesh", &TinyUSDZLoaderNative::getInstancesForMesh)
      .function("generateBoneTexture", &TinyUSDZLoaderNative::generateBoneTexture)
      .function("getMaterial", select_overload<emscripten::val(int) const>(&TinyUSDZLoaderNative::getMaterial))
      .function("getMaterialWithFormat", select_overload<emscripten::val(int, const std::string&) const>(&TinyUSDZLoaderNative::getMaterial))
      .function("numMaterials", &TinyUSDZLoaderNative::numMaterials)
      .function("getLight", &TinyUSDZLoaderNative::getLight)
      .function("getLightWithFormat", &TinyUSDZLoaderNative::getLightWithFormat)
      .function("getAllLights", &TinyUSDZLoaderNative::getAllLights)
      .function("numLights", &TinyUSDZLoaderNative::numLights)
      .function("getCamera", &TinyUSDZLoaderNative::getCamera)
      .function("numCameras", &TinyUSDZLoaderNative::numCameras)
      .function("getTexture", &TinyUSDZLoaderNative::getTexture)
      .function("numTextures", &TinyUSDZLoaderNative::numTextures)
      .function("getImage", &TinyUSDZLoaderNative::getImage)  // deprecated: use getImagePtr/getImageCopy
      .function("getImagePtr", &TinyUSDZLoaderNative::getImagePtr)
      .function("getImageCopy", &TinyUSDZLoaderNative::getImageCopy)
      .function("numImages", &TinyUSDZLoaderNative::numImages)
      .function("numUDIMTextures", &TinyUSDZLoaderNative::numUDIMTextures)
      .function("getUDIMTexture", &TinyUSDZLoaderNative::getUDIMTexture)
      .function("setCombineUDIMTiles",
                &TinyUSDZLoaderNative::setCombineUDIMTiles)
      .function("getCombineUDIMTiles",
                &TinyUSDZLoaderNative::getCombineUDIMTiles)
      .function("setAllowParentRelativeAssetPaths",
                &TinyUSDZLoaderNative::setAllowParentRelativeAssetPaths)
      .function("getAllowParentRelativeAssetPaths",
                &TinyUSDZLoaderNative::getAllowParentRelativeAssetPaths)
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

      // Skeleton hierarchy methods
      .function("numSkeletons", &TinyUSDZLoaderNative::numSkeletons)
      .function("getSkeleton", &TinyUSDZLoaderNative::getSkeleton)
      .function("getAllSkeletons", &TinyUSDZLoaderNative::getAllSkeletons)
      .function("getSkeletonJointsFlat", &TinyUSDZLoaderNative::getSkeletonJointsFlat)

      .function("setLoadTextureInNative",
                &TinyUSDZLoaderNative::setLoadTextureInNative)

      .function("setMaxMemoryLimitMB",
                &TinyUSDZLoaderNative::setMaxMemoryLimitMB)
      .function("getMaxMemoryLimitMB",
                &TinyUSDZLoaderNative::getMaxMemoryLimitMB)

      // Bone reduction configuration
      // Sphere tessellation
      .function("setSphereSubdivisions",
                &TinyUSDZLoaderNative::setSphereSubdivisions)
      .function("getSphereSubdivisions",
                &TinyUSDZLoaderNative::getSphereSubdivisions)

      .function("setEnableBoneReduction",
                &TinyUSDZLoaderNative::setEnableBoneReduction)
      .function("getEnableBoneReduction",
                &TinyUSDZLoaderNative::getEnableBoneReduction)
      .function("setEnableValueClips",
                &TinyUSDZLoaderNative::setEnableValueClips)
      .function("getEnableValueClips",
                &TinyUSDZLoaderNative::getEnableValueClips)
      .function("setValueClipSampleRate",
                &TinyUSDZLoaderNative::setValueClipSampleRate)
      .function("getValueClipSampleRate",
                &TinyUSDZLoaderNative::getValueClipSampleRate)
      .function("setValueClipUseTimeRange",
                &TinyUSDZLoaderNative::setValueClipUseTimeRange)
      .function("getValueClipUseTimeRange",
                &TinyUSDZLoaderNative::getValueClipUseTimeRange)
      .function("setValueClipTimeRange",
                &TinyUSDZLoaderNative::setValueClipTimeRange)
      .function("getValueClipStartTime",
                &TinyUSDZLoaderNative::getValueClipStartTime)
      .function("getValueClipEndTime",
                &TinyUSDZLoaderNative::getValueClipEndTime)
      .function("setTargetBoneCount",
                &TinyUSDZLoaderNative::setTargetBoneCount)
      .function("getTargetBoneCount",
                &TinyUSDZLoaderNative::getTargetBoneCount)
      .function("setRoundBoneCount",
                &TinyUSDZLoaderNative::setRoundBoneCount)
      .function("getRoundBoneCount",
                &TinyUSDZLoaderNative::getRoundBoneCount)

      // Deferred tangent computation
      .function("setDeferTangentComputation",
                &TinyUSDZLoaderNative::setDeferTangentComputation)
      .function("getDeferTangentComputation",
                &TinyUSDZLoaderNative::getDeferTangentComputation)
      .function("computeMeshTangents",
                &TinyUSDZLoaderNative::computeMeshTangents)

      // MMap zero-copy (experimental, default off)
      .function("setMMapZeroCopy",
                &TinyUSDZLoaderNative::setMMapZeroCopy)
      .function("getMMapZeroCopy",
                &TinyUSDZLoaderNative::getMMapZeroCopy)

      .function("setEnableComposition",
                &TinyUSDZLoaderNative::setEnableComposition)
      .function("extractSublayerAssetPaths",
                &TinyUSDZLoaderNative::extractSublayerAssetPaths)
      .function("extractReferencesAssetPaths",
                &TinyUSDZLoaderNative::extractReferencesAssetPaths)
      .function("extractPayloadAssetPaths",
                &TinyUSDZLoaderNative::extractPayloadAssetPaths)

      .function("hasSublayers",
                &TinyUSDZLoaderNative::hasSublayers)

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
                &TinyUSDZLoaderNative::hasVariants)

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
      .function("getAssetCacheSizeBytes",
                &TinyUSDZLoaderNative::getAssetCacheSizeBytes)
      .function("setAssetCacheMaxSizeBytes",
                &TinyUSDZLoaderNative::setAssetCacheMaxSizeBytes)
      .function("getAssetCacheMaxSizeBytes",
                &TinyUSDZLoaderNative::getAssetCacheMaxSizeBytes)
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
      .function("validateFromBinary",
                &TinyUSDZLoaderNative::validateFromBinary)
      .function("validateLoadedLayer",
                &TinyUSDZLoaderNative::validateLoadedLayer)
      
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

      // USD Export
      .function("exportAsUSDA", &TinyUSDZLoaderNative::exportAsUSDA)
      .function("exportAsUSDC", &TinyUSDZLoaderNative::exportAsUSDC)
      .function("exportLayerAsUSDCWithOptions", &TinyUSDZLoaderNative::exportLayerAsUSDCWithOptions)
      .function("exportLayerAsUSDCToBufferWithOptions", &TinyUSDZLoaderNative::exportLayerAsUSDCToBufferWithOptions)
      .function("exportStageAsUSDCToBufferWithOptions", &TinyUSDZLoaderNative::exportStageAsUSDCToBufferWithOptions)
      .function("flattenLayer", &TinyUSDZLoaderNative::flattenLayer)
      .function("setUSDCExportLimitMB", &TinyUSDZLoaderNative::setUSDCExportLimitMB)
      .function("debugLogMemory", &TinyUSDZLoaderNative::debugLogMemory)
      .function("exportAsUSDZ", &TinyUSDZLoaderNative::exportAsUSDZ)
      .function("exportAsUSDZWithRemap", &TinyUSDZLoaderNative::exportAsUSDZWithRemap)
      .function("exportAsUSDZWithOptions", &TinyUSDZLoaderNative::exportAsUSDZWithOptions)
      .function("exportLayerAsUSDZWithOptions", &TinyUSDZLoaderNative::exportLayerAsUSDZWithOptions)
      .function("extractPhysicsSceneJSON", &TinyUSDZLoaderNative::extractPhysicsSceneJSON)
      .function("createSampleScene", &TinyUSDZLoaderNative::createSampleScene)
      .function("clearURDFMeshBuffers", &TinyUSDZLoaderNative::clearURDFMeshBuffers)
      .function("setVisualMesh", &TinyUSDZLoaderNative::setVisualMesh)
      .function("setCollisionMesh", &TinyUSDZLoaderNative::setCollisionMesh)
      .function("createURDFPhysicsScene", &TinyUSDZLoaderNative::createURDFPhysicsScene)
      .function("encodeImageNative", &TinyUSDZLoaderNative::encodeImageNative)

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

// ---------------------------------------------------------------------------
// USDZ-convert texture helpers (resize/re-encode + generic channel repack).
//
// NOTE: fpnge uses x86 SIMD intrinsics and is NOT compiled for WASM, so PNG
// encoding here transparently falls back to the portable `fpng` encoder.
// ---------------------------------------------------------------------------

// Copy a byte vector into a fresh JS Uint8Array (survives the C++ buffer).
static emscripten::val bytesToUint8Array(const std::vector<uint8_t>& v) {
  emscripten::val u8 = emscripten::val::global("Uint8Array").new_(emscripten::val(static_cast<double>(v.size())));
  if (!v.empty()) {
    u8.call<void>("set", emscripten::val(emscripten::typed_memory_view(
                             v.size(), v.data())));
  }
  return u8;
}

static int optInt(const emscripten::val& opts, const char* key, int def) {
  if (opts.isUndefined() || opts.isNull()) return def;
  emscripten::val v = opts[key];
  if (v.isUndefined() || v.isNull()) return def;
  return v.as<int>();
}

static std::string optStr(const emscripten::val& opts, const char* key,
                          const std::string& def) {
  if (opts.isUndefined() || opts.isNull()) return def;
  emscripten::val v = opts[key];
  if (v.isUndefined() || v.isNull()) return def;
  return v.as<std::string>();
}

static tinyusdz::image::PngEncoder parsePngEncoder(const std::string& s) {
  if (s == "fpng") return tinyusdz::image::PngEncoder::Fpng;
  if (s == "fpnge") return tinyusdz::image::PngEncoder::Fpnge;  // falls back to fpng in WASM
  return tinyusdz::image::PngEncoder::Auto;
}

// Drop the alpha channel (RGBA -> RGB) for JPEG output.
static tinyusdz::Image dropAlpha(const tinyusdz::Image& img) {
  if (img.channels != 4) return img;
  if (img.width <= 0 || img.height <= 0) return img;
  size_t npix = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
  if (img.data.size() < npix * 4) return img;  // truncated source
  if (npix > SIZE_MAX / 3) return img;  // overflow guard
  tinyusdz::Image out;
  out.width = img.width; out.height = img.height; out.channels = 3;
  out.bpp = 8; out.format = img.format; out.colorspace = img.colorspace;
  out.data.resize(npix * 3);
  for (size_t i = 0; i < npix; i++) {
    out.data[3 * i + 0] = img.data[4 * i + 0];
    out.data[3 * i + 1] = img.data[4 * i + 1];
    out.data[3 * i + 2] = img.data[4 * i + 2];
  }
  return out;
}

// ACES filmic tonemap (Stephen Hill's "ACES Fitted"): scene-linear -> ACEScg,
// the RRT+ODT fit, then back to sRGB/Rec.709 linear. This compresses HDR
// highlights the way DCC tools (Blender, Unreal) do by default.
static inline float acesRrtOdtFit(float v) {
  const float a = v * (v + 0.0245786f) - 0.000090537f;
  const float b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
  return (b != 0.0f) ? (a / b) : 0.0f;
}
// In-place ACES tonemap of one linear RGB triple. Output is sRGB/Rec.709 linear
// (apply the sRGB OETF afterwards before quantizing to 8-bit).
static inline void acesFittedRGB(float& r, float& g, float& b) {
  // sRGB/Rec.709 linear -> ACEScg (ACESInputMat).
  const float ir = 0.59719f * r + 0.35458f * g + 0.04823f * b;
  const float ig = 0.07600f * r + 0.90834f * g + 0.01566f * b;
  const float ib = 0.02840f * r + 0.13383f * g + 0.83777f * b;
  const float fr = acesRrtOdtFit(ir);
  const float fg = acesRrtOdtFit(ig);
  const float fb = acesRrtOdtFit(ib);
  // ACEScg -> sRGB/Rec.709 linear (ACESOutputMat).
  r =  1.60475f * fr - 0.53108f * fg - 0.07367f * fb;
  g = -0.10208f * fr + 1.10813f * fg - 0.00605f * fb;
  b = -0.00327f * fr - 0.07276f * fg + 1.07602f * fb;
}

// Convert an fp32 (HDR/EXR) image to 8-bit LDR so it can be written as PNG/JPEG.
// USDZ delivers color textures as sRGB-encoded 8-bit, while EXR data is
// scene-linear, so we apply a default ACES filmic tonemap to the color channels
// and then the sRGB OETF; an alpha channel (index 3) is treated as linear data.
// Returns the input unchanged if it is not fp32 float.
// TODO: exposure/EV control and per-texture colorspace (data vs color) handling.
static tinyusdz::Image floatImageTo8bit(const tinyusdz::Image& img) {
  using PF = tinyusdz::Image::PixelFormat;
  if (!(img.bpp == 32 && img.format == PF::Float)) return img;
  if (img.width <= 0 || img.height <= 0 || img.channels < 1 || img.channels > 4) {
    return img;
  }
  const size_t npix = static_cast<size_t>(img.width) *
                      static_cast<size_t>(img.height);
  const size_t ch = static_cast<size_t>(img.channels);
  if (npix > SIZE_MAX / ch) return img;
  if (img.data.size() < npix * ch * sizeof(float)) return img;

  auto srgb8 = [](float x) -> uint8_t {
    if (!(x > 0.0f)) x = 0.0f;  // also maps NaN -> 0
    if (x > 1.0f) x = 1.0f;
    const float s = (x <= 0.0031308f) ? (12.92f * x)
                                      : (1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f);
    int v = static_cast<int>(s * 255.0f + 0.5f);
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
  };
  auto lin8 = [](float x) -> uint8_t {
    if (!(x > 0.0f)) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    int v = static_cast<int>(x * 255.0f + 0.5f);
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
  };

  const float* src = reinterpret_cast<const float*>(img.data.data());
  tinyusdz::Image out;
  out.width = img.width; out.height = img.height; out.channels = img.channels;
  out.bpp = 8; out.format = PF::UInt; out.colorspace = img.colorspace;
  out.data.resize(npix * ch);
  for (size_t i = 0; i < npix; i++) {
    const float* p = &src[i * ch];
    uint8_t* o = &out.data[i * ch];
    if (ch >= 3) {
      float r = p[0], g = p[1], b = p[2];
      acesFittedRGB(r, g, b);     // tonemap color, then sRGB-encode below.
      o[0] = srgb8(r);
      o[1] = srgb8(g);
      o[2] = srgb8(b);
      if (ch == 4) o[3] = lin8(p[3]);
    } else if (ch == 2) {
      o[0] = srgb8(acesRrtOdtFit(p[0]));  // grayscale tonemap (no matrix)
      o[1] = lin8(p[1]);
    } else {  // ch == 1
      o[0] = srgb8(acesRrtOdtFit(p[0]));
    }
  }
  return out;
}

// Read a scalar token/string attribute from a PrimSpec (default time).
static std::string psAttrStr(const tinyusdz::PrimSpec& ps, const char* name) {
  auto it = ps.props().find(name);
  if (it == ps.props().end() || !it->second.is_attribute()) return "";
  const auto& a = it->second.get_attribute();
  if (auto v = a.get_value<tinyusdz::value::token>()) return v.value().str();
  if (auto v = a.get_value<std::string>()) return v.value();
  if (auto v = a.get_value<tinyusdz::value::AssetPath>())
    return v.value().GetAssetPath();
  return "";
}

// Walk PrimSpecs collecting {texture-file-basename -> sourceColorSpace} from
// UsdUVTexture shaders (authored value only; absent => "auto").
static void collectTexColorspaces(const tinyusdz::PrimSpec& ps,
                                  emscripten::val& out) {
  if (ps.typeName() == "Shader" &&
      psAttrStr(ps, "info:id") == "UsdUVTexture") {
    std::string file = psAttrStr(ps, "inputs:file");
    if (!file.empty()) {
      size_t s = file.find_last_of("/\\");
      std::string base = (s == std::string::npos) ? file : file.substr(s + 1);
      if (!base.empty()) {
        std::string cs = psAttrStr(ps, "inputs:sourceColorSpace");
        out.set(base, cs.empty() ? std::string("auto") : cs);
      }
    }
  }
  for (const auto& c : ps.children()) collectTexColorspaces(c, out);
}

// getTextureColorspaceMap(rootUsdcBytes) -> { "<basename>": "sRGB"|"raw"|"auto" }
// Loads the root layer and reports each UsdUVTexture's authored sourceColorSpace
// so the JS pipeline can pick a per-texture resize colorspace (role-aware).
// Returns an empty object on load failure (caller falls back to a global default).
emscripten::val getTextureColorspaceMap(const emscripten::val& data) {
  emscripten::val result = emscripten::val::object();
  std::vector<uint8_t> buffer;
  copyFromJSBuffer(data, buffer);
  tinyusdz::Layer layer;
  std::string warn, err;
  if (!tinyusdz::LoadLayerFromMemory(buffer.data(), buffer.size(), "root",
                                     &layer, &warn, &err)) {
    return result;
  }
  for (const auto& kv : layer.primspecs()) collectTexColorspaces(kv.second, result);
  return result;
}

// convertImage(data, opts) -> { success, data?:Uint8Array, width, height, resized, error? }
// opts: { maxSize?, width?, height?, format?:"png"|"jpeg", pngEncoder?, jpegQuality? }
// The streaming PNG->PNG transcoder now lives in src/imageio/png-stream.cc
// (tinyusdz::imageio::TranscodePNG); convertImage() calls it for the PNG
// no-resize fast path below.
emscripten::val convertImage(const emscripten::val& data,
                             const emscripten::val& opts) {
  using namespace tinyusdz;
  emscripten::val result = emscripten::val::object();

  std::vector<uint8_t> buffer;
  copyFromJSBuffer(data, buffer);

  // Fast path: PNG -> PNG, scanline-streamed (peak ~a few scanlines + the
  // compressed output) instead of decoding the whole image to RGBA + a
  // full-image resize/encode buffer (~hundreds of MB for large textures).
  //   - no resize        -> TranscodePNG (filter-optimize + recompress)
  //   - resize requested -> ResizePNG (stbir scanline callbacks)
  // Falls through to the whole-image path on any unsupported case.
  {
    static const uint8_t PNGSIG[8] = {0x89, 'P', 'N', 'G', 0x0D,
                                      0x0A, 0x1A, 0x0A};
    const std::string fmt0 = optStr(opts, "format", "png");
    const bool is_png = buffer.size() > 24 &&
                        std::memcmp(buffer.data(), PNGSIG, 8) == 0;
    auto rd32 = [](const uint8_t* p) {
      return (int)((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                   (uint32_t(p[2]) << 8) | uint32_t(p[3]));
    };
    if (fmt0 == "png" && is_png) {
      const int W = rd32(buffer.data() + 16);  // IHDR width
      const int H = rd32(buffer.data() + 20);  // IHDR height
      const int maxSize = optInt(opts, "maxSize", 0);
      int tw = optInt(opts, "width", 0);
      int th = optInt(opts, "height", 0);
      // Mirror the whole-image path's target-dimension computation exactly.
      if ((tw <= 0 || th <= 0) && maxSize > 0 && W > 0 && H > 0) {
        const int longest = (std::max)(W, H);
        if (longest > maxSize) {
          const double sc = double(maxSize) / double(longest);
          tw = (std::max)(1, int(W * sc + 0.5));
          th = (std::max)(1, int(H * sc + 0.5));
        }
      }
      const bool wantResize = (tw > 0 && th > 0 && (tw != W || th != H));
      // Resize colorspace: "srgb" resamples sRGB color textures in linear light
      // (correct downsampling, avoids the gamma-space darkening of mipmaps).
      // Default keeps gamma-space (linear-filter) resampling, which matches the
      // legacy path AND is correct for linear DATA maps (normal / ORM / height)
      // — forcing sRGB on those would corrupt them, so this is opt-in.
      const std::string rcs = optStr(opts, "resizeColorspace", "");
      const bool resizeSrgb = (rcs == "srgb");
      // Optional sRGB<->linear colorspace conversion (no-resize PNG path).
      const std::string cs = optStr(opts, "colorspace", "");
      const bool wantCS = (cs == "srgb-to-linear" || cs == "srgbToLinear" ||
                           cs == "linear-to-srgb" || cs == "linearToSrgb");
      std::vector<uint8_t> trans;
      if (!wantResize && wantCS) {
        auto xf = (cs[0] == 's')
                      ? tinyusdz::imageio::ColorspaceXform::SrgbToLinear
                      : tinyusdz::imageio::ColorspaceXform::LinearToSrgb;
        if (tinyusdz::imageio::ConvertColorspacePNG(buffer.data(),
                                                    buffer.size(), xf, trans)) {
          result.set("success", true);
          result.set("width", rd32(trans.data() + 16));
          result.set("height", rd32(trans.data() + 20));
          result.set("resized", false);
          result.set("data", bytesToUint8Array(trans));
          return result;
        }
      } else if (!wantResize) {
        if (tinyusdz::imageio::TranscodePNG(buffer.data(), buffer.size(),
                                            trans)) {
          result.set("success", true);
          result.set("width", rd32(trans.data() + 16));
          result.set("height", rd32(trans.data() + 20));
          result.set("resized", false);
          result.set("data", bytesToUint8Array(trans));
          return result;
        }
      } else {
        // srgb=false (linear) matches ResizeImage(Auto) on a colorspace-less PNG;
        // resizeColorspace:"srgb" opts into linear-light resampling.
        if (tinyusdz::imageio::ResizePNG(buffer.data(), buffer.size(),
                                         (uint32_t)tw, (uint32_t)th,
                                         resizeSrgb, trans)) {
          result.set("success", true);
          result.set("width", rd32(trans.data() + 16));
          result.set("height", rd32(trans.data() + 20));
          result.set("resized", true);
          result.set("data", bytesToUint8Array(trans));
          return result;
        }
      }
    }
  }

  Image img;
  {
    // EXR -> EXR keeps half precision end-to-end: fp16 decode -> fp16 resize ->
    // fp16 encode, with NO fp32 widening (halves HDR memory). Taken only when
    // every channel is half (DecodeImageEXRHalf returns false otherwise) and the
    // output is EXR; everything else uses the fp32 LoadImageFromMemory path.
    static const uint8_t EXRMAGIC[4] = {0x76, 0x2f, 0x31, 0x01};
    const bool is_exr = buffer.size() > 4 &&
                        std::memcmp(buffer.data(), EXRMAGIC, 4) == 0;
    const bool out_exr = optStr(opts, "format", "png") == "exr";
    bool got = false;
    if (is_exr && out_exr) {
      std::string e;
      got = tinyusdz::image::DecodeImageEXRHalf(buffer.data(), buffer.size(),
                                                "mem", &img, &e);
    }
    if (!got) {
      auto loaded =
          image::LoadImageFromMemory(buffer.data(), buffer.size(), "mem");
      if (!loaded) {
        result.set("success", false);
        result.set("error", loaded.error());
        return result;
      }
      img = std::move(loaded.value().image);
    }
  }
  // The compressed input is no longer needed once decoded; release it so it
  // does not coexist with the (much larger) decoded RGBA + the encoder output.
  std::vector<uint8_t>().swap(buffer);

  const int maxSize = optInt(opts, "maxSize", 0);
  int tw = optInt(opts, "width", 0);
  int th = optInt(opts, "height", 0);
  const std::string format = optStr(opts, "format", "png");
  const std::string pngEnc = optStr(opts, "pngEncoder", "auto");
  const int jpegQ = optInt(opts, "jpegQuality", 90);
  // resizeColorspace:"srgb" resamples in linear light (correct for sRGB color
  // textures); default keeps gamma-space (Auto -> linear on colorspace-less
  // images), correct for linear data maps.
  const bool resizeSrgb = optStr(opts, "resizeColorspace", "") == "srgb";

  // Resize works for both 8-bit (LDR) and fp32 (HDR/EXR) images now.
  bool resized = false;
  {
    if ((tw <= 0 || th <= 0) && maxSize > 0) {
      const int longest = (std::max)(img.width, img.height);
      if (longest > maxSize) {
        const double sc = double(maxSize) / double(longest);
        tw = (std::max)(1, int(img.width * sc + 0.5));
        th = (std::max)(1, int(img.height * sc + 0.5));
      }
    }
    if (tw > 0 && th > 0 && (tw != img.width || th != img.height)) {
      Image out; std::string rerr;
      const tydra::ResizeFilter rfilter =
          resizeSrgb ? tydra::ResizeFilter::SRGB : tydra::ResizeFilter::Auto;
      if (tydra::ResizeImage(img, tw, th, &out, rfilter, &rerr)) {
        img = std::move(out);
        resized = true;
      } else {
        result.set("success", false);
        result.set("error", rerr);
        return result;
      }
    }
  }

  image::WriteOption wopt;
  wopt.png_encoder = parsePngEncoder(pngEnc);
  wopt.jpeg_quality = jpegQ;
  if (format == "exr") {
    // Keep HDR/float data; WriteImageToMemory promotes 8-bit input if needed.
    // (EXR output is already encoded as fp16 — the compact texture form.)
    wopt.format = image::WriteImageFormat::EXR;
  } else if (format == "jpeg" || format == "jpg") {
    wopt.format = image::WriteImageFormat::JPEG;
    img = floatImageTo8bit(img);  // tone-map fp32 -> 8-bit (no-op if 8-bit)
    img = dropAlpha(img);
  } else {
    wopt.format = image::WriteImageFormat::PNG;
    img = floatImageTo8bit(img);  // tone-map fp32 -> 8-bit (no-op if 8-bit)
  }

  auto enc = image::WriteImageToMemory(img, wopt);
  if (!enc) {
    result.set("success", false);
    result.set("error", enc.error());
    return result;
  }

  result.set("success", true);
  result.set("data", bytesToUint8Array(enc.value()));
  result.set("width", img.width);
  result.set("height", img.height);
  result.set("resized", resized);
  return result;
}

// repackChannels(opts) -> { success, data?:Uint8Array, width, height, channels, error? }
// opts: { channels?, width?, height?, format?, pngEncoder?, jpegQuality?,
//         r/g/b/a: { data?:Uint8Array, channel?:int, const?:int } }
emscripten::val repackChannels(const emscripten::val& opts) {
  using namespace tinyusdz;
  emscripten::val result = emscripten::val::object();

  const char* slot_names[4] = {"r", "g", "b", "a"};

  // Streaming fast path: all referenced inputs are 8-bit, same-size, non-palette
  // PNGs, output is PNG, and no resize is requested. Pack per-scanline (peak ~N
  // input rows + one output row) instead of decoding every input whole plus a
  // whole-image output buffer. Falls through to the whole-image path otherwise.
  {
    const std::string fmt = optStr(opts, "format", "png");
    if (fmt == "png") {
      struct SlotS { bool has = false; std::vector<uint8_t> bytes; int channel = 0; uint8_t cst = 0; };
      SlotS s[4];
      const int req_w = optInt(opts, "width", 0);
      const int req_h = optInt(opts, "height", 0);
      int reqCh = optInt(opts, "channels", 0);
      int inferred = 0;
      bool any = false;
      for (int c = 0; c < 4; c++) {
        emscripten::val slot = opts[slot_names[c]];
        if (slot.isUndefined() || slot.isNull()) { s[c].cst = (c == 3) ? 255 : 0; continue; }
        inferred = (std::max)(inferred, c + 1);
        emscripten::val sd = slot["data"];
        if (!sd.isUndefined() && !sd.isNull()) {
          copyFromJSBuffer(sd, s[c].bytes);
          s[c].has = true;
          s[c].channel = optInt(slot, "channel", 0);
          any = true;
        } else {
          s[c].cst = uint8_t(optInt(slot, "const", 0) & 0xff);
        }
      }
      const int out_channels = (reqCh >= 1 && reqCh <= 4) ? reqCh : (inferred > 0 ? inferred : 4);
      bool ok = any;
      uint32_t W = 0, H = 0;
      int in_ch[4] = {0, 0, 0, 0};
      tinyusdz::imageio::PngScanlineReader rd[4];
      for (int c = 0; c < 4 && ok; c++) {
        if (!s[c].has) continue;
        if (!rd[c].Open(s[c].bytes.data(), s[c].bytes.size())) { ok = false; break; }
        const auto& info = rd[c].info();
        if (info.bit_depth != 8 || info.color_type == 3) { ok = false; break; }  // palette/sub-byte -> fallback
        in_ch[c] = info.channels;
        if (W == 0) { W = info.width; H = info.height; }
        else if (info.width != W || info.height != H) { ok = false; break; }
        if (s[c].channel < 0 || s[c].channel >= in_ch[c]) { ok = false; break; }
      }
      if (ok && req_w > 0 && uint32_t(req_w) != W) ok = false;  // resize needed -> fallback
      if (ok && req_h > 0 && uint32_t(req_h) != H) ok = false;
      if (ok && W > 0 && H > 0) {
        const uint8_t out_ct = (out_channels == 1) ? 0 : (out_channels == 2) ? 4 : (out_channels == 3) ? 2 : 6;
        tinyusdz::imageio::PngScanlineWriter wr;
        tinyusdz::imageio::PngImageInfo oi;
        oi.width = W; oi.height = H; oi.bit_depth = 8; oi.color_type = out_ct;
        if (wr.Begin(oi)) {
          std::vector<uint8_t> rows[4];
          for (int c = 0; c < 4; c++) if (s[c].has) rows[c].resize(rd[c].info().row_bytes);
          std::vector<uint8_t> outrow((size_t)W * out_channels);
          // Per-output-channel source descriptors (row pointers are stable across
          // scanlines); the SIMD kernel does the per-row gather/interleave.
          tinyusdz::imageproc::PackSource srcs[4];
          for (int oc = 0; oc < out_channels; oc++) {
            if (s[oc].has) {
              srcs[oc].in = rows[oc].data();
              srcs[oc].in_stride = in_ch[oc];
              srcs[oc].channel = s[oc].channel;
            } else {
              srcs[oc].in = nullptr;
              srcs[oc].constant = s[oc].cst;
            }
          }
          bool good = true;
          for (uint32_t y = 0; y < H && good; y++) {
            for (int c = 0; c < 4; c++)
              if (s[c].has && !rd[c].NextRow(rows[c].data())) { good = false; break; }
            if (!good) break;
            tinyusdz::imageproc::PackChannels8(outrow.data(), W, out_channels, srcs);
            if (!wr.WriteRow(outrow.data())) good = false;
          }
          std::vector<uint8_t> outpng;
          if (good && wr.Finish(outpng)) {
            result.set("success", true);
            result.set("data", bytesToUint8Array(outpng));
            result.set("width", (int)W);
            result.set("height", (int)H);
            result.set("channels", out_channels);
            return result;
          }
        }
      }
    }
  }

  std::vector<Image> images;

  tydra::ChannelPackSpec spec;
  spec.out_channels = optInt(opts, "channels", 0);
  spec.out_width = optInt(opts, "width", 0);
  spec.out_height = optInt(opts, "height", 0);
  tydra::ChannelSource* dst[4] = {&spec.r, &spec.g, &spec.b, &spec.a};

  int inferred = 0;
  for (int c = 0; c < 4; c++) {
    emscripten::val slot = opts[slot_names[c]];
    if (slot.isUndefined() || slot.isNull()) {
      dst[c]->input_index = -1;
      dst[c]->constant = (c == 3) ? 255 : 0;
      continue;
    }
    inferred = (std::max)(inferred, c + 1);
    emscripten::val sdata = slot["data"];
    if (!sdata.isUndefined() && !sdata.isNull()) {
      std::vector<uint8_t> buf;
      copyFromJSBuffer(sdata, buf);
      auto loaded = image::LoadImageFromMemory(buf.data(), buf.size(), "mem");
      if (!loaded) {
        result.set("success", false);
        result.set("error", std::string("repack: failed to decode a channel image: ") + loaded.error());
        return result;
      }
      dst[c]->input_index = int(images.size());
      dst[c]->channel = optInt(slot, "channel", 0);
      images.push_back(std::move(loaded.value().image));
    } else {
      dst[c]->input_index = -1;
      dst[c]->constant = uint8_t(optInt(slot, "const", 0) & 0xff);
    }
  }

  if (spec.out_channels < 1 || spec.out_channels > 4) {
    spec.out_channels = inferred > 0 ? inferred : 4;
  }

  Image packed; std::string perr;
  if (!tydra::PackChannels(images, spec, &packed, &perr)) {
    result.set("success", false);
    result.set("error", perr);
    return result;
  }

  image::WriteOption wopt;
  wopt.png_encoder = parsePngEncoder(optStr(opts, "pngEncoder", "auto"));
  wopt.jpeg_quality = optInt(opts, "jpegQuality", 90);
  const std::string format = optStr(opts, "format", "png");
  if (format == "jpeg" || format == "jpg") {
    wopt.format = image::WriteImageFormat::JPEG;
    packed = dropAlpha(packed);
  } else {
    wopt.format = image::WriteImageFormat::PNG;
  }

  auto enc = image::WriteImageToMemory(packed, wopt);
  if (!enc) {
    result.set("success", false);
    result.set("error", enc.error());
    return result;
  }

  result.set("success", true);
  result.set("data", bytesToUint8Array(enc.value()));
  result.set("width", packed.width);
  result.set("height", packed.height);
  result.set("channels", packed.channels);
  return result;
}

// fitTextures(opts) -> { success, results:[{data:Uint8Array, ext, width, height, name}], error? }
// opts: { images:[{data:Uint8Array, name:string}], targetBytes, strategy:"size"|"quality",
//         startMaxSize?, minTextureSize?, minQuality?, jpegQuality?, pngEncoder? }
emscripten::val fitTextures(const emscripten::val& opts) {
  using namespace tinyusdz;
  emscripten::val result = emscripten::val::object();

  emscripten::val jsImages = opts["images"];
  if (jsImages.isUndefined() || jsImages.isNull()) {
    result.set("success", false);
    result.set("error", std::string("fitTextures: missing 'images'"));
    return result;
  }
  const size_t n = jsImages["length"].as<size_t>();

  std::vector<tydra::FitTextureInput> inputs;
  std::vector<std::string> names;
  inputs.reserve(n);
  names.reserve(n);
  for (size_t i = 0; i < n; i++) {
    emscripten::val im = jsImages[i];
    std::vector<uint8_t> buf;
    copyFromJSBuffer(im["data"], buf);
    const std::string name = im["name"].as<std::string>();

    tydra::FitTextureInput fi;
    fi.original_bytes = buf;
    // lowercase extension from name
    {
      auto dot = name.rfind('.');
      if (dot != std::string::npos) {
        fi.ext = name.substr(dot + 1);
        for (auto& c : fi.ext) c = static_cast<char>(std::tolower(c));
      }
    }
    auto dec = image::LoadImageFromMemory(buf.data(), buf.size(), name);
    if (dec) {
      const Image& dimg = dec.value().image;
      // 8-bit LDR can be resized/transcoded; fp32 EXR can be resized (kept as
      // EXR under the size strategy). The fit logic decides per strategy.
      const bool fittable =
          (dimg.bpp == 8) ||
          (dimg.bpp == 32 && dimg.format == Image::PixelFormat::Float);
      if (fittable) {
        fi.image = std::move(dec.value().image);
        fi.reencodable = true;
      } else {
        fi.reencodable = false;
      }
    } else {
      fi.reencodable = false;
    }
    inputs.push_back(std::move(fi));
    names.push_back(name);
  }

  tydra::FitTextureOptions fopts;
  {
    emscripten::val tb = opts["targetBytes"];
    fopts.target_total_bytes =
        (tb.isUndefined() || tb.isNull()) ? 0 : size_t(tb.as<double>());
  }
  fopts.strategy = (optStr(opts, "strategy", "size") == "quality")
                       ? tydra::FitStrategy::Quality
                       : tydra::FitStrategy::Size;
  fopts.start_max_size = optInt(opts, "startMaxSize", 0);
  fopts.min_texture_size = optInt(opts, "minTextureSize", 64);
  fopts.min_jpeg_quality = optInt(opts, "minQuality", 30);
  fopts.jpeg_quality = optInt(opts, "jpegQuality", 90);
  fopts.png_encoder = parsePngEncoder(optStr(opts, "pngEncoder", "auto"));

  std::vector<tydra::FitTextureOutput> outs;
  std::string warn, err;
  if (!tydra::FitTexturesToBudget(inputs, fopts, &outs, &warn, &err)) {
    result.set("success", false);
    result.set("error", err);
    return result;
  }

  emscripten::val arr = emscripten::val::array();
  size_t total = 0;
  size_t limit = (std::min)(outs.size(), names.size());
  if (outs.size() != names.size()) {
    if (!warn.empty()) {
      warn += "Texture count mismatch: ";
    }
    warn += std::to_string(names.size()) + " inputs but " +
            std::to_string(outs.size()) + " outputs; results may be incomplete.";
  }
  for (size_t i = 0; i < limit; i++) {
    emscripten::val r = emscripten::val::object();
    r.set("data", bytesToUint8Array(outs[i].bytes));
    r.set("ext", outs[i].ext);
    r.set("width", outs[i].width);
    r.set("height", outs[i].height);
    r.set("name", names[i]);
    arr.call<void>("push", r);
    total += outs[i].bytes.size();
  }
  result.set("success", true);
  result.set("results", arr);
  result.set("totalBytes", double(total));
  result.set("warn", warn);
  return result;
}

// usddiff(opts) -> { success, hasDiffs, text?, json?, error?, warn? }
// opts: { left:{data:Uint8Array, name?:string}, right:{data:Uint8Array, name?:string},
//         format?:"text"|"json"|"both" (default "text") }
//
// Loads both inputs as Layers (pre-composition, so the full PrimSpec/Attribute
// tree is preserved) and diffs them with tinyusdz::tydra. Mirrors the native
// `tusddiff` tool (tools/tusddiff/tusddiff.cc).
emscripten::val usddiff(const emscripten::val& opts) {
  using namespace tinyusdz;
  emscripten::val result = emscripten::val::object();

  if (opts.isUndefined() || opts.isNull()) {
    result.set("success", false);
    result.set("error", std::string("usddiff: missing options"));
    return result;
  }

  emscripten::val left = opts["left"];
  emscripten::val right = opts["right"];
  if (left.isUndefined() || left.isNull() || right.isUndefined() ||
      right.isNull()) {
    result.set("success", false);
    result.set("error", std::string("usddiff: 'left' and 'right' are required"));
    return result;
  }

  std::vector<uint8_t> lhsBuf, rhsBuf;
  copyFromJSBuffer(left["data"], lhsBuf);
  copyFromJSBuffer(right["data"], rhsBuf);

  const std::string lhsName = optStr(left, "name", "left");
  const std::string rhsName = optStr(right, "name", "right");
  const std::string format = optStr(opts, "format", "text");

  USDLoadOptions loadOpts;

  Layer lhsLayer, rhsLayer;
  std::string warn, err;

  if (!LoadLayerFromMemory(lhsBuf.data(), lhsBuf.size(), lhsName, &lhsLayer,
                           &warn, &err, loadOpts)) {
    result.set("success", false);
    result.set("error", std::string("Error loading ") + lhsName + ": " + err);
    return result;
  }
  std::string accumWarn = warn;

  warn.clear();
  err.clear();
  if (!LoadLayerFromMemory(rhsBuf.data(), rhsBuf.size(), rhsName, &rhsLayer,
                           &warn, &err, loadOpts)) {
    result.set("success", false);
    result.set("error", std::string("Error loading ") + rhsName + ": " + err);
    return result;
  }
  if (!warn.empty()) {
    if (!accumWarn.empty()) accumWarn += "\n";
    accumWarn += warn;
  }

  tinyusdz::HashMap<std::string, tydra::PrimSpecDiff> psDiffs;
  tinyusdz::HashMap<std::string, tydra::PropDiff> propDiffs;
  tydra::Diff(lhsLayer, rhsLayer, psDiffs, propDiffs);

  const bool hasDiffs = !psDiffs.empty() || !propDiffs.empty();

  result.set("success", true);
  result.set("hasDiffs", hasDiffs);
  if (!accumWarn.empty()) result.set("warn", accumWarn);

  if (format == "json" || format == "both") {
    result.set("json", tydra::DiffToJSON(lhsLayer, rhsLayer, lhsName, rhsName));
  }
  if (format == "text" || format == "both") {
    if (hasDiffs) {
      result.set("text", tydra::DiffToText(lhsLayer, rhsLayer, lhsName, rhsName));
    } else {
      result.set("text", std::string("No differences found.\n"));
    }
  }

  return result;
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

  // USDZ-convert texture helpers.
  // convertImage(data, {maxSize?, width?, height?, format?, pngEncoder?, jpegQuality?})
  //   -> { success, data:Uint8Array, width, height, resized }
  function("convertImage", &convertImage);
  function("getTextureColorspaceMap", &getTextureColorspaceMap);
  // repackChannels({channels?, width?, height?, format?, r/g/b/a:{data?,channel?,const?}})
  //   -> { success, data:Uint8Array, width, height, channels }
  function("repackChannels", &repackChannels);
  // fitTextures({images:[{data,name}], targetBytes, strategy:"size"|"quality", ...})
  //   -> { success, results:[{data, ext, width, height, name}], totalBytes }
  function("fitTextures", &fitTextures);

  // usddiff({left:{data,name?}, right:{data,name?}, format?:"text"|"json"|"both"})
  //   -> { success, hasDiffs, text?, json?, error?, warn? }
  function("usddiff", &usddiff);
}

// ===========================================================================
// tinysubdiv (src/tsd) streaming subdivision binding.
//
// SubdivStreamer.refineStream(...) refines a control mesh and delivers the
// refined surface to a JS callback in bounded batches (zero-copy heap views),
// so the full level-N output never resides in the wasm heap at once. The JS
// side concatenates batches into renderable buffers; the wasm heap high-water
// mark (heapBytes) stays bounded by `batchFaces`.
// ===========================================================================

namespace {

class SubdivStreamer {
 public:
  // points: Float32Array (xyz interleaved). fvc/fvi: Uint32Array.
  // scheme: 0=catmullClark, 1=loop, 2=bilinear.
  // boundary: 0=edgeAndCorner, 1=edgeOnly, 2=none.
  // uvValues: Float32Array (stride 2) or null/empty for no texturing.
  // uvIndices: Uint32Array (per face-corner) or null for identity.
  // uvInterp: 0 = linear ("all"); 1 = smooth seam-split ("cornersPlus1").
  // batchFaces: parent faces per output batch (0 => default).
  // blockFaces: >0 bounds the WORKING set -- refine in blocks of this many base
  //             faces plus a halo, so peak heap is independent of mesh size and
  //             level (for huge meshes). 0 => whole-mesh streaming.
  // haloRings: block halo radius (0 => library default); ignored if blockFaces=0.
  // onBatch(positions, normals|null, indices, faceSource, uv|null,
  //         numVertices, numFaces, batchIndex): typed-array views valid only
  //         for the call. `uv` is per-corner (parallel to indices) when present.
  //         In block mode, vertex_source/indices are block-local (border verts
  //         are duplicated); the JS side concatenates batches as usual.
  // Returns "" on success, else an error message.
  std::string refineStream(const emscripten::val &points,
                           const emscripten::val &fvc,
                           const emscripten::val &fvi,
                           const emscripten::val &uvValues,
                           const emscripten::val &uvIndices, int uvInterp,
                           int scheme, int boundary, int level, int batchFaces,
                           int blockFaces, int haloRings, bool wantNormals,
                           emscripten::val onBatch) {
    namespace tsd = tinyusdz::tsd;

    std::vector<float> pts;
    std::vector<uint32_t> counts;
    std::vector<uint32_t> indices;
    detail::copyTypedArray(points, pts, "Float32Array");
    detail::copyTypedArray(fvc, counts, "Uint32Array");
    detail::copyTypedArray(fvi, indices, "Uint32Array");
    if ((pts.size() % 3) != 0) {
      return "points length must be a multiple of 3";
    }
    if (counts.empty() || indices.empty()) {
      return "empty mesh";
    }

    // Optional UV faceVarying channel. uvInterp selects how it interpolates:
    // 0 = linear ("all" mode, bilinear per corner); 1 = smooth seam-split
    // ("cornersPlus1", the USD default -- UVs follow the limit surface, less
    // distortion on curved regions, seams preserved at island boundaries).
    std::vector<float> uvs;
    std::vector<uint32_t> uvidx;
    detail::copyTypedArray(uvValues, uvs, "Float32Array");
    detail::copyTypedArray(uvIndices, uvidx, "Uint32Array");
    const bool has_uv = (uvs.size() >= 2) && ((uvs.size() % 2) == 0);

    tsd::MeshView mesh;
    mesh.points = pts.data();
    mesh.num_points = uint32_t(pts.size() / 3);
    mesh.face_vertex_counts = counts.data();
    mesh.num_faces = uint32_t(counts.size());
    mesh.face_vertex_indices = indices.data();
    mesh.num_face_vertex_indices = uint32_t(indices.size());

    tsd::FVarChannelView uvchan;
    if (has_uv) {
      uvchan.values = uvs.data();
      uvchan.num_values = uint32_t(uvs.size() / 2);
      uvchan.indices = uvidx.empty() ? nullptr : uvidx.data();
      uvchan.stride = 2;
      uvchan.interpolation = (uvInterp == 1)
                                 ? tsd::FVarLinearInterpolation::CornersPlus1
                                 : tsd::FVarLinearInterpolation::All;
    }

    tsd::Options opts;
    opts.scheme = (scheme == 1)   ? tsd::Scheme::Loop
                  : (scheme == 2) ? tsd::Scheme::Bilinear
                                  : tsd::Scheme::CatmullClark;
    opts.boundary = (boundary == 1)   ? tsd::BoundaryInterpolation::EdgeOnly
                    : (boundary == 2) ? tsd::BoundaryInterpolation::None
                                      : tsd::BoundaryInterpolation::EdgeAndCorner;
    opts.level = level;
    opts.remove_holes = true;

    tsd::StreamOptions so;
    so.batch_faces = (batchFaces > 0) ? uint32_t(batchFaces) : 4096u;
    so.emit_triangles = true;
    so.want_normals = wantNormals;
    so.dedup_within_batch = true;
    // Block mode: bound the WORKING set (not just the output) for very large
    // meshes -- refine in blocks of `blockFaces` base faces with a `haloRings`
    // halo (0 => the library's level-independent default). 0 => whole-mesh
    // streaming. Streams geometry, normals, and faceVarying (UVs) alike.
    so.block_faces = (blockFaces > 0) ? uint32_t(blockFaces) : 0u;
    so.halo_rings = (haloRings > 0) ? uint32_t(haloRings) : 0u;

    struct SinkCtx {
      emscripten::val *cb;
      bool want_normals;
    } ctx{&onBatch, wantNormals};

    auto sink = [](void *user, const tsd::StreamBatch *b) -> bool {
      SinkCtx *c = static_cast<SinkCtx *>(user);
      emscripten::val pos(emscripten::typed_memory_view(
          size_t(b->num_vertices) * 3, const_cast<float *>(b->positions)));
      emscripten::val nrm =
          (c->want_normals && b->normals)
              ? emscripten::val(emscripten::typed_memory_view(
                    size_t(b->num_vertices) * 3, const_cast<float *>(b->normals)))
              : emscripten::val::null();
      emscripten::val idx(emscripten::typed_memory_view(
          size_t(b->num_indices), const_cast<uint32_t *>(b->indices)));
      emscripten::val fsrc(emscripten::typed_memory_view(
          size_t(b->num_faces), const_cast<uint32_t *>(b->face_source)));
      emscripten::val uv =
          (b->num_fvar == 1)
              ? emscripten::val(emscripten::typed_memory_view(
                    size_t(b->num_indices) * 2,
                    const_cast<float *>(b->fvar[0].values)))
              : emscripten::val::null();
      (*c->cb)(pos, nrm, idx, fsrc, uv, b->num_vertices, b->num_faces,
               b->batch_index);
      return true;
    };

    std::string err;
    const tsd::Result r = tsd::RefineStream(
        mesh, has_uv ? &uvchan : nullptr, has_uv ? 1u : 0u, nullptr, 0, opts, so,
        sink, &ctx, &err);
    if (r != tsd::Result::Success) {
      return std::string("RefineStream failed (") + tsd::to_string(r) +
             "): " + err;
    }
    return "";
  }

  // Total wasm linear-memory bytes. Under ALLOW_MEMORY_GROWTH this is grow-only,
  // so it is the heap high-water mark.
  double heapBytes() const {
    return emscripten::val::module_property("HEAPU8")["length"].as<double>();
  }
};

}  // namespace

EMSCRIPTEN_BINDINGS(tsd_subdiv_module) {
  emscripten::class_<SubdivStreamer>("SubdivStreamer")
      .constructor<>()
      .function("refineStream", &SubdivStreamer::refineStream)
      .function("heapBytes", &SubdivStreamer::heapBytes);
}
