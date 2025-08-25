// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#include "usd-to-json.hh"

#include "tinyusdz.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// nlohmann json
#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "common-macros.inc"
#include "pprinter.hh"
#include "str-util.hh"

using namespace nlohmann;

namespace tinyusdz {

// Implementation of USDToJSONContext::AddArrayData
size_t USDToJSONContext::AddArrayData(const void* data, size_t elementSize, size_t elementCount, 
                                       const std::string& componentType, const std::string& type) {
  if (!data || elementCount == 0) {
    return SIZE_MAX;  // Invalid accessor index
  }
  
  size_t totalBytes = elementSize * elementCount;
  
  // Create or use existing buffer
  if (buffers.empty()) {
    buffers.emplace_back();
  }
  
  JSONBuffer& buffer = buffers.back();
  size_t bufferIndex = buffers.size() - 1;
  size_t byteOffset = buffer.data.size();
  
  // Add data to buffer with proper alignment
  const uint8_t* srcData = static_cast<const uint8_t*>(data);
  buffer.data.insert(buffer.data.end(), srcData, srcData + totalBytes);
  buffer.byteLength = buffer.data.size();
  
  // Create buffer view
  JSONBufferView bufferView;
  bufferView.buffer = bufferIndex;
  bufferView.byteOffset = byteOffset;
  bufferView.byteLength = totalBytes;
  bufferView.byteStride = 0;  // Tightly packed
  
  size_t bufferViewIndex = bufferViews.size();
  bufferViews.push_back(bufferView);
  
  // Create accessor
  JSONAccessor accessor;
  accessor.bufferView = bufferViewIndex;
  accessor.byteOffset = 0;
  accessor.componentType = componentType;
  accessor.count = elementCount;
  accessor.type = type;
  
  size_t accessorIndex = accessors.size();
  accessors.push_back(accessor);
  
  return accessorIndex;
}

namespace {

// Helper functions for array serialization to base64
template<typename T>
std::string SerializeArrayToBase64(const std::vector<T>& array) {
  if (array.empty()) {
    return "";
  }
  
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(array.data());
  size_t byte_size = array.size() * sizeof(T);
  
  return base64_encode(bytes, static_cast<unsigned int>(byte_size));
}

// Specialized versions for different types
std::string SerializeIntArrayToBase64(const std::vector<int>& array) {
  return SerializeArrayToBase64(array);
}

std::string SerializeFloatArrayToBase64(const std::vector<float>& array) {
  return SerializeArrayToBase64(array);
}

std::string SerializeDoubleArrayToBase64(const std::vector<double>& array) {
  return SerializeArrayToBase64(array);
}

// Helper functions for mixed-mode serialization
template<typename T>
json SerializeArrayData(const std::vector<T>& array, USDToJSONContext* context, 
                        const std::string& componentType, const std::string& type) {
  if (array.empty()) {
    return json::object();
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    // Base64 mode
    return json{
      {"data", SerializeArrayToBase64(array)},
      {"count", array.size()},
      {"type", type + "[]"}
    };
  } else {
    // Buffer/accessor mode
    size_t accessorIndex = context->AddArrayData(array.data(), sizeof(T), array.size(), componentType, type);
    if (accessorIndex == SIZE_MAX) {
      // Fallback to base64 on error
      return json{
        {"data", SerializeArrayToBase64(array)},
        {"count", array.size()},
        {"type", type + "[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", array.size()},
      {"type", type + "[]"}
    };
  }
}

// Helper function to serialize attribute metadata
json SerializeAttributeMetadata(const AttrMetas& metas) {
  json metadata;
  
  // Serialize interpolation
  if (metas.interpolation) {
    switch (metas.interpolation.value()) {
      case Interpolation::Constant:
        metadata["interpolation"] = "constant";
        break;
      case Interpolation::Uniform:
        metadata["interpolation"] = "uniform";
        break;
      case Interpolation::Varying:
        metadata["interpolation"] = "varying";
        break;
      case Interpolation::Vertex:
        metadata["interpolation"] = "vertex";
        break;
      case Interpolation::FaceVarying:
        metadata["interpolation"] = "faceVarying";
        break;
      case Interpolation::Invalid:
        metadata["interpolation"] = "[[invalid]]";
        break;
    }
  }
  
  // Serialize elementSize
  if (metas.elementSize) {
    metadata["elementSize"] = metas.elementSize.value();
  }
  
  // Serialize hidden
  if (metas.hidden) {
    metadata["hidden"] = metas.hidden.value();
  }
  
  // Serialize comment
  if (metas.comment) {
    metadata["comment"] = metas.comment.value().value;
  }
  
  // Serialize weight (for BlendShapes)
  if (metas.weight) {
    metadata["weight"] = metas.weight.value();
  }
  
  // Serialize usdShade metadata
  if (metas.connectability) {
    metadata["connectability"] = metas.connectability.value().str();
  }
  
  if (metas.outputName) {
    metadata["outputName"] = metas.outputName.value().str();
  }
  
  if (metas.renderType) {
    metadata["renderType"] = metas.renderType.value().str();
  }
  
  // Serialize display metadata
  if (metas.displayName) {
    metadata["displayName"] = metas.displayName.value();
  }
  
  if (metas.displayGroup) {
    metadata["displayGroup"] = metas.displayGroup.value();
  }
  
  // Serialize bindMaterialAs
  if (metas.bindMaterialAs) {
    metadata["bindMaterialAs"] = metas.bindMaterialAs.value().str();
  }
  
  // Serialize customData
  if (metas.customData) {
    json customDataJson;
    const auto& customData = metas.customData.value();
    for (const auto& item : customData) {
      // For now, serialize as string representation
      // TODO: Implement proper Dictionary to JSON conversion
      customDataJson[item.first] = "[CustomData]";
    }
    if (!customDataJson.empty()) {
      metadata["customData"] = customDataJson;
    }
  }
  
  // Serialize sdrMetadata
  if (metas.sdrMetadata) {
    json sdrJson;
    const auto& sdrData = metas.sdrMetadata.value();
    for (const auto& item : sdrData) {
      // For now, serialize as string representation
      // TODO: Implement proper Dictionary to JSON conversion
      sdrJson[item.first] = "[SdrMetadata]";
    }
    if (!sdrJson.empty()) {
      metadata["sdrMetadata"] = sdrJson;
    }
  }
  
  // Serialize other custom metadata
  for (const auto& item : metas.meta) {
    // TODO: Implement proper MetaVariable to JSON conversion
    metadata[item.first] = "[MetaVariable]";
  }
  
  // Serialize string data
  if (!metas.stringData.empty()) {
    json stringArray = json::array();
    for (const auto& str : metas.stringData) {
      stringArray.push_back(str.value);
    }
    metadata["stringData"] = stringArray;
  }
  
  return metadata;
}

// Specialized array serialization functions
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-template"

json SerializeIntArray(const std::vector<int>& array, USDToJSONContext* context = nullptr) {
  return SerializeArrayData(array, context, "UNSIGNED_INT", "SCALAR");
}

json SerializeFloatArray(const std::vector<float>& array, USDToJSONContext* context = nullptr) {
  return SerializeArrayData(array, context, "FLOAT", "SCALAR");
}

json SerializeDoubleArray(const std::vector<double>& array, USDToJSONContext* context = nullptr) {
  return SerializeArrayData(array, context, "FLOAT", "SCALAR");  // Note: JSON doesn't distinguish float/double
}

// Overloaded functions with attribute metadata support
template<typename T>
json SerializeArrayDataWithMetadata(const std::vector<T>& array, const AttrMetas* metas, USDToJSONContext* context, 
                                     const std::string& componentType, const std::string& type) {
  json result = SerializeArrayData(array, context, componentType, type);
  
  // Add metadata if present and array is not empty
  if (metas && metas->authored() && !array.empty()) {
    json metadata = SerializeAttributeMetadata(*metas);
    if (!metadata.empty()) {
      result["metadata"] = metadata;
    }
  }
  
  return result;
}

// Metadata-aware array serialization functions
json SerializeIntArrayWithMetadata(const std::vector<int>& array, const AttrMetas* metas = nullptr, USDToJSONContext* context = nullptr) {
  return SerializeArrayDataWithMetadata(array, metas, context, "UNSIGNED_INT", "SCALAR");
}

json SerializeFloatArrayWithMetadata(const std::vector<float>& array, const AttrMetas* metas = nullptr, USDToJSONContext* context = nullptr) {
  return SerializeArrayDataWithMetadata(array, metas, context, "FLOAT", "SCALAR");
}

json SerializeDoubleArrayWithMetadata(const std::vector<double>& array, const AttrMetas* metas = nullptr, USDToJSONContext* context = nullptr) {
  return SerializeArrayDataWithMetadata(array, metas, context, "FLOAT", "SCALAR");
}

// Vector serialization helpers
json SerializePoint3fArray(const std::vector<value::point3f>& points, USDToJSONContext* context = nullptr) {
  if (points.empty()) {
    return json::object();
  }
  
  // Convert to flat float array
  std::vector<float> float_data;
  float_data.reserve(points.size() * 3);
  for (const auto& pt : points) {
    float_data.push_back(pt[0]);
    float_data.push_back(pt[1]);
    float_data.push_back(pt[2]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", points.size()},
      {"type", "point3f[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC3");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", points.size()},
        {"type", "point3f[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", points.size()},
      {"type", "point3f[]"}
    };
  }
}

json SerializeNormal3fArray(const std::vector<value::normal3f>& normals, USDToJSONContext* context = nullptr) {
  if (normals.empty()) {
    return json::object();
  }
  
  // Convert to flat float array
  std::vector<float> float_data;
  float_data.reserve(normals.size() * 3);
  for (const auto& n : normals) {
    float_data.push_back(n[0]);
    float_data.push_back(n[1]);
    float_data.push_back(n[2]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", normals.size()},
      {"type", "normal3f[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC3");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", normals.size()},
        {"type", "normal3f[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", normals.size()},
      {"type", "normal3f[]"}
    };
  }
}

// Vector array serialization helpers for integer types
json SerializeInt2Array(const std::vector<value::int2>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat int array
  std::vector<int> int_data;
  int_data.reserve(vectors.size() * 2);
  for (const auto& v : vectors) {
    int_data.push_back(v[0]);
    int_data.push_back(v[1]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeIntArrayToBase64(int_data)},
      {"count", vectors.size()},
      {"type", "int2[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(int_data.data(), sizeof(int), int_data.size(), "UNSIGNED_INT", "VEC2");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeIntArrayToBase64(int_data)},
        {"count", vectors.size()},
        {"type", "int2[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "int2[]"}
    };
  }
}

json SerializeInt3Array(const std::vector<value::int3>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat int array
  std::vector<int> int_data;
  int_data.reserve(vectors.size() * 3);
  for (const auto& v : vectors) {
    int_data.push_back(v[0]);
    int_data.push_back(v[1]);
    int_data.push_back(v[2]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeIntArrayToBase64(int_data)},
      {"count", vectors.size()},
      {"type", "int3[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(int_data.data(), sizeof(int), int_data.size(), "UNSIGNED_INT", "VEC3");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeIntArrayToBase64(int_data)},
        {"count", vectors.size()},
        {"type", "int3[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "int3[]"}
    };
  }
}

json SerializeInt4Array(const std::vector<value::int4>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat int array
  std::vector<int> int_data;
  int_data.reserve(vectors.size() * 4);
  for (const auto& v : vectors) {
    int_data.push_back(v[0]);
    int_data.push_back(v[1]);
    int_data.push_back(v[2]);
    int_data.push_back(v[3]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeIntArrayToBase64(int_data)},
      {"count", vectors.size()},
      {"type", "int4[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(int_data.data(), sizeof(int), int_data.size(), "UNSIGNED_INT", "VEC4");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeIntArrayToBase64(int_data)},
        {"count", vectors.size()},
        {"type", "int4[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "int4[]"}
    };
  }
}

// Vector array serialization helpers for float types
json SerializeFloat2Array(const std::vector<value::float2>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat float array
  std::vector<float> float_data;
  float_data.reserve(vectors.size() * 2);
  for (const auto& v : vectors) {
    float_data.push_back(v[0]);
    float_data.push_back(v[1]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", vectors.size()},
      {"type", "float2[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC2");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", vectors.size()},
        {"type", "float2[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "float2[]"}
    };
  }
}

json SerializeFloat4Array(const std::vector<value::float4>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat float array
  std::vector<float> float_data;
  float_data.reserve(vectors.size() * 4);
  for (const auto& v : vectors) {
    float_data.push_back(v[0]);
    float_data.push_back(v[1]);
    float_data.push_back(v[2]);
    float_data.push_back(v[3]);
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", vectors.size()},
      {"type", "float4[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC4");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", vectors.size()},
        {"type", "float4[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "float4[]"}
    };
  }
}

// Vector array serialization helpers for half types
json SerializeHalf2Array(const std::vector<value::half2>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat float array (convert half to float for JSON)
  std::vector<float> float_data;
  float_data.reserve(vectors.size() * 2);
  for (const auto& v : vectors) {
    float_data.push_back(value::half_to_float(v[0]));
    float_data.push_back(value::half_to_float(v[1]));
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", vectors.size()},
      {"type", "half2[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC2");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", vectors.size()},
        {"type", "half2[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "half2[]"}
    };
  }
}

json SerializeHalf3Array(const std::vector<value::half3>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat float array (convert half to float for JSON)
  std::vector<float> float_data;
  float_data.reserve(vectors.size() * 3);
  for (const auto& v : vectors) {
    float_data.push_back(value::half_to_float(v[0]));
    float_data.push_back(value::half_to_float(v[1]));
    float_data.push_back(value::half_to_float(v[2]));
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", vectors.size()},
      {"type", "half3[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC3");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", vectors.size()},
        {"type", "half3[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "half3[]"}
    };
  }
}

json SerializeHalf4Array(const std::vector<value::half4>& vectors, USDToJSONContext* context = nullptr) {
  if (vectors.empty()) {
    return json::object();
  }
  
  // Convert to flat float array (convert half to float for JSON)
  std::vector<float> float_data;
  float_data.reserve(vectors.size() * 4);
  for (const auto& v : vectors) {
    float_data.push_back(value::half_to_float(v[0]));
    float_data.push_back(value::half_to_float(v[1]));
    float_data.push_back(value::half_to_float(v[2]));
    float_data.push_back(value::half_to_float(v[3]));
  }
  
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    return json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", vectors.size()},
      {"type", "half4[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC4");
    if (accessorIndex == SIZE_MAX) {
      return json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", vectors.size()},
        {"type", "half4[]"}
      };
    }
    
    return json{
      {"accessor", accessorIndex},
      {"count", vectors.size()},
      {"type", "half4[]"}
    };
  }
}

// Metadata-aware vector serialization functions
json SerializePoint3fArrayWithMetadata(const std::vector<value::point3f>& points, const AttrMetas* metas = nullptr, USDToJSONContext* context = nullptr) {
  if (points.empty()) {
    return json::object();
  }
  
  // Convert to flat float array
  std::vector<float> float_data;
  float_data.reserve(points.size() * 3);
  for (const auto& pt : points) {
    float_data.push_back(pt[0]);
    float_data.push_back(pt[1]);
    float_data.push_back(pt[2]);
  }
  
  json result;
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    result = json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", points.size()},
      {"type", "point3f[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC3");
    if (accessorIndex == SIZE_MAX) {
      result = json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", points.size()},
        {"type", "point3f[]"}
      };
    } else {
      result = json{
        {"accessor", accessorIndex},
        {"count", points.size()},
        {"type", "point3f[]"}
      };
    }
  }
  
  // Add metadata if present
  if (metas && metas->authored()) {
    json metadata = SerializeAttributeMetadata(*metas);
    if (!metadata.empty()) {
      result["metadata"] = metadata;
    }
  }
  
  return result;
}

json SerializeNormal3fArrayWithMetadata(const std::vector<value::normal3f>& normals, const AttrMetas* metas = nullptr, USDToJSONContext* context = nullptr) {
  if (normals.empty()) {
    return json::object();
  }
  
  // Convert to flat float array
  std::vector<float> float_data;
  float_data.reserve(normals.size() * 3);
  for (const auto& n : normals) {
    float_data.push_back(n[0]);
    float_data.push_back(n[1]);
    float_data.push_back(n[2]);
  }
  
  json result;
  if (!context || context->options.arrayMode == ArraySerializationMode::Base64) {
    result = json{
      {"data", SerializeFloatArrayToBase64(float_data)},
      {"count", normals.size()},
      {"type", "normal3f[]"}
    };
  } else {
    size_t accessorIndex = context->AddArrayData(float_data.data(), sizeof(float), float_data.size(), "FLOAT", "VEC3");
    if (accessorIndex == SIZE_MAX) {
      result = json{
        {"data", SerializeFloatArrayToBase64(float_data)},
        {"count", normals.size()},
        {"type", "normal3f[]"}
      };
    } else {
      result = json{
        {"accessor", accessorIndex},
        {"count", normals.size()},
        {"type", "normal3f[]"}
      };
    }
  }
  
  // Add metadata if present
  if (metas && metas->authored()) {
    json metadata = SerializeAttributeMetadata(*metas);
    if (!metadata.empty()) {
      result["metadata"] = metadata;
    }
  }
  
  return result;
}

// Matrix array serialization
template<typename MatrixType>
std::string SerializeMatrixArrayToBase64(const std::vector<MatrixType>& array) {
  return SerializeArrayToBase64(array);
}

// Specialized matrix serializers
std::string SerializeMatrix2fArrayToBase64(const std::vector<value::matrix2f>& array) {
  if (array.empty()) {
    return "";
  }
  
  std::vector<float> float_data;
  float_data.reserve(array.size() * 4);
  for (const auto& mat : array) {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 2; ++j) {
        float_data.push_back(mat.m[i][j]);
      }
    }
  }
  return SerializeFloatArrayToBase64(float_data);
}

std::string SerializeMatrix3fArrayToBase64(const std::vector<value::matrix3f>& array) {
  if (array.empty()) {
    return "";
  }
  
  std::vector<float> float_data;
  float_data.reserve(array.size() * 9);
  for (const auto& mat : array) {
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        float_data.push_back(mat.m[i][j]);
      }
    }
  }
  return SerializeFloatArrayToBase64(float_data);
}

