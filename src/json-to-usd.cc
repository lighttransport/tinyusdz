#include "json-to-usd.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

namespace detail {

bool ParseStringArray(const nlohmann::json &j, std::vector<std::string> *result, std::string *warn, std::string *err) {
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

bool JSONToPropertyImpl(const nlohmann::json &j, PrimSpec *ps, std::string *warn, std::string *err) {

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

bool JSONToPrimSpecImpl(const nlohmann::json &j, PrimSpec *ps, std::string *warn, std::string *err) {
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

} // namespace detail

bool JSONToPrimSpec(const std::string &j_str, PrimSpec *ps, std::string *warn, std::string *err) {

  nlohmann::json j = nlohmann::json::parse(j_str, /* callback */nullptr, /* allow_exceptions */false);
  if (j.is_discarded()) {
    if (err) {
      (*err) = "Failed to parse string as JSON\n";
    }
    return false;
  }

  return detail::JSONToPrimSpecImpl(j, ps, warn, err);

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

  Layer layer;

  // Layer meta
  if (j.contains("metadata")) {
    nlohmann::json meta = j.at("metadata");

    if (meta.contains("upAxis")) {
      std::string s = meta.at("upAxis");
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
        
      
  }

  (*dst_layer) = std::move(layer);

  return true;

}

} // namespace tinyusdz
