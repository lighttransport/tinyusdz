#include "json-to-usd.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "layer.hh"
#include "str-util.hh"
#include "common-macros.inc"
#include "usdGeom.hh"
#include "usd-to-json.hh"

namespace tinyusdz {

///
/// JSON to USD conversion context (stores buffers, views, and accessors for deserialization)
///
struct JSONToUSDContext {
  std::vector<std::vector<uint8_t>> buffers;  // Raw buffer data
  std::vector<JSONBufferView> bufferViews;    // Buffer view information
  std::vector<JSONAccessor> accessors;        // Accessor information
  
  // Parse buffer data from JSON
  bool ParseBuffers(const nlohmann::json& j, std::string* err = nullptr);
  bool ParseBufferViews(const nlohmann::json& j, std::string* err = nullptr);
  bool ParseAccessors(const nlohmann::json& j, std::string* err = nullptr);
  
  // Get array data from accessor
  template<typename T>
  bool GetArrayFromAccessor(size_t accessorIndex, std::vector<T>* result, std::string* err = nullptr);
};

// Implementation of buffer parsing methods
bool JSONToUSDContext::ParseBuffers(const nlohmann::json& j, std::string* err) {
  if (!j.is_array()) {
    if (err) (*err) = "Buffers must be an array";
    return false;
  }
  
  buffers.clear();
  buffers.reserve(j.size());
  
  for (const auto& buffer_obj : j) {
    if (!buffer_obj.is_object() || !buffer_obj.contains("byteLength") || !buffer_obj.contains("uri")) {
      if (err) (*err) = "Invalid buffer object";
      return false;
    }
    
    size_t byteLength = buffer_obj["byteLength"].get<size_t>();
    std::string uri = buffer_obj["uri"].get<std::string>();
    
    std::vector<uint8_t> buffer_data;
    
    if (uri.find("data:application/octet-stream;base64,") == 0) {
      // Embedded base64 data
      std::string base64_data = uri.substr(37);  // Skip "data:application/octet-stream;base64,"
      std::string decoded = base64_decode(base64_data);
      
      if (decoded.size() != byteLength) {
        if (err) (*err) = "Buffer size mismatch";
        return false;
      }
      
      buffer_data.resize(decoded.size());
      std::memcpy(buffer_data.data(), decoded.data(), decoded.size());
    } else {
      // External file - would need file I/O implementation
      if (err) (*err) = "External buffer files not yet supported";
      return false;
    }
    
    buffers.push_back(std::move(buffer_data));
  }
  
  return true;
}

bool JSONToUSDContext::ParseBufferViews(const nlohmann::json& j, std::string* err) {
  if (!j.is_array()) {
    if (err) (*err) = "BufferViews must be an array";
    return false;
  }
  
  bufferViews.clear();
  bufferViews.reserve(j.size());
  
  for (const auto& bufferView_obj : j) {
    if (!bufferView_obj.is_object() || !bufferView_obj.contains("buffer") ||
        !bufferView_obj.contains("byteOffset") || !bufferView_obj.contains("byteLength")) {
      if (err) (*err) = "Invalid bufferView object";
      return false;
    }
    
    JSONBufferView bufferView;
    bufferView.buffer = bufferView_obj["buffer"].get<size_t>();
    bufferView.byteOffset = bufferView_obj["byteOffset"].get<size_t>();  
    bufferView.byteLength = bufferView_obj["byteLength"].get<size_t>();
    bufferView.byteStride = 0;
    
    if (bufferView_obj.contains("byteStride")) {
      bufferView.byteStride = bufferView_obj["byteStride"].get<size_t>();
    }
    
    bufferViews.push_back(bufferView);
  }
  
  return true;
}

bool JSONToUSDContext::ParseAccessors(const nlohmann::json& j, std::string* err) {
  if (!j.is_array()) {
    if (err) (*err) = "Accessors must be an array";
    return false;
  }
  
  accessors.clear();
  accessors.reserve(j.size());
  
  for (const auto& accessor_obj : j) {
    if (!accessor_obj.is_object() || !accessor_obj.contains("bufferView") ||
        !accessor_obj.contains("componentType") || !accessor_obj.contains("count") ||
        !accessor_obj.contains("type")) {
      if (err) (*err) = "Invalid accessor object";
      return false;
    }
    
    JSONAccessor accessor;
    accessor.bufferView = accessor_obj["bufferView"].get<size_t>();
    accessor.byteOffset = 0;
    accessor.componentType = accessor_obj["componentType"].get<std::string>();
    accessor.count = accessor_obj["count"].get<size_t>();
    accessor.type = accessor_obj["type"].get<std::string>();
    
    if (accessor_obj.contains("byteOffset")) {
      accessor.byteOffset = accessor_obj["byteOffset"].get<size_t>();
    }
    
    accessors.push_back(accessor);
  }
  
  return true;
}

template<typename T>
bool JSONToUSDContext::GetArrayFromAccessor(size_t accessorIndex, std::vector<T>* result, std::string* err) {
  if (!result) {
    if (err) (*err) = "Result pointer is null";
    return false;
  }
  
  if (accessorIndex >= accessors.size()) {
    if (err) (*err) = "Accessor index out of range";
    return false;
  }
  
  const auto& accessor = accessors[accessorIndex];
  
  if (accessor.bufferView >= bufferViews.size()) {
    if (err) (*err) = "BufferView index out of range";
    return false;
  }
  
  const auto& bufferView = bufferViews[accessor.bufferView];
  
  if (bufferView.buffer >= buffers.size()) {
    if (err) (*err) = "Buffer index out of range";
    return false;
  }
  
  const auto& buffer = buffers[bufferView.buffer];
  
  // Calculate total byte size needed
  size_t elementSize = sizeof(T);
  size_t totalBytes = accessor.count * elementSize;
  
  if (bufferView.byteOffset + accessor.byteOffset + totalBytes > buffer.size()) {
    if (err) (*err) = "Buffer access out of bounds";
    return false;
  }
  
  // Extract data
  result->resize(accessor.count);
  const uint8_t* srcData = buffer.data() + bufferView.byteOffset + accessor.byteOffset;
  std::memcpy(result->data(), srcData, totalBytes);
  
  return true;
}

namespace detail {

// Helper functions for array deserialization from base64
template<typename T>
bool DeserializeArrayFromBase64(const std::string& base64_data, std::vector<T>* result) {
  if (!result) {
    return false;
  }
  
  if (base64_data.empty()) {
    result->clear();
    return true;
  }
  
  std::string decoded = base64_decode(base64_data);
  if (decoded.empty()) {
    return false;
  }
  
  // Check if the size is valid for type T
  if (decoded.size() % sizeof(T) != 0) {
    return false;
  }
  
  size_t count = decoded.size() / sizeof(T);
  result->resize(count);
  
  // Copy decoded bytes to result array
  std::memcpy(result->data(), decoded.data(), decoded.size());
  
  return true;
}

#if 0
// Specialized versions for different types
static bool DeserializeIntArrayFromBase64(const std::string& base64_data, std::vector<int>* result) {
  return DeserializeArrayFromBase64(base64_data, result);
}
#endif

static bool DeserializeFloatArrayFromBase64(const std::string& base64_data, std::vector<float>* result) {
  return DeserializeArrayFromBase64(base64_data, result);
}

#if 0 // TODO
static bool DeserializeDoubleArrayFromBase64(const std::string& base64_data, std::vector<double>* result) {
  return DeserializeArrayFromBase64(base64_data, result);
}

// Vector deserialization helpers for integer types
static bool DeserializeInt2ArrayFromBase64(const std::string& base64_data, std::vector<value::int2>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<int> int_data;
  if (!DeserializeIntArrayFromBase64(base64_data, &int_data)) {
    return false;
  }
  
  if (int_data.size() % 2 != 0) {
    return false;
  }
  
  size_t vector_count = int_data.size() / 2;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = int_data[i * 2 + 0];
    (*result)[i][1] = int_data[i * 2 + 1];
  }
  