std::string SerializeMatrix4fArrayToBase64(const std::vector<value::matrix4f>& array) {
  if (array.empty()) {
    return "";
  }
  
  std::vector<float> float_data;
  float_data.reserve(array.size() * 16);
  for (const auto& mat : array) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        float_data.push_back(mat.m[i][j]);
      }
    }
  }
  return SerializeFloatArrayToBase64(float_data);
}

std::string SerializeMatrix2dArrayToBase64(const std::vector<value::matrix2d>& array) {
  if (array.empty()) {
    return "";
  }
  
  std::vector<double> double_data;
  double_data.reserve(array.size() * 4);
  for (const auto& mat : array) {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 2; ++j) {
        double_data.push_back(mat.m[i][j]);
      }
    }
  }
  return SerializeDoubleArrayToBase64(double_data);
}

std::string SerializeMatrix3dArrayToBase64(const std::vector<value::matrix3d>& array) {
  if (array.empty()) {
    return "";
  }
  
  std::vector<double> double_data;
  double_data.reserve(array.size() * 9);
  for (const auto& mat : array) {
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        double_data.push_back(mat.m[i][j]);
      }
    }
  }
  return SerializeDoubleArrayToBase64(double_data);
}

std::string SerializeMatrix4dArrayToBase64(const std::vector<value::matrix4d>& array) {
  if (array.empty()) {
    return "";
  }
  
  std::vector<double> double_data;
  double_data.reserve(array.size() * 16);
  for (const auto& mat : array) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        double_data.push_back(mat.m[i][j]);
      }
    }
  }
  return SerializeDoubleArrayToBase64(double_data);
}

