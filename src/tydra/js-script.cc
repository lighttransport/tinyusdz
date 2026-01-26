#include "js-script.hh"
#include "prim-types.hh"
#include "layer.hh"
#include "tinyusdz.hh"

#if defined(TINYUSDZ_WITH_QJS)
// external

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
// -Wdisabled-macro-expansion is clang-specific, not GCC
#endif

#include "external/quickjs-ng/quickjs.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include "value-types.hh"
#include "str-util.hh"  // For dragonbox-based dtos()
#endif

namespace tinyusdz {
namespace tydra {



#if defined(TINYUSDZ_WITH_QJS)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
// -Wdisabled-macro-expansion is clang-specific, not GCC
#endif

static std::string PrimMetasToJSON(const PrimMetas* metas) {
  if (!metas) {
    return "{}";
  }

  std::ostringstream oss;
  oss << std::setprecision(17);
  oss << "{";

  bool first = true;

  // Helper to add comma if not first element
  auto addComma = [&first, &oss]() {
    if (!first) oss << ",";
    first = false;
  };

  // Helper to escape JSON strings
  auto escapeJson = [](const std::string& str) -> std::string {
    std::string escaped;
    for (char c : str) {
      switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
      }
    }
    return escaped;
  };

  // active
  if (metas->active.has_value()) {
    addComma();
    oss << "\"active\":" << (metas->active.value() ? "true" : "false");
  }

  // hidden
  if (metas->hidden.has_value()) {
    addComma();
    oss << "\"hidden\":" << (metas->hidden.value() ? "true" : "false");
  }

  // instanceable
  if (metas->instanceable.has_value()) {
    addComma();
    oss << "\"instanceable\":" << (metas->instanceable.value() ? "true" : "false");
  }

  // kind
  if (metas->kind.has_value() || !metas->_kind_str.empty()) {
    addComma();
    oss << "\"kind\":\"" << escapeJson(metas->get_kind()) << "\"";
  }

  // doc
  if (metas->doc.has_value()) {
    addComma();
    oss << "\"documentation\":\"" << escapeJson(metas->doc.value().value) << "\"";
  }

  // comment
  if (metas->comment.has_value()) {
    addComma();
    oss << "\"comment\":\"" << escapeJson(metas->comment.value().value) << "\"";
  }

  // sceneName
  if (metas->sceneName.has_value()) {
    addComma();
    oss << "\"sceneName\":\"" << escapeJson(metas->sceneName.value()) << "\"";
  }

  // displayName
  if (metas->displayName.has_value()) {
    addComma();
    oss << "\"displayName\":\"" << escapeJson(metas->displayName.value()) << "\"";
  }

  // apiSchemas - simplified for now, just store the count
  if (metas->apiSchemas.has_value()) {
    addComma();
    oss << "\"apiSchemas_count\":" << metas->apiSchemas.value().names.size();
  }

  // primChildren
  if (!metas->primChildren.empty()) {
    addComma();
    oss << "\"primChildren\":[";
    for (size_t i = 0; i < metas->primChildren.size(); i++) {
      if (i > 0) oss << ",";
      oss << "\"" << escapeJson(metas->primChildren[i].str()) << "\"";
    }
    oss << "]";
  }

  // properties
  if (!metas->properties.empty()) {
    addComma();
    oss << "\"properties\":[";
    for (size_t i = 0; i < metas->properties.size(); i++) {
      if (i > 0) oss << ",";
      oss << "\"" << escapeJson(metas->properties[i].str()) << "\"";
    }
    oss << "]";
  }

  oss << "}";
  return oss.str();
}

static bool PrimMetasFromJSON(const std::string& json, PrimMetas* metas, std::string* err) {
  if (!metas || json.empty()) {
    if (err) *err = "Invalid parameters";
    return false;
  }

  // Reset metas to empty state
  *metas = PrimMetas();

  // Simple JSON parsing - in production use a proper JSON parser

  // Check for active
  size_t pos = json.find("\"active\":");
  if (pos != std::string::npos) {
    pos += 9; // length of "active":
    if (json.substr(pos, 4) == "true") {
      metas->active = true;
    } else if (json.substr(pos, 5) == "false") {
      metas->active = false;
    }
  }

  // Check for hidden
  pos = json.find("\"hidden\":");
  if (pos != std::string::npos) {
    pos += 9; // length of "hidden":
    if (json.substr(pos, 4) == "true") {
      metas->hidden = true;
    } else if (json.substr(pos, 5) == "false") {
      metas->hidden = false;
    }
  }

  // Check for instanceable
  pos = json.find("\"instanceable\":");
  if (pos != std::string::npos) {
    pos += 15; // length of "instanceable":
    if (json.substr(pos, 4) == "true") {
      metas->instanceable = true;
    } else if (json.substr(pos, 5) == "false") {
      metas->instanceable = false;
    }
  }

  // Check for kind
  pos = json.find("\"kind\":");
  if (pos != std::string::npos) {
    pos += 7; // length of "kind":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      std::string kind_str = json.substr(pos, end - pos);
      // Check for standard kinds
      if (kind_str == "model") {
        metas->kind = Kind::Model;
      } else if (kind_str == "group") {
        metas->kind = Kind::Group;
      } else if (kind_str == "assembly") {
        metas->kind = Kind::Assembly;
      } else if (kind_str == "component") {
        metas->kind = Kind::Component;
      } else if (kind_str == "subcomponent") {
        metas->kind = Kind::Subcomponent;
      } else {
        // User-defined kind
        metas->_kind_str = kind_str;
      }
    }
  }

  // Check for documentation
  pos = json.find("\"documentation\":");
  if (pos != std::string::npos) {
    pos += 16; // length of "documentation":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      metas->doc = value::StringData();
      metas->doc->value = json.substr(pos, end - pos);
    }
  }

  // Check for comment
  pos = json.find("\"comment\":");
  if (pos != std::string::npos) {
    pos += 10; // length of "comment":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      metas->comment = value::StringData();
      metas->comment->value = json.substr(pos, end - pos);
    }
  }

  // Check for sceneName
  pos = json.find("\"sceneName\":");
  if (pos != std::string::npos) {
    pos += 12; // length of "sceneName":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      metas->sceneName = json.substr(pos, end - pos);
    }
  }

  // Check for displayName
  pos = json.find("\"displayName\":");
  if (pos != std::string::npos) {
    pos += 14; // length of "displayName":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      metas->displayName = json.substr(pos, end - pos);
    }
  }

  return true;
}

static std::string AttrMetasToJSON(const AttrMetas* metas) {
  if (!metas) {
    return "{}";
  }

  std::ostringstream oss;
  oss << std::setprecision(17);
  oss << "{";

  bool first = true;

  // Helper to add comma if not first element
  auto addComma = [&first, &oss]() {
    if (!first) oss << ",";
    first = false;
  };

  // interpolation
  if (metas->interpolation.has_value()) {
    addComma();
    oss << "\"interpolation\":\"";
    switch (metas->interpolation.value()) {
      case Interpolation::Constant: oss << "constant"; break;
      case Interpolation::Uniform: oss << "uniform"; break;
      case Interpolation::Varying: oss << "varying"; break;
      case Interpolation::Vertex: oss << "vertex"; break;
      case Interpolation::FaceVarying: oss << "faceVarying"; break;
      default: oss << "invalid"; break;
    }
    oss << "\"";
  }

  // elementSize
  if (metas->elementSize.has_value()) {
    addComma();
    oss << "\"elementSize\":" << metas->elementSize.value();
  }

  // hidden
  if (metas->hidden.has_value()) {
    addComma();
    oss << "\"hidden\":" << (metas->hidden.value() ? "true" : "false");
  }

  // weight
  if (metas->weight.has_value()) {
    addComma();
    oss << "\"weight\":" << metas->weight.value();
  }

  // comment
  if (metas->comment.has_value()) {
    addComma();
    auto escapeJson = [](const std::string& str) -> std::string {
      std::string escaped;
      for (char c : str) {
        switch (c) {
          case '"': escaped += "\\\""; break;
          case '\\': escaped += "\\\\"; break;
          case '\b': escaped += "\\b"; break;
          case '\f': escaped += "\\f"; break;
          case '\n': escaped += "\\n"; break;
          case '\r': escaped += "\\r"; break;
          case '\t': escaped += "\\t"; break;
          default: escaped += c; break;
        }
      }
      return escaped;
    };
    oss << "\"comment\":\"" << escapeJson(metas->comment.value().value) << "\"";
  }

  // connectability
  if (metas->connectability.has_value()) {
    addComma();
    oss << "\"connectability\":\"" << metas->connectability.value().str() << "\"";
  }

  // outputName
  if (metas->outputName.has_value()) {
    addComma();
    oss << "\"outputName\":\"" << metas->outputName.value().str() << "\"";
  }

  // renderType
  if (metas->renderType.has_value()) {
    addComma();
    oss << "\"renderType\":\"" << metas->renderType.value().str() << "\"";
  }

  // displayName
  if (metas->displayName.has_value()) {
    addComma();
    oss << "\"displayName\":\"" << metas->displayName.value() << "\"";
  }

  // displayGroup
  if (metas->displayGroup.has_value()) {
    addComma();
    oss << "\"displayGroup\":\"" << metas->displayGroup.value() << "\"";
  }

  // bindMaterialAs
  if (metas->bindMaterialAs.has_value()) {
    addComma();
    oss << "\"bindMaterialAs\":\"" << metas->bindMaterialAs.value().str() << "\"";
  }

  oss << "}";
  return oss.str();
}