  return true;
}

static bool DeserializeInt3ArrayFromBase64(const std::string& base64_data, std::vector<value::int3>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<int> int_data;
  if (!DeserializeIntArrayFromBase64(base64_data, &int_data)) {
    return false;
  }
  
  if (int_data.size() % 3 != 0) {
    return false;
  }
  
  size_t vector_count = int_data.size() / 3;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = int_data[i * 3 + 0];
    (*result)[i][1] = int_data[i * 3 + 1];
    (*result)[i][2] = int_data[i * 3 + 2];
  }
  
  return true;
}

static bool DeserializeInt4ArrayFromBase64(const std::string& base64_data, std::vector<value::int4>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<int> int_data;
  if (!DeserializeIntArrayFromBase64(base64_data, &int_data)) {
    return false;
  }
  
  if (int_data.size() % 4 != 0) {
    return false;
  }
  
  size_t vector_count = int_data.size() / 4;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = int_data[i * 4 + 0];
    (*result)[i][1] = int_data[i * 4 + 1];
    (*result)[i][2] = int_data[i * 4 + 2];
    (*result)[i][3] = int_data[i * 4 + 3];
  }
  
  return true;
}

// Vector deserialization helpers for float types
static bool DeserializeFloat2ArrayFromBase64(const std::string& base64_data, std::vector<value::float2>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<float> float_data;
  if (!DeserializeFloatArrayFromBase64(base64_data, &float_data)) {
    return false;
  }
  
  if (float_data.size() % 2 != 0) {
    return false;
  }
  
  size_t vector_count = float_data.size() / 2;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = float_data[i * 2 + 0];
    (*result)[i][1] = float_data[i * 2 + 1];
  }
  
  return true;
}