#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

json ToJSON(tinyusdz::Xform& xform) {
  json j;

  j["name"] = xform.name;
  j["typeName"] = "Xform";

  if (xform.xformOps.size()) {
    json jxformOpOrder;

    std::vector<std::string> ops;
    for (const auto &xformOp : xform.xformOps) {
      ops.push_back(xformOp.suffix);
    }

    j["xformOpOrder"] = ops;
  }

  return j;
}

json ToJSON(tinyusdz::GeomMesh& mesh) {
  return ToJSON(mesh, nullptr);  // Use base64 mode by default
}

json ToJSON(tinyusdz::GeomMesh& mesh, USDToJSONContext* context) {
  json j;

  j["name"] = mesh.name;
  j["typeName"] = "GeomMesh";

  // Serialize geometry arrays using context-aware method
  
  // Points array (point3f[])
  if (mesh.points.authored()) {
    auto points_opt = mesh.points.get_value();
    if (points_opt) {
      std::vector<value::point3f> points_data;
      if (points_opt.value().get(value::TimeCode::Default(), &points_data)) {
        j["points"] = SerializePoint3fArrayWithMetadata(points_data, &mesh.points.metas(), context);
      }
    }
  }

  // Face vertex counts (int[])
  if (mesh.faceVertexCounts.authored()) {
    auto face_counts_opt = mesh.faceVertexCounts.get_value();
    if (face_counts_opt) {
      std::vector<int> face_counts_data;
      if (face_counts_opt.value().get(value::TimeCode::Default(), &face_counts_data)) {
        j["faceVertexCounts"] = SerializeIntArrayWithMetadata(face_counts_data, &mesh.faceVertexCounts.metas(), context);
      }
    }
  }

  // Face vertex indices (int[])
  if (mesh.faceVertexIndices.authored()) {
    auto face_indices_opt = mesh.faceVertexIndices.get_value();
    if (face_indices_opt) {
      std::vector<int> face_indices_data;
      if (face_indices_opt.value().get(value::TimeCode::Default(), &face_indices_data)) {
        j["faceVertexIndices"] = SerializeIntArrayWithMetadata(face_indices_data, &mesh.faceVertexIndices.metas(), context);
      }
    }
  }

  // Normals array (normal3f[])
  if (mesh.normals.authored()) {
    auto normals_opt = mesh.normals.get_value();
    if (normals_opt) {
      std::vector<value::normal3f> normals_data;
      if (normals_opt.value().get(value::TimeCode::Default(), &normals_data)) {
        j["normals"] = SerializeNormal3fArrayWithMetadata(normals_data, &mesh.normals.metas(), context);
      }
    }
  }

  // Subdivision surface arrays
  if (mesh.cornerIndices.authored()) {
    auto corner_indices_opt = mesh.cornerIndices.get_value();
    if (corner_indices_opt) {
      std::vector<int> corner_indices_data;
      if (corner_indices_opt.value().get(value::TimeCode::Default(), &corner_indices_data)) {
        j["cornerIndices"] = SerializeIntArrayWithMetadata(corner_indices_data, &mesh.cornerIndices.metas(), context);
      }
    }
  }

  if (mesh.cornerSharpnesses.authored()) {
    auto corner_sharpnesses_opt = mesh.cornerSharpnesses.get_value();
    if (corner_sharpnesses_opt) {
      std::vector<float> corner_sharpnesses_data;
      if (corner_sharpnesses_opt.value().get(value::TimeCode::Default(), &corner_sharpnesses_data)) {
        j["cornerSharpnesses"] = SerializeFloatArrayWithMetadata(corner_sharpnesses_data, &mesh.cornerSharpnesses.metas(), context);
      }
    }
  }

  if (mesh.creaseIndices.authored()) {
    auto crease_indices_opt = mesh.creaseIndices.get_value();
    if (crease_indices_opt) {
      std::vector<int> crease_indices_data;
      if (crease_indices_opt.value().get(value::TimeCode::Default(), &crease_indices_data)) {
        j["creaseIndices"] = SerializeIntArrayWithMetadata(crease_indices_data, &mesh.creaseIndices.metas(), context);
      }
    }
  }

  if (mesh.creaseLengths.authored()) {
    auto crease_lengths_opt = mesh.creaseLengths.get_value();
    if (crease_lengths_opt) {
      std::vector<int> crease_lengths_data;
      if (crease_lengths_opt.value().get(value::TimeCode::Default(), &crease_lengths_data)) {
        j["creaseLengths"] = SerializeIntArrayWithMetadata(crease_lengths_data, &mesh.creaseLengths.metas(), context);
      }
    }
  }

  if (mesh.creaseSharpnesses.authored()) {
    auto crease_sharpnesses_opt = mesh.creaseSharpnesses.get_value();
    if (crease_sharpnesses_opt) {
      std::vector<float> crease_sharpnesses_data;
      if (crease_sharpnesses_opt.value().get(value::TimeCode::Default(), &crease_sharpnesses_data)) {
        j["creaseSharpnesses"] = SerializeFloatArrayWithMetadata(crease_sharpnesses_data, &mesh.creaseSharpnesses.metas(), context);
      }
    }
  }

  return j;
}