static bool AttrMetasFromJSON(const std::string& json, AttrMetas* metas, std::string* err) {
  if (!metas || json.empty()) {
    if (err) *err = "Invalid parameters";
    return false;
  }

  // Parse JSON using QuickJS's JSON parser would be complex,
  // so we'll use a simple approach for now
  // This is a simplified parser - in production you'd want a proper JSON parser

  // Reset metas to empty state
  *metas = AttrMetas();

  // For now, we'll parse some common fields manually
  // This is a simplified implementation

  // Check for interpolation
  size_t pos = json.find("\"interpolation\":");
  if (pos != std::string::npos) {
    pos += 16; // length of "interpolation":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      std::string value = json.substr(pos, end - pos);
      if (value == "constant") metas->interpolation = Interpolation::Constant;
      else if (value == "uniform") metas->interpolation = Interpolation::Uniform;
      else if (value == "varying") metas->interpolation = Interpolation::Varying;
      else if (value == "vertex") metas->interpolation = Interpolation::Vertex;
      else if (value == "faceVarying") metas->interpolation = Interpolation::FaceVarying;
    }
  }

  // Check for elementSize
  pos = json.find("\"elementSize\":");
  if (pos != std::string::npos) {
    pos += 14; // length of "elementSize":
    size_t end = json.find_first_of(",}", pos);
    if (end != std::string::npos) {
      std::string value = json.substr(pos, end - pos);
      // Use safe conversion without exceptions
      char* endptr;
      unsigned long val = std::strtoul(value.c_str(), &endptr, 10);
      if (endptr != value.c_str() && *endptr == '\0') {
        metas->elementSize = static_cast<uint32_t>(val);
      }
    }
  }

  // Check for hidden
  pos = json.find("\"hidden\":");
  if (pos != std::string::npos) {
    pos += 9; // length of "hidden":
    if (json.substr(pos, 4) == "true") {
      metas->hidden = true;
    } else if (json.substr(pos, 5) == "false") {
      metas->hidden = false;
    }
  }

  // Check for weight
  pos = json.find("\"weight\":");
  if (pos != std::string::npos) {
    pos += 9; // length of "weight":
    size_t end = json.find_first_of(",}", pos);
    if (end != std::string::npos) {
      std::string value = json.substr(pos, end - pos);
      // Use safe conversion without exceptions
      char* endptr;
      double val = std::strtod(value.c_str(), &endptr);
      if (endptr != value.c_str() && *endptr == '\0') {
        metas->weight = val;
      }
    }
  }

  // Check for displayName
  pos = json.find("\"displayName\":");
  if (pos != std::string::npos) {
    pos += 14; // length of "displayName":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      metas->displayName = json.substr(pos, end - pos);
    }
  }

  // Check for displayGroup
  pos = json.find("\"displayGroup\":");
  if (pos != std::string::npos) {
    pos += 15; // length of "displayGroup":
    while (pos < json.length() && json[pos] != '"') pos++;
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end != std::string::npos) {
      metas->displayGroup = json.substr(pos, end - pos);
    }
  }

  return true;
}

static std::string LayerMetasToJSON(const LayerMetas* metas) {
  if (!metas) {
    return "null";
  }
  
  std::ostringstream oss;
  oss << std::setprecision(17);
  oss << "{";
  
  // upAxis
  oss << "\"upAxis\":\"";
  switch (metas->upAxis.get_value()) {
    case Axis::X: oss << "X"; break;
    case Axis::Y: oss << "Y"; break;
    case Axis::Z: oss << "Z"; break;
    case Axis::Invalid: oss << "Invalid"; break;
  }
  oss << "\",";
  
  // defaultPrim
  oss << "\"defaultPrim\":\"" << metas->defaultPrim.str() << "\",";
  
  // numeric values
  oss << "\"metersPerUnit\":" << metas->metersPerUnit.get_value() << ",";
  oss << "\"timeCodesPerSecond\":" << metas->timeCodesPerSecond.get_value() << ",";
  oss << "\"framesPerSecond\":" << metas->framesPerSecond.get_value() << ",";
  oss << "\"startTimeCode\":" << metas->startTimeCode.get_value() << ",";
  
  // Handle potentially infinite endTimeCode
  double endTime = metas->endTimeCode.get_value();
  if (std::isinf(endTime)) {
    oss << "\"endTimeCode\":null,";
  } else {
    oss << "\"endTimeCode\":" << endTime << ",";
  }
  
  oss << "\"kilogramsPerUnit\":" << metas->kilogramsPerUnit.get_value() << ",";
  
  // comment and doc - escape JSON special characters
  auto escapeJson = [](const std::string& str) -> std::string {
    std::string escaped;
    for (char c : str) {
      switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
      }
    }
    return escaped;
  };
  
  oss << "\"comment\":\"" << escapeJson(metas->comment.value) << "\",";
  oss << "\"doc\":\"" << escapeJson(metas->doc.value) << "\",";
  
  // USDZ extension
  oss << "\"autoPlay\":" << (metas->autoPlay.get_value() ? "true" : "false") << ",";
  oss << "\"playbackMode\":\"";
  switch (metas->playbackMode.get_value()) {
    case LayerMetas::PlaybackMode::PlaybackModeNone: oss << "PlaybackModeNone"; break;
    case LayerMetas::PlaybackMode::PlaybackModeLoop: oss << "PlaybackModeLoop"; break;
    //default: oss << "Unknown"; break;
  }
  oss << "\",";
  
  // subLayers array
  oss << "\"subLayers\":[";
  for (size_t i = 0; i < metas->subLayers.size(); i++) {
    if (i > 0) oss << ",";
    oss << "{";
    oss << "\"assetPath\":\"" << metas->subLayers[i].assetPath.GetAssetPath() << "\",";
    oss << "\"layerOffset\":{";
    oss << "\"offset\":" << metas->subLayers[i].layerOffset._offset << ",";
    oss << "\"scale\":" << metas->subLayers[i].layerOffset._scale;
    oss << "}";
    oss << "}";
  }
  oss << "],";
  
  // primChildren array
  oss << "\"primChildren\":[";
  for (size_t i = 0; i < metas->primChildren.size(); i++) {
    if (i > 0) oss << ",";
    oss << "\"" << metas->primChildren[i].str() << "\"";
  }
  oss << "]";
  
  oss << "}";
  return oss.str();
}

static const LayerMetas* g_current_layer_metas = nullptr;
static const Attribute* g_current_attribute = nullptr;
static const class Layer* g_current_layer = nullptr;