static bool DeserializeFloat4ArrayFromBase64(const std::string& base64_data, std::vector<value::float4>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<float> float_data;
  if (!DeserializeFloatArrayFromBase64(base64_data, &float_data)) {
    return false;
  }
  
  if (float_data.size() % 4 != 0) {
    return false;
  }
  
  size_t vector_count = float_data.size() / 4;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = float_data[i * 4 + 0];
    (*result)[i][1] = float_data[i * 4 + 1];
    (*result)[i][2] = float_data[i * 4 + 2];
    (*result)[i][3] = float_data[i * 4 + 3];
  }
  
  return true;
}

// Vector deserialization helpers for half types
static bool DeserializeHalf2ArrayFromBase64(const std::string& base64_data, std::vector<value::half2>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<float> float_data;
  if (!DeserializeFloatArrayFromBase64(base64_data, &float_data)) {
    return false;
  }
  
  if (float_data.size() % 2 != 0) {
    return false;
  }
  
  size_t vector_count = float_data.size() / 2;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = value::float_to_half_full(float_data[i * 2 + 0]);
    (*result)[i][1] = value::float_to_half_full(float_data[i * 2 + 1]);
  }
  
  return true;
}

static bool DeserializeHalf3ArrayFromBase64(const std::string& base64_data, std::vector<value::half3>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<float> float_data;
  if (!DeserializeFloatArrayFromBase64(base64_data, &float_data)) {
    return false;
  }
  
  if (float_data.size() % 3 != 0) {
    return false;
  }
  
  size_t vector_count = float_data.size() / 3;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = value::float_to_half_full(float_data[i * 3 + 0]);
    (*result)[i][1] = value::float_to_half_full(float_data[i * 3 + 1]);
    (*result)[i][2] = value::float_to_half_full(float_data[i * 3 + 2]);
  }
  
  return true;
}

static bool DeserializeHalf4ArrayFromBase64(const std::string& base64_data, std::vector<value::half4>* result) {
  if (!result) {
    return false;
  }
  
  std::vector<float> float_data;
  if (!DeserializeFloatArrayFromBase64(base64_data, &float_data)) {
    return false;
  }
  
  if (float_data.size() % 4 != 0) {
    return false;
  }
  
  size_t vector_count = float_data.size() / 4;
  result->resize(vector_count);
  
  for (size_t i = 0; i < vector_count; ++i) {
    (*result)[i][0] = value::float_to_half_full(float_data[i * 4 + 0]);
    (*result)[i][1] = value::float_to_half_full(float_data[i * 4 + 1]);
    (*result)[i][2] = value::float_to_half_full(float_data[i * 4 + 2]);
    (*result)[i][3] = value::float_to_half_full(float_data[i * 4 + 3]);
  }
  
  return true;
}
#endif

