// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.

#include "layer.hh"
#include "prim-types.hh"  // For PrimSpec, LayerMetas, etc.
#include "path-util.hh"   // For Path
#include "str-util.hh"    // For split function
#include "common-macros.inc"
#include "tiny-format.hh"

#if defined(TINYUSDZ_ENABLE_THREAD)
#include <mutex>
#endif

namespace tinyusdz {

namespace {

// Forward declare helper functions
bool HasReferencesRec(uint32_t depth, const PrimSpec &primspec, const uint32_t max_depth);
bool HasPayloadRec(uint32_t depth, const PrimSpec &primspec, const uint32_t max_depth);
bool HasVariantRec(uint32_t depth, const PrimSpec &primspec, const uint32_t max_depth);
bool HasInheritsRec(uint32_t depth, const PrimSpec &primspec, const uint32_t max_depth);
bool HasSpecializesRec(uint32_t depth, const PrimSpec &primspec, const uint32_t max_depth);
bool HasOverRec(uint32_t depth, const PrimSpec &primspec, const uint32_t max_depth);
nonstd::optional<const PrimSpec *> GetPrimSpecAtPathRec(
    const PrimSpec *parent, const std::string &parent_path, const Path &path,
    uint32_t depth);

bool HasReferencesRec(uint32_t depth, const PrimSpec &primspec,
                      const uint32_t max_depth = 1024 * 128) {
  if (depth > max_depth) {
    // too deep
    return false;
  }

  if (primspec.metas().references) {
    return true;
  }

  for (auto &child : primspec.children()) {
    if (HasReferencesRec(depth + 1, child, max_depth)) {
      return true;
    }
  }

  return false;
}

bool HasPayloadRec(uint32_t depth, const PrimSpec &primspec,
                   const uint32_t max_depth = 1024 * 128) {
  if (depth > max_depth) {
    // too deep
    return false;
  }

  if (primspec.metas().payload) {
    return true;
  }

  for (auto &child : primspec.children()) {
    if (HasPayloadRec(depth + 1, child, max_depth)) {
      return true;
    }
  }

  return false;
}

bool HasVariantRec(uint32_t depth, const PrimSpec &primspec,
                   const uint32_t max_depth = 1024 * 128) {
  if (depth > max_depth) {
    // too deep
    return false;
  }

  // TODO: Also check if PrimSpec::variantSets is empty?
  if (primspec.metas().variants && primspec.metas().variantSets) {
    return true;
  }

  for (auto &child : primspec.children()) {
    if (HasVariantRec(depth + 1, child, max_depth)) {
      return true;
    }
  }

  return false;
}

bool HasInheritsRec(uint32_t depth, const PrimSpec &primspec,
                    const uint32_t max_depth = 1024 * 128) {
  if (depth > max_depth) {
    // too deep
    return false;
  }

  if (primspec.metas().inherits) {
    return true;
  }

  for (auto &child : primspec.children()) {
    if (HasInheritsRec(depth + 1, child, max_depth)) {
      return true;
    }
  }

  return false;
}

bool HasSpecializesRec(uint32_t depth, const PrimSpec &primspec,
                    const uint32_t max_depth = 1024 * 128) {
  if (depth > max_depth) {
    // too deep
    return false;
  }

  if (primspec.metas().specializes) {
    return true;
  }

  for (auto &child : primspec.children()) {
    if (HasSpecializesRec(depth + 1, child, max_depth)) {
      return true;
    }
  }

  return false;
}

bool HasOverRec(uint32_t depth, const PrimSpec &primspec,
                       const uint32_t max_depth = 1024 * 128) {
  if (depth > max_depth) {
    // too deep
    return false;
  }

  if (primspec.specifier() == Specifier::Over) {
    return true;
  }

  for (auto &child : primspec.children()) {
    if (HasOverRec(depth + 1, child, max_depth)) {
      return true;
    }
  }

  return false;
}

nonstd::optional<const PrimSpec *> GetPrimSpecAtPathRec(
    const PrimSpec *parent, const std::string &parent_path, const Path &path,
    uint32_t depth) {
  if (depth > (1024 * 1024 * 128)) {
    // Too deep.
    return nonstd::nullopt;
  }

  if (!parent) {
    return nonstd::nullopt;
  }

  std::string abs_path;
  {
    std::string elementName = parent->name();

    abs_path = parent_path + "/" + elementName;

    if (abs_path == path.full_path_name()) {
      return parent;
    }
  }

  for (const auto &child : parent->children()) {
    if (auto pv = GetPrimSpecAtPathRec(&child, abs_path, path, depth + 1)) {
      return pv.value();
    }
  }

  // not found
  return nonstd::nullopt;
}

// Helper function to estimate PrimSpec memory usage
static size_t EstimatePrimSpecMemory(const PrimSpec& ps);

static size_t EstimatePrimSpecMemory(const PrimSpec& ps) {
  size_t total = sizeof(PrimSpec);
  
  // String members
  total += ps.name().capacity();
  total += ps.typeName().capacity();
  
  // Properties map
  for (const auto& prop_pair : ps.props()) {
    total += prop_pair.first.capacity(); // key string
    total += prop_pair.second.estimate_memory_usage(); 
  }
  
  // Children vector
  for (const auto& child : ps.children()) {
    total += EstimatePrimSpecMemory(child); // Recursive estimation
  }
  
  // VariantSets map
  for (const auto& vs_pair : ps.variantSets()) {
    total += vs_pair.first.capacity(); // key string
    total += sizeof(VariantSet); // VariantSet base size
  }
  
  // TODO: Add more accurate memory estimation for complex nested types
  // like PrimMeta, Property values, etc.
  
  return total;
}

}  // namespace

// LayerImpl - internal implementation
struct LayerImpl {
  std::string _name;  // layer name ~= USD filename