// JavaScript fp16 library and TUSDZFloat16Array implementation
static const char* fp16_library_js = R"(
// Simple IEEE 754 half-precision (fp16) conversion utilities
(function() {
  'use strict';
  
  // Convert float32 to half-precision uint16
  function float32ToHalf(f32) {
    var floatView = new Float32Array(1);
    var int32View = new Int32Array(floatView.buffer);
    floatView[0] = f32;
    var f = int32View[0];
    
    var sign = (f >> 31) & 0x1;
    var exp = (f >> 23) & 0xFF;
    var frac = f & 0x7FFFFF;
    
    var newExp, newFrac;
    if (exp === 0) {
      // Zero or denormal
      newExp = 0;
      newFrac = 0;
    } else if (exp === 0xFF) {
      // Infinity or NaN
      newExp = 0x1F;
      newFrac = frac ? 0x3FF : 0;
    } else {
      // Normal number
      newExp = exp - 127 + 15; // Convert from float32 bias to half bias
      if (newExp >= 0x1F) {
        // Overflow to infinity
        newExp = 0x1F;
        newFrac = 0;
      } else if (newExp <= 0) {
        // Underflow to zero
        newExp = 0;
        newFrac = 0;
      } else {
        newFrac = frac >> 13; // Keep top 10 bits of fraction
      }
    }
    
    return (sign << 15) | (newExp << 10) | newFrac;
  }
  
  // Convert half-precision uint16 to float32
  function halfToFloat32(h) {
    var sign = (h >> 15) & 0x1;
    var exp = (h >> 10) & 0x1F;
    var frac = h & 0x3FF;
    
    var newExp, newFrac;
    if (exp === 0) {
      if (frac === 0) {
        // Zero
        newExp = 0;
        newFrac = 0;
      } else {
        // Denormal - convert to normal
        newExp = 127 - 15 + 1; // float32 bias - half bias + 1
        newFrac = frac << 13;
        // Normalize
        while ((newFrac & 0x800000) === 0) {
          newFrac <<= 1;
          newExp--;
        }
        newFrac &= 0x7FFFFF;
      }
    } else if (exp === 0x1F) {
      // Infinity or NaN
      newExp = 0xFF;
      newFrac = frac << 13;
    } else {
      // Normal
      newExp = exp - 15 + 127; // Convert from half bias to float32 bias
      newFrac = frac << 13;
    }
    
    var floatView = new Float32Array(1);
    var int32View = new Int32Array(floatView.buffer);
    int32View[0] = (sign << 31) | (newExp << 23) | newFrac;
    return floatView[0];
  }
  
  // TUSDZFloat16Array class - similar interface to typed arrays
  function TUSDZFloat16Array(arg) {
    if (typeof arg === 'number') {
      // Create array of given length
      this.length = arg;
      this._buffer = new Uint16Array(arg);
    } else if (arg instanceof Array || arg instanceof Uint16Array) {
      // Create from array
      this.length = arg.length;
      this._buffer = new Uint16Array(arg.length);
      for (var i = 0; i < arg.length; i++) {
        if (arg instanceof Uint16Array) {
          this._buffer[i] = arg[i];
        } else {
          this._buffer[i] = float32ToHalf(arg[i]);
        }
      }
    } else {
      throw new Error('Invalid argument to TUSDZFloat16Array constructor');
    }
  }
  
  TUSDZFloat16Array.prototype.get = function(index) {
    if (index < 0 || index >= this.length) {
      return undefined;
    }
    return halfToFloat32(this._buffer[index]);
  };
  
  TUSDZFloat16Array.prototype.set = function(index, value) {
    if (index >= 0 && index < this.length) {
      this._buffer[index] = float32ToHalf(value);
    }
  };
  
  TUSDZFloat16Array.prototype.getUint16 = function(index) {
    if (index < 0 || index >= this.length) {
      return undefined;
    }
    return this._buffer[index];
  };
  
  TUSDZFloat16Array.prototype.setUint16 = function(index, value) {
    if (index >= 0 && index < this.length) {
      this._buffer[index] = value & 0xFFFF;
    }
  };
  
  TUSDZFloat16Array.prototype.toArray = function() {
    var result = new Array(this.length);
    for (var i = 0; i < this.length; i++) {
      result[i] = this.get(i);
    }
    return result;
  };
  
  TUSDZFloat16Array.prototype.toUint16Array = function() {
    return new Uint16Array(this._buffer);
  };
  
  // Static methods
  TUSDZFloat16Array.fromFloat32Array = function(arr) {
    return new TUSDZFloat16Array(arr);
  };
  
  TUSDZFloat16Array.fromUint16Array = function(arr) {
    var result = new TUSDZFloat16Array(arr.length);
    result._buffer = new Uint16Array(arr);
    return result;
  };
  
  // Export to global scope
  globalThis.TUSDZFloat16Array = TUSDZFloat16Array;
  globalThis.float32ToHalf = float32ToHalf;
  globalThis.halfToFloat32 = halfToFloat32;
})();
)";