// Attribute metadata deserialization
static bool DeserializeAttributeMetadata(const nlohmann::json& metadata_json, AttrMetas* metas, std::string* err = nullptr) {
  (void)err;
  if (!metas || !metadata_json.is_object()) {
    return false;
  }
  
  // Parse interpolation
  if (metadata_json.contains("interpolation") && metadata_json["interpolation"].is_string()) {
    std::string interp_str = metadata_json["interpolation"].get<std::string>();
    if (interp_str == "constant") {
      metas->set_interpolation_enum(Interpolation::Constant);
    } else if (interp_str == "uniform") {
      metas->set_interpolation_enum(Interpolation::Uniform);
    } else if (interp_str == "varying") {
      metas->set_interpolation_enum(Interpolation::Varying);
    } else if (interp_str == "vertex") {
      metas->set_interpolation_enum(Interpolation::Vertex);
    } else if (interp_str == "faceVarying") {
      metas->set_interpolation_enum(Interpolation::FaceVarying);
    }
  }

  // Parse elementSize
  if (metadata_json.contains("elementSize") && metadata_json["elementSize"].is_number_integer()) {
    metas->set_elementSize(static_cast<uint32_t>(metadata_json["elementSize"].get<int>()));
  }

  // Parse hidden flag
  if (metadata_json.contains("hidden") && metadata_json["hidden"].is_boolean()) {
    metas->set_hidden(metadata_json["hidden"].get<bool>());
  }

  // Parse comment
  if (metadata_json.contains("comment") && metadata_json["comment"].is_string()) {
    value::StringData comment_data;
    comment_data.value = metadata_json["comment"].get<std::string>();
    metas->set_comment(comment_data);
  }

  // Parse displayName
  if (metadata_json.contains("displayName") && metadata_json["displayName"].is_string()) {
    metas->set_displayName(metadata_json["displayName"].get<std::string>());
  }
  
  // TODO: Parse customData (requires Dictionary support)
  // TODO: Parse sdrMetadata (requires Dictionary support)
  
  return true;
}

// Helper function to parse array data from JSON (supports both base64 and accessor modes)
static bool ParseArrayFromJSON(const nlohmann::json& j, JSONToUSDContext* context, std::string* base64_data, 
                        size_t* accessor_index, size_t* count, std::string* type, std::string* err) {
  if (!j.is_object()) {
    if (err) {
      (*err) = "Array data must be an object";
    }
    return false;
  }
  
  if (!j.contains("count") || !j.contains("type")) {
    if (err) {
      (*err) = "Array object must contain 'count' and 'type' fields";
    }
    return false;
  }
  
  if (!j["count"].is_number_unsigned()) {
    if (err) {
      (*err) = "'count' field must be a positive number";
    }
    return false;
  }
  
  if (!j["type"].is_string()) {
    if (err) {
      (*err) = "'type' field must be a string";
    }
    return false;
  }
  
  *count = j["count"].get<size_t>();
  *type = j["type"].get<std::string>();
  
  // Check for base64 mode
  if (j.contains("data")) {
    if (!j["data"].is_string()) {
      if (err) {
        (*err) = "'data' field must be a string";
      }
      return false;
    }
    *base64_data = j["data"].get<std::string>();
    *accessor_index = SIZE_MAX;  // Invalid accessor index indicates base64 mode
    return true;
  }
  
  // Check for accessor mode
  if (j.contains("accessor")) {
    if (!j["accessor"].is_number_unsigned()) {
      if (err) {
        (*err) = "'accessor' field must be a positive number";
      }
      return false;
    }
    
    if (!context) {
      if (err) {
        (*err) = "Context required for accessor mode but not provided";
      }
      return false;
    }
    
    *accessor_index = j["accessor"].get<size_t>();
    base64_data->clear();  // No base64 data in accessor mode
    return true;
  }
  
  if (err) {
    (*err) = "Array object must contain either 'data' (base64) or 'accessor' field";
  }
  return false;
}

// Template helper for parsing and deserializing arrays
template<typename T>
static bool ParseAndDeserializeArray(const nlohmann::json& array_json, JSONToUSDContext* context, 
                              const std::string& expected_type, std::vector<T>* result, std::string* err) {
  std::string base64_data, type;
  size_t accessor_index, count;
  
  if (!ParseArrayFromJSON(array_json, context, &base64_data, &accessor_index, &count, &type, err)) {
    return false;
  }
  
  if (type != expected_type) {
    if (err) {
      (*err) = "Unexpected array type: " + type + ", expected: " + expected_type;
    }
    return false;
  }
  
  if (accessor_index == SIZE_MAX) {
    // Base64 mode
    return DeserializeArrayFromBase64(base64_data, result);
  } else {
    // Accessor mode
    if (context) {
      return context->GetArrayFromAccessor(accessor_index, result, err);
    } else {
      if (err) {
        (*err) = "Context required for accessor mode";
      }
      return false;
    }
  }
}