json ToJSON(tinyusdz::GeomBasisCurves& curves) {
  
  json j;
  j["name"] = curves.name;
  j["typeName"] = "GeomBasisCurves";

  // TODO
  USDToJSONContext *context = nullptr;

  // Points array (point3f[])
  if (curves.points.authored()) {
    auto points_opt = curves.points.get_value();
    if (points_opt) {
      std::vector<value::point3f> points_data;
      if (points_opt.value().get(value::TimeCode::Default(), &points_data)) {
        j["points"] = SerializePoint3fArrayWithMetadata(points_data, &curves.points.metas(), context);
      }
    }
  }

  // TODO: Serialize other attribs
  
  return j;
}

json ToJSON(const tinyusdz::value::Value &v) {
  if (auto pv = v.get_value<tinyusdz::Xform>()) {
    return ToJSON(pv.value());
  }


  return json();


}

nonstd::expected<json, std::string> ToJSON(const tinyusdz::StageMetas& metas) {
  json j;

  if (metas.upAxis.authored()) {
    j["upAxis"] = to_string(metas.upAxis.get_value());
  }

  if (metas.comment.value.size()) {
    // TODO: escape and quote
    j["comment"] = metas.comment.value;
  }

  return j;
}

bool PrimToJSONRec(json &root, const tinyusdz::Prim& prim, int depth) {
  json j = ToJSON(prim.data());

  json jchildren = json::object();

  // TODO: Traverse Prim according to primChildren.
  for (const auto &child : prim.children()) {
    json cj;
    if (!PrimToJSONRec(cj, child, depth+1)) {
      return false;
    }
    std::string cname = child.element_name();
    jchildren[cname] = cj;
  }

  if (jchildren.size()) {
    j["primChildren"] = jchildren;
  }

  root[prim.element_name()] = j;

  return true;
}