static std::string AttributeToJSON(const Attribute* attr) {
  if (!attr) {
    return "null";
  }
  
  std::ostringstream oss;
  oss << std::setprecision(17);
  oss << "{";
  
  // Basic attribute info
  oss << "\"name\":\"" << attr->name() << "\",";
  oss << "\"type_name\":\"" << attr->type_name() << "\",";
  oss << "\"type_id\":" << attr->type_id() << ",";
  oss << "\"is_blocked\":" << (attr->is_blocked() ? "true" : "false") << ",";
  oss << "\"has_value\":" << (attr->has_value() ? "true" : "false") << ",";
  oss << "\"is_connection\":" << (attr->is_connection() ? "true" : "false") << ",";
  oss << "\"is_timesamples\":" << (attr->has_timesamples() ? "true" : "false") << ",";
  
  // Add variability info
  std::string variability_str = "Unknown";
  switch (attr->variability()) {
    case Variability::Varying: variability_str = "Varying"; break;
    case Variability::Uniform: variability_str = "Uniform"; break;
    case Variability::Config: variability_str = "Config"; break;
    case Variability::Invalid: variability_str = "Invalid"; break;
  }
  oss << "\"variability\":\"" << variability_str << "\",";
  
  // Handle value based on type
  oss << "\"value\":";
  
  if (attr->is_blocked() || !attr->has_value()) {
    oss << "null";
  } else {
    uint32_t tid = attr->type_id();
    bool handled = false;
    
    // Handle scalar types
    if (tid == value::TypeTraits<float>::type_id()) {
      auto v = attr->get_value<float>();
      oss << (v ? std::to_string(v.value()) : "null");
      handled = true;
    } else if (tid == value::TypeTraits<double>::type_id()) {
      auto v = attr->get_value<double>();
      oss << (v ? std::to_string(v.value()) : "null");
      handled = true;
    } else if (tid == value::TypeTraits<int>::type_id()) {
      auto v = attr->get_value<int>();
      oss << (v ? std::to_string(v.value()) : "null");
      handled = true;
    } else if (tid == value::TypeTraits<bool>::type_id()) {
      auto v = attr->get_value<bool>();
      oss << (v ? (v.value() ? "true" : "false") : "null");
      handled = true;
    }
    // Handle int compound types
    else if (tid == value::TypeTraits<value::int2>::type_id()) {
      auto v = attr->get_value<value::int2>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::int3>::type_id()) {
      auto v = attr->get_value<value::int3>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "," << v.value()[2] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::int4>::type_id()) {
      auto v = attr->get_value<value::int4>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "," << v.value()[2] << "," << v.value()[3] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Handle compound types with flattened arrays
    else if (tid == value::TypeTraits<value::float3>::type_id()) {
      auto v = attr->get_value<value::float3>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "," << v.value()[2] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::float2>::type_id()) {
      auto v = attr->get_value<value::float2>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::float4>::type_id()) {
      auto v = attr->get_value<value::float4>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "," << v.value()[2] << "," << v.value()[3] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::double3>::type_id()) {
      auto v = attr->get_value<value::double3>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "," << v.value()[2] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::double2>::type_id()) {
      auto v = attr->get_value<value::double2>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::double4>::type_id()) {
      auto v = attr->get_value<value::double4>();
      if (v) {
        oss << "[" << v.value()[0] << "," << v.value()[1] << "," << v.value()[2] << "," << v.value()[3] << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } 
    // Handle matrix types (flattened)
    else if (tid == value::TypeTraits<value::matrix4f>::type_id()) {
      auto v = attr->get_value<value::matrix4f>();
      if (v) {
        oss << "[";
        for (int i = 0; i < 4; i++) {
          for (int j = 0; j < 4; j++) {
            if (i > 0 || j > 0) oss << ",";
            oss << v.value().m[i][j];
          }
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::matrix3f>::type_id()) {
      auto v = attr->get_value<value::matrix3f>();
      if (v) {
        oss << "[";
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 3; j++) {
            if (i > 0 || j > 0) oss << ",";
            oss << v.value().m[i][j];
          }
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::matrix3d>::type_id()) {
      auto v = attr->get_value<value::matrix3d>();
      if (v) {
        oss << "[";
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 3; j++) {
            if (i > 0 || j > 0) oss << ",";
            oss << dtos(v.value().m[i][j]);
          }
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<value::matrix4d>::type_id()) {
      auto v = attr->get_value<value::matrix4d>();
      if (v) {
        oss << "[";
        for (int i = 0; i < 4; i++) {
          for (int j = 0; j < 4; j++) {
            if (i > 0 || j > 0) oss << ",";
            oss << dtos(v.value().m[i][j]);
          }
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Handle individual half compound types
    else if (tid == value::TypeTraits<value::half2>::type_id()) {
      auto v = attr->get_value<value::half2>();
      if (v) {
        oss << "[" << dtos(value::half_to_float(v.value()[0])) << "," << dtos(value::half_to_float(v.value()[1])) << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    else if (tid == value::TypeTraits<value::half3>::type_id()) {
      auto v = attr->get_value<value::half3>();
      if (v) {
        oss << "[" << dtos(value::half_to_float(v.value()[0])) << "," << dtos(value::half_to_float(v.value()[1])) << "," << dtos(value::half_to_float(v.value()[2])) << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    else if (tid == value::TypeTraits<value::half4>::type_id()) {
      auto v = attr->get_value<value::half4>();
      if (v) {
        oss << "[" << dtos(value::half_to_float(v.value()[0])) << "," << dtos(value::half_to_float(v.value()[1])) << ","
            << dtos(value::half_to_float(v.value()[2])) << "," << dtos(value::half_to_float(v.value()[3])) << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Handle string/token types
    else if (tid == value::TypeTraits<value::token>::type_id()) {
      auto v = attr->get_value<value::token>();
      oss << "\"" << (v ? v.value().str() : "") << "\"";
      handled = true;
    } else if (tid == value::TypeTraits<std::string>::type_id()) {
      auto v = attr->get_value<std::string>();
      oss << "\"" << (v ? v.value() : "") << "\"";
      handled = true;
    }
    // Handle array types
    else if (tid == value::TypeTraits<std::vector<float>>::type_id()) {
      auto v = attr->get_value<std::vector<float>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          oss << dtos(v.value()[i]);
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<int>>::type_id()) {
      auto v = attr->get_value<std::vector<int>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          oss << v.value()[i];
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::float3>>::type_id()) {
      auto v = attr->get_value<std::vector<value::float3>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each float3 into the main array
          oss << dtos(v.value()[i][0]) << "," << dtos(v.value()[i][1]) << "," << dtos(v.value()[i][2]);
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Handle double array types
    else if (tid == value::TypeTraits<std::vector<double>>::type_id()) {
      auto v = attr->get_value<std::vector<double>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          oss << dtos(v.value()[i]);
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::double2>>::type_id()) {
      auto v = attr->get_value<std::vector<value::double2>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each double2 into the main array
          oss << dtos(v.value()[i][0]) << "," << dtos(v.value()[i][1]);
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::double3>>::type_id()) {
      auto v = attr->get_value<std::vector<value::double3>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each double3 into the main array
          oss << dtos(v.value()[i][0]) << "," << dtos(v.value()[i][1]) << "," << dtos(v.value()[i][2]);
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::double4>>::type_id()) {
      auto v = attr->get_value<std::vector<value::double4>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each double4 into the main array
          oss << dtos(v.value()[i][0]) << "," << dtos(v.value()[i][1]) << "," << dtos(v.value()[i][2]) << "," << dtos(v.value()[i][3]);
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Handle int array types  
    else if (tid == value::TypeTraits<std::vector<value::int2>>::type_id()) {
      auto v = attr->get_value<std::vector<value::int2>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each int2 into the main array
          oss << v.value()[i][0] << "," << v.value()[i][1];
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::int3>>::type_id()) {
      auto v = attr->get_value<std::vector<value::int3>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each int3 into the main array
          oss << v.value()[i][0] << "," << v.value()[i][1] << "," << v.value()[i][2];
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::int4>>::type_id()) {
      auto v = attr->get_value<std::vector<value::int4>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each int4 into the main array
          oss << v.value()[i][0] << "," << v.value()[i][1] << "," << v.value()[i][2] << "," << v.value()[i][3];
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Handle matrix array types
    else if (tid == value::TypeTraits<std::vector<value::matrix3d>>::type_id()) {
      auto v = attr->get_value<std::vector<value::matrix3d>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each matrix3d into the main array (9 elements per matrix)
          for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
              if (row > 0 || col > 0) oss << ",";
              oss << dtos(v.value()[i].m[row][col]);
            }
          }
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    } else if (tid == value::TypeTraits<std::vector<value::matrix4d>>::type_id()) {
      auto v = attr->get_value<std::vector<value::matrix4d>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); i++) {
          if (i > 0) oss << ",";
          // Flatten each matrix4d into the main array (16 elements per matrix)
          for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
              if (row > 0 || col > 0) oss << ",";
              oss << dtos(v.value()[i].m[row][col]);
            }
          }
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    // Add half array types with flattened output
    else if (tid == value::TypeTraits<std::vector<value::half>>::type_id()) {
      auto v = attr->get_value<std::vector<value::half>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); ++i) {
          if (i > 0) oss << ",";
          oss << dtos(value::half_to_float(v.value()[i]));
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    else if (tid == value::TypeTraits<std::vector<value::half2>>::type_id()) {
      auto v = attr->get_value<std::vector<value::half2>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); ++i) {
          if (i > 0) oss << ",";
          oss << dtos(value::half_to_float(v.value()[i][0])) << "," << dtos(value::half_to_float(v.value()[i][1]));
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    else if (tid == value::TypeTraits<std::vector<value::half3>>::type_id()) {
      auto v = attr->get_value<std::vector<value::half3>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); ++i) {
          if (i > 0) oss << ",";
          oss << dtos(value::half_to_float(v.value()[i][0])) << "," << dtos(value::half_to_float(v.value()[i][1])) << ","
              << dtos(value::half_to_float(v.value()[i][2]));
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    else if (tid == value::TypeTraits<std::vector<value::half4>>::type_id()) {
      auto v = attr->get_value<std::vector<value::half4>>();
      if (v) {
        oss << "[";
        for (size_t i = 0; i < v.value().size(); ++i) {
          if (i > 0) oss << ",";
          oss << dtos(value::half_to_float(v.value()[i][0])) << "," << dtos(value::half_to_float(v.value()[i][1])) << ","
              << dtos(value::half_to_float(v.value()[i][2])) << "," << dtos(value::half_to_float(v.value()[i][3]));
        }
        oss << "]";
      } else {
        oss << "null";
      }
      handled = true;
    }
    
    if (!handled) {
      oss << "\"unsupported_type_" << attr->type_name() << "\"";
    }
  }
  
  // Add typed array info for JavaScript typed arrays
  oss << ",\"jsTypedArray\":";
  if (!attr->has_value() || attr->is_blocked()) {
    oss << "null";
  } else {
    uint32_t tid = attr->type_id();
    if (tid == value::TypeTraits<value::float3>::type_id() || 
        tid == value::TypeTraits<value::float2>::type_id() ||
        tid == value::TypeTraits<value::float4>::type_id() ||
        tid == value::TypeTraits<value::matrix4f>::type_id() ||
        tid == value::TypeTraits<value::matrix3f>::type_id() ||
        tid == value::TypeTraits<std::vector<float>>::type_id() ||
        tid == value::TypeTraits<std::vector<value::float3>>::type_id()) {
      oss << "\"Float32Array\"";
    } else if (tid == value::TypeTraits<value::double3>::type_id() || 
               tid == value::TypeTraits<value::double2>::type_id() ||
               tid == value::TypeTraits<value::double4>::type_id() ||
               tid == value::TypeTraits<value::matrix3d>::type_id() ||
               tid == value::TypeTraits<value::matrix4d>::type_id() ||
               tid == value::TypeTraits<std::vector<double>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::double2>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::double3>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::double4>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::matrix3d>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::matrix4d>>::type_id()) {
      oss << "\"Float64Array\"";
    } else if (tid == value::TypeTraits<std::vector<int>>::type_id() ||
               tid == value::TypeTraits<value::int2>::type_id() ||
               tid == value::TypeTraits<value::int3>::type_id() ||
               tid == value::TypeTraits<value::int4>::type_id() ||
               tid == value::TypeTraits<std::vector<value::int2>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::int3>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::int4>>::type_id()) {
      oss << "\"Int32Array\"";
    } else if (tid == value::TypeTraits<value::half2>::type_id() ||
               tid == value::TypeTraits<value::half3>::type_id() ||
               tid == value::TypeTraits<value::half4>::type_id() ||
               tid == value::TypeTraits<std::vector<value::half>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::half2>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::half3>>::type_id() ||
               tid == value::TypeTraits<std::vector<value::half4>>::type_id()) {
      oss << "\"TUSDZFloat16Array\"";
    } else {
      oss << "null";
    }
  }
  
  oss << "}";
  return oss.str();
}

static std::string PrimMetaToJSON_OLD(const PrimMeta* metas) {
  if (!metas) {
    return "null";
  }
  
  std::ostringstream oss;
  oss << std::setprecision(17);
  oss << "{";
  
  // Basic metadata flags
  oss << "\"active\":";
  if (metas->active.has_value()) {
    oss << (metas->active.value() ? "true" : "false");
  } else {
    oss << "null";
  }
  oss << ",";
  
  oss << "\"hidden\":";
  if (metas->hidden.has_value()) {
    oss << (metas->hidden.value() ? "true" : "false");
  } else {
    oss << "null";
  }
  oss << ",";
  
  oss << "\"instanceable\":";
  if (metas->instanceable.has_value()) {
    oss << (metas->instanceable.value() ? "true" : "false");
  } else {
    oss << "null";
  }
  oss << ",";
  
  // Kind
  oss << "\"kind\":\"" << metas->get_kind() << "\",";
  
  // Documentation and comment
  oss << "\"documentation\":";
  if (metas->doc.has_value()) {
    oss << "\"" << metas->doc.value().value << "\"";
  } else {
    oss << "null";
  }
  oss << ",";
  
  oss << "\"comment\":";
  if (metas->comment.has_value()) {
    oss << "\"" << metas->comment.value().value << "\"";
  } else {
    oss << "null";
  }
  oss << ",";
  
  // Display name and scene name (extensions)
  oss << "\"displayName\":";
  if (metas->displayName.has_value()) {
    oss << "\"" << metas->displayName.value() << "\"";
  } else {
    oss << "null";
  }
  oss << ",";
  
  oss << "\"sceneName\":";
  if (metas->sceneName.has_value()) {
    oss << "\"" << metas->sceneName.value() << "\"";
  } else {
    oss << "null";
  }
  oss << ",";
  
  // References count
  oss << "\"hasReferences\":";
  if (metas->references.has_value() && !metas->references.value().second.empty()) {
    oss << "true,";
    oss << "\"referencesCount\":" << metas->references.value().second.size();
  } else {
    oss << "false,";
    oss << "\"referencesCount\":0";
  }
  oss << ",";
  
  // Payload count
  oss << "\"hasPayload\":";
  if (metas->payload.has_value() && !metas->payload.value().second.empty()) {
    oss << "true,";
    oss << "\"payloadCount\":" << metas->payload.value().second.size();
  } else {
    oss << "false,";
    oss << "\"payloadCount\":0";
  }
  oss << ",";
  
  // Inherits count
  oss << "\"hasInherits\":";
  if (metas->inherits.has_value() && !metas->inherits.value().second.empty()) {
    oss << "true,";
    oss << "\"inheritsCount\":" << metas->inherits.value().second.size();
  } else {
    oss << "false,";
    oss << "\"inheritsCount\":0";
  }
  oss << ",";
  
  // Variants info
  oss << "\"hasVariants\":";
  if (metas->variants.has_value() && !metas->variants.value().empty()) {
    oss << "true,";
    oss << "\"variantsCount\":" << metas->variants.value().size() << ",";
    oss << "\"variantNames\":[";
    bool first = true;
    for (const auto& variant : metas->variants.value()) {
      if (!first) oss << ",";
      oss << "\"" << variant.first << "\"";
      first = false;
    }
    oss << "]";
  } else {
    oss << "false,";
    oss << "\"variantsCount\":0,";
    oss << "\"variantNames\":[]";
  }
  oss << ",";
  
  // VariantSets info
  oss << "\"hasVariantSets\":";
  if (metas->variantSets.has_value() && !metas->variantSets.value().second.empty()) {
    oss << "true,";
    oss << "\"variantSetsCount\":" << metas->variantSets.value().second.size() << ",";
    oss << "\"variantSetNames\":[";
    bool first = true;
    for (const auto& varSet : metas->variantSets.value().second) {
      if (!first) oss << ",";
      oss << "\"" << varSet << "\"";
      first = false;
    }
    oss << "]";
  } else {
    oss << "false,";
    oss << "\"variantSetsCount\":0,";
    oss << "\"variantSetNames\":[]";
  }
  oss << ",";
  
  // Custom data and unregistered metas
  oss << "\"hasCustomData\":" << (metas->customData.has_value() ? "true" : "false") << ",";
  oss << "\"hasAssetInfo\":" << (metas->assetInfo.has_value() ? "true" : "false") << ",";
  oss << "\"unregisteredMetasCount\":" << metas->unregisteredMetas.size() << ",";
  
  // Unregistered metadata names
  oss << "\"unregisteredMetaNames\":[";
  bool first = true;
  for (const auto& meta : metas->unregisteredMetas) {
    if (!first) oss << ",";
    oss << "\"" << meta.first << "\"";
    first = false;
  }
  oss << "],";
  
  // Authored flag
  oss << "\"authored\":" << (metas->authored() ? "true" : "false");
  
  oss << "}";
  return oss.str();
}

static std::string PrimSpecToJSON(const PrimSpec* ps) {
  if (!ps) {
    return "null";
  }
  
  std::ostringstream oss;
  oss << std::setprecision(17);
  oss << "{";
  
  // Basic PrimSpec info
  oss << "\"name\":\"" << ps->name() << "\",";
  oss << "\"typeName\":\"" << ps->typeName() << "\",";
  oss << "\"specifier\":\"";
  switch (ps->specifier()) {
    case Specifier::Def: oss << "def"; break;
    case Specifier::Over: oss << "over"; break;
    case Specifier::Class: oss << "class"; break;
    case Specifier::Invalid: oss << "invalid"; break;
  }
  oss << "\",";
  
  // Add property count
  oss << "\"propertyCount\":" << ps->props().size() << ",";
  
  // Add children count
  oss << "\"childrenCount\":" << ps->children().size() << ",";
  
  // Property names array
  oss << "\"propertyNames\":[";
  bool first = true;
  for (const auto& prop : ps->props()) {
    if (!first) oss << ",";
    oss << "\"" << prop.first << "\"";
    first = false;
  }
  oss << "],";
  
  // Children names array
  oss << "\"childrenNames\":[";
  first = true;
  for (const auto& child : ps->children()) {
    if (!first) oss << ",";
    oss << "\"" << child.name() << "\"";
    first = false;
  }
  oss << "]";
  
  oss << "}";
  return oss.str();
}

static JSValue js_getLayerMetas(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  std::string json = LayerMetasToJSON(g_current_layer_metas);
  
  JSValue result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<layermetas>");
  if (JS_IsException(result)) {
    return JS_EXCEPTION;
  }
  return result;
}

static JSValue js_getAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  std::string json = AttributeToJSON(g_current_attribute);
  
  JSValue result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<attribute>");
  if (JS_IsException(result)) {
    return JS_EXCEPTION;
  }
  return result;
}

static JSValue js_findPrimSpecByPath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "findPrimSpecByPath requires 1 argument (path string)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_EXCEPTION;
  }

  // Parse the path string into a Path object
  std::string prim_path = std::string(path_str);
  tinyusdz::Path path(prim_path, "");
  if (!path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(path, &ps, &err);
  
  JS_FreeCString(ctx, path_str);
  
  if (!found || !ps) {
    return JS_NULL;
  }

  // Convert PrimSpec to JSON and parse it
  std::string json = PrimSpecToJSON(ps);
  JSValue result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<primspec>");
  if (JS_IsException(result)) {
    return JS_EXCEPTION;
  }
  return result;
}

static JSValue js_getPrimSpecMetadata(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "getPrimSpecMetadata requires 1 argument (path string)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_EXCEPTION;
  }

  // Parse the path string into a Path object
  tinyusdz::Path path(path_str, "");
  if (!path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(path, &ps, &err);
  
  JS_FreeCString(ctx, path_str);
  
  if (!found || !ps) {
    return JS_NULL;
  }

  // Convert PrimSpec metadata to JSON and parse it
  std::string json = PrimMetasToJSON(&ps->metas());
  JSValue result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<primspec_metadata>");
  if (JS_IsException(result)) {
    return JS_EXCEPTION;
  }
  return result;
}

bool RunJSScript(const std::string &js_code, std::string &err) {
  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    err = "Failed to create JavaScript runtime";
    return false;
  }

  JSContext *ctx = JS_NewContext(rt);
  if (!ctx) {
    err = "Failed to create JavaScript context";
    JS_FreeRuntime(rt);
    return false;
  }

  JSValue result = JS_Eval(ctx, js_code.c_str(), js_code.length(),
                          "<eval>", JS_EVAL_TYPE_GLOBAL);

  bool success = true;
  if (JS_IsException(result)) {
    success = false;
    
    JSValue exception = JS_GetException(ctx);
    const char *error_str = JS_ToCString(ctx, exception);
    if (error_str) {
      err = std::string("JavaScript error: ") + error_str;
      JS_FreeCString(ctx, error_str);
    } else {
      err = "JavaScript error: unable to get error message";
    }
    JS_FreeValue(ctx, exception);
  }

  JS_FreeValue(ctx, result);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  return success;
}

bool RunJSScriptWithLayerMetas(const std::string &js_code, const LayerMetas* layer_metas, std::string &err) {
  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    err = "Failed to create JavaScript runtime";
    return false;
  }

  JSContext *ctx = JS_NewContext(rt);
  if (!ctx) {
    err = "Failed to create JavaScript context";
    JS_FreeRuntime(rt);
    return false;
  }

  // Set the global LayerMetas pointer and add the function to the global context
  g_current_layer_metas = layer_metas;
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue func = JS_NewCFunctionData(ctx, js_getLayerMetas, 0, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getLayerMetas", func);
  JS_FreeValue(ctx, global_obj);

  JSValue result = JS_Eval(ctx, js_code.c_str(), js_code.length(),
                          "<eval>", JS_EVAL_TYPE_GLOBAL);

  bool success = true;
  if (JS_IsException(result)) {
    success = false;
    
    JSValue exception = JS_GetException(ctx);
    const char *error_str = JS_ToCString(ctx, exception);
    if (error_str) {
      err = std::string("JavaScript error: ") + error_str;
      JS_FreeCString(ctx, error_str);
    } else {
      err = "JavaScript error: unable to get error message";
    }
    JS_FreeValue(ctx, exception);
  }

  JS_FreeValue(ctx, result);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  return success;
}

bool RunJSScriptWithAttribute(const std::string &js_code, const Attribute* attribute, std::string &err) {
  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    err = "Failed to create JavaScript runtime";
    return false;
  }

  JSContext *ctx = JS_NewContext(rt);
  if (!ctx) {
    err = "Failed to create JavaScript context";
    JS_FreeRuntime(rt);
    return false;
  }

  // Load the fp16 library first
  JSValue fp16_result = JS_Eval(ctx, fp16_library_js, strlen(fp16_library_js), "<fp16_library>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(fp16_result)) {
    err = "Failed to load fp16 library";
    JS_FreeValue(ctx, fp16_result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return false;
  }
  JS_FreeValue(ctx, fp16_result);
  
  // Set the global Attribute pointer and add the function to the global context
  g_current_attribute = attribute;
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JSValue func = JS_NewCFunctionData(ctx, js_getAttribute, 0, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getAttribute", func);
  JS_FreeValue(ctx, global_obj);

  JSValue result = JS_Eval(ctx, js_code.c_str(), js_code.length(),
                          "<eval>", JS_EVAL_TYPE_GLOBAL);

  bool success = true;
  if (JS_IsException(result)) {
    success = false;
    
    JSValue exception = JS_GetException(ctx);
    const char *error_str = JS_ToCString(ctx, exception);
    if (error_str) {
      err = std::string("JavaScript error: ") + error_str;
      JS_FreeCString(ctx, error_str);
    } else {
      err = "JavaScript error: unable to get error message";
    }
    JS_FreeValue(ctx, exception);
  }

  JS_FreeValue(ctx, result);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  return success;
}

static JSValue js_findAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "findAttribute requires 2 arguments (path string, attribute name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_EXCEPTION;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_EXCEPTION;
  }

  tinyusdz::Path path(path_str, "");
  if (!path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the PrimSpec
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the attribute
  const Attribute* attr = ps->FindAttribute(attr_name);

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);

  if (!attr) {
    return JS_NULL;
  }

  // Convert attribute to JSON
  std::ostringstream oss;
  oss << "{";
  oss << "\"name\":\"" << attr->name() << "\",";
  oss << "\"typeName\":\"" << attr->type_name() << "\",";
  oss << "\"hasValue\":" << (attr->has_value() ? "true" : "false") << ",";
  oss << "\"hasTimeSamples\":" << (attr->has_timesamples() ? "true" : "false") << ",";
  oss << "\"isConnection\":" << (attr->is_connection() ? "true" : "false") << ",";
  oss << "\"isBlocked\":" << (attr->is_blocked() ? "true" : "false");
  oss << "}";

  std::string json = oss.str();
  JSValue result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<attribute>");
  if (JS_IsException(result)) {
    return JS_EXCEPTION;
  }
  return result;
}

static JSValue js_getPrimMetas(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "getPrimMetas requires 1 argument (path string)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_NULL;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(prim_path, &ps, &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  // Get the PrimMetas
  std::string json = PrimMetasToJSON(&ps->metas());
  JSValue result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<primmetas>");

  JS_FreeCString(ctx, path_str);
  return result;
}

static JSValue js_setPrimMetas(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "setPrimMetas requires 2 arguments (path string, metas object)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  // Convert JavaScript object to JSON string
  JSValue json_val = JS_JSONStringify(ctx, argv[1], JS_UNDEFINED, JS_UNDEFINED);
  if (JS_IsException(json_val)) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  const char *json_str = JS_ToCString(ctx, json_val);
  if (!json_str) {
    JS_FreeCString(ctx, path_str);
    JS_FreeValue(ctx, json_val);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, json_str);
    JS_FreeValue(ctx, json_val);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(prim_path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, json_str);
    JS_FreeValue(ctx, json_val);
    return JS_FALSE;
  }

  // Parse and set the new metas
  PrimMetas new_metas;
  std::string parse_err;
  bool success = false;
  if (PrimMetasFromJSON(std::string(json_str), &new_metas, &parse_err)) {
    ps->metas() = new_metas;
    success = true;
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, json_str);
  JS_FreeValue(ctx, json_val);

  return success ? JS_TRUE : JS_FALSE;
}

static JSValue js_getRelationship(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "getRelationship requires 2 arguments (path string, relationship name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_NULL;
  }

  const char *rel_name = JS_ToCString(ctx, argv[1]);
  if (!rel_name) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, rel_name);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(prim_path, &ps, &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, rel_name);
    return JS_NULL;
  }

  // Find the relationship
  const auto &props = ps->props();
  auto it = props.find(rel_name);

  JSValue result = JS_NULL;
  if (it != props.end() && it->second.is_relationship()) {
    const Relationship &rel = it->second.get_relationship();

    if (rel.is_blocked()) {
      // Return null for blocked relationships
      result = JS_NULL;
    } else if (rel.is_path()) {
      // Single path
      result = JS_NewString(ctx, rel.targetPath.full_path_name().c_str());
    } else if (rel.is_pathvector()) {
      // Array of paths
      result = JS_NewArray(ctx);
      for (size_t i = 0; i < rel.targetPathVector.size(); i++) {
        JSValue path_val = JS_NewString(ctx, rel.targetPathVector[i].full_path_name().c_str());
        JS_SetPropertyUint32(ctx, result, i, path_val);
      }
    }
    // DefineOnly returns null
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, rel_name);
  return result;
}

static JSValue js_setRelationship(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "setRelationship requires 3 arguments (path string, relationship name, target path(s))");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  const char *rel_name = JS_ToCString(ctx, argv[1]);
  if (!rel_name) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, rel_name);
    return JS_FALSE;
  }

  // Parse target paths from string or array
  Relationship new_rel;

  if (JS_IsNull(argv[2]) || JS_IsUndefined(argv[2])) {
    // Set as blocked (None)
    new_rel.set_blocked();
  } else if (JS_IsString(argv[2])) {
    // Single target path
    const char *target_str = JS_ToCString(ctx, argv[2]);
    if (target_str) {
      if (strlen(target_str) == 0) {
        // Empty string means define-only
        new_rel.set_novalue();
      } else {
        Path target_path(target_str, "");
        new_rel.set(target_path);
      }
      JS_FreeCString(ctx, target_str);
    }
  } else if (JS_IsArray(argv[2])) {
    // Array of target paths
    JSValue length_val = JS_GetPropertyStr(ctx, argv[2], "length");
    uint32_t length;
    JS_ToUint32(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);

    std::vector<Path> target_paths;
    for (uint32_t i = 0; i < length; i++) {
      JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
      if (JS_IsString(elem)) {
        const char *target_str = JS_ToCString(ctx, elem);
        if (target_str) {
          target_paths.push_back(Path(target_str, ""));
          JS_FreeCString(ctx, target_str);
        }
      }
      JS_FreeValue(ctx, elem);
    }

    if (!target_paths.empty()) {
      new_rel.set(target_paths);
    } else {
      new_rel.set_novalue();
    }
  } else {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, rel_name);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(prim_path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, rel_name);
    return JS_FALSE;
  }

  // Find and modify or create the relationship
  bool success = false;
  auto &props = ps->props();
  auto it = props.find(rel_name);

  if (it != props.end() && it->second.is_relationship()) {
    // Modify existing relationship
    it->second.relationship() = new_rel;
    success = true;
  } else if (it == props.end()) {
    // Create new relationship
    Property prop(new_rel);
    ps->props()[rel_name] = prop;
    success = true;
  }
  // If property exists but is not a relationship, return false

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, rel_name);
  return success ? JS_TRUE : JS_FALSE;
}