// Metadata-aware array parsing function
template<typename T>
bool ParseAndDeserializeArrayWithMetadata(const nlohmann::json& array_json, JSONToUSDContext* context, 
                                         const std::string& expected_type, std::vector<T>* result, 
                                         AttrMetas* metas, std::string* err) {
  if (!array_json.is_object()) {
    if (err) (*err) = "Array data must be an object";
    return false;
  }
  
  // Parse array data (base64 or accessor)
  if (!ParseAndDeserializeArray(array_json, context, expected_type, result, err)) {
    return false;
  }
  
  // Parse metadata if present
  if (array_json.contains("metadata") && metas) {
    if (!DeserializeAttributeMetadata(array_json["metadata"], metas, err)) {
      // Continue even if metadata parsing fails (metadata is optional)
      if (err) {
        *err = "Warning: Failed to parse metadata - " + *err;
      }
    }
  }
  
  return true;
}

// Specialized metadata-aware parsing for point3f arrays
static bool ParsePoint3fArrayWithMetadata(const nlohmann::json& array_json, JSONToUSDContext* context, 
                                  std::vector<value::point3f>* result, AttrMetas* metas, std::string* err) {
  if (!array_json.is_object()) {
    if (err) (*err) = "Points array must be an object";
    return false;
  }
  
  std::string base64_data, type;
  size_t accessor_index, count;
  
  if (!ParseArrayFromJSON(array_json, context, &base64_data, &accessor_index, &count, &type, err)) {
    return false;
  }
  
  if (type != "point3f[]") {
    if (err) (*err) = "Expected point3f[] type, got: " + type;
    return false;
  }
  
  std::vector<float> float_data;
  bool data_success = false;
  
  if (accessor_index == SIZE_MAX) {
    // Base64 mode
    data_success = DeserializeFloatArrayFromBase64(base64_data, &float_data);
  } else {
    // Accessor mode
    if (context) {
      data_success = context->GetArrayFromAccessor(accessor_index, &float_data, err);
    }
  }
  
  if (!data_success || float_data.size() != count * 3) {
    if (err) (*err) = "Failed to parse points data or size mismatch";
    return false;
  }
  
  result->reserve(count);
  for (size_t i = 0; i < count; ++i) {
    value::point3f pt;
    pt[0] = float_data[i * 3 + 0];
    pt[1] = float_data[i * 3 + 1];
    pt[2] = float_data[i * 3 + 2];
    result->push_back(pt);
  }
  
  // Parse metadata if present
  if (array_json.contains("metadata") && metas) {
    DeserializeAttributeMetadata(array_json["metadata"], metas, err);
  }
  
  return true;
}

// Specialized metadata-aware parsing for normal3f arrays
static bool ParseNormal3fArrayWithMetadata(const nlohmann::json& array_json, JSONToUSDContext* context, 
                                   std::vector<value::normal3f>* result, AttrMetas* metas, std::string* err) {
  if (!array_json.is_object()) {
    if (err) (*err) = "Normals array must be an object";
    return false;
  }
  
  std::vector<float> float_data;
  if (!ParseAndDeserializeArray(array_json, context, "normal3f[]", &float_data, err)) {
    return false;
  }
  
  if (float_data.size() % 3 != 0) {
    if (err) (*err) = "Normal array size must be divisible by 3";
    return false;
  }
  
  size_t count = float_data.size() / 3;
  result->reserve(count);
  for (size_t i = 0; i < count; ++i) {
    value::normal3f normal;
    normal[0] = float_data[i * 3 + 0];
    normal[1] = float_data[i * 3 + 1];
    normal[2] = float_data[i * 3 + 2];
    result->push_back(normal);
  }
  
  // Parse metadata if present
  if (array_json.contains("metadata") && metas) {
    DeserializeAttributeMetadata(array_json["metadata"], metas, err);
  }
  
  return true;
}