// Helper function to serialize context to JSON
json SerializeContextToJSON(const USDToJSONContext& context) {
  json j;
  
  // Serialize buffers
  if (!context.buffers.empty()) {
    json buffers_array = json::array();
    for (size_t i = 0; i < context.buffers.size(); ++i) {
      const auto& buffer = context.buffers[i];
      json buffer_obj;
      buffer_obj["byteLength"] = buffer.byteLength;
      
      if (context.options.embedBuffers) {
        // Embed as data URI
        std::string base64_data = base64_encode(buffer.data.data(), static_cast<unsigned int>(buffer.data.size()));
        buffer_obj["uri"] = "data:application/octet-stream;base64," + base64_data;
      } else {
        // External file reference
        buffer_obj["uri"] = context.options.bufferPrefix + std::to_string(i) + ".bin";
      }
      
      buffers_array.push_back(buffer_obj);
    }
    j["buffers"] = buffers_array;
  }
  
  // Serialize buffer views
  if (!context.bufferViews.empty()) {
    json bufferViews_array = json::array();
    for (const auto& bufferView : context.bufferViews) {
      json bufferView_obj;
      bufferView_obj["buffer"] = bufferView.buffer;
      bufferView_obj["byteOffset"] = bufferView.byteOffset;
      bufferView_obj["byteLength"] = bufferView.byteLength;
      if (bufferView.byteStride > 0) {
        bufferView_obj["byteStride"] = bufferView.byteStride;
      }
      bufferViews_array.push_back(bufferView_obj);
    }
    j["bufferViews"] = bufferViews_array;
  }
  
  // Serialize accessors
  if (!context.accessors.empty()) {
    json accessors_array = json::array();
    for (const auto& accessor : context.accessors) {
      json accessor_obj;
      accessor_obj["bufferView"] = accessor.bufferView;
      accessor_obj["byteOffset"] = accessor.byteOffset;
      accessor_obj["componentType"] = accessor.componentType;
      accessor_obj["count"] = accessor.count;
      accessor_obj["type"] = accessor.type;
      accessors_array.push_back(accessor_obj);
    }
    j["accessors"] = accessors_array;
  }
  
  return j;
}



}  // namespace