static JSValue js_isAttributeConnection(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "isAttributeConnection requires 2 arguments (path string, attribute name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(prim_path, &ps, &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  // Find the attribute
  const auto &props = ps->props();
  auto it = props.find(attr_name);

  bool is_connection = false;
  if (it != props.end() && it->second.is_attribute()) {
    const Attribute &attr = it->second.get_attribute();
    is_connection = attr.is_connection();
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  return is_connection ? JS_TRUE : JS_FALSE;
}

static JSValue js_getAttributeConnections(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "getAttributeConnections requires 2 arguments (path string, attribute name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_NULL;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(prim_path, &ps, &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the attribute
  const auto &props = ps->props();
  auto it = props.find(attr_name);

  JSValue result = JS_NULL;
  if (it != props.end() && it->second.is_attribute()) {
    const Attribute &attr = it->second.get_attribute();
    const std::vector<Path> &connections = attr.connections();

    if (connections.empty()) {
      result = JS_NULL;
    } else if (connections.size() == 1) {
      // Return single string for single connection
      result = JS_NewString(ctx, connections[0].full_path_name().c_str());
    } else {
      // Return array of strings for multiple connections
      result = JS_NewArray(ctx);
      for (size_t i = 0; i < connections.size(); i++) {
        JSValue path_val = JS_NewString(ctx, connections[i].full_path_name().c_str());
        JS_SetPropertyUint32(ctx, result, i, path_val);
      }
    }
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  return result;
}

static JSValue js_setAttributeConnections(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "setAttributeConnections requires 3 arguments (path string, attribute name, connection path(s))");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  // Parse connection paths from string or array
  std::vector<Path> connection_paths;

  if (JS_IsString(argv[2])) {
    // Single connection path
    const char *conn_str = JS_ToCString(ctx, argv[2]);
    if (conn_str) {
      connection_paths.push_back(Path(conn_str, ""));
      JS_FreeCString(ctx, conn_str);
    }
  } else if (JS_IsArray(argv[2])) {
    // Array of connection paths
    JSValue length_val = JS_GetPropertyStr(ctx, argv[2], "length");
    uint32_t length;
    JS_ToUint32(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);

    for (uint32_t i = 0; i < length; i++) {
      JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
      if (JS_IsString(elem)) {
        const char *conn_str = JS_ToCString(ctx, elem);
        if (conn_str) {
          connection_paths.push_back(Path(conn_str, ""));
          JS_FreeCString(ctx, conn_str);
        }
      }
      JS_FreeValue(ctx, elem);
    }
  } else if (JS_IsNull(argv[2]) || JS_IsUndefined(argv[2])) {
    // Clear connections
    connection_paths.clear();
  } else {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(prim_path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  // Find and modify the attribute
  bool success = false;
  auto &props = ps->props();
  auto it = props.find(attr_name);

  if (it != props.end() && it->second.is_attribute()) {
    Attribute &attr = it->second.attribute();
    if (connection_paths.empty()) {
      // Clear connections
      attr.connections().clear();
    } else {
      // Set connections
      attr.set_connections(connection_paths);
    }
    success = true;
  } else if (it == props.end() && !connection_paths.empty()) {
    // Create new attribute with connections
    Attribute attr;
    attr.set_name(attr_name);
    attr.set_connections(connection_paths);
    Property prop(attr);
    ps->props()[attr_name] = prop;
    success = true;
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  return success ? JS_TRUE : JS_FALSE;
}

static JSValue js_getAttributeMetas(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "getAttributeMetas requires 2 arguments (path string, attribute name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_NULL;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(prim_path, &ps, &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the attribute
  const auto &props = ps->props();
  auto it = props.find(attr_name);

  JSValue result = JS_NULL;
  if (it != props.end() && it->second.is_attribute()) {
    const Attribute &attr = it->second.get_attribute();
    std::string json = AttrMetasToJSON(&attr.metas());
    result = JS_ParseJSON(ctx, json.c_str(), json.length(), "<attrmetas>");
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  return result;
}

static JSValue js_setAttributeMetas(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "setAttributeMetas requires 3 arguments (path string, attribute name, metas object)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  // Convert JavaScript object to JSON string
  JSValue json_val = JS_JSONStringify(ctx, argv[2], JS_UNDEFINED, JS_UNDEFINED);
  if (JS_IsException(json_val)) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  const char *json_str = JS_ToCString(ctx, json_val);
  if (!json_str) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    JS_FreeValue(ctx, json_val);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    JS_FreeCString(ctx, json_str);
    JS_FreeValue(ctx, json_val);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(prim_path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    JS_FreeCString(ctx, json_str);
    JS_FreeValue(ctx, json_val);
    return JS_FALSE;
  }

  // Find and modify the attribute
  bool success = false;
  auto &props = ps->props();
  auto it = props.find(attr_name);

  if (it != props.end() && it->second.is_attribute()) {
    AttrMetas new_metas;
    std::string parse_err;
    if (AttrMetasFromJSON(std::string(json_str), &new_metas, &parse_err)) {
      it->second.attribute().metas() = new_metas;
      success = true;
    }
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  JS_FreeCString(ctx, json_str);
  JS_FreeValue(ctx, json_val);

  return success ? JS_TRUE : JS_FALSE;
}

static JSValue js_getAttributeType(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_NULL;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "getAttributeType requires 2 arguments (path string, attribute name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_NULL;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_NULL;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the PrimSpec
  const PrimSpec *ps = nullptr;
  std::string err;
  bool found = g_current_layer->find_primspec_at(prim_path, &ps, &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_NULL;
  }

  // Find the attribute
  const auto &props = ps->props();
  auto it = props.find(attr_name);

  JSValue result = JS_NULL;
  if (it != props.end() && it->second.is_attribute()) {
    const Attribute &attr = it->second.get_attribute();
    std::string type_name = attr.get_attribute_type();
    result = JS_NewString(ctx, type_name.c_str());
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  return result;
}

static JSValue js_setAttributeType(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "setAttributeType requires 3 arguments (path string, attribute name, type name)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  const char *type_name = JS_ToCString(ctx, argv[2]);
  if (!type_name) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    JS_FreeCString(ctx, type_name);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(prim_path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    JS_FreeCString(ctx, type_name);
    return JS_FALSE;
  }

  // Find and modify the attribute
  bool success = false;
  auto &props = ps->props();
  auto it = props.find(attr_name);

  if (it != props.end() && it->second.is_attribute()) {
    success = it->second.attribute().set_attribute_type(std::string(type_name));
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);
  JS_FreeCString(ctx, type_name);
  return success ? JS_TRUE : JS_FALSE;
}

static JSValue js_setPrimSpecName(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "setPrimSpecName requires 2 arguments (path string, new name string)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_FALSE;
  }

  const char *new_name = JS_ToCString(ctx, argv[1]);
  if (!new_name) {
    JS_FreeCString(ctx, path_str);
    return JS_FALSE;
  }

  tinyusdz::Path prim_path(path_str, "");
  if (!prim_path.is_valid() || prim_path.is_root_path()) {
    // Root PrimSpec - not allowed to rename
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, new_name);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(prim_path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, new_name);
    return JS_FALSE;
  }

  // Set the new name
  ps->set_name(std::string(new_name));

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, new_name);

  return JS_TRUE;
}

static JSValue js_modifyAttributeValue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValueConst *func_data) {
  if (!g_current_layer) {
    return JS_FALSE;
  }

  if (argc < 3) {
    return JS_ThrowTypeError(ctx, "modifyAttributeValue requires 3 arguments (path string, attribute name, value)");
  }

  const char *path_str = JS_ToCString(ctx, argv[0]);
  if (!path_str) {
    return JS_EXCEPTION;
  }

  const char *attr_name = JS_ToCString(ctx, argv[1]);
  if (!attr_name) {
    JS_FreeCString(ctx, path_str);
    return JS_EXCEPTION;
  }

  tinyusdz::Path path(path_str, "");
  if (!path.is_valid()) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  // Find the PrimSpec (need non-const for modification)
  PrimSpec *ps = nullptr;
  std::string err;
  bool found = const_cast<Layer*>(g_current_layer)->find_primspec_at(path, const_cast<const PrimSpec**>(&ps), &err);

  if (!found || !ps) {
    JS_FreeCString(ctx, path_str);
    JS_FreeCString(ctx, attr_name);
    return JS_FALSE;
  }

  bool success = false;

  // Determine the value type and modify accordingly
  if (JS_IsBool(argv[2])) {
    bool val = JS_ToBool(ctx, argv[2]);
    success = ps->ModifyAttributeValue(attr_name, val);
  } else if (JS_IsNumber(argv[2])) {
    double val;
    if (JS_ToFloat64(ctx, &val, argv[2]) == 0) {
      // Check if it's an integer
      int32_t ival;
      if (JS_ToInt32(ctx, &ival, argv[2]) == 0 && (double)ival == val) {
        success = ps->ModifyAttributeValue(attr_name, ival);
      } else {
        success = ps->ModifyAttributeValue(attr_name, (float)val);
      }
    }
  } else if (JS_IsString(argv[2])) {
    const char *str_val = JS_ToCString(ctx, argv[2]);
    if (str_val) {
      success = ps->ModifyAttributeValue(attr_name, std::string(str_val));
      JS_FreeCString(ctx, str_val);
    }
  } else if (JS_IsArray(argv[2])) {
    // Handle array values (e.g., vec3)
    JSValue length_val = JS_GetPropertyStr(ctx, argv[2], "length");
    uint32_t length;
    JS_ToUint32(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);

    if (length == 3) {
      value::float3 vec;
      for (uint32_t i = 0; i < 3; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
        double val;
        JS_ToFloat64(ctx, &val, elem);
        vec[i] = (float)val;
        JS_FreeValue(ctx, elem);
      }
      success = ps->ModifyAttributeValue(attr_name, vec);
    } else if (length == 2) {
      value::float2 vec;
      for (uint32_t i = 0; i < 2; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
        double val;
        JS_ToFloat64(ctx, &val, elem);
        vec[i] = (float)val;
        JS_FreeValue(ctx, elem);
      }
      success = ps->ModifyAttributeValue(attr_name, vec);
    } else if (length == 4) {
      value::float4 vec;
      for (uint32_t i = 0; i < 4; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
        double val;
        JS_ToFloat64(ctx, &val, elem);
        vec[i] = (float)val;
        JS_FreeValue(ctx, elem);
      }
      success = ps->ModifyAttributeValue(attr_name, vec);
    }
  }

  JS_FreeCString(ctx, path_str);
  JS_FreeCString(ctx, attr_name);

  return JS_NewBool(ctx, success);
}

bool RunJSScriptWithLayer(const std::string &js_code, const class Layer* layer, std::string &err) {
  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    err = "Failed to create JavaScript runtime";
    return false;
  }

  JSContext *ctx = JS_NewContext(rt);
  if (!ctx) {
    err = "Failed to create JavaScript context";
    JS_FreeRuntime(rt);
    return false;
  }

  // Set the global Layer pointer and add functions to the global context
  g_current_layer = layer;
  JSValue global_obj = JS_GetGlobalObject(ctx);
  
  JSValue findFunc = JS_NewCFunctionData(ctx, js_findPrimSpecByPath, 1, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "findPrimSpecByPath", findFunc);
  
  JSValue metaFunc = JS_NewCFunctionData(ctx, js_getPrimSpecMetadata, 1, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getPrimSpecMetadata", metaFunc);

  JSValue findAttrFunc = JS_NewCFunctionData(ctx, js_findAttribute, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "findAttribute", findAttrFunc);

  JSValue modifyAttrFunc = JS_NewCFunctionData(ctx, js_modifyAttributeValue, 3, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "modifyAttributeValue", modifyAttrFunc);

  JSValue setPrimNameFunc = JS_NewCFunctionData(ctx, js_setPrimSpecName, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "setPrimSpecName", setPrimNameFunc);

  JSValue getAttrTypeFunc = JS_NewCFunctionData(ctx, js_getAttributeType, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getAttributeType", getAttrTypeFunc);

  JSValue setAttrTypeFunc = JS_NewCFunctionData(ctx, js_setAttributeType, 3, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "setAttributeType", setAttrTypeFunc);

  JSValue getAttrMetasFunc = JS_NewCFunctionData(ctx, js_getAttributeMetas, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getAttributeMetas", getAttrMetasFunc);

  JSValue setAttrMetasFunc = JS_NewCFunctionData(ctx, js_setAttributeMetas, 3, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "setAttributeMetas", setAttrMetasFunc);

  JSValue isAttrConnFunc = JS_NewCFunctionData(ctx, js_isAttributeConnection, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "isAttributeConnection", isAttrConnFunc);

  JSValue getAttrConnFunc = JS_NewCFunctionData(ctx, js_getAttributeConnections, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getAttributeConnections", getAttrConnFunc);

  JSValue setAttrConnFunc = JS_NewCFunctionData(ctx, js_setAttributeConnections, 3, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "setAttributeConnections", setAttrConnFunc);

  JSValue getRelFunc = JS_NewCFunctionData(ctx, js_getRelationship, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getRelationship", getRelFunc);

  JSValue setRelFunc = JS_NewCFunctionData(ctx, js_setRelationship, 3, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "setRelationship", setRelFunc);

  JSValue getPrimMetasFunc = JS_NewCFunctionData(ctx, js_getPrimMetas, 1, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "getPrimMetas", getPrimMetasFunc);

  JSValue setPrimMetasFunc = JS_NewCFunctionData(ctx, js_setPrimMetas, 2, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, global_obj, "setPrimMetas", setPrimMetasFunc);

  JS_FreeValue(ctx, global_obj);

  JSValue result = JS_Eval(ctx, js_code.c_str(), js_code.length(),
                          "<eval>", JS_EVAL_TYPE_GLOBAL);

  bool success = true;
  if (JS_IsException(result)) {
    success = false;
    
    JSValue exception = JS_GetException(ctx);
    const char *error_str = JS_ToCString(ctx, exception);
    if (error_str) {
      err = std::string("JavaScript error: ") + error_str;
      JS_FreeCString(ctx, error_str);
    } else {
      err = "JavaScript error: unable to get error message";
    }
    JS_FreeValue(ctx, exception);
  }

  JS_FreeValue(ctx, result);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  return success;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#else

bool RunJSScript(const std::string &js_code, std::string &err) {
  err = "JavaScript is not supported in this build.\n";
  (void)js_code;
  return false;
}

bool RunJSScriptWithLayerMetas(const std::string &js_code, const LayerMetas* layer_metas, std::string &err) {
  err = "JavaScript is not supported in this build.\n";
  (void)js_code;
  (void)layer_metas;
  return false;
}

bool RunJSScriptWithAttribute(const std::string &js_code, const Attribute* attribute, std::string &err) {
  err = "JavaScript is not supported in this build.\n";
  (void)js_code;
  (void)attribute;
  return false;
}

bool RunJSScriptWithLayer(const std::string &js_code, const class Layer* layer, std::string &err) {
  err = "JavaScript is not supported in this build.\n";
  (void)js_code;
  (void)layer;
  return false;
}
#endif

} // namespace tydra
} // namespace tinyusdz