// JSON to GeomMesh conversion (with context support)
static bool JSONToGeomMesh(const nlohmann::json& j, GeomMesh* mesh, JSONToUSDContext* context, std::string* warn, std::string* err) {
  (void)warn;

  if (!mesh) {
    if (err) {
      (*err) = "Internal error: mesh is null";
    }
    return false;
  }
  
  if (!j.is_object()) {
    if (err) {
      (*err) = "JSON must be an object";
    }
    return false;
  }
  
  // Set name if present
  if (j.contains("name") && j["name"].is_string()) {
    mesh->name = j["name"].get<std::string>();
  }
  
  // Parse points array with metadata
  if (j.contains("points")) {
    std::vector<value::point3f> points;
    if (ParsePoint3fArrayWithMetadata(j["points"], context, &points, &mesh->points.metas(), err)) {
      Animatable<std::vector<value::point3f>> animatable_points;
      animatable_points.set(points);
      mesh->points.set_value(animatable_points);
    } else {
      return false;
    }
  }
  
  // Parse face vertex counts array
  if (j.contains("faceVertexCounts")) {
    std::vector<int> int_data;
    if (ParseAndDeserializeArrayWithMetadata(j["faceVertexCounts"], context, "int[]", &int_data, &mesh->faceVertexCounts.metas(), err)) {
      Animatable<std::vector<int>> animatable_face_counts;
      animatable_face_counts.set(int_data);
      mesh->faceVertexCounts.set_value(animatable_face_counts);
    } else {
      return false;
    }
  }

  // Parse face vertex indices array  
  if (j.contains("faceVertexIndices")) {
    std::vector<int> int_data;
    if (ParseAndDeserializeArrayWithMetadata(j["faceVertexIndices"], context, "int[]", &int_data, &mesh->faceVertexIndices.metas(), err)) {
      Animatable<std::vector<int>> animatable_face_indices;
      animatable_face_indices.set(int_data);
      mesh->faceVertexIndices.set_value(animatable_face_indices);
    } else {
      return false;
    }
  }

  // Parse normals array with metadata
  if (j.contains("normals")) {
    std::vector<value::normal3f> normals;
    if (ParseNormal3fArrayWithMetadata(j["normals"], context, &normals, &mesh->normals.metas(), err)) {
      Animatable<std::vector<value::normal3f>> animatable_normals;
      animatable_normals.set(normals);
      mesh->normals.set_value(animatable_normals);
    } else {
      return false;
    }
  }

  // Parse subdivision surface arrays using the helper
  if (j.contains("cornerIndices")) {
    std::vector<int> int_data;
    if (ParseAndDeserializeArrayWithMetadata(j["cornerIndices"], context, "int[]", &int_data, &mesh->cornerIndices.metas(), err)) {
      Animatable<std::vector<int>> animatable_corner_indices;
      animatable_corner_indices.set(int_data);
      mesh->cornerIndices.set_value(animatable_corner_indices);
    }
  }

  if (j.contains("cornerSharpnesses")) {
    std::vector<float> float_data;
    if (ParseAndDeserializeArrayWithMetadata(j["cornerSharpnesses"], context, "float[]", &float_data, &mesh->cornerSharpnesses.metas(), err)) {
      Animatable<std::vector<float>> animatable_corner_sharpnesses;
      animatable_corner_sharpnesses.set(float_data);
      mesh->cornerSharpnesses.set_value(animatable_corner_sharpnesses);
    }
  }

  if (j.contains("creaseIndices")) {
    std::vector<int> int_data;
    if (ParseAndDeserializeArrayWithMetadata(j["creaseIndices"], context, "int[]", &int_data, &mesh->creaseIndices.metas(), err)) {
      Animatable<std::vector<int>> animatable_crease_indices;
      animatable_crease_indices.set(int_data);
      mesh->creaseIndices.set_value(animatable_crease_indices);
    }
  }

  if (j.contains("creaseLengths")) {
    std::vector<int> int_data;
    if (ParseAndDeserializeArrayWithMetadata(j["creaseLengths"], context, "int[]", &int_data, &mesh->creaseLengths.metas(), err)) {
      Animatable<std::vector<int>> animatable_crease_lengths;
      animatable_crease_lengths.set(int_data);
      mesh->creaseLengths.set_value(animatable_crease_lengths);
    }
  }

  if (j.contains("creaseSharpnesses")) {
    std::vector<float> float_data;
    if (ParseAndDeserializeArrayWithMetadata(j["creaseSharpnesses"], context, "float[]", &float_data, &mesh->creaseSharpnesses.metas(), err)) {
      Animatable<std::vector<float>> animatable_crease_sharpnesses;
      animatable_crease_sharpnesses.set(float_data);
      mesh->creaseSharpnesses.set_value(animatable_crease_sharpnesses);
    }
  }

  return true;
}

} // namespace detail