json ToJSON(const tinyusdz::Layer& layer) {
  USDToJSONContext context;  // Default context (base64 mode)
  return ToJSON(layer, context);
}

json ToJSON(const tinyusdz::Layer& layer, USDToJSONContext& context) {
  json j;

  // Layer name
  j["name"] = layer.name();
  j["typeName"] = "Layer";

  // Layer metadata
  const LayerMetas& metas = layer.metas();
  
  json layerMetas;
  // Basic layer properties
  if (metas.upAxis.authored()) {
    layerMetas["upAxis"] = to_string(metas.upAxis.get_value());
  }
  
  if (!metas.defaultPrim.str().empty()) {
    layerMetas["defaultPrim"] = metas.defaultPrim.str();
  }
  
  if (metas.metersPerUnit.authored()) {
    layerMetas["metersPerUnit"] = metas.metersPerUnit.get_value();
  }
  
  if (metas.timeCodesPerSecond.authored()) {
    layerMetas["timeCodesPerSecond"] = metas.timeCodesPerSecond.get_value();
  }
  
  if (metas.framesPerSecond.authored()) {
    layerMetas["framesPerSecond"] = metas.framesPerSecond.get_value();
  }
  
  if (metas.startTimeCode.authored()) {
    layerMetas["startTimeCode"] = metas.startTimeCode.get_value();
  }
  
  if (metas.endTimeCode.authored()) {
    layerMetas["endTimeCode"] = metas.endTimeCode.get_value();
  }
  
  if (metas.kilogramsPerUnit.authored()) {
    layerMetas["kilogramsPerUnit"] = metas.kilogramsPerUnit.get_value();
  }
  
  // SubLayers
  if (metas.subLayers.size() > 0) {
    json subLayersArray = json::array();
    for (const auto& subLayer : metas.subLayers) {
      json subLayerObj;
      subLayerObj["assetPath"] = subLayer.assetPath.GetAssetPath();
      // TODO: layerOffset
      //if (subLayer.layerOffset.offset != 0.0 || subLayer.layerOffset.scale != 1.0) {
      //  json layerOffsetObj;
      //  layerOffsetObj["offset"] = subLayer.layerOffset.offset;
      //  layerOffsetObj["scale"] = subLayer.layerOffset.scale;
      //  subLayerObj["layerOffset"] = layerOffsetObj;
      //}
      subLayersArray.push_back(subLayerObj);
    }
    layerMetas["subLayers"] = subLayersArray;
  }
  
  // Documentation and comment
  if (!metas.doc.value.empty()) {
    layerMetas["doc"] = metas.doc.value;
  }
  
  if (!metas.comment.value.empty()) {
    layerMetas["comment"] = metas.comment.value;
  }
  
  // Custom layer data
  if (metas.customLayerData.size() > 0) {
    json customData;
    for (const auto& item : metas.customLayerData) {
      // TODO: Implement proper custom data serialization
      customData[item.first] = "[CustomData]";
    }
    layerMetas["customLayerData"] = customData;
  }
  
  // USDZ extensions
  if (metas.autoPlay.authored()) {
    layerMetas["autoPlay"] = metas.autoPlay.get_value();
  }
  
  if (metas.playbackMode.authored()) {
    auto playbackMode = metas.playbackMode.get_value();
    if (playbackMode == LayerMetas::PlaybackMode::PlaybackModeLoop) {
      layerMetas["playbackMode"] = "loop";
    } else {
      layerMetas["playbackMode"] = "none";
    }
  }
  
  // PrimChildren
  if (metas.primChildren.size() > 0) {
    json primChildrenArray = json::array();
    for (const auto& primChild : metas.primChildren) {
      primChildrenArray.push_back(primChild.str());
    }
    layerMetas["primChildren"] = primChildrenArray;
  }
  
  // Only add metas if there's content
  if (!layerMetas.empty()) {
    j["metas"] = layerMetas;
  }
  
  // PrimSpecs
  const auto& primspecs = layer.primspecs();
  if (primspecs.size() > 0) {
    json primSpecsObj;
    for (const auto& item : primspecs) {
      // TODO: Implement PrimSpec to JSON conversion with context
      json primSpecJson;
      primSpecJson["name"] = item.first;
      primSpecJson["typeName"] = "PrimSpec";
      // Add basic PrimSpec info - would need ToJSON for PrimSpec
      primSpecsObj[item.first] = primSpecJson;
    }
    j["primSpecs"] = primSpecsObj;
  }
  
  // Add buffer/accessor data if using buffer mode
  if (context.options.arrayMode == ArraySerializationMode::Buffer) {
    json contextData = SerializeContextToJSON(context);
    if (contextData.contains("buffers")) {
      j["buffers"] = contextData["buffers"];
    }
    if (contextData.contains("bufferViews")) {
      j["bufferViews"] = contextData["bufferViews"];
    }
    if (contextData.contains("accessors")) {
      j["accessors"] = contextData["accessors"];
    }
  }

  return j;
}