  // key = prim name
  std::unordered_map<std::string, PrimSpec> _prim_specs;
  LayerMetas _metas;

  // Cached primspec path.
  // key : prim_part string (e.g. "/path/bora")
  mutable std::map<std::string, const PrimSpec *> _primspec_path_cache;
  mutable bool _dirty{true};

  // Cached flags for composition.
  // true by default even PrimSpec tree does not contain any `references`, `payload`, etc.
  mutable bool _has_unresolved_references{true};
  mutable bool _has_unresolved_payload{true};
  mutable bool _has_unresolved_variant{true};
  mutable bool _has_unresolved_inherits{true};
  mutable bool _has_unresolved_specializes{true};
  mutable bool _has_over_primspec{true};
  mutable bool _has_class_primspec{true};

  //
  // Record AssetResolution state(search paths, current working directory)
  // when this layer is opened by compostion(`references`, `payload`, `subLayers`)
  //
  mutable std::string _current_working_path;
  mutable std::vector<std::string> _asset_search_paths;
  mutable void *_asset_resolution_userdata{nullptr};
};

// Layer implementation

Layer::Layer() : _impl(std::make_unique<LayerImpl>()) {}

Layer::~Layer() = default;

Layer::Layer(const Layer& other) : _impl(std::make_unique<LayerImpl>(*other._impl)) {}

Layer& Layer::operator=(const Layer& other) {
  if (this != &other) {
    *_impl = *other._impl;
  }
  return *this;
}

Layer::Layer(Layer&& other) noexcept : _impl(std::move(other._impl)) {}

Layer& Layer::operator=(Layer&& other) noexcept {
  if (this != &other) {
    _impl = std::move(other._impl);
  }
  return *this;
}

const std::string Layer::name() const { 
  return _impl->_name;
}

void Layer::set_name(const std::string name) {
  _impl->_name = name;
}

void Layer::clear_primspecs() {
  _impl->_prim_specs.clear();
}

bool Layer::has_primspec(const std::string &primname) const {
  return _impl->_prim_specs.count(primname) > 0;
}

const LayerMetas &Layer::metas() const { 
  return _impl->_metas; 
}

LayerMetas &Layer::metas() { 
  return _impl->_metas; 
}

const std::unordered_map<std::string, PrimSpec> &Layer::primspecs() const {
  return _impl->_prim_specs;
}

std::unordered_map<std::string, PrimSpec> &Layer::primspecs() {
  return _impl->_prim_specs;
}

bool Layer::add_primspec(const std::string &name, const PrimSpec &ps) {
  if (name.empty()) {
    return false;
  }

  if (!ValidatePrimElementName(name)) {
    return false;
  }

  if (has_primspec(name)) {
    return false;
  }

  _impl->_prim_specs.emplace(name, ps);

  return true;
}

bool Layer::emplace_primspec(const std::string &name, PrimSpec &&ps) {
  if (name.empty()) {
    return false;
  }

  if (!ValidatePrimElementName(name)) {
    return false;
  }

  if (has_primspec(name)) {
    return false;
  }

  _impl->_prim_specs.emplace(name, std::move(ps));

  return true;
}

bool Layer::replace_primspec(const std::string &name, const PrimSpec &ps) {
  if (name.empty()) {
    return false;
  }

  if (!ValidatePrimElementName(name)) {
    return false;
  }

  if (!has_primspec(name)) {
    return false;
  }

  _impl->_prim_specs.at(name) = ps;

  return true;
}

bool Layer::replace_primspec(const std::string &name, PrimSpec &&ps) {
  if (name.empty()) {
    return false;
  }

  if (!ValidatePrimElementName(name)) {
    return false;
  }

  if (!has_primspec(name)) {
    return false;
  }

  _impl->_prim_specs.at(name) = std::move(ps);

  return true;
}

bool Layer::has_unresolved_references() const {
  return _impl->_has_unresolved_references;
}

bool Layer::has_unresolved_payload() const {
  return _impl->_has_unresolved_payload;
}

bool Layer::has_unresolved_variant() const {
  return _impl->_has_unresolved_variant;
}

bool Layer::has_over_primspec() const {
  return _impl->_has_over_primspec;
}

bool Layer::has_class_primspec() const {
  return _impl->_has_class_primspec;
}

bool Layer::has_unresolved_inherits() const {
  return _impl->_has_unresolved_inherits;
}

bool Layer::has_unresolved_specializes() const {
  return _impl->_has_unresolved_specializes;
}

void Layer::set_asset_resolution_state(
  const std::string &cwp, const std::vector<std::string> &search_paths, void *userdata) {
  _impl->_current_working_path = cwp;
  _impl->_asset_search_paths = search_paths;
  _impl->_asset_resolution_userdata = userdata;
}

void Layer::get_asset_resolution_state(
  std::string &cwp, std::vector<std::string> &search_paths, void *&userdata) {
  cwp = _impl->_current_working_path;
  search_paths = _impl->_asset_search_paths;
  userdata = _impl->_asset_resolution_userdata;
}

const std::string Layer::get_current_working_path() const {
  return _impl->_current_working_path;
}

const std::vector<std::string> Layer::get_asset_search_paths() const {
  return _impl->_asset_search_paths;
}

bool Layer::find_primspec_at(const Path &path, const PrimSpec **ps,
                             std::string *err) const {
  
#define PushError(msg) \
  if (err) {           \
    (*err) += msg;     \
    (*err) += "\n";    \
  }
  if (!ps) {
    PUSH_ERROR_AND_RETURN("Invalid PrimSpec dst argument");
  }

  if (!path.is_valid()) {
    DCOUT("Invalid path.");
    PUSH_ERROR_AND_RETURN("Invalid path");
  }

  if (path.is_relative_path()) {
    // TODO
    PUSH_ERROR_AND_RETURN(fmt::format("TODO: Relative path: {}", path.full_path_name()));
  }

  if (!path.is_absolute_path()) {
    PUSH_ERROR_AND_RETURN(fmt::format("Path is not absolute path: {}", path.full_path_name()));
  }

  if (_impl->_dirty) {
    DCOUT("clear cache.");
    // Clear cache.
    _impl->_primspec_path_cache.clear();

    _impl->_dirty = false;
  } else {
    // First find from a cache.
    auto ret = _impl->_primspec_path_cache.find(path.prim_part());
    if (ret != _impl->_primspec_path_cache.end()) {
      DCOUT("Found cache.");
      (*ps) = ret->second;
      return true;
    }
  }

  // Brute-force search.
  for (const auto &parent : _impl->_prim_specs) {
    if (auto pv = GetPrimSpecAtPathRec(&parent.second, /* parent_path */ "",
                                       path, /* depth */ 0)) {
      (*ps) = pv.value();

      // Add to cache.
      // Assume pointer address does not change unless dirty state changes.
      _impl->_primspec_path_cache[path.prim_part()] = pv.value();
      return true;
    }
  }

  return false;

#undef PushError
}

bool Layer::check_unresolved_references(const uint32_t max_depth) const {
  bool ret = false;

  for (const auto &item : _impl->_prim_specs) {
    if (HasReferencesRec(/* depth */ 0, item.second, max_depth)) {
      ret = true;
      break;
    }
  }

  _impl->_has_unresolved_references = ret;
  return _impl->_has_unresolved_references;
}

bool Layer::check_unresolved_payload(const uint32_t max_depth) const {
  bool ret = false;

  for (const auto &item : _impl->_prim_specs) {
    if (HasPayloadRec(/* depth */ 0, item.second, max_depth)) {
      ret = true;
      break;
    }
  }

  _impl->_has_unresolved_payload = ret;
  return _impl->_has_unresolved_payload;
}

bool Layer::check_unresolved_variant(const uint32_t max_depth) const {
  bool ret = false;

  for (const auto &item : _impl->_prim_specs) {
    if (HasVariantRec(/* depth */ 0, item.second, max_depth)) {
      ret = true;
      break;
    }
  }

  _impl->_has_unresolved_variant = ret;
  return _impl->_has_unresolved_variant;
}

bool Layer::check_unresolved_inherits(const uint32_t max_depth) const {
  bool ret = false;

  for (const auto &item : _impl->_prim_specs) {
    if (HasInheritsRec(/* depth */ 0, item.second, max_depth)) {
      ret = true;
      break;
    }
  }

  _impl->_has_unresolved_inherits = ret;
  return _impl->_has_unresolved_inherits;
}

bool Layer::check_unresolved_specializes(const uint32_t max_depth) const {
  bool ret = false;

  for (const auto &item : _impl->_prim_specs) {
    if (HasSpecializesRec(/* depth */ 0, item.second, max_depth)) {
      ret = true;
      break;
    }
  }

  _impl->_has_unresolved_specializes = ret;
  return _impl->_has_unresolved_specializes;
}

bool Layer::check_over_primspec(const uint32_t max_depth) const {
  bool ret = false;

  for (const auto &item : _impl->_prim_specs) {
    if (HasOverRec(/* depth */ 0, item.second, max_depth)) {
      ret = true;
      break;
    }
  }

  _impl->_has_over_primspec = ret;
  return _impl->_has_over_primspec;
}

size_t Layer::estimate_memory_usage() const {
  size_t total = sizeof(Layer);
  
  // Layer name
  total += _impl->_name.capacity();
  
  // PrimSpecs map
  total += _impl->_prim_specs.bucket_count() * sizeof(void*); // Hash table buckets
  for (const auto& prim_pair : _impl->_prim_specs) {
    total += prim_pair.first.capacity(); // key string
    total += EstimatePrimSpecMemory(prim_pair.second); // PrimSpec data
  }
  
  // LayerMetas
  total += sizeof(LayerMetas);
  // Add metadata strings
  // defaultPrim is a value::token (Token), not optional
  total += _impl->_metas.defaultPrim.str().capacity();
  
  // comment and doc are StringData structs with a value member
  total += _impl->_metas.comment.value.capacity();
  total += _impl->_metas.doc.value.capacity();
  
  // SubLayers
  for (const auto& sublayer : _impl->_metas.subLayers) {
    total += sizeof(SubLayer);
    // AssetPath contains strings
    total += sublayer.assetPath.GetAssetPath().capacity();
  }
  
  // CustomLayerData map
  for (const auto& custom_pair : _impl->_metas.customLayerData) {
    total += custom_pair.first.capacity(); // key string
    total += sizeof(MetaVariable); // Simplified estimate for MetaVariable
  }
  
  // PrimSpec path cache
  total += _impl->_primspec_path_cache.size() * (sizeof(std::string) + sizeof(const PrimSpec*));
  for (const auto& cache_pair : _impl->_primspec_path_cache) {
    total += cache_pair.first.capacity();
  }
  
  // Asset resolution state
  total += _impl->_current_working_path.capacity();
  for (const auto& path : _impl->_asset_search_paths) {
    total += path.capacity();
  }
  
  return total;
}

}  // namespace tinyusdz