#if 0
static bool ParseStringArray(const nlohmann::json &j, std::vector<std::string> *result, std::string *warn, std::string *err) {
  (void)warn;

  if (!result) {
    if (err) {
      (*err) = "Internal error.";
    }
    return false;
  }

  std::vector<std::string> arr;

  if (!j.is_array()) {
    if (err) {
      (*err) = "JSON is not an array.";
    }
    return false;
  }

  for (const auto &item : j) {
    if (!item.is_string()) {
      if (err) {
        (*err) = "array element is not string.";
      }
      return false;
    }

    std::string s = item;
    arr.push_back(item);
  }

  (*result) = arr;

  return true;
}

static bool JSONToPropertyImpl(const nlohmann::json &j, PrimSpec *ps, std::string *warn, std::string *err) {
  (void)warn;
  (void)ps;

  // TODO:
  // [ ] timeSamples
  // [ ] None

  if (!j.contains("typeName")) {
    if (err) {
      (*err) = "`typeName` is missing."; 
    }
    return false;
  }

  if (j.contains("value")) {
  }

  return true;
}
#endif

static bool JSONToPrimSpecImpl(const nlohmann::json &j, PrimSpec *ps, std::string *warn, std::string *err) {
  (void)err;
  (void)warn;
  (void)ps;

  if (j.contains("metadata")) {
    nlohmann::json meta = j["metadata"];

    if (meta.contains("references")) {
      nlohmann::json ref = meta["references"];

      if (ref.contains("qual")) {
        std::string qual = ref["qual"];
        if (qual == "append") {
        } else if (qual == "prepend") {
        } else if (qual == "prepend") {
        } else if (qual == "delete") {
        } else {
          // treat as append
        }
      }
    }
  }

  return true;
}

bool JSONToPrimSpec(const std::string &j_str, PrimSpec *ps, std::string *warn, std::string *err) {
  nlohmann::json j = nlohmann::json::parse(j_str, /* callback */nullptr, /* allow_exceptions */false);
  if (j.is_discarded()) {
    if (err) {
      (*err) = "Failed to parse string as JSON\n";
    }
    return false;
  }

  return JSONToPrimSpecImpl(j, ps, warn, err);
}

