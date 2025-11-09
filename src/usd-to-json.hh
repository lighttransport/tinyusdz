// SPDX-License-Identifier: Apache 2.0
// Experimental USD to JSON converter

#include <string>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"
#include "external/jsonhpp/nlohmann/json_fwd.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "tinyusdz.hh"

namespace tinyusdz {

///
/// Array serialization mode
///
enum class ArraySerializationMode {
  Base64,  // Serialize arrays as base64 strings (default, compact)
  Buffer   // Serialize arrays using buffer/accessor system like glTF (efficient for large data)
};

///
/// Buffer data structure (similar to glTF buffer)
///
struct JSONBuffer {
  std::vector<uint8_t> data;     // Raw binary data
  std::string uri;               // Optional URI (for external files, empty for embedded)
  size_t byteLength;             // Length in bytes
  
  JSONBuffer() : byteLength(0) {}
};

///
/// Buffer view data structure (similar to glTF bufferView)
///
struct JSONBufferView {
  size_t buffer;                 // Index into buffers array
  size_t byteOffset;             // Offset into buffer in bytes
  size_t byteLength;             // Length in bytes
  size_t byteStride;             // Optional stride between elements (0 = tightly packed)
  
  JSONBufferView() : buffer(0), byteOffset(0), byteLength(0), byteStride(0) {}
};

///
/// Accessor data structure (similar to glTF accessor)
///
struct JSONAccessor {
  size_t bufferView;             // Index into bufferViews array
  size_t byteOffset;             // Offset into bufferView in bytes
  std::string componentType;     // "BYTE", "UNSIGNED_BYTE", "SHORT", "UNSIGNED_SHORT", "UNSIGNED_INT", "FLOAT"
  size_t count;                  // Number of elements
  std::string type;              // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
  
  JSONAccessor() : bufferView(0), byteOffset(0), count(0) {}
};

///
/// USD to JSON conversion options
///
struct USDToJSONOptions {
  ArraySerializationMode arrayMode = ArraySerializationMode::Base64;
  bool embedBuffers = true;      // If true, embed buffer data in JSON; if false, write separate .bin files
  std::string bufferPrefix = "buffer";  // Prefix for external buffer files
  
  USDToJSONOptions() = default;
};

///
/// USD to JSON conversion context (holds buffers, views, and accessors)
///
struct USDToJSONContext {
  std::vector<JSONBuffer> buffers;
  std::vector<JSONBufferView> bufferViews;
  std::vector<JSONAccessor> accessors;
  USDToJSONOptions options;
  
  // Add data to buffer and return accessor index
  size_t AddArrayData(const void* data, size_t elementSize, size_t elementCount, 
                      const std::string& componentType, const std::string& type);
  
  USDToJSONContext() = default;
  explicit USDToJSONContext(const USDToJSONOptions& opts) : options(opts) {}
};

///
/// Convert USD Stage to JSON
///
/// @returns JSON string or error message(std::string) when failed to convert.
///
nonstd::expected<std::string, std::string> ToJSON(const tinyusdz::Stage &stage);

///
/// Convert USD Stage to JSON with options
///
/// @returns JSON string or error message(std::string) when failed to convert.
///
nonstd::expected<std::string, std::string> ToJSON(const tinyusdz::Stage &stage, const USDToJSONOptions& options);

///
/// Convert USD Stage to JSON (nlohmann::json object)
///
nlohmann::json ToJSON(const tinyusdz::Stage &stage, USDToJSONContext* context);

///
/// Convert USD Layer to JSON (nlohmann::json object)
///
nlohmann::json ToJSON(const tinyusdz::Layer &layer);

///
/// Convert USD Layer to JSON with context (nlohmann::json object)
///
nlohmann::json ToJSON(const tinyusdz::Layer &layer, USDToJSONContext& context);

///
/// Convert USD Layer to JSON
///
bool to_json_string(const tinyusdz::Layer &layer, std::string *json_str, std::string *warn, std::string *err);

///
/// Convert USD Layer to JSON with options
///
bool to_json_string(const tinyusdz::Layer &layer, const USDToJSONOptions& options, std::string *json_str, std::string *warn, std::string *err);

///
/// Convert GeomMesh to JSON with context
///
nlohmann::json ToJSON(tinyusdz::GeomMesh& mesh, USDToJSONContext* context);

///
/// Convert Attribute to JSON
///
nlohmann::json ToJSON(const tinyusdz::Attribute& attribute, USDToJSONContext* context = nullptr);

///
/// Convert Relationship to JSON
///
nlohmann::json ToJSON(const tinyusdz::Relationship& relationship);

///
/// Convert Property to JSON
///
nlohmann::json ToJSON(const tinyusdz::Property& property, USDToJSONContext* context = nullptr);

///
/// Convert Properties map to JSON
///
nlohmann::json PropertiesToJSON(const std::map<std::string, tinyusdz::Property>& properties, USDToJSONContext* context = nullptr);

///
/// USDZ to JSON conversion result structure
///
struct USDZToJSONResult {
  std::string usd_json;          // JSON representation of USD content
  std::string assets_json;       // JSON representation of assets (filename -> base64 data)
  std::string main_usd_filename; // Name of the main USD file in USDZ
  std::vector<std::string> asset_filenames; // List of all asset filenames
  
  USDZToJSONResult() = default;
};

///
/// Convert USDZ file to JSON (separate USD content and assets)
///
/// @param[in] filename Path to USDZ file
/// @param[out] result USDZ to JSON conversion result
/// @param[out] warn Warning messages
/// @param[out] err Error messages
/// @param[in] options Conversion options
///
/// @return true on success, false on failure
///
bool USDZToJSON(const std::string& filename, USDZToJSONResult* result,
                std::string* warn, std::string* err,
                const USDToJSONOptions& options = USDToJSONOptions());

///
/// Convert USDZ from memory to JSON (separate USD content and assets)
///
/// @param[in] addr Pointer to USDZ data in memory
/// @param[in] length Size of USDZ data in bytes
/// @param[in] filename Filename (for reference, can be empty)
/// @param[out] result USDZ to JSON conversion result
/// @param[out] warn Warning messages
/// @param[out] err Error messages
/// @param[in] options Conversion options
///
/// @return true on success, false on failure
///
bool USDZToJSONFromMemory(const uint8_t* addr, size_t length, const std::string& filename,
                          USDZToJSONResult* result, std::string* warn, std::string* err,
                          const USDToJSONOptions& options = USDToJSONOptions());

///
/// Extract assets from USDZ and convert to JSON map (filename -> base64 data)
///
/// @param[in] usdz_asset USDZ asset data structure
/// @param[out] assets_json JSON string containing asset map
/// @param[out] warn Warning messages
/// @param[out] err Error messages
///
/// @return true on success, false on failure
///
bool USDZAssetsToJSON(const tinyusdz::USDZAsset& usdz_asset, std::string* assets_json,
                      std::string* warn, std::string* err);

} // namespace tinyusdz