nonstd::expected<std::string, std::string> ToJSON(
    const tinyusdz::Stage& stage) {
  json j;  // root

  auto jstageMetas = ToJSON(stage.metas());
  if (!jstageMetas) {
    return nonstd::make_unexpected(jstageMetas.error());
  }
  
  // Stage metadatum is represented as properties.
  if (!jstageMetas->is_null()) {
    j["properties"] = *jstageMetas;
  }

  j["version"] = 1.0;

  json cj;
  for (const auto& item : stage.root_prims()) {
    if (!PrimToJSONRec(cj, item, 0)) {
      return nonstd::make_unexpected("Failed to convert Prim to JSON.");
    }
  }

  j["primChildren"] = cj;

  tinyusdz::GeomMesh mesh;
  json jmesh = ToJSON(mesh);

  (void)jmesh;

  tinyusdz::GeomBasisCurves curves;
  json jcurves = ToJSON(curves);

  (void)jcurves;

  std::string str = j.dump(/* indent*/ 2);

  return str;
}

bool to_json_string(const tinyusdz::Layer &layer, std::string *json_str, std::string *warn, std::string *err) {
  if (!json_str) {
    return false;
  }

  (void)warn;
  (void)err;

  USDToJSONContext context;  // Default context (base64 mode)
  json j = ToJSON(layer, context);

  (*json_str) = j.dump();

  return true;

}

#pragma clang diagnostic pop

bool to_json_string(const tinyusdz::Layer &layer, const USDToJSONOptions& options, std::string *json_str, std::string *warn, std::string *err) {

  // TODO: options
  (void)options;
  
  return to_json_string(layer, json_str, warn, err);

}

// ================================================================
// Property, Attribute, and Relationship to JSON conversion
// ================================================================

json ToJSON(const tinyusdz::Attribute& attribute, USDToJSONContext* /* context */) {
  json j;
  
  // Basic attribute information
  j["name"] = attribute.name();
  j["typeName"] = attribute.type_name();
  
  // Variability
  switch (attribute.variability()) {
    case Variability::Varying:
      j["variability"] = "varying";
      break;
    case Variability::Uniform:
      j["variability"] = "uniform";
      break;
    case Variability::Config:
      j["variability"] = "config";
      break;
    case Variability::Invalid:
      j["variability"] = "invalid";
      break;
  }
  
  // Interpolation (from metadata)
  if (attribute.metas().interpolation) {
    switch (attribute.metas().interpolation.value()) {
      case Interpolation::Invalid:
        j["interpolation"] = "invalid";
        break;
      case Interpolation::Constant:
        j["interpolation"] = "constant";
        break;
      case Interpolation::Uniform:
        j["interpolation"] = "uniform";
        break;
      case Interpolation::Varying:
        j["interpolation"] = "varying";
        break;
      case Interpolation::Vertex:
        j["interpolation"] = "vertex";
        break;
      case Interpolation::FaceVarying:
        j["interpolation"] = "faceVarying";
        break;
    }
  }
  
  // Attribute metadata
  if (attribute.metas().authored()) {
    j["metadata"] = SerializeAttributeMetadata(attribute.metas());
  }
  
  // Connection information
  if (attribute.is_connection()) {
    j["isConnection"] = true;
    auto connections = attribute.connections();
    if (connections.size() == 1) {
      j["connection"] = connections[0].full_path_name();
    } else if (connections.size() > 1) {
      json connections_array = json::array();
      for (const auto& conn : connections) {
        connections_array.push_back(conn.full_path_name());
      }
      j["connections"] = connections_array;
    }
  } else {
    j["isConnection"] = false;
  }
  
  // Value information
  if (attribute.is_blocked()) {
    j["hasValue"] = false;
    j["valueType"] = "blocked";
    j["value"] = nullptr;
  } else {
    // Check if attribute has value by accessing the internal value container
    const auto& var = attribute.get_var();
    if (var.is_valid()) {
      j["hasValue"] = true;
      j["valueType"] = "data";
      
      // For now, serialize as a string representation
      // TODO: Implement proper value type serialization based on type_id
      j["value"] = "[Attribute value - serialization not yet implemented]";
      
      // Store type information for debugging
      j["valueTypeName"] = attribute.type_name();
    } else {
      j["hasValue"] = false;
      j["valueType"] = "empty";
      j["value"] = nullptr;
    }
  }
  
  // Time samples information
  if (attribute.is_timesamples()) {
    j["hasTimeSamples"] = true;
    // TODO: Serialize time sample data
    j["timeSamples"] = "[TimeSamples data - not yet serialized]";
  } else {
    j["hasTimeSamples"] = false;
  }
  
  return j;
}