bool JSONToLayer(const std::string &j_str, Layer *dst_layer, std::string *warn, std::string *err) {
  if (!dst_layer) {
    if (err) {
      (*err) = "Internal error.";
    }
    return false;
  }

  nlohmann::json j = nlohmann::json::parse(j_str, /* callback */nullptr, /* allow_exceptions */false);
  if (j.is_discarded()) {
    if (err) {
      (*err) = "Failed to parse string as JSON";
    }
    return false;
  }

  // Create context for buffer parsing if needed
  JSONToUSDContext context;
  
  // Parse buffer data if present
  if (j.contains("buffers")) {
    if (!context.ParseBuffers(j["buffers"], err)) {
      return false;
    }
  }
  
  if (j.contains("bufferViews")) {
    if (!context.ParseBufferViews(j["bufferViews"], err)) {
      return false;
    }
  }
  
  if (j.contains("accessors")) {
    if (!context.ParseAccessors(j["accessors"], err)) {
      return false;
    }
  }

  Layer layer;

  // Set name if present
  if (j.contains("name") && j["name"].is_string()) {
    layer.set_name(j["name"].get<std::string>());
  }

  // Parse layer metadata
  if (j.contains("metas")) {
    nlohmann::json metas = j["metas"];
    
    if (metas.contains("upAxis") && metas["upAxis"].is_string()) {
      std::string s = metas["upAxis"].get<std::string>();
      if (s == "X") {
        layer.metas().upAxis = tinyusdz::Axis::X;
      } else if (s == "Y") {
        layer.metas().upAxis = tinyusdz::Axis::Y;
      } else if (s == "Z") {
        layer.metas().upAxis = tinyusdz::Axis::Z;
      } else {
        if (err) {
          (*err) = "Unknown upAxis value: " + s;
        }
        return false;
      }
    }
    
    if (metas.contains("defaultPrim") && metas["defaultPrim"].is_string()) {
      layer.metas().defaultPrim = value::token(metas["defaultPrim"].get<std::string>());
    }
    
    if (metas.contains("metersPerUnit") && metas["metersPerUnit"].is_number()) {
      layer.metas().metersPerUnit = metas["metersPerUnit"].get<double>();
    }
    
    if (metas.contains("timeCodesPerSecond") && metas["timeCodesPerSecond"].is_number()) {
      layer.metas().timeCodesPerSecond = metas["timeCodesPerSecond"].get<double>();
    }
    
    if (metas.contains("framesPerSecond") && metas["framesPerSecond"].is_number()) {
      layer.metas().framesPerSecond = metas["framesPerSecond"].get<double>();
    }
    
    if (metas.contains("startTimeCode") && metas["startTimeCode"].is_number()) {
      layer.metas().startTimeCode = metas["startTimeCode"].get<double>();
    }
    
    if (metas.contains("endTimeCode") && metas["endTimeCode"].is_number()) {
      layer.metas().endTimeCode = metas["endTimeCode"].get<double>();
    }
    
    if (metas.contains("kilogramsPerUnit") && metas["kilogramsPerUnit"].is_number()) {
      layer.metas().kilogramsPerUnit = metas["kilogramsPerUnit"].get<double>();
    }
    
    if (metas.contains("doc") && metas["doc"].is_string()) {
      layer.metas().doc = metas["doc"].get<std::string>();
    }
    
    if (metas.contains("comment") && metas["comment"].is_string()) {
      layer.metas().comment = metas["comment"].get<std::string>();
    }
    
    if (metas.contains("autoPlay") && metas["autoPlay"].is_boolean()) {
      layer.metas().autoPlay = metas["autoPlay"].get<bool>();
    }
    
    if (metas.contains("playbackMode") && metas["playbackMode"].is_string()) {
      std::string mode = metas["playbackMode"].get<std::string>();
      if (mode == "loop") {
        layer.metas().playbackMode = LayerMetas::PlaybackMode::PlaybackModeLoop;
      } else {
        layer.metas().playbackMode = LayerMetas::PlaybackMode::PlaybackModeNone;
      }
    }
    
    if (metas.contains("primChildren") && metas["primChildren"].is_array()) {
      std::vector<std::string> children_strs;
      for (const auto& child : metas["primChildren"]) {
        if (child.is_string()) {
          children_strs.push_back(child.get<std::string>());
        }
      }
      std::vector<value::token> children_tokens;
      for (const auto& child_str : children_strs) {
        children_tokens.emplace_back(child_str);
      }
      layer.metas().primChildren = children_tokens;
    }
    
    if (metas.contains("subLayers") && metas["subLayers"].is_array()) {
      for (const auto& subLayer : metas["subLayers"]) {
        if (subLayer.is_object() && subLayer.contains("assetPath") && subLayer["assetPath"].is_string()) {
          SubLayer sub;
          sub.assetPath = value::AssetPath(subLayer["assetPath"].get<std::string>());
          layer.metas().subLayers.push_back(sub);
        }
      }
    }
  }
  
  // Parse primSpecs
  if (j.contains("primSpecs") && j["primSpecs"].is_object()) {
    for (auto it = j["primSpecs"].begin(); it != j["primSpecs"].end(); ++it) {
      const std::string& prim_name = it.key();
      const nlohmann::json& prim_obj = it.value();
      
      if (prim_obj.is_object()) {
        // For now, create basic PrimSpecs
        // TODO: Implement full PrimSpec parsing including geometry data
        PrimSpec primspec;
        primspec.name() = prim_name;
        
        // Add basic type information if available
        if (prim_obj.contains("typeName") && prim_obj["typeName"].is_string()) {
          std::string type_name = prim_obj["typeName"].get<std::string>();
          if (type_name == "GeomMesh") {
            // Create a GeomMesh and convert it to PrimSpec representation
            GeomMesh mesh;
            if (detail::JSONToGeomMesh(prim_obj, &mesh, &context, warn, err)) {
              // TODO: Convert GeomMesh to PrimSpec format
              // This is a complex conversion that would require more work
              // For now, just add the basic structure
            }
          }
        }
        
        layer.primspecs()[prim_name] = primspec;
      }
    }
  }

  (*dst_layer) = std::move(layer);

  return true;
}

bool JSONToGeomMesh(const std::string &j_str, GeomMesh *mesh, std::string *warn, std::string *err) {
  if (!mesh) {
    if (err) {
      (*err) = "Internal error: mesh is null";
    }
    return false;
  }

  nlohmann::json j = nlohmann::json::parse(j_str, /* callback */nullptr, /* allow_exceptions */false);
  if (j.is_discarded()) {
    if (err) {
      (*err) = "Failed to parse string as JSON";
    }
    return false;
  }

  return detail::JSONToGeomMesh(j, mesh, nullptr, warn, err);
}

} // namespace tinyusdz