json ToJSON(const tinyusdz::Relationship& relationship) {
  json j;
  
  j["type"] = "relationship";
  
  // List edit qualifier
  switch (relationship.get_listedit_qual()) {
    case ListEditQual::ResetToExplicit:
      j["listEditQual"] = "resetToExplicit";
      break;
    case ListEditQual::Append:
      j["listEditQual"] = "append";
      break;
    case ListEditQual::Add:
      j["listEditQual"] = "add";
      break;
    case ListEditQual::Delete:
      j["listEditQual"] = "delete";
      break;
    case ListEditQual::Prepend:
      j["listEditQual"] = "prepend";
      break;
    case ListEditQual::Order:
      j["listEditQual"] = "order";
      break;
    case ListEditQual::Invalid:
      j["listEditQual"] = "invalid";
      break;
  }
  
  // Relationship value type and targets
  switch (relationship.type) {
    case Relationship::Type::DefineOnly:
      j["valueType"] = "defineOnly";
      j["hasTargets"] = false;
      break;
      
    case Relationship::Type::Path:
      j["valueType"] = "path";
      j["hasTargets"] = true;
      j["target"] = relationship.targetPath.full_path_name();
      break;
      
    case Relationship::Type::PathVector:
      {
        j["valueType"] = "pathVector";
        j["hasTargets"] = true;
        json targets_array = json::array();
        for (const auto& path : relationship.targetPathVector) {
          targets_array.push_back(path.full_path_name());
        }
        j["targets"] = targets_array;
        j["targetCount"] = relationship.targetPathVector.size();
        break;
      }
      
    case Relationship::Type::ValueBlock:
      j["valueType"] = "valueBlock";
      j["hasTargets"] = false;
      j["blocked"] = true;
      break;
  }
  
  return j;
}

json ToJSON(const tinyusdz::Property& property, USDToJSONContext* context) {
  json j;
  
  // Property type
  switch (property.get_property_type()) {
    case Property::Type::EmptyAttrib:
      j["propertyType"] = "emptyAttribute";
      j["typeName"] = property.value_type_name();
      break;
      
    case Property::Type::Attrib:
      j["propertyType"] = "attribute";
      j["attribute"] = ToJSON(property.get_attribute(), context);
      break;
      
    case Property::Type::Relation:
      j["propertyType"] = "relationship";
      j["relationship"] = ToJSON(property.get_relationship());
      break;
      
    case Property::Type::NoTargetsRelation:
      j["propertyType"] = "noTargetsRelationship";
      j["relationship"] = ToJSON(property.get_relationship());
      break;
      
    case Property::Type::Connection:
      j["propertyType"] = "connection";
      j["attribute"] = ToJSON(property.get_attribute(), context);
      j["valueTypeName"] = property.value_type_name();
      break;
  }
  
  // Custom flag
  j["isCustom"] = property.has_custom();
  
  // List edit qualifier (mainly for relationships)
  switch (property.get_listedit_qual()) {
    case ListEditQual::ResetToExplicit:
      j["listEditQual"] = "resetToExplicit";
      break;
    case ListEditQual::Append:
      j["listEditQual"] = "append";
      break;
    case ListEditQual::Add:
      j["listEditQual"] = "add";
      break;
    case ListEditQual::Delete:
      j["listEditQual"] = "delete";
      break;
    case ListEditQual::Prepend:
      j["listEditQual"] = "prepend";
      break;
    case ListEditQual::Order:
      j["listEditQual"] = "order";
      break;
    case ListEditQual::Invalid:
      j["listEditQual"] = "invalid";
      break;
  }
  
  // Convenience methods for relationships
  if (property.is_relationship()) {
    auto target = property.get_relationTarget();
    if (target) {
      j["relationTarget"] = target->full_path_name();
    }
    
    auto targets = property.get_relationTargets();
    if (!targets.empty()) {
      json targets_array = json::array();
      for (const auto& t : targets) {
        targets_array.push_back(t.full_path_name());
      }
      j["relationTargets"] = targets_array;
    }
  }
  
  // Helper flags
  j["isAttribute"] = property.is_attribute();
  j["isRelationship"] = property.is_relationship();
  j["isEmpty"] = property.is_empty();
  j["isAttributeConnection"] = property.is_attribute_connection();
  
  return j;
}

json PropertiesToJSON(const std::map<std::string, tinyusdz::Property>& properties, USDToJSONContext* context) {
  json j = json::object();
  
  for (const auto& prop_pair : properties) {
    const std::string& prop_name = prop_pair.first;
    const Property& property = prop_pair.second;
    
    j[prop_name] = ToJSON(property, context);
  }
  
  return j;
}


}  // namespace tinyusdz
