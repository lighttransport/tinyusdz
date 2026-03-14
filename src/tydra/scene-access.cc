// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment, Inc.
//

// src
#include "common-macros.inc"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "prim-types.hh"
#include "primvar.hh"
#include "tiny-container.hh"
#include "tiny-format.hh"
#include "tydra/prim-apply.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "value-pprint.hh"
#include "xform.hh"  // For matrix inverse

// src/tydra
#include "attribute-eval.hh"
#include "scene-access.hh"

#include <unordered_set>

namespace tinyusdz {
namespace tydra {

constexpr auto kInfoId = "info:id";

namespace {

// For PUSH_ERROR_AND_RETURN
#define PushError(msg) \
  if (err) {           \
    (*err) += msg;     \
  }

// Typed TimeSamples to typeless TimeSamples
template <typename T>
value::TimeSamples ToTypelessTimeSamples(const TypedTimeSamples<T> &ts) {
  value::TimeSamples dst;

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  const auto &times = ts.get_times();
  const auto &values = ts.get_values();
  const auto &blocked = ts.get_blocked();

  for (size_t i = 0; i < times.size(); i++) {
    if (blocked[i]) {
      // For untyped TimeSamples, blocked samples need a dummy value
      dst.add_blocked_sample(times[i], value::Value());
    } else {
      dst.add_sample(times[i], values[i]);
    }
  }
#else
  const std::vector<typename TypedTimeSamples<T>::Sample> &samples =
      ts.get_samples();

  for (size_t i = 0; i < samples.size(); i++) {
    if (samples[i].blocked) {
      // For untyped TimeSamples, blocked samples need a dummy value
      dst.add_blocked_sample(samples[i].t, value::Value());
    } else {
      dst.add_sample(samples[i].t, samples[i].value);
    }
  }
#endif

  return dst;
}

// Enum TimeSamples to typeless TimeSamples
template <typename T>
value::TimeSamples EnumTimeSamplesToTypelessTimeSamples(
    const TypedTimeSamples<T> &ts) {
  value::TimeSamples dst;

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  const auto &times = ts.get_times();
  const auto &values = ts.get_values();
  const auto &blocked = ts.get_blocked();

  for (size_t i = 0; i < times.size(); i++) {
    if (blocked[i]) {
      // For untyped TimeSamples, blocked samples need a dummy value
      dst.add_blocked_sample(times[i], value::Value());
    } else {
      // to token
      value::token tok(to_string(values[i]));
      dst.add_sample(times[i], tok);
    }
  }
#else
  const std::vector<typename TypedTimeSamples<T>::Sample> &samples =
      ts.get_samples();

  for (size_t i = 0; i < samples.size(); i++) {
    if (samples[i].blocked) {
      // For untyped TimeSamples, blocked samples need a dummy value
      dst.add_blocked_sample(samples[i].t, value::Value());
    } else {
      // to token
      value::token tok(to_string(samples[i].value));
      dst.add_sample(samples[i].t, tok);
    }
  }
#endif

  return dst;
}

// Optimized iterative traversal using explicit stack
// Avoids recursion and reuses path buffer to minimize string allocations
template <typename T>
bool TraverseIterative(const tinyusdz::Prim &root_prim, PathPrimMap<T> &itemmap,
                       size_t max_iter = kMaxDefaultTraversalLimit) {
  // Stack stores: (prim pointer, child index, path length before this prim)
  StackVector<std::tuple<const tinyusdz::Prim *, size_t, size_t>, 4> stack;
  stack.reserve(64);

  // Shared path buffer - reuse to avoid allocations
  std::string path_buffer;
  path_buffer.reserve(256);

  // Process root prim
  path_buffer = "/" + root_prim.local_path().full_path_name();

  if (root_prim.is<T>()) {
    if (const T *pv = root_prim.as<T>()) {
      DCOUT("Path : <" << path_buffer << "> is " << tinyusdz::value::TypeTraits<T>::type_name());
      itemmap[path_buffer] = pv;
    }
  }

  if (!root_prim.children().empty()) {
    stack.emplace_back(&root_prim, 0, 0);  // path_len=0 since "/" is implicit
  }

  size_t iter = 0;
  while (!stack.empty()) {
    if (iter++ >= max_iter) break;
    auto &top = stack.back();
    const tinyusdz::Prim *parent = std::get<0>(top);
    size_t &child_idx = std::get<1>(top);
    const size_t parent_path_len = std::get<2>(top);

    if (child_idx >= parent->children().size()) {
      // All children processed, backtrack
      // Restore path to parent's length
      path_buffer.resize(parent_path_len);
      stack.pop_back();
      continue;
    }

    const tinyusdz::Prim &child = parent->children()[child_idx];
    ++child_idx;

    // Build path for this child
    size_t current_path_len = path_buffer.size();
    path_buffer += "/";
    path_buffer += child.local_path().full_path_name();

    // Check and add to map if type matches
    if (child.is<T>()) {
      if (const T *pv = child.as<T>()) {
        DCOUT("Path : <" << path_buffer << "> is " << tinyusdz::value::TypeTraits<T>::type_name());
        itemmap[path_buffer] = pv;
      }
    }

    // Push child to stack if it has children
    if (!child.children().empty()) {
      stack.emplace_back(&child, 0, current_path_len);
    } else {
      // No children, restore path immediately
      path_buffer.resize(current_path_len);
    }
  }

  return true;
}

// Optimized iterative shader traversal using explicit stack
// Avoids recursion and reuses path buffer to minimize string allocations
template <typename ShaderTy>
bool TraverseShaderIterative(const tinyusdz::Prim &root_prim,
                             PathShaderMap<ShaderTy> &itemmap,
                             size_t max_iter = kMaxDefaultTraversalLimit) {
  // Stack stores: (prim pointer, child index, path length before this prim)
  StackVector<std::tuple<const tinyusdz::Prim *, size_t, size_t>, 4> stack;
  stack.reserve(64);

  // Shared path buffer - reuse to avoid allocations
  std::string path_buffer;
  path_buffer.reserve(256);

  // Process root prim
  path_buffer = "/" + root_prim.local_path().full_path_name();

  // Check if root is a Shader of the wanted type
  if (const Shader *ps = root_prim.as<Shader>()) {
    if (const ShaderTy *s = ps->value.as<ShaderTy>()) {
      itemmap[path_buffer] = std::make_pair(ps, s);
    }
  }

  if (!root_prim.children().empty()) {
    stack.emplace_back(&root_prim, 0, 0);
  }

  size_t iter = 0;
  while (!stack.empty()) {
    if (iter++ >= max_iter) break;
    auto &top = stack.back();
    const tinyusdz::Prim *parent = std::get<0>(top);
    size_t &child_idx = std::get<1>(top);
    const size_t parent_path_len = std::get<2>(top);

    if (child_idx >= parent->children().size()) {
      // All children processed, backtrack
      path_buffer.resize(parent_path_len);
      stack.pop_back();
      continue;
    }

    const tinyusdz::Prim &child = parent->children()[child_idx];
    ++child_idx;

    // Build path for this child
    size_t current_path_len = path_buffer.size();
    path_buffer += "/";
    path_buffer += child.local_path().full_path_name();

    // Check if this is a Shader of the wanted type
    if (const Shader *ps = child.as<Shader>()) {
      if (const ShaderTy *s = ps->value.as<ShaderTy>()) {
        itemmap[path_buffer] = std::make_pair(ps, s);
      }
    }

    // Push child to stack if it has children
    if (!child.children().empty()) {
      stack.emplace_back(&child, 0, current_path_len);
    } else {
      // No children, restore path immediately
      path_buffer.resize(current_path_len);
    }
  }

  return true;
}

bool ListSceneNamesRec(const tinyusdz::Prim &root, uint32_t depth,
                       std::vector<std::pair<bool, std::string>> *sceneNames) {
  if (!sceneNames) {
    return false;
  }

  if (depth > kMaxDefaultTraversalLimit) {
    // Too deep
    return false;
  }

  if (root.metas().has_sceneName()) {
    bool is_over = (root.specifier() == Specifier::Over);

    sceneNames->push_back(
        std::make_pair(is_over, root.metas().get_sceneName()));
  }

  return true;
}

}  // namespace

template <typename T>
bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<T> &m /* output */) {
  // Should report error at compilation stege.
  static_assert(
      (value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id()) &&
          (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id()),
      "Not a Prim type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_MODEL_BEGIN <= value::TypeTraits<T>::type_id()) &&
      (value::TypeId::TYPE_ID_MODEL_END > value::TypeTraits<T>::type_id())) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.root_prims()) {
    TraverseIterative(root_prim, m);
  }

  return true;
}

template <typename T>
bool ListShaders(const tinyusdz::Stage &stage,
                 PathShaderMap<T> &m /* output */) {
  // Concrete Shader type(e.g. UsdPreviewSurface) is classified as Imaging
  // Should report error at compilation stege.
  static_assert((value::TypeId::TYPE_ID_IMAGING_BEGIN <=
                 value::TypeTraits<T>::type_id()) &&
                    (value::TypeId::TYPE_ID_IMAGING_END >
                     value::TypeTraits<T>::type_id()),
                "Not a Shader type.");

  // Check at runtime. Just in case...
  if ((value::TypeId::TYPE_ID_IMAGING_BEGIN <=
       value::TypeTraits<T>::type_id()) &&
      (value::TypeId::TYPE_ID_IMAGING_END > value::TypeTraits<T>::type_id())) {
    // Ok
  } else {
    return false;
  }

  for (const auto &root_prim : stage.root_prims()) {
    TraverseShaderIterative(root_prim, m);
  }

  return true;
}

const Prim *GetParentPrim(const tinyusdz::Stage &stage,
                          const tinyusdz::Path &path, std::string *err) {
  if (!path.is_valid()) {
    if (err) {
      (*err) = "Input Path " + tinyusdz::to_string(path) + " is invalid.\n";
    }
    return nullptr;
  }

  if (path.is_root_path()) {
    if (err) {
      (*err) = "Input Path is root(\"/\").\n";
    }
    return nullptr;
  }

  if (path.is_root_prim()) {
    if (err) {
      (*err) = "Input Path is root Prim, so no parent Prim exists.\n";
    }
    return nullptr;
  }

  if (!path.is_absolute_path()) {
    if (err) {
      (*err) = "Input Path must be absolute path(i.e. starts with \"/\").\n";
    }
    return nullptr;
  }

  tinyusdz::Path parentPath = path.get_parent_prim_path();

  nonstd::expected<const Prim *, std::string> ret =
      stage.GetPrimAtPath(parentPath);
  if (ret) {
    return ret.value();
  } else {
    if (err) {
      (*err) += "Failed to get parent Prim from Path " +
                tinyusdz::to_string(path) + ". Reason = " + ret.error() + "\n";
    }
    return nullptr;
  }
}

//
// Template Instanciations
//
#define LISTPRIMS_INSTANCIATE(__ty) \
  template bool ListPrims(const tinyusdz::Stage &stage, PathPrimMap<__ty> &m);

APPLY_FUNC_TO_PRIM_TYPES(LISTPRIMS_INSTANCIATE)

#undef LISTPRIMS_INSTANCIATE

template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPreviewSurface> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdUVTexture> &m);

template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_string> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_int> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float2> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float3> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_float4> &m);
template bool ListShaders(const tinyusdz::Stage &stage,
                          PathShaderMap<UsdPrimvarReader_matrix> &m);

namespace {

// Optimized iterative version of VisitPrimsRec
// Handles primChildren ordering and early termination
bool VisitPrimsIterative(const tinyusdz::Path &start_abs_path,
                         const tinyusdz::Prim &start_prim, int32_t start_level,
                         VisitPrimFunction visitor_fun, void *userdata,
                         std::string *err,
                         size_t max_iter = kMaxDefaultTraversalLimit) {
  // Stack entry: (prim pointer, ordered children to visit, current child index, level, parent path)
  struct StackEntry {
    const tinyusdz::Prim *prim;
    std::vector<const tinyusdz::Prim *> ordered_children;
    size_t child_idx;
    int32_t level;
    tinyusdz::Path abs_path;

    StackEntry(const tinyusdz::Prim *p, int32_t lvl, tinyusdz::Path path)
        : prim(p), child_idx(0), level(lvl), abs_path(std::move(path)) {}
  };

  StackVector<StackEntry, 4> stack;
  stack.reserve(64);

  // Helper to get ordered children list
  auto get_ordered_children = [err](const tinyusdz::Prim &prim)
      -> std::pair<bool, std::vector<const tinyusdz::Prim *>> {
    std::vector<const tinyusdz::Prim *> result;

    if (prim.children().empty()) {
      return {true, result};
    }

    // If primChildren metadata matches children count, use it for ordering
    if (prim.metas().primChildren.size() == prim.children().size()) {
      std::unordered_map<std::string, const tinyusdz::Prim *, FNV1StringHash>
          primNameTable;
      primNameTable.reserve(prim.children().size());
      for (size_t i = 0; i < prim.children().size(); i++) {
        primNameTable.emplace(prim.children()[i].element_name(),
                              &prim.children()[i]);
      }

      for (size_t i = 0; i < prim.metas().primChildren.size(); i++) {
        value::token nameTok = prim.metas().primChildren[i];
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          result.push_back(it->second);
        } else {
          if (err) {
            (*err) += fmt::format(
                "Prim name `{}` in `primChildren` metadatum not found in this "
                "Prim's children",
                nameTok.str());
          }
          return {false, {}};
        }
      }
    } else {
      // Use natural order
      for (const auto &child : prim.children()) {
        result.push_back(&child);
      }
    }

    return {true, result};
  };

  // Visit start prim first
  {
    std::string fun_error;
    bool ret = visitor_fun(start_abs_path, start_prim, start_level, userdata, &fun_error);
    if (!ret) {
      if (fun_error.empty()) {
        DCOUT("Early termination requested");
      } else {
        if (err) {
          (*err) += fmt::format(
              "Visit function returned an error for Prim {} (id {}). err = {}",
              start_abs_path.full_path_name(), start_prim.prim_id(), fun_error);
        }
      }
      return false;
    }
  }

  // Get ordered children for start prim
  std::pair<bool, std::vector<const tinyusdz::Prim *>> start_result =
      get_ordered_children(start_prim);
  if (!start_result.first) {
    return false;
  }

  if (!start_result.second.empty()) {
    StackEntry entry(&start_prim, start_level, start_abs_path);
    entry.ordered_children = std::move(start_result.second);
    stack.push_back(std::move(entry));
  }

  // Iterative traversal
  size_t iter = 0;
  while (!stack.empty()) {
    if (iter++ >= max_iter) {
      if (err) {
        (*err) += "VisitPrims exceeded max iteration limit.\n";
      }
      return false;
    }
    auto &top = stack.back();

    if (top.child_idx >= top.ordered_children.size()) {
      // All children processed, backtrack
      stack.pop_back();
      continue;
    }

    const tinyusdz::Prim *child = top.ordered_children[top.child_idx];
    ++top.child_idx;

    // Build path for this child
    tinyusdz::Path child_abs_path = top.abs_path.AppendPrim(child->element_name());
    int32_t child_level = top.level + 1;

    // Call visitor
    std::string fun_error;
    bool ret = visitor_fun(child_abs_path, *child, child_level, userdata, &fun_error);
    if (!ret) {
      if (fun_error.empty()) {
        DCOUT("Early termination requested");
      } else {
        if (err) {
          (*err) += fmt::format(
              "Visit function returned an error for Prim {} (id {}). err = {}",
              child_abs_path.full_path_name(), child->prim_id(), fun_error);
        }
      }
      return false;
    }

    // Get ordered children for this child
    std::pair<bool, std::vector<const tinyusdz::Prim *>> child_result =
        get_ordered_children(*child);
    if (!child_result.first) {
      return false;
    }

    if (!child_result.second.empty()) {
      StackEntry entry(child, child_level, std::move(child_abs_path));
      entry.ordered_children = std::move(child_result.second);
      stack.push_back(std::move(entry));
    }
  }

  return true;
}


// Scalar-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
template <typename T>
bool ToProperty(const TypedAttribute<T> &input, Property &output, std::string *err) {


  Attribute attr;
  attr.variability() = Variability::Uniform;
  attr.set_type_name(value::TypeTraits<T>::type_name());

  if (input.is_blocked()) {
    attr.set_blocked(input.is_blocked());
  }

  if (input.has_connections()) {
    attr.set_connections(input.get_connections());
  }

  if (input.has_value()) {
    // Includes !authored()
    if (auto pv = input.get_value()) {
      value::Value val(pv.value());
      primvar::PrimVar pvar;
      pvar.set_value(val);

      attr.set_var(std::move(pvar));
    } else {
      if (err) {
        (*err) += fmt::format("[InternalError] Invalid TypedAttribute<{}> value.", value::TypeTraits<T>::type_name());
      }

      return false;
    }
  }

  attr.metas() = input.metas();

  output = Property(std::move(attr), /* custom */false);


  return true;
}

// Scalar or TimeSample-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
//
template <typename T>
bool ToProperty(const TypedAttribute<Animatable<T>> &input, Property &output, std::string *err) {

  DCOUT("ToProperty ");
  (void)err;

  Attribute attr;

  attr.variability() = Variability::Varying;
  attr.set_type_name(value::TypeTraits<T>::type_name());

  DCOUT("has_connections" << input.has_connections());
  DCOUT("has_value " << input.has_value());
  DCOUT("is_blocked " << input.is_blocked());

  if (input.is_blocked()) {
    attr.set_blocked(input.is_blocked());
  }

  if (input.has_connections()) {
    

    attr.set_connections(input.get_connections());
  }

  //DCOUT("has_default " << input.has_default());
  //DCOUT("has_timesamples " << input.has_timesamples());

  {
    primvar::PrimVar pvar;

    // Includes !authored()
    nonstd::optional<Animatable<T>> aval = input.get_value();
    if (aval) {

      if (aval.value().is_blocked()) {
        attr.set_blocked(true);
      }

      if (aval.value().has_value()) {
        T a;
        if (aval.value().get_default(&a)) {
          value::Value val(a);
          pvar.set_value(val);
        }
      }

      if (aval.value().has_timesamples()) {
        value::TimeSamples ts = ToTypelessTimeSamples(aval.value().get_timesamples());
        pvar.set_timesamples(std::move(ts));
      }

      if (aval.value().has_value() || aval.value().has_timesamples()) {
        attr.set_var(std::move(pvar));
      }

    } else {
      DCOUT("no animatable value.");
    }
  }

  attr.metas() = input.metas();

  output = Property(std::move(attr), /*custom*/ false);

  return true;
}

template <typename T>
bool ToProperty(const TypedAttributeWithFallback<T> &input, Property &output,
                std::string *err) {

  Attribute attr;
  attr.variability() = Variability::Uniform;
  attr.set_type_name(value::TypeTraits<T>::type_name());

  if (input.is_blocked()) {
    attr.set_blocked(input.is_blocked());
  }

  if (input.has_connections()) {
    attr.set_connections(input.get_connections());
  }

  if (!input.is_value_empty()) {
    primvar::PrimVar pvar;
    pvar.set_value(value::Value(input.get_value()));
    attr.set_var(std::move(pvar));
  }

  attr.metas() = input.metas();
  output = Property(std::move(attr), /* custom */ false);

  (void)err;
  return true;
}

// Scalar or TimeSample-valued attribute.
// TypedAttribute* => Attribute defined in USD schema, so not a custom attr.
//
// TODO: Support timeSampled attribute.
template <typename T>
bool ToProperty(const TypedAttributeWithFallback<Animatable<T>> &input,
                Property &output, std::string *err) {

  Attribute attr;
  attr.variability() = Variability::Varying;
  attr.set_type_name(value::TypeTraits<T>::type_name());

  DCOUT("has_connections " << input.has_connections());

  if (input.is_blocked()) {
    attr.set_blocked(input.is_blocked());
  }

  if (input.has_connections()) {
    attr.set_connections(input.get_connections());
  }

  {
    // Includes !authored()
    // FIXME: Currently scalar only.
    Animatable<T> v = input.get_value();

    primvar::PrimVar pvar;
    DCOUT("has_timesamples " << v.has_timesamples());
    DCOUT("has_value " << v.has_value());

    if (v.has_timesamples()) {
      value::TimeSamples ts = ToTypelessTimeSamples(v.get_timesamples());
      pvar.set_timesamples(std::move(ts));
    }

    if (v.has_value()) {
      T a;
      if (v.get_scalar(&a)) {
        value::Value val(a);
        pvar.set_value(val);
      } else {
        DCOUT("??? Invalid Animatable value.");
        if (err) {
          (*err) += "[InternalError] Invalid Animatable value.";
        }
        return false;
      }
    }

    attr.set_var(std::move(pvar));
  }

  attr.metas() = input.metas();

  output = Property(std::move(attr), /* custom */ false);


  return true;
}

// To Property with token type
template <typename T>
bool ToTokenProperty(const TypedAttributeWithFallback<Animatable<T>> &input,
                     Property &output, std::string *err) {

  Attribute attr;
  attr.variability() = Variability::Varying;
  attr.set_type_name(value::kToken);

  if (input.is_blocked()) {
    attr.set_blocked(input.is_blocked());
  }

  if (input.has_connections()) {
    attr.set_connections(input.get_connections());
  }

  {
    // Includes !authored()
    const Animatable<T> &v = input.get_value();

    primvar::PrimVar pvar;

    if (v.has_timesamples()) {
      value::TimeSamples ts =
          EnumTimeSamplesToTypelessTimeSamples(v.get_timesamples());
      pvar.set_timesamples(std::move(ts));
    }

    if (v.has_default()) {
      T a;
      if (v.get_default(&a)) {
        // to token type
        value::token tok(to_string(a));
        value::Value val(tok);
        pvar.set_value(val);
      } else {
        if (err) {
          (*err) += "[InternalError] Invalid Animatable value.";
        }
        return false;
      }
    }

    if (v.has_timesamples() || v.has_default()) {
      attr.set_var(std::move(pvar));
    }

  }

  attr.metas() = input.metas();

  output = Property(attr, /* custom */ false);

  return true;
}

// To Property with token type
template <typename T>
bool ToTokenProperty(const TypedAttributeWithFallback<T> &input,
                     Property &output, std::string *err) {
  (void)err;

  Attribute attr;
  attr.variability() = Variability::Uniform;
  attr.set_type_name(value::kToken);

  if (input.is_blocked()) {
    attr.set_blocked(input.is_blocked());
  }

  if (input.has_connections()) {
    attr.set_connections(input.get_connections());
  }

  {
    if (!input.is_value_empty()) {
      primvar::PrimVar pvar;
      value::token tok(to_string(input.get_value()));
      value::Value val(tok);
      pvar.set_value(val);
      attr.set_var(std::move(pvar));
    }
  }

  attr.metas() = input.metas();
  output = Property(attr, /* custom */ false);

  return true;
}

template <typename T>
nonstd::optional<Property> TypedTerminalAttributeToProperty(
    const TypedTerminalAttribute<T> &input) {
  if (!input.authored()) {
    // nothing to do
    return nonstd::nullopt;
  }

  Property output;

  // type info only
  if (input.has_actual_type()) {
    // type info only
    output = Property::MakeEmptyAttrib(input.get_actual_type_name(),
                                       /* custom */ false);
  } else {
    output = Property::MakeEmptyAttrib(input.type_name(), /* custom */ false);
  }

  return output;
}

bool XformOpToProperty(const XformOp &x, Property &prop) {
  primvar::PrimVar pv;

  Attribute attr;

  switch (x.op_type) {
    case XformOp::OpType::ResetXformStack: {
      // ??? Not exists in Prim's property
      return false;
    }
    case XformOp::OpType::Transform:
    case XformOp::OpType::Scale:
    case XformOp::OpType::Translate:
    case XformOp::OpType::RotateX:
    case XformOp::OpType::RotateY:
    case XformOp::OpType::RotateZ:
    case XformOp::OpType::Orient:
    case XformOp::OpType::RotateXYZ:
    case XformOp::OpType::RotateXZY:
    case XformOp::OpType::RotateYXZ:
    case XformOp::OpType::RotateYZX:
    case XformOp::OpType::RotateZXY:
    case XformOp::OpType::RotateZYX: {
      pv = x.get_var();
    }
  }

  attr.set_var(std::move(pv));
  // TODO: attribute meta

  prop = Property(attr, /* custom */ false);

  return true;
}

bool ToRelationshipProperty(const nonstd::optional<Relationship> &rel,
                            Property *out_prop) {
  if (!out_prop) {
    return false;
  }

  if (!rel) {
    return false;
  }

  (*out_prop) = Property(rel.value(), /* custom */ false);
  return true;
}

bool GetXformablePropertyImpl(const Xformable &xformable,
                              const std::map<std::string, Property> &props,
                              const std::string &prop_name,
                              Property *out_prop) {
  if (!out_prop) {
    return false;
  }

  if (prop_name == "xformOpOrder") {
    std::vector<value::token> toks = xformable.xformOpOrder();
    primvar::PrimVar pvar;
    pvar.set_value(toks);

    Attribute attr;
    attr.set_var(std::move(pvar));
    attr.variability() = Variability::Uniform;

    Property prop;
    prop.set_attribute(attr);
    (*out_prop) = prop;
    return true;
  }

  for (const auto &item : xformable.xformOps) {
    std::string op_name = to_string(item.op_type);
    if (!item.suffix.empty()) {
      op_name += ":" + item.suffix;
    }

    if (op_name == prop_name) {
      return XformOpToProperty(item, *out_prop);
    }
  }

  const auto it = props.find(prop_name);
  if (it == props.end()) {
    return false;
  }

  (*out_prop) = it->second;
  return true;
}

void AppendXformablePropertyNames(const Xformable &xformable,
                                  std::vector<std::string> *prop_names) {
  if (!prop_names) {
    return;
  }

  for (const auto &xop : xformable.xformOps) {
    if (xop.op_type == XformOp::OpType::ResetXformStack) {
      continue;
    }

    std::string varname = to_string(xop.op_type);
    if (!xop.suffix.empty()) {
      varname += ":" + xop.suffix;
    }

    prop_names->push_back(varname);
  }

  if (!xformable.xformOps.empty()) {
    prop_names->push_back("xformOpOrder");
  }
}

#define TO_PROPERTY(__prop_name, __v)                                         \
  if (prop_name == __prop_name) {                                             \
    if (!ToProperty(__v, *out_prop, &err)) {                                  \
      return nonstd::make_unexpected(                                         \
          fmt::format("Convert Property {} failed: {}\n", __prop_name, err)); \
    }                                                                         \
  } else

#define TO_TOKEN_PROPERTY(__prop_name, __v)                                   \
  if (prop_name == __prop_name) {                                             \
    if (!ToTokenProperty(__v, *out_prop, &err)) {                             \
      return nonstd::make_unexpected(                                         \
          fmt::format("Convert Property {} failed: {}\n", __prop_name, err)); \
    }                                                                         \
  } else

#define TO_COMPAT_PROPERTY(__canonical_name, __legacy_name, __v)              \
  if ((prop_name == __canonical_name) || (prop_name == __legacy_name)) {      \
    if (!ToProperty(__v, *out_prop, &err)) {                                  \
      return nonstd::make_unexpected(fmt::format(                             \
          "Convert Property {} failed: {}\n", __canonical_name, err));        \
    }                                                                         \
  } else

// Return false: something went wrong
// `attr_prop` true: Include Attribute property.
// `rel_prop` true: Include Relationship property.
template <typename T>
bool GetPrimPropertyNamesImpl(const T &prim,
                              std::vector<std::string> *prop_names,
                              bool attr_prop = true, bool rel_prop = true);

// Return true: Property found(`out_prop` filled)
// Return false: Property not found
// Return unexpected: Some eror happened.
template <typename T>
nonstd::expected<bool, std::string> GetPrimProperty(
    const T &prim, const std::string &prop_name, Property *out_prop);

template <typename T>
void AppendPropertyNameIfAuthored(const T &prop, const std::string &name,
                                  std::vector<std::string> *prop_names) {
  if (!prop_names) {
    return;
  }

  if (prop.authored()) {
    prop_names->push_back(name);
  }
}

template <typename T>
void AppendPropertyNamesFromCustomProps(const std::map<std::string, T> &props,
                                        std::vector<std::string> *prop_names,
                                        bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return;
  }

  for (const auto &prop : props) {
    if (prop.second.is_relationship()) {
      if (rel_prop) {
        prop_names->push_back(prop.first);
      }
    } else if (attr_prop) {
      prop_names->push_back(prop.first);
    }
  }
}

void AppendRelationshipPropertyNameIfAuthored(
    const nonstd::optional<Relationship> &rel, const std::string &name,
    std::vector<std::string> *prop_names) {
  if (!prop_names) {
    return;
  }

  if (rel) {
    prop_names->push_back(name);
  }
}

template <typename T>
nonstd::expected<bool, std::string> GetPrimvarReaderPropertyImpl(
    const UsdPrimvarReader<T> &preader, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("inputs:fallback", preader.fallback)
  TO_PROPERTY("inputs:varname", preader.varname)

  if (prop_name == "outputs:result") {
    if (auto pv = TypedTerminalAttributeToProperty(preader.result)) {
      (*out_prop) = pv.value();
    } else {
      return false;
    }
  } else {
    const auto it = preader.props.find(prop_name);
    if (it == preader.props.end()) {
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <typename T>
bool GetPrimvarReaderPropertyNamesImpl(const UsdPrimvarReader<T> &preader,
                                       std::vector<std::string> *prop_names,
                                       bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(preader.fallback, "inputs:fallback",
                                 prop_names);
    AppendPropertyNameIfAuthored(preader.varname, "inputs:varname", prop_names);
    AppendPropertyNameIfAuthored(preader.result, "outputs:result", prop_names);
  }

  AppendPropertyNamesFromCustomProps(preader.props, prop_names, attr_prop,
                                     rel_prop);

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Model &model, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  const auto it = model.props.find(prop_name);
  if (it == model.props.end()) {
    // Attribute not found.
    return false;
  }

  (*out_prop) = it->second;

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Scope &scope, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  const auto it = scope.props.find(prop_name);
  if (it == scope.props.end()) {
    // Attribute not found.
    return false;
  }

  (*out_prop) = it->second;

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Xform &xform, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  if (!GetXformablePropertyImpl(xform, xform.props, prop_name, out_prop)) {
    return false;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const GeomMesh &mesh, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("points", mesh.points)
  TO_PROPERTY("faceVertexCounts", mesh.faceVertexCounts)
  TO_PROPERTY("faceVertexIndices", mesh.faceVertexIndices)
  TO_PROPERTY("normals", mesh.normals)
  TO_PROPERTY("velocities", mesh.velocities)
  TO_PROPERTY("cornerIndices", mesh.cornerIndices)
  TO_PROPERTY("cornerSharpnesses", mesh.cornerSharpnesses)
  TO_PROPERTY("creaseIndices", mesh.creaseIndices)
  TO_PROPERTY("creaseSharpnesses", mesh.creaseSharpnesses)
  TO_PROPERTY("holeIndices", mesh.holeIndices)
  TO_TOKEN_PROPERTY("interpolateBoundary", mesh.interpolateBoundary)
  TO_TOKEN_PROPERTY("subdivisionScheme", mesh.subdivisionScheme)
  TO_TOKEN_PROPERTY("faceVaryingLinearInterpolation",
                    mesh.faceVaryingLinearInterpolation)

  if (prop_name == "skeleton") {
    if (mesh.skeleton) {
      const Relationship &rel = mesh.skeleton.value();
      (*out_prop) = Property(rel, /* custom */ false);
    } else {
      // empty
      return false;
    }
  } else {
    const auto it = mesh.props.find(prop_name);
    if (it == mesh.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const GeomSubset &subset, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  // Currently GeomSubset does not support TimeSamples and AttributeMeta

  std::string err;

  DCOUT("prop_name = " << prop_name);
  TO_PROPERTY("indices", subset.indices);
  TO_TOKEN_PROPERTY("elementType", subset.elementType);
  // TO_TOKEN_PROPERTY("familyType", subset.familyType);
  TO_PROPERTY("familyName", subset.familyName);

  if (prop_name == "material:binding") {
    if (subset.materialBinding) {
      const Relationship &rel = subset.materialBinding.value();
      (*out_prop) = Property(rel, /* custom */ false);
    } else {
      return false;
    }
  } else {
    const auto it = subset.props.find(prop_name);
    if (it == subset.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdUVTexture &tex, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("inputs:file", tex.file)
  TO_PROPERTY("inputs:st", tex.st)
  TO_PROPERTY("inputs:uv_set", tex.uv_set)
  TO_PROPERTY("inputs:uv_set_name", tex.uv_set_name)
  TO_TOKEN_PROPERTY("inputs:wrapS", tex.wrapS)
  TO_TOKEN_PROPERTY("inputs:wrapT", tex.wrapT)
  TO_PROPERTY("inputs:fallback", tex.fallback)
  TO_TOKEN_PROPERTY("inputs:sourceColorSpace", tex.sourceColorSpace)
  TO_PROPERTY("inputs:scale", tex.scale)
  TO_PROPERTY("inputs:bias", tex.bias)

  if (prop_name == "outputs:r") {
    if (auto pv = TypedTerminalAttributeToProperty(tex.outputsR)) {
      (*out_prop) = pv.value();
    } else {
      return false;
    }
  } else if (prop_name == "outputs:g") {
    if (auto pv = TypedTerminalAttributeToProperty(tex.outputsG)) {
      (*out_prop) = pv.value();
    } else {
      return false;
    }
  } else if (prop_name == "outputs:b") {
    if (auto pv = TypedTerminalAttributeToProperty(tex.outputsB)) {
      (*out_prop) = pv.value();
    } else {
      return false;
    }
  } else if (prop_name == "outputs:a") {
    if (auto pv = TypedTerminalAttributeToProperty(tex.outputsA)) {
      (*out_prop) = pv.value();
    } else {
      return false;
    }
  } else if (prop_name == "outputs:rgb") {
    if (auto pv = TypedTerminalAttributeToProperty(tex.outputsRGB)) {
      (*out_prop) = pv.value();
    } else {
      return false;
    }
  } else {
    const auto it = tex.props.find(prop_name);
    if (it == tex.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_float2 &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_float3 &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_float4 &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_float &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_int &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_string &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_vector &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_normal &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_point &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPrimvarReader_matrix &preader, const std::string &prop_name,
    Property *out_prop) {
  return GetPrimvarReaderPropertyImpl(preader, prop_name, out_prop);
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdTransform2d &tx, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_COMPAT_PROPERTY("inputs:in", "in", tx.in)
  TO_COMPAT_PROPERTY("inputs:rotation", "rotation", tx.rotation)
  TO_COMPAT_PROPERTY("inputs:scale", "scale", tx.scale)
  TO_COMPAT_PROPERTY("inputs:translation", "translation", tx.translation)

  if (prop_name == "outputs:result") {
    // Terminal attribute
    if (!tx.result.authored()) {
      // not authored
      return false;
    }

    // empty. type info only
    std::string typeName = tx.result.has_actual_type()
                               ? tx.result.get_actual_type_name()
                               : tx.result.type_name();
    (*out_prop) = Property::MakeEmptyAttrib(typeName, /* custom */ false);
  } else {
    const auto it = tx.props.find(prop_name);
    if (it == tx.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const UsdPreviewSurface &surface, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_COMPAT_PROPERTY("inputs:diffuseColor", "diffuseColor", surface.diffuseColor)
  TO_COMPAT_PROPERTY("inputs:emissiveColor", "emissiveColor", surface.emissiveColor)
  TO_COMPAT_PROPERTY("inputs:specularColor", "specularColor", surface.specularColor)
  TO_COMPAT_PROPERTY("inputs:useSpecularWorkflow", "useSpecularWorkflow",
                     surface.useSpecularWorkflow)
  TO_COMPAT_PROPERTY("inputs:metallic", "metallic", surface.metallic)
  TO_COMPAT_PROPERTY("inputs:clearcoat", "clearcoat", surface.clearcoat)
  TO_COMPAT_PROPERTY("inputs:clearcoatRoughness", "clearcoatRoughness",
                     surface.clearcoatRoughness)
  TO_COMPAT_PROPERTY("inputs:roughness", "roughness", surface.roughness)
  TO_COMPAT_PROPERTY("inputs:opacity", "opacity", surface.opacity)
  TO_COMPAT_PROPERTY("inputs:opacityThreshold", "opacityThreshold",
                     surface.opacityThreshold)
  TO_COMPAT_PROPERTY("inputs:ior", "ior", surface.ior)
  TO_COMPAT_PROPERTY("inputs:normal", "normal", surface.normal)
  TO_COMPAT_PROPERTY("inputs:displacement", "displacement",
                     surface.displacement)
  TO_COMPAT_PROPERTY("inputs:occlusion", "occlusion", surface.occlusion)

  if (prop_name == "outputs:surface") {
    if (surface.outputsSurface.authored()) {
      // empty. type info only
      (*out_prop) =
          Property::MakeEmptyAttrib(value::kToken, /* custom */ false);
    } else {
      // Not authored
      return false;
    }
  } else if (prop_name == "outputs:displacement") {
    if (surface.outputsDisplacement.authored()) {
      // empty. type info only
      (*out_prop) =
          Property::MakeEmptyAttrib(value::kToken, /* custom */ false);
    } else {
      // Not authored
      return false;
    }
  } else {
    const auto it = surface.props.find(prop_name);
    if (it == surface.props.end()) {
      // Attribute not found.
      // TODO: report warn?
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Material &material, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  if (prop_name == "outputs:surface") {
    if (material.surface.authored()) {
      Attribute attr;
      attr.set_type_name(value::TypeTraits<value::token>::type_name());
      attr.set_connections(material.surface.get_connections());
      attr.metas() = material.surface.metas();
      (*out_prop) = Property(attr, /* custom */ false);
      out_prop->set_listedit_qual(material.surface.get_listedit_qual());
    } else {
      // Not authored
      return false;
    }
  } else if (prop_name == "outputs:displacement") {
    if (material.displacement.authored()) {
      Attribute attr;
      attr.set_type_name(value::TypeTraits<value::token>::type_name());
      attr.set_connections(material.displacement.get_connections());
      attr.metas() = material.displacement.metas();
      (*out_prop) = Property(attr, /* custom */ false);
      out_prop->set_listedit_qual(material.displacement.get_listedit_qual());
    } else {
      // Not authored
      return false;
    }
  } else if (prop_name == "outputs:volume") {
    if (material.volume.authored()) {
      Attribute attr;
      attr.set_type_name(value::TypeTraits<value::token>::type_name());
      attr.set_connections(material.volume.get_connections());
      attr.metas() = material.volume.metas();
      (*out_prop) = Property(attr, /* custom */ false);
      out_prop->set_listedit_qual(material.volume.get_listedit_qual());
    } else {
      // Not authored
      return false;
    }
  } else {
    const auto it = material.props.find(prop_name);
    if (it == material.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }

  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const SkelRoot &skelroot, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("extent", skelroot.extent)
  TO_TOKEN_PROPERTY("purpose", skelroot.purpose)
  TO_TOKEN_PROPERTY("visibility", skelroot.visibility)

  if (prop_name == "proxyPrim") {
    if (!ToRelationshipProperty(skelroot.proxyPrim, out_prop)) {
      return false;
    }
  } else if (prop_name == "animationSource") {
    if (!ToRelationshipProperty(skelroot.animationSource, out_prop)) {
      return false;
    }
  } else if (prop_name == "skeleton") {
    if (!ToRelationshipProperty(skelroot.skeleton, out_prop)) {
      return false;
    }
  } else if (!GetXformablePropertyImpl(skelroot, skelroot.props, prop_name,
                                       out_prop)) {
    return false;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());

  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const BlendShape &blendshape, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("offsets", blendshape.offsets)
  TO_PROPERTY("normalOffsets", blendshape.normalOffsets)
  TO_PROPERTY("pointIndices", blendshape.pointIndices)

  {
    const auto it = blendshape.props.find(prop_name);
    if (it == blendshape.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Skeleton &skel, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("bindTransforms", skel.bindTransforms)
  TO_PROPERTY("jointNames", skel.jointNames)
  TO_PROPERTY("joints", skel.joints)
  TO_PROPERTY("restTransforms", skel.restTransforms)
  TO_PROPERTY("extent", skel.extent)
  TO_TOKEN_PROPERTY("purpose", skel.purpose)
  TO_TOKEN_PROPERTY("visibility", skel.visibility)

  if (prop_name == "proxyPrim") {
    if (!ToRelationshipProperty(skel.proxyPrim, out_prop)) {
      return false;
    }
  } else if (prop_name == "animationSource") {
    if (!ToRelationshipProperty(skel.animationSource, out_prop)) {
      return false;
    }
  } else if (!GetXformablePropertyImpl(skel, skel.props, prop_name, out_prop)) {
    return false;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const SkelAnimation &anim, const std::string &prop_name,
    Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  DCOUT("prop_name = " << prop_name);
  std::string err;

  TO_PROPERTY("blendShapes", anim.blendShapes)
  TO_PROPERTY("blendShapeWeights", anim.blendShapeWeights)
  TO_PROPERTY("joints", anim.joints)
  TO_PROPERTY("rotations", anim.rotations)
  TO_PROPERTY("scales", anim.scales)
  TO_PROPERTY("translations", anim.translations)

  {
    const auto it = anim.props.find(prop_name);
    if (it == anim.props.end()) {
      // Attribute not found.
      return false;
    }

    (*out_prop) = it->second;
  }
  DCOUT("Prop found: " << prop_name
                       << ", ty = " << out_prop->value_type_name());
  return true;
}

template <>
nonstd::expected<bool, std::string> GetPrimProperty(
    const Shader &shader, const std::string &prop_name, Property *out_prop) {
  if (!out_prop) {
    return nonstd::make_unexpected(
        "[InternalError] nullptr in output Property is not allowed.");
  }

  if (prop_name == kInfoId) {
    if (shader.info_id.empty()) {
      return false;
    }

    (*out_prop) =
        Property(Attribute::Uniform(value::token(shader.info_id)),
                 /* custom */ false);
    return true;
  }

  if (const auto preader_f = shader.value.as<UsdPrimvarReader_float>()) {
    return GetPrimProperty(*preader_f, prop_name, out_prop);
  } else if (const auto preader_f2 =
                 shader.value.as<UsdPrimvarReader_float2>()) {
    return GetPrimProperty(*preader_f2, prop_name, out_prop);
  } else if (const auto preader_f3 =
                 shader.value.as<UsdPrimvarReader_float3>()) {
    return GetPrimProperty(*preader_f3, prop_name, out_prop);
  } else if (const auto preader_f4 =
                 shader.value.as<UsdPrimvarReader_float4>()) {
    return GetPrimProperty(*preader_f4, prop_name, out_prop);
  } else if (const auto preader_i = shader.value.as<UsdPrimvarReader_int>()) {
    return GetPrimProperty(*preader_i, prop_name, out_prop);
  } else if (const auto preader_s =
                 shader.value.as<UsdPrimvarReader_string>()) {
    return GetPrimProperty(*preader_s, prop_name, out_prop);
  } else if (const auto preader_v =
                 shader.value.as<UsdPrimvarReader_vector>()) {
    return GetPrimProperty(*preader_v, prop_name, out_prop);
  } else if (const auto preader_n =
                 shader.value.as<UsdPrimvarReader_normal>()) {
    return GetPrimProperty(*preader_n, prop_name, out_prop);
  } else if (const auto preader_p =
                 shader.value.as<UsdPrimvarReader_point>()) {
    return GetPrimProperty(*preader_p, prop_name, out_prop);
  } else if (const auto preader_m =
                 shader.value.as<UsdPrimvarReader_matrix>()) {
    return GetPrimProperty(*preader_m, prop_name, out_prop);
  } else if (const auto ptx2d = shader.value.as<UsdTransform2d>()) {
    return GetPrimProperty(*ptx2d, prop_name, out_prop);
  } else if (const auto ptex = shader.value.as<UsdUVTexture>()) {
    return GetPrimProperty(*ptex, prop_name, out_prop);
  } else if (const auto psurf = shader.value.as<UsdPreviewSurface>()) {
    return GetPrimProperty(*psurf, prop_name, out_prop);
  } else {
    return nonstd::make_unexpected("TODO: " + shader.value.type_name());
  }
}

template <>
bool GetPrimPropertyNamesImpl(const Model &model,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  // TODO: Use propertyNames()
  for (const auto &prop : model.props) {
    if (prop.second.is_relationship()) {
      if (rel_prop) {
        prop_names->push_back(prop.first);
      }
    } else {  // assume attribute
      if (attr_prop) {
        prop_names->push_back(prop.first);
      }
    }
  }

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const Scope &scope,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  // TODO: Use propertyNames()
  for (const auto &prop : scope.props) {
    if (prop.second.is_relationship()) {
      if (rel_prop) {
        prop_names->push_back(prop.first);
      }
    } else {  // assume attribute
      if (attr_prop) {
        prop_names->push_back(prop.first);
      }
    }
  }

  return true;
}

bool GetGPrimPropertyNamesImpl(const GPrim *gprim,
                               std::vector<std::string> *prop_names,
                               bool attr_prop, bool rel_prop) {
  if (!gprim) {
    return false;
  }

  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    if (gprim->doubleSided.authored()) {
      prop_names->push_back("doubleSided");
    }

    if (gprim->orientation.authored()) {
      prop_names->push_back("orientation");
    }

    if (gprim->purpose.authored()) {
      prop_names->push_back("purpose");
    }

    if (gprim->extent.authored()) {
      prop_names->push_back("extent");
    }

    if (gprim->visibility.authored()) {
      prop_names->push_back("visibility");
    }

    // xformOps.
    for (const auto &xop : gprim->xformOps) {
      if (xop.op_type == XformOp::OpType::ResetXformStack) {
        // skip
        continue;
      }
      std::string varname = to_string(xop.op_type);
      if (!xop.suffix.empty()) {
        varname += ":" + xop.suffix;
      }
      prop_names->push_back(varname);
    }
  }

  if (rel_prop) {
    if (gprim->materialBinding) {
      prop_names->push_back(kMaterialBinding);
    }

    if (gprim->materialBindingPreview) {
      prop_names->push_back(kMaterialBindingPreview);
    }

    if (gprim->materialBindingFull) {
      prop_names->push_back(kMaterialBindingFull);
    }

    for (const auto &item : gprim->materialBindingMap()) {
      prop_names->push_back("material:binding:" + item.first);
    }

    for (const auto &collection : gprim->materialBindingCollectionMap()) {
      std::string purpose_name;
      if (!collection.first.empty()) {
        purpose_name = ":" + collection.first;
      }

      for (size_t i = 0; i < collection.second.size(); i++) {
        const std::string &coll_name = collection.second.keys()[i];
        std::string rel_name;
        if (collection.first.empty()) {
          rel_name = kMaterialBindingCollection + purpose_name;
        } else {
          rel_name = kMaterialBindingCollection + std::string(":") + coll_name +
                     purpose_name;
        }

        prop_names->push_back(rel_name);
      }
    }

    if (gprim->proxyPrim.authored()) {
      prop_names->push_back("proxyPrim");
    }
  }

  // other props
  for (const auto &prop : gprim->props) {
    if (prop.second.is_relationship()) {
      if (rel_prop) {
        prop_names->push_back(prop.first);
      }
    } else {  // assume attribute
      if (attr_prop) {
        prop_names->push_back(prop.first);
      }
    }
  }

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const Xform &xform,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (!GetGPrimPropertyNamesImpl(&xform, prop_names, attr_prop, rel_prop)) {
    return false;
  }

  if (attr_prop && !xform.xformOps.empty()) {
    prop_names->push_back("xformOpOrder");
  }

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const GeomMesh &mesh,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (!GetGPrimPropertyNamesImpl(&mesh, prop_names, attr_prop, rel_prop)) {
    return false;
  }

  if (attr_prop) {
    if (mesh.points.authored()) {
      prop_names->push_back("points");
    }

    if (mesh.faceVertexCounts.authored()) {
      prop_names->push_back("faceVertexCounts");
    }

    if (mesh.faceVertexIndices.authored()) {
      prop_names->push_back("faceVertexIndices");
    }

    if (mesh.normals.authored()) {
      prop_names->push_back("normals");
    }

    if (mesh.velocities.authored()) {
      prop_names->push_back("velocities");
    }

    if (mesh.cornerIndices.authored()) {
      prop_names->push_back("cornerIndices");
    }

    if (mesh.cornerSharpnesses.authored()) {
      prop_names->push_back("cornerSharpnesses");
    }

    if (mesh.creaseIndices.authored()) {
      prop_names->push_back("creaseIndices");
    }

    if (mesh.creaseSharpnesses.authored()) {
      prop_names->push_back("creaseSharpnesses");
    }

    if (mesh.holeIndices.authored()) {
      prop_names->push_back("holeIndices");
    }

    if (mesh.interpolateBoundary.authored()) {
      prop_names->push_back("interpolateBoundary");
    }

    if (mesh.subdivisionScheme.authored()) {
      prop_names->push_back("subdivisionScheme");
    }

    if (mesh.faceVaryingLinearInterpolation.authored()) {
      prop_names->push_back("faceVaryingLinearInterpolation");
    }
  }

  if (rel_prop && mesh.skeleton) {
    prop_names->push_back("skeleton");
  }

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const GeomSubset &subset,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  (void)rel_prop;

  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    if (subset.elementType.authored()) {
      prop_names->push_back("elementType");
    }

    if (subset.familyName.authored()) {
      prop_names->push_back("familyName");
    }

    if (subset.indices.authored()) {
      prop_names->push_back("indices");
    }

    DCOUT("TODO: more attrs...");
  }

  if (rel_prop && subset.materialBinding) {
    prop_names->push_back("material:binding");
  }

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const SkelRoot &skelroot,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(skelroot.extent, "extent", prop_names);
    AppendPropertyNameIfAuthored(skelroot.purpose, "purpose", prop_names);
    AppendPropertyNameIfAuthored(skelroot.visibility, "visibility",
                                 prop_names);
    AppendXformablePropertyNames(skelroot, prop_names);
  }

  if (rel_prop) {
    AppendRelationshipPropertyNameIfAuthored(skelroot.proxyPrim, "proxyPrim",
                                             prop_names);
    AppendRelationshipPropertyNameIfAuthored(skelroot.animationSource,
                                             "animationSource", prop_names);
    AppendRelationshipPropertyNameIfAuthored(skelroot.skeleton, "skeleton",
                                             prop_names);
  }

  AppendPropertyNamesFromCustomProps(skelroot.props, prop_names, attr_prop,
                                     rel_prop);
  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const BlendShape &blendshape,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(blendshape.offsets, "offsets", prop_names);
    AppendPropertyNameIfAuthored(blendshape.normalOffsets, "normalOffsets",
                                 prop_names);
    AppendPropertyNameIfAuthored(blendshape.pointIndices, "pointIndices",
                                 prop_names);
  }

  AppendPropertyNamesFromCustomProps(blendshape.props, prop_names, attr_prop,
                                     rel_prop);
  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const Skeleton &skel,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(skel.bindTransforms, "bindTransforms",
                                 prop_names);
    AppendPropertyNameIfAuthored(skel.jointNames, "jointNames", prop_names);
    AppendPropertyNameIfAuthored(skel.joints, "joints", prop_names);
    AppendPropertyNameIfAuthored(skel.restTransforms, "restTransforms",
                                 prop_names);
    AppendPropertyNameIfAuthored(skel.extent, "extent", prop_names);
    AppendPropertyNameIfAuthored(skel.purpose, "purpose", prop_names);
    AppendPropertyNameIfAuthored(skel.visibility, "visibility", prop_names);
    AppendXformablePropertyNames(skel, prop_names);
  }

  if (rel_prop) {
    AppendRelationshipPropertyNameIfAuthored(skel.proxyPrim, "proxyPrim",
                                             prop_names);
    AppendRelationshipPropertyNameIfAuthored(skel.animationSource,
                                             "animationSource", prop_names);
  }

  AppendPropertyNamesFromCustomProps(skel.props, prop_names, attr_prop,
                                     rel_prop);
  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const SkelAnimation &anim,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(anim.blendShapes, "blendShapes", prop_names);
    AppendPropertyNameIfAuthored(anim.blendShapeWeights,
                                 "blendShapeWeights", prop_names);
    AppendPropertyNameIfAuthored(anim.joints, "joints", prop_names);
    AppendPropertyNameIfAuthored(anim.rotations, "rotations", prop_names);
    AppendPropertyNameIfAuthored(anim.scales, "scales", prop_names);
    AppendPropertyNameIfAuthored(anim.translations, "translations", prop_names);
  }

  AppendPropertyNamesFromCustomProps(anim.props, prop_names, attr_prop,
                                     rel_prop);
  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const UsdUVTexture &tex,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(tex.file, "inputs:file", prop_names);
    AppendPropertyNameIfAuthored(tex.st, "inputs:st", prop_names);
    AppendPropertyNameIfAuthored(tex.uv_set, "inputs:uv_set", prop_names);
    AppendPropertyNameIfAuthored(tex.uv_set_name, "inputs:uv_set_name",
                                 prop_names);
    AppendPropertyNameIfAuthored(tex.wrapS, "inputs:wrapS", prop_names);
    AppendPropertyNameIfAuthored(tex.wrapT, "inputs:wrapT", prop_names);
    AppendPropertyNameIfAuthored(tex.fallback, "inputs:fallback", prop_names);
    AppendPropertyNameIfAuthored(tex.sourceColorSpace,
                                 "inputs:sourceColorSpace", prop_names);
    AppendPropertyNameIfAuthored(tex.scale, "inputs:scale", prop_names);
    AppendPropertyNameIfAuthored(tex.bias, "inputs:bias", prop_names);
    AppendPropertyNameIfAuthored(tex.outputsR, "outputs:r", prop_names);
    AppendPropertyNameIfAuthored(tex.outputsG, "outputs:g", prop_names);
    AppendPropertyNameIfAuthored(tex.outputsB, "outputs:b", prop_names);
    AppendPropertyNameIfAuthored(tex.outputsA, "outputs:a", prop_names);
    AppendPropertyNameIfAuthored(tex.outputsRGB, "outputs:rgb", prop_names);
  }

  AppendPropertyNamesFromCustomProps(tex.props, prop_names, attr_prop,
                                     rel_prop);

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_float &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_float2 &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_float3 &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_float4 &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_int &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_string &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_vector &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_normal &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_point &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPrimvarReader_matrix &preader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  return GetPrimvarReaderPropertyNamesImpl(preader, prop_names, attr_prop,
                                           rel_prop);
}

template <>
bool GetPrimPropertyNamesImpl(const UsdTransform2d &tx,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(tx.in, "inputs:in", prop_names);
    AppendPropertyNameIfAuthored(tx.rotation, "inputs:rotation", prop_names);
    AppendPropertyNameIfAuthored(tx.scale, "inputs:scale", prop_names);
    AppendPropertyNameIfAuthored(tx.translation, "inputs:translation",
                                 prop_names);
    AppendPropertyNameIfAuthored(tx.result, "outputs:result", prop_names);
  }

  AppendPropertyNamesFromCustomProps(tx.props, prop_names, attr_prop,
                                     rel_prop);

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const UsdPreviewSurface &surface,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(surface.diffuseColor, "inputs:diffuseColor",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.emissiveColor,
                                 "inputs:emissiveColor", prop_names);
    AppendPropertyNameIfAuthored(surface.specularColor,
                                 "inputs:specularColor", prop_names);
    AppendPropertyNameIfAuthored(surface.useSpecularWorkflow,
                                 "inputs:useSpecularWorkflow", prop_names);
    AppendPropertyNameIfAuthored(surface.metallic, "inputs:metallic",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.clearcoat, "inputs:clearcoat",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.clearcoatRoughness,
                                 "inputs:clearcoatRoughness", prop_names);
    AppendPropertyNameIfAuthored(surface.roughness, "inputs:roughness",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.opacity, "inputs:opacity",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.opacityThreshold,
                                 "inputs:opacityThreshold", prop_names);
    AppendPropertyNameIfAuthored(surface.ior, "inputs:ior", prop_names);
    AppendPropertyNameIfAuthored(surface.normal, "inputs:normal", prop_names);
    AppendPropertyNameIfAuthored(surface.displacement,
                                 "inputs:displacement", prop_names);
    AppendPropertyNameIfAuthored(surface.occlusion, "inputs:occlusion",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.outputsSurface, "outputs:surface",
                                 prop_names);
    AppendPropertyNameIfAuthored(surface.outputsDisplacement,
                                 "outputs:displacement", prop_names);
  }

  AppendPropertyNamesFromCustomProps(surface.props, prop_names, attr_prop,
                                     rel_prop);

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const Material &material,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop) {
    AppendPropertyNameIfAuthored(material.surface, "outputs:surface",
                                 prop_names);
    AppendPropertyNameIfAuthored(material.displacement,
                                 "outputs:displacement", prop_names);
    AppendPropertyNameIfAuthored(material.volume, "outputs:volume",
                                 prop_names);
  }

  AppendPropertyNamesFromCustomProps(material.props, prop_names, attr_prop,
                                     rel_prop);

  return true;
}

template <>
bool GetPrimPropertyNamesImpl(const Shader &shader,
                              std::vector<std::string> *prop_names,
                              bool attr_prop, bool rel_prop) {
  if (!prop_names) {
    return false;
  }

  if (attr_prop && !shader.info_id.empty()) {
    prop_names->push_back(kInfoId);
  }

  if (const auto preader_f = shader.value.as<UsdPrimvarReader_float>()) {
    return GetPrimPropertyNamesImpl(*preader_f, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_f2 =
                 shader.value.as<UsdPrimvarReader_float2>()) {
    return GetPrimPropertyNamesImpl(*preader_f2, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_f3 =
                 shader.value.as<UsdPrimvarReader_float3>()) {
    return GetPrimPropertyNamesImpl(*preader_f3, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_f4 =
                 shader.value.as<UsdPrimvarReader_float4>()) {
    return GetPrimPropertyNamesImpl(*preader_f4, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_i = shader.value.as<UsdPrimvarReader_int>()) {
    return GetPrimPropertyNamesImpl(*preader_i, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_s =
                 shader.value.as<UsdPrimvarReader_string>()) {
    return GetPrimPropertyNamesImpl(*preader_s, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_v =
                 shader.value.as<UsdPrimvarReader_vector>()) {
    return GetPrimPropertyNamesImpl(*preader_v, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_n =
                 shader.value.as<UsdPrimvarReader_normal>()) {
    return GetPrimPropertyNamesImpl(*preader_n, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_p =
                 shader.value.as<UsdPrimvarReader_point>()) {
    return GetPrimPropertyNamesImpl(*preader_p, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto preader_m =
                 shader.value.as<UsdPrimvarReader_matrix>()) {
    return GetPrimPropertyNamesImpl(*preader_m, prop_names, attr_prop,
                                    rel_prop);
  } else if (const auto ptx2d = shader.value.as<UsdTransform2d>()) {
    return GetPrimPropertyNamesImpl(*ptx2d, prop_names, attr_prop, rel_prop);
  } else if (const auto ptex = shader.value.as<UsdUVTexture>()) {
    return GetPrimPropertyNamesImpl(*ptex, prop_names, attr_prop, rel_prop);
  } else if (const auto psurf = shader.value.as<UsdPreviewSurface>()) {
    return GetPrimPropertyNamesImpl(*psurf, prop_names, attr_prop, rel_prop);
  }

  AppendPropertyNamesFromCustomProps(shader.props, prop_names, attr_prop,
                                     rel_prop);
  return true;
}

#undef TO_PROPERTY
#undef TO_TOKEN_PROPERTY
#undef TO_COMPAT_PROPERTY

}  // namespace

bool VisitPrims(const tinyusdz::Stage &stage, VisitPrimFunction visitor_fun,
                void *userdata, std::string *err) {
  // if `primChildren` is available, use it
  if (stage.metas().primChildren.size() == stage.root_prims().size()) {
    std::unordered_map<std::string, const Prim *, FNV1StringHash> primNameTable;
    primNameTable.reserve(stage.root_prims().size());
    for (size_t i = 0; i < stage.root_prims().size(); i++) {
      primNameTable.emplace(stage.root_prims()[i].element_name(),
                            &stage.root_prims()[i]);
    }

    for (size_t i = 0; i < stage.metas().primChildren.size(); i++) {
      value::token nameTok = stage.metas().primChildren[i];
      const auto it = primNameTable.find(nameTok.str());
      if (it != primNameTable.end()) {
        const Path root_abs_path("/" + nameTok.str(), "");
        if (!VisitPrimsIterative(root_abs_path, *it->second, 0, visitor_fun,
                                 userdata, err)) {
          return false;
        }
      } else {
        if (err) {
          (*err) += fmt::format(
              "Prim name `{}` in root Layer's `primChildren` metadatum not "
              "found in Layer root.",
              nameTok.str());
        }
        return false;
      }
    }

  } else {
    for (const auto &root : stage.root_prims()) {
      const Path root_abs_path("/" + root.element_name(), /* prop part */ "");
      if (!VisitPrimsIterative(root_abs_path, root, /* root level */ 0,
                               visitor_fun, userdata, err)) {
        return false;
      }
    }
  }

  return true;
}

bool GetProperty(const tinyusdz::Prim &prim, const std::string &attr_name,
                 Property *out_prop, std::string *err) {
#define GET_PRIM_PROPERTY(__ty)                                         \
  if (prim.is<__ty>()) {                                                \
    auto ret = GetPrimProperty(*prim.as<__ty>(), attr_name, out_prop);  \
    if (ret) {                                                          \
      if (!ret.value()) {                                               \
        PUSH_ERROR_AND_RETURN(                                          \
            fmt::format("Attribute `{}` does not exist in Prim {}({})", \
                        attr_name, prim.element_path().prim_part(),     \
                        value::TypeTraits<__ty>::type_name()));         \
      }                                                                 \
    } else {                                                            \
      PUSH_ERROR_AND_RETURN(ret.error());                               \
    }                                                                   \
  } else

  GET_PRIM_PROPERTY(Model)
  GET_PRIM_PROPERTY(Xform)
  GET_PRIM_PROPERTY(Scope)
  GET_PRIM_PROPERTY(GeomMesh)
  GET_PRIM_PROPERTY(GeomSubset)
  GET_PRIM_PROPERTY(Shader)
  GET_PRIM_PROPERTY(Material)
  GET_PRIM_PROPERTY(SkelRoot)
  GET_PRIM_PROPERTY(BlendShape)
  GET_PRIM_PROPERTY(Skeleton)
  GET_PRIM_PROPERTY(SkelAnimation) {
    PUSH_ERROR_AND_RETURN("TODO: Prim type " << prim.type_name());
  }

#undef GET_PRIM_PROPERTY

  return true;
}

bool GetPropertyNames(const tinyusdz::Prim &prim,
                      std::vector<std::string> *out_prop_names,
                      std::string *err) {
#define GET_PRIM_PROPERTY_NAMES(__ty)                                     \
  if (prim.is<__ty>()) {                                                  \
    auto ret = GetPrimPropertyNamesImpl(*prim.as<__ty>(), out_prop_names, \
                                        true, true);                      \
    if (!ret) {                                                           \
      PUSH_ERROR_AND_RETURN(                                              \
          fmt::format("Failed to list up Property names of Prim type {}", \
                      value::TypeTraits<__ty>::type_name()));             \
    }                                                                     \
  } else

  GET_PRIM_PROPERTY_NAMES(Model)
  GET_PRIM_PROPERTY_NAMES(Xform)
  GET_PRIM_PROPERTY_NAMES(Scope)
  GET_PRIM_PROPERTY_NAMES(GeomMesh)
  GET_PRIM_PROPERTY_NAMES(GeomSubset)
  GET_PRIM_PROPERTY_NAMES(Shader)
  GET_PRIM_PROPERTY_NAMES(Material)
  GET_PRIM_PROPERTY_NAMES(SkelRoot)
  GET_PRIM_PROPERTY_NAMES(BlendShape)
  GET_PRIM_PROPERTY_NAMES(Skeleton)
  GET_PRIM_PROPERTY_NAMES(SkelAnimation)
  {
    PUSH_ERROR_AND_RETURN("TODO: Prim type " << prim.type_name());
  }

#undef GET_PRIM_PROPERTY_NAMES

  return true;
}

bool GetAttributeNames(const tinyusdz::Prim &prim,
                       std::vector<std::string> *out_attr_names,
                       std::string *err) {
#define GET_PRIM_ATTRIBUTE_NAMES(__ty)                                       \
  if (prim.is<__ty>()) {                                                     \
    auto ret = GetPrimPropertyNamesImpl(*prim.as<__ty>(), out_attr_names,    \
                                        true, false);                        \
    if (!ret) {                                                              \
      PUSH_ERROR_AND_RETURN(                                                 \
          fmt::format("Failed to list up Attribute names of Prim type {}",   \
                      value::TypeTraits<__ty>::type_name()));                \
    }                                                                        \
  } else

  GET_PRIM_ATTRIBUTE_NAMES(Model)
  GET_PRIM_ATTRIBUTE_NAMES(Xform)
  GET_PRIM_ATTRIBUTE_NAMES(Scope)
  GET_PRIM_ATTRIBUTE_NAMES(GeomMesh)
  GET_PRIM_ATTRIBUTE_NAMES(GeomSubset)
  GET_PRIM_ATTRIBUTE_NAMES(Shader)
  GET_PRIM_ATTRIBUTE_NAMES(Material)
  GET_PRIM_ATTRIBUTE_NAMES(SkelRoot)
  GET_PRIM_ATTRIBUTE_NAMES(BlendShape)
  GET_PRIM_ATTRIBUTE_NAMES(Skeleton)
  GET_PRIM_ATTRIBUTE_NAMES(SkelAnimation) {
    PUSH_ERROR_AND_RETURN("TODO: Prim type " << prim.type_name());
  }

#undef GET_PRIM_ATTRIBUTE_NAMES

  return true;
}

bool GetRelationshipNames(const tinyusdz::Prim &prim,
                          std::vector<std::string> *out_rel_names,
                          std::string *err) {
#define GET_PRIM_RELATIONSHIP_NAMES(__ty)                                 \
  if (prim.is<__ty>()) {                                                  \
    auto ret = GetPrimPropertyNamesImpl(*prim.as<__ty>(), out_rel_names,  \
                                        false, true);                     \
    if (!ret) {                                                           \
      PUSH_ERROR_AND_RETURN(                                              \
          fmt::format("Failed to list up Property names of Prim type {}", \
                      value::TypeTraits<__ty>::type_name()));             \
    }                                                                     \
  } else

  GET_PRIM_RELATIONSHIP_NAMES(Model)
  GET_PRIM_RELATIONSHIP_NAMES(Xform)
  GET_PRIM_RELATIONSHIP_NAMES(Scope)
  GET_PRIM_RELATIONSHIP_NAMES(GeomMesh)
  GET_PRIM_RELATIONSHIP_NAMES(GeomSubset)
  GET_PRIM_RELATIONSHIP_NAMES(Shader)
  GET_PRIM_RELATIONSHIP_NAMES(Material)
  GET_PRIM_RELATIONSHIP_NAMES(SkelRoot)
  GET_PRIM_RELATIONSHIP_NAMES(BlendShape)
  GET_PRIM_RELATIONSHIP_NAMES(Skeleton)
  GET_PRIM_RELATIONSHIP_NAMES(SkelAnimation)
  {
    PUSH_ERROR_AND_RETURN("TODO: Prim type " << prim.type_name());
  }

#undef GET_PRIM_PROPERTY_NAMES

  return true;
}

bool GetAttribute(const tinyusdz::Prim &prim, const std::string &attr_name,
                  Attribute *out_attr, std::string *err) {
  if (!out_attr) {
    PUSH_ERROR_AND_RETURN("`out_attr` argument is nullptr.");
  }

  // First lookup as Property, then check if its Attribute
  Property prop;
  if (!GetProperty(prim, attr_name, &prop, err)) {
    return false;
  }

  if (prop.is_attribute()) {
    (*out_attr) = std::move(prop.get_attribute());
    return true;
  }

  PUSH_ERROR_AND_RETURN(fmt::format("{} is not a Attribute.", attr_name));
}

bool GetRelationship(const tinyusdz::Prim &prim, const std::string &rel_name,
                     Relationship *out_rel, std::string *err) {
  if (!out_rel) {
    PUSH_ERROR_AND_RETURN("`out_rel` argument is nullptr.");
  }

  // First lookup as Property, then check if its Relationship
  Property prop;
  if (!GetProperty(prim, rel_name, &prop, err)) {
    return false;
  }

  if (prop.is_relationship()) {
    (*out_rel) = std::move(prop.get_relationship());
    return true;
  }

  PUSH_ERROR_AND_RETURN(fmt::format("{} is not a Relationship.", rel_name));

  return true;
}

bool ListSceneNames(const tinyusdz::Prim &root,
                    std::vector<std::pair<bool, std::string>> *sceneNames) {
  if (!sceneNames) {
    return false;
  }

  bool has_sceneLibrary = false;
  if (root.metas().has_kind()) {
    if (root.metas().get_kind_enum() == Kind::SceneLibrary) {
      // ok
      has_sceneLibrary = true;
    }
  }

  if (!has_sceneLibrary) {
    return false;
  }

  for (const Prim &child : root.children()) {
    if (!ListSceneNamesRec(child, /* depth */ 0, sceneNames)) {
      return false;
    }
  }

  return true;
}

namespace {

// Helper to compute XformNode properties from a Prim
static void ComputeXformNodeProperties(
    const Prim *prim, const Path &parent_abs_path,
    const value::matrix4d &parent_world_mat, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp,
    XformNode &node) {

  node.element_name = prim->element_name();
  node.absolute_path = parent_abs_path.AppendPrim(prim->element_name());
  node.prim_id = prim->prim_id();
  node.prim = prim;  // Assume Prim's address does not change.

  DCOUT(prim->element_name() << ": IsXformablePrim" << IsXformablePrim(*prim));

  if (IsXformablePrim(*prim)) {
    bool resetXformStack{false};

    value::matrix4d localMat =
        GetLocalTransform(*prim, &resetXformStack, t, tinterp);
    DCOUT("local mat = " << localMat);

    node.has_resetXformStack() = resetXformStack;

    value::matrix4d m;

    if (resetXformStack) {
      // Ignore parent Xform.
      m = localMat;
    } else {
      // matrix is row-major, so local first
      m = localMat * parent_world_mat;
    }

    node.set_parent_world_matrix(parent_world_mat);
    node.set_local_matrix(localMat);
    node.set_world_matrix(m);
    node.has_xform() = true;
  } else {
    DCOUT("Not xformable");
    node.has_xform() = false;
    node.has_resetXformStack() = false;
    node.set_parent_world_matrix(parent_world_mat);
    node.set_world_matrix(parent_world_mat);
    node.set_local_matrix(value::matrix4d::identity());
  }
}

// Iterative version of BuildXformNodeFromStage using explicit stack
bool BuildXformNodeFromStageIterative(
    const tinyusdz::Stage &stage, const Path &initial_parent_path, const Prim *root_prim,
    XformNode *nodeOut, /* out */
    value::matrix4d rootMat, const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp,
    size_t max_iter = kMaxDefaultTraversalLimit) {

  (void)stage;  // Currently unused

  if (!nodeOut) {
    return false;
  }

  // Stack entry for iterative processing
  struct StackEntry {
    const Prim *prim;
    Path parent_path;
    value::matrix4d parent_world_mat;
    size_t child_idx;
    XformNode node;

    StackEntry(const Prim *p, Path pp, value::matrix4d pwm)
        : prim(p), parent_path(std::move(pp)), parent_world_mat(pwm), child_idx(0) {}
  };

  StackVector<StackEntry, 4> stack;
  stack.reserve(64);

  // Initialize with root prim
  stack.emplace_back(root_prim, initial_parent_path, rootMat);

  // Compute root node properties
  ComputeXformNodeProperties(root_prim, initial_parent_path, rootMat, t, tinterp,
                             stack.back().node);

  size_t iter = 0;
  while (!stack.empty()) {
    if (iter++ >= max_iter) break;
    StackEntry &curr = stack.back();
    const auto &children = curr.prim->children();

    if (curr.child_idx < children.size()) {
      // Push next child
      const Prim &child = children[curr.child_idx];
      curr.child_idx++;

      stack.emplace_back(&child, curr.node.absolute_path, curr.node.get_world_matrix());

      // Compute new child's properties
      StackEntry &new_entry = stack.back();
      ComputeXformNodeProperties(new_entry.prim, new_entry.parent_path,
                                 new_entry.parent_world_mat, t, tinterp,
                                 new_entry.node);
    } else {
      // All children processed
      if (stack.size() > 1) {
        // Move completed node to parent's children
        XformNode completed = std::move(curr.node);
        stack.pop_back();
        // Note: parent pointer will point to stack.back().node, which will be
        // moved later. This preserves the same behavior as the recursive version.
        completed.parent = &stack.back().node;
        stack.back().node.children.emplace_back(std::move(completed));
      } else {
        // Root node - copy to output
        *nodeOut = std::move(curr.node);
        stack.pop_back();
      }
    }
  }

  return true;
}

// Iterative version of DumpXformNode using explicit stack
std::string DumpXformNodeIterative(const XformNode &root,
                                   size_t max_iter = kMaxDefaultTraversalLimit) {
  std::stringstream ss;

  // Stack entry: (node pointer, indent, child index, closing_brace_pending)
  // child_idx == SIZE_MAX means we haven't printed this node yet
  struct StackEntry {
    const XformNode *node;
    uint32_t indent;
    size_t child_idx;
    StackEntry(const XformNode *n, uint32_t i)
        : node(n), indent(i), child_idx(SIZE_MAX) {}
  };

  StackVector<StackEntry, 4> stack;
  stack.reserve(64);
  stack.emplace_back(&root, 0);

  size_t iter = 0;
  while (!stack.empty()) {
    if (iter++ >= max_iter) break;
    StackEntry &entry = stack.back();

    if (entry.child_idx == SIZE_MAX) {
      // First visit: print node info
      ss << pprint::Indent(entry.indent) << "Prim name: " << entry.node->element_name
         << " PrimID: " << entry.node->prim_id << " (Path " << entry.node->absolute_path
         << ") Xformable? " << entry.node->has_xform() << " resetXformStack? "
         << entry.node->has_resetXformStack() << " {\n";
      ss << pprint::Indent(entry.indent + 1)
         << "parent_world: " << entry.node->get_parent_world_matrix() << "\n";
      ss << pprint::Indent(entry.indent + 1) << "world: " << entry.node->get_world_matrix()
         << "\n";
      ss << pprint::Indent(entry.indent + 1) << "local: " << entry.node->get_local_matrix()
         << "\n";
      entry.child_idx = 0;
    }

    // Process children
    const auto &children = entry.node->children;
    if (entry.child_idx < children.size()) {
      size_t idx = entry.child_idx++;
      stack.emplace_back(&children[idx], entry.indent + 1);
    } else {
      // All children processed, print closing brace and pop
      ss << pprint::Indent(entry.indent + 1) << "}\n";
      stack.pop_back();
    }
  }

  return ss.str();
}

}  // namespace local

bool BuildXformNodeFromStage(
    const tinyusdz::Stage &stage, XformNode *rootNode, /* out */
    const double t,
    const tinyusdz::value::TimeSampleInterpolationType tinterp) {
  if (!rootNode) {
    return false;
  }

  XformNode stage_root;
  stage_root.element_name = "";  // Stage root element name is empty.
  stage_root.absolute_path = Path("/", "");
  stage_root.has_xform() = false;
  stage_root.parent = nullptr;
  stage_root.prim = nullptr;  // No prim for stage root.
  stage_root.prim_id = -1;
  stage_root.has_resetXformStack() = false;
  stage_root.set_parent_world_matrix(value::matrix4d::identity());
  stage_root.set_world_matrix(value::matrix4d::identity());
  stage_root.set_local_matrix(value::matrix4d::identity());

  for (const auto &root : stage.root_prims()) {
    XformNode node;

    value::matrix4d rootMat{value::matrix4d::identity()};

    if (!BuildXformNodeFromStageIterative(stage, stage_root.absolute_path, &root,
                                          &node, rootMat, t, tinterp)) {
      return false;
    }

    stage_root.children.emplace_back(std::move(node));
  }

  (*rootNode) = stage_root;

  return true;
}

std::string DumpXformNode(const XformNode &node) {
  return DumpXformNodeIterative(node);
}

template <typename T>
bool PrimToPrimSpecImpl(const T &p, PrimSpec &ps, std::string *err);

template <>
bool PrimToPrimSpecImpl(const Model &p, PrimSpec &ps, std::string *err) {
  (void)err;

  ps.name() = p.name;
  ps.specifier() = p.spec;

  ps.props() = p.props;
  ps.metas() = p.meta;

  // TODO: variantSet
  // ps.variantSets

  return true;
}

template <>
bool PrimToPrimSpecImpl(const Xform &p, PrimSpec &ps, std::string *err) {
  (void)err;

  ps.name() = p.name;
  ps.specifier() = p.spec;

  ps.props() = p.props;

  // TODO..
  std::vector<value::token> toks;
  Attribute xformOpOrderAttr;
  xformOpOrderAttr.set_value(std::move(toks));
  ps.props().emplace("xformOpOrder",
                     Property(xformOpOrderAttr, /* custom */ false));

  ps.metas() = p.meta;

  // TODO: variantSet
  // ps.variantSets

  return true;
}

bool PrimToPrimSpec(const Prim &prim, PrimSpec &ps, std::string *err) {
#define TO_PRIMSPEC(__ty)                                   \
  if (prim.as<__ty>()) {                                    \
    return PrimToPrimSpecImpl(*(prim.as<__ty>()), ps, err); \
  } else

  TO_PRIMSPEC(Model) {
    if (err) {
      (*err) +=
          "Unsupported/unimplemented Prim type: " + prim.prim_type_name() +
          "\n";
    }
    return false;
  }

#undef TO_PRIMSPEC
}

bool ShaderToPrimSpec(const UsdTransform2d &node, PrimSpec &ps,
                      std::string *warn, std::string *err) {
  (void)warn;

#define TO_PROPERTY(__prop_name, __v)                                    \
  {                                                                      \
    Property prop;                                                       \
    if (!ToProperty(__v, prop, err)) {                                   \
      PUSH_ERROR_AND_RETURN(                                             \
          fmt::format("Convert {} to Property failed.\n", __prop_name)); \
    }                                                                    \
    ps.props()[__prop_name] = prop;                                      \
  }

  // inputs
  TO_PROPERTY("inputs:in", node.in)
  TO_PROPERTY("inputs:rotation", node.rotation)
  TO_PROPERTY("inputs:scale", node.scale)
  TO_PROPERTY("inputs:translation", node.translation)

  // outputs
  if (auto pv = TypedTerminalAttributeToProperty(node.result)) {
    ps.props()["outputs:result"] = pv.value();
  }

  for (auto prop : node.props) {
    ps.props()[prop.first] = prop.second;
  }

  ps.props()[kInfoId] =
      Property(Attribute::Uniform(value::token(kUsdTransform2d)));
  ps.metas() = node.metas();
  ps.name() = node.name;
  ps.specifier() = node.spec;

  return true;
}

std::vector<const GeomSubset *> GetGeomSubsets(
    const tinyusdz::Stage &stage, const tinyusdz::Path &prim_path,
    const tinyusdz::value::token &familyName, bool prim_must_be_geommesh) {
  std::vector<const GeomSubset *> result;

  const Prim *pprim{nullptr};
  if (!stage.find_prim_at_path(prim_path, pprim)) {
    return result;
  }

  if (!pprim) {
    return result;
  }

  if (prim_must_be_geommesh && !pprim->is<GeomMesh>()) {
    return result;
  }

  // Only account for child Prims.
  for (const auto &p : pprim->children()) {
    if (auto pv = p.as<GeomSubset>()) {
      if (familyName.valid()) {
        if (pv->familyName.authored()) {
          if (pv->familyName.get_value().has_value()) {
            const value::token tok = pv->familyName.get_value().value();
            if (familyName.str() == tok.str()) {
              result.push_back(pv);
            }
          } else {
            // connection attr or value block?
            // skip adding this GeomSubset.
          }
        } else {
          result.push_back(pv);
        }
      } else {
        result.push_back(pv);
      }
    }
  }

  return result;
}

std::vector<const GeomSubset *> GetGeomSubsetChildren(
    const tinyusdz::Prim &prim, const tinyusdz::value::token &familyName,
    bool prim_must_be_geommesh) {
  std::vector<const GeomSubset *> result;

  if (prim_must_be_geommesh && !prim.is<GeomMesh>()) {
    return result;
  }

  // Only account for child Prims.
  for (const auto &p : prim.children()) {
    if (auto pv = p.as<GeomSubset>()) {
      if (familyName.valid()) {
        if (pv->familyName.authored()) {
          if (pv->familyName.get_value().has_value()) {
            const value::token tok = pv->familyName.get_value().value();
            if (familyName.str() == tok.str()) {
              result.push_back(pv);
            }
          } else {
            // connection attr or value block?
            // skip adding this GeomSubset.
          }
        } else {
          result.push_back(pv);
        }
      } else {
        result.push_back(pv);
      }
    }
  }

  return result;
}


bool GetCollection(const Prim &prim, const Collection **dst) {
  if (!dst) {
    return false;
  }

  auto fn = [dst](const Collection *coll) {
    (*dst) = coll;
    return true;
  };

  bool ret = ApplyToCollection(prim, fn);

  return ret;
}

bool IsPathIncluded(const CollectionMembershipQuery &query, const Stage &stage,
                    const Path &abs_path,
                    const CollectionInstance::ExpansionRule expansionRule) {
  (void)query;
  (void)stage;
  (void)expansionRule;

  DCOUT("TODO");

  if (!abs_path.is_valid()) {
    return false;
  }

  if (abs_path.is_root_path()) {
    return true;
  }

  return false;
}

std::vector<std::pair<std::string, const tinyusdz::BlendShape *>>
GetBlendShapes(const tinyusdz::Stage &stage, const tinyusdz::Prim &prim,
               std::string *err) {
  std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> dst;

  auto *pmesh = prim.as<GeomMesh>();
  if (!pmesh) {
    if (err) {
      (*err) += "Prim must be GeomMesh.\n";
    }
    return dst;
  }

  //
  // BlendShape Prim may not be a child of GeomMesh. So need to search Prim in
  // Stage
  //
  if (pmesh->blendShapes.authored() && pmesh->blendShapeTargets.has_value()) {
    // TODO: connection?
    std::vector<value::token> blendShapeNames;

    if (!pmesh->blendShapes.get_value(&blendShapeNames)) {
      if (err) {
        (*err) += "Failed to get `skel:blendShapes` attribute.\n";
      }
      return dst;
    }

    if (pmesh->blendShapeTargets.value().is_path()) {
      if (blendShapeNames.size() != 1) {
        if (err) {
          (*err) +=
              "Array size mismatch with `skel:blendShapes` and "
              "`skel:blendShapeTargets`.\n";
        }
        return dst;
      }

      const Path &targetPath = pmesh->blendShapeTargets.value().targetPath;
      const Prim *bsprim{nullptr};
      if (!stage.find_prim_at_path(targetPath, bsprim, err)) {
        return dst;
      }
      if (!bsprim) {
        if (err) {
          (*err) += "Internal error. BlendShape Prim is nullptr.\n";
        }
        return dst;
      }

      if (const auto *bs = bsprim->as<BlendShape>()) {
        dst.push_back(std::make_pair(blendShapeNames[0].str(), bs));
      } else {
        if (err) {
          (*err) += fmt::format("{} is not BlendShape Prim.\n",
                                targetPath.full_path_name());
        }
        return dst;
      }

    } else if (pmesh->blendShapeTargets.value().is_pathvector()) {
      if (blendShapeNames.size() !=
          pmesh->blendShapeTargets.value().targetPathVector.size()) {
        if (err) {
          (*err) +=
              "Array size mismatch with `skel:blendShapes` and "
              "`skel:blendShapeTargets`.\n";
        }
        return dst;
      }
    } else {
      if (err) {
        (*err) +=
            "Invalid or unsupported definition of `skel:blendShapeTargets` "
            "relationship.\n";
      }
      return dst;
    }

    for (size_t i = 0;
         i < pmesh->blendShapeTargets.value().targetPathVector.size(); i++) {
      const Path &targetPath =
          pmesh->blendShapeTargets.value().targetPathVector[i];
      const Prim *bsprim{nullptr};
      if (!stage.find_prim_at_path(targetPath, bsprim, err)) {
        return dst;
      }
      if (!bsprim) {
        if (err) {
          (*err) += "Internal error. BlendShape Prim is nullptr.";
        }
        return dst;
      }

      if (const auto *bs = bsprim->as<BlendShape>()) {
        dst.push_back(std::make_pair(blendShapeNames[i].str(), bs));
      } else {
        if (err) {
          (*err) += fmt::format("{} is not BlendShape Prim.",
                                targetPath.full_path_name());
        }
        return dst;
      }
    }
  }

  return dst;
}

bool GetGeomPrimvar(const Stage &stage, const GPrim *gprim,
                    const std::string &varname, GeomPrimvar *out_primvar,
                    std::string *err) {
  if (!out_primvar) {
    PUSH_ERROR_AND_RETURN("Output GeomPrimvar is nullptr.");
  }

  if (!gprim) {
    PUSH_ERROR_AND_RETURN("Input `gprim` arg is nullptr.");
  }

  GeomPrimvar primvar;

  constexpr auto kPrimvars = "primvars:";
  constexpr auto kIndices = ":indices";

  std::string primvar_name = kPrimvars + varname;

  const auto it = gprim->props.find(primvar_name);
  if (it == gprim->props.end()) {
    return false;
  }

  // The order of Attribute value evaluation:
  // - default or timesamples
  // - connection


  if (it->second.is_attribute()) {
    const Attribute &attr = it->second.get_attribute();

    if (attr.is_connection()) { // attribute only contains 'connection'
      // follow targetPath to get Attribute 
      Attribute terminal_attr;
      bool ret = tydra::GetTerminalAttribute(stage, attr, primvar_name,
                                             &terminal_attr, err);
      if (!ret) {
        return false;
      }

      primvar.set_value(terminal_attr);

    } else {
      // default, timeSamples
      primvar.set_value(attr);
    }

    primvar.set_name(varname);

    if (attr.metas().has_interpolation()) {
      primvar.set_interpolation(attr.metas().get_interpolation_enum());
    }
    if (attr.metas().has_elementSize()) {
      primvar.set_elementSize(attr.metas().get_elementSize());
    }
    if (attr.metas().has_unauthoredValuesIndex()) {
      primvar.set_unauthoredValuesIndex(attr.metas().get_unauthoredValuesIndex());
    }
    // TODO: copy other attribute metas?

  } else {
    PUSH_ERROR_AND_RETURN(
        fmt::format("{} is not Attribute(Maybe Relationship?).", primvar_name));
  }

  // has indices?
  std::string index_name = primvar_name + kIndices;
  const auto indexIt = gprim->props.find(index_name);

  // Primvar indices are only relevant for non-constant interpolation modes
  bool constant_interpolation = primvar.get_interpolation() == tinyusdz::Interpolation::Constant;

  if (indexIt != gprim->props.end() && !constant_interpolation) {
    if (indexIt->second.is_attribute()) {
      const Attribute &indexAttr = indexIt->second.get_attribute();

      if (!(primvar.get_attribute().type_id() & value::TYPE_ID_1D_ARRAY_BIT)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Indexed GeomPrimVar with scalar PrimVar Attribute is "
                        "not supported. PrimVar name: {}",
                        primvar_name));
      }

      if (indexAttr.is_connection()) { // attribute only contains 'connection'
        // follow targetPath to get Attribute 
        Attribute terminal_indexAttr;
        bool ret = tydra::GetTerminalAttribute(stage, indexAttr, index_name,
                                               &terminal_indexAttr, err);
        if (!ret) {
          return false;
        }

        if (!terminal_indexAttr.has_value() && !terminal_indexAttr.has_timesamples()) {
          PUSH_ERROR_AND_RETURN("[Internal Error] Invalid Terminal Index Attribute. Terminal Index Attribute does not have `default` or timesamples value.");
        }

        if (terminal_indexAttr.has_timesamples()) {
          const auto &ts = terminal_indexAttr.get_var().ts_raw();
          TypedTimeSamples<std::vector<int32_t>> tss;
          if (!tss.from_timesamples(ts)) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "Index Attribute seems not an timesamples with int[] type: {}",
                index_name));
          }
        
          primvar.set_timesampled_indices(tss);
        }

        if (terminal_indexAttr.has_value()) {

          // TODO: Support uint[]?
          std::vector<int32_t> indices;
          if (!terminal_indexAttr.get_value(&indices)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Index Attribute is not int[] type. Got {}",
                            indexAttr.type_name()));
          }

          primvar.set_default_indices(indices);

        }
      
      } else if (indexAttr.is_blocked()) {
        // Value blocked. e.g. `float2[] primvars:st:indices = None`
        // We can simply skip reading indices.
      } else {

        if (!indexAttr.has_value() && !indexAttr.has_timesamples()) {
          PUSH_ERROR_AND_RETURN("[Internal Error] Invalid Index Attribute. Index Attribute does not have `default` or timesamples value.");
        }

        if (indexAttr.has_value()) {
          // Check if int[] type.
          // TODO: Support uint[]?
          std::vector<int32_t> indices;
          if (!indexAttr.get_value(&indices)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("Index Attribute is not int[] type. Got {}",
                            indexAttr.type_name()));
          }


          primvar.set_default_indices(indices);
        }

        if (indexAttr.has_timesamples()) {
          const auto &ts = indexAttr.get_var().ts_raw();
          TypedTimeSamples<std::vector<int32_t>> tss;
          if (!tss.from_timesamples(ts)) {
            PUSH_ERROR_AND_RETURN(fmt::format("Index Attribute seems not an timesamples with int[] type: {}", index_name));
          }
        
          primvar.set_timesampled_indices(tss);
        }

      }
    } else {
      // indices are optional, so ok to skip it.
    }
  }

  (*out_primvar) = primvar;

  return true;
}

bool FindPrimvarWithInheritance(const Stage &stage, const Path &prim_path,
    const std::string &primvar_name, GeomPrimvar *out,
    std::string *err) {
  if (!out) {
    if (err) (*err) = "Output GeomPrimvar is nullptr.\n";
    return false;
  }

  if (!prim_path.is_valid() || !prim_path.is_absolute_path()) {
    if (err) (*err) = "Input path must be a valid absolute path.\n";
    return false;
  }

  Path current = prim_path;

  while (true) {
    auto ret = stage.GetPrimAtPath(current);
    if (!ret) {
      break;
    }

    const Prim *prim = ret.value();
    const GPrim *gprim = nullptr;

    // Try to get GPrim from prim. Use value::TypeTraits approach.
    // GPrim is the base for geometry types, so we need to check the actual type.
    if (auto p = prim->as<GeomMesh>()) {
      gprim = p;
    } else if (auto p2 = prim->as<GeomPoints>()) {
      gprim = p2;
    } else if (auto p3 = prim->as<GeomBasisCurves>()) {
      gprim = p3;
    } else if (auto p4 = prim->as<GeomNurbsCurves>()) {
      gprim = p4;
    } else if (auto p5 = prim->as<GeomSphere>()) {
      gprim = p5;
    } else if (auto p6 = prim->as<GeomCube>()) {
      gprim = p6;
    } else if (auto p7 = prim->as<GeomCone>()) {
      gprim = p7;
    } else if (auto p8 = prim->as<GeomCylinder>()) {
      gprim = p8;
    } else if (auto p9 = prim->as<GeomCapsule>()) {
      gprim = p9;
    } else if (auto p10 = prim->as<Xform>()) {
      gprim = p10;
    }

    if (gprim) {
      GeomPrimvar primvar;
      if (GetGeomPrimvar(stage, gprim, primvar_name, &primvar)) {
        *out = primvar;
        return true;
      }
    }

    if (current.is_root_prim() || current.is_root_path()) {
      break;
    }
    current = current.get_parent_prim_path();
  }

  return false;
}

namespace {

//
// visited_paths : To prevent circular referencing of attribute connection.
//
bool GetTerminalAttributeImpl(const tinyusdz::Stage &stage,
                              const tinyusdz::Prim &prim,
                              const std::string &attr_name, Attribute *value,
                              std::string *err,
                              std::unordered_set<std::string, FNV1StringHash>
                                  &visited_paths) {
  DCOUT("Prim : " << prim.element_path().element_name() << "("
                  << prim.type_name() << ") attr_name " << attr_name);

  Property prop;
  if (!GetProperty(prim, attr_name, &prop, err)) {
    return false;
  }

  if (prop.is_attribute_connection()) {
    // Follow connection target Path(singple targetPath only).
    std::vector<Path> pv = prop.get_attribute().connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection."));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (!visited_paths.emplace(abs_path).second) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }

      return GetTerminalAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                      value, err, visited_paths);

    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }
  } else if (prop.is_relationship()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Property `{}` is a Relation.", attr_name));
  } else if (prop.is_empty()) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "Attribute `{}` is a define-only attribute(no value assigned).",
        attr_name));
  } else if (prop.is_attribute()) {
    (*value) = prop.get_attribute();

  } else {
    // ???
    PUSH_ERROR_AND_RETURN(
        fmt::format("[InternalError] Invalid Attribute `{}`.", attr_name));
  }

  return true;
}

}  // namespace

bool GetTerminalAttribute(const tinyusdz::Stage &stage,
                          const tinyusdz::Attribute &attr,
                          const std::string &attr_name, Attribute *value,
                          std::string *err) {
  if (!value) {
    PUSH_ERROR_AND_RETURN("`value` arg is nullptr.");
  }

  std::unordered_set<std::string, FNV1StringHash> visited_paths;
  visited_paths.reserve(16);

  if (attr.is_connection()) {
    std::vector<Path> pv = attr.connections();
    if (pv.empty()) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Connection targetPath is empty for Attribute {}.", attr_name));
    }

    if (pv.size() > 1) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Multiple targetPaths assigned to .connection."));
    }

    auto target = pv[0];

    std::string targetPrimPath = target.prim_part();
    std::string targetPrimPropName = target.prop_part();
    DCOUT("connection targetPath : " << target << "(Prim: " << targetPrimPath
                                     << ", Prop: " << targetPrimPropName
                                     << ")");

    auto targetPrimRet =
        stage.GetPrimAtPath(Path(targetPrimPath, /* prop */ ""));
    if (targetPrimRet) {
      // Follow the connetion
      const Prim *targetPrim = targetPrimRet.value();

      std::string abs_path = target.full_path_name();

      if (!visited_paths.emplace(abs_path).second) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Circular referencing detected. connectionTargetPath = {}",
            to_string(target)));
      }

      return GetTerminalAttributeImpl(stage, *targetPrim, targetPrimPropName,
                                      value, err, visited_paths);

    } else {
      PUSH_ERROR_AND_RETURN(targetPrimRet.error());
    }

  } else {
    (*value) = attr;
    return true;
  }

  return false;
}

namespace detail {

static bool BuildSkelHierarchyImpl(
    /* inout */ SkelNode &rootNode,
    const std::vector<std::vector<size_t>> &childrenMap,
    const std::vector<value::token> &joints,
    const std::vector<value::token> &jointNames,
    const std::vector<value::matrix4d> &bindTransforms,
    const std::vector<value::matrix4d> &restTransforms,
    std::string *err = nullptr) {
  // Iterative traversal using explicit stack to avoid stack overflow on deep hierarchies
  struct StackEntry {
    SkelNode *parent;
    size_t child_list_idx;  // index into childrenMap[parentIdx]
  };

  // Guard: max iterations = total number of joints (each joint is visited exactly once)
  // plus one pop per stack frame. A reasonable upper bound is 2 * joints.size() + 1.
  const size_t kMaxIter = joints.size() * 2 + 1;
  size_t iter = 0;

  std::vector<StackEntry> stack;
  stack.push_back({&rootNode, 0});

  while (!stack.empty()) {
    if (iter++ >= kMaxIter) {
      if (err) {
        (*err) += "BuildSkelHierarchyImpl: exceeded maximum iteration count. "
                  "Possible cycle in skeleton hierarchy.";
      }
      return false;
    }

    auto &top = stack.back();
    size_t parentIdx = size_t(top.parent->joint_id);

    if (parentIdx >= childrenMap.size() ||
        top.child_list_idx >= childrenMap[parentIdx].size()) {
      stack.pop_back();
      continue;
    }

    size_t i = childrenMap[parentIdx][top.child_list_idx];
    top.child_list_idx++;

    DCOUT("add joint " << i << "(parent = " << top.parent->joint_id << ")");
    SkelNode node;
    node.joint_id = int(i);
    node.joint_path = joints[i].str();
    node.joint_name = jointNames[i].str();
    node.bind_transform = bindTransforms[i];
    node.rest_transform = restTransforms[i];

    top.parent->children.emplace_back(std::move(node));

    // Push newly added child to process its children
    SkelNode *childPtr = &top.parent->children.back();
    stack.push_back({childPtr, 0});
  }

  return true;
}

}  // namespace detail

bool BuildSkelHierarchy(const Skeleton &skel, SkelNode &dst, std::string *err) {
  if (!skel.joints.authored()) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "Skeleton.joints attribute is not authored: {}", skel.name));
  }

  std::vector<value::token> joints;
  if (!skel.joints.get_value(&joints)) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Failed to get Skeleton.joints attribute: {}", skel.name));
  }

  if (joints.empty()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Skeleton.joints attribute is empty: {}", skel.name));
  }

  std::vector<value::token> jointNames;

  if (skel.jointNames.authored()) {
    if (!skel.jointNames.get_value(&jointNames)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to get Skeleton.jointNames attribute: {}", skel.name));
    }

    if (joints.size() != jointNames.size()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Skeleton.joints.size {} must be equal to "
                      "Skeleton.jointNames.size {}: {}",
                      joints.size(), jointNames.size(), skel.name));
    }
  } else {
    // Use joints 
    jointNames.resize(joints.size());
    for (size_t i = 0; i < joints.size(); i++) {
      jointNames[i] = joints[i];
    }
  }


  // Track whether restTransforms is authored (for fallback computation later)
  bool restTransformsAuthored = skel.restTransforms.authored();
  bool bindTransformsAuthored = skel.bindTransforms.authored();

  // Read bindTransforms first (needed for potential restTransforms fallback)
  std::vector<value::matrix4d> bindTransforms;
  if (bindTransformsAuthored) {
    DCOUT("bindTransforms is authored");
    if (!skel.bindTransforms.get_value(&bindTransforms)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to get Skeleton.bindTransforms attribute: {}", skel.name));
    }
    DCOUT("bindTransforms.size() = " << bindTransforms.size());
    if (bindTransforms.size() > 0) {
      DCOUT("bindTransforms[0] = " << bindTransforms[0]);
    }
  } else {
    DCOUT("bindTransforms is NOT authored - using identity");
    // Use identity when bindTransforms is not authored
    bindTransforms.assign(joints.size(), value::matrix4d::identity());
  }

  if (joints.size() != bindTransforms.size()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Skeleton.joints.size {} must be equal to "
                    "Skeleton.bindTransforms.size {}: {}",
                    joints.size(), bindTransforms.size(), skel.name));
  }

  std::vector<value::matrix4d> restTransforms;
  if (restTransformsAuthored) {
    DCOUT("restTransforms is authored");
    if (!skel.restTransforms.get_value(&restTransforms)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to get Skeleton.restTransforms attribute: {}", skel.name));
    }
    DCOUT("restTransforms.size() = " << restTransforms.size());
    if (restTransforms.size() > 0) {
      DCOUT("restTransforms[0] = " << restTransforms[0]);
    }
  } else if (bindTransformsAuthored) {
    // Fallback: compute restTransforms (local) from bindTransforms (world)
    // restTransform[i] = inverse(bindTransform[parent[i]]) * bindTransform[i]
    // For root joints (no parent), restTransform = bindTransform
    DCOUT("restTransforms is NOT authored - computing from bindTransforms");
  } else {
    DCOUT("restTransforms is NOT authored - using identity");
    // Neither authored: use identity matrices
    restTransforms.assign(joints.size(), value::matrix4d::identity());
  }

  // Build topology once (used for both restTransforms fallback and hierarchy construction)
  std::vector<int> parentJointIds;
  if (!BuildSkelTopology(joints, parentJointIds, err)) {
    return false;
  }

  // Compute restTransforms from bindTransforms if needed (uses parentJointIds built above)
  if (!restTransformsAuthored && bindTransformsAuthored) {
    restTransforms.resize(joints.size());
    for (size_t i = 0; i < joints.size(); i++) {
      int parentIdx = parentJointIds[i];
      if (parentIdx < 0) {
        // Root joint: use bindTransform directly (world space becomes local space)
        restTransforms[i] = bindTransforms[i];
      } else {
        // Child joint: compute local transform from world transforms
        // localTransform = inverse(parentWorldTransform) * childWorldTransform
        value::matrix4d parentInverse;
        if (!inverse(bindTransforms[size_t(parentIdx)], parentInverse)) {
          DCOUT("Failed to compute inverse of parent bindTransform, using identity for restTransform");
          restTransforms[i] = value::matrix4d::identity();
        } else {
          restTransforms[i] = parentInverse * bindTransforms[i];
        }
      }
    }
    DCOUT("Computed restTransforms from bindTransforms");
  }

  if (joints.size() != restTransforms.size()) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Skeleton.joints.size {} must be equal to "
                    "Skeleton.restTransforms.size {}: {}",
                    joints.size(), restTransforms.size(), skel.name));
  }

  // Just in case. Chek if topology is single-rooted.
  auto nroots = std::count_if(parentJointIds.begin(), parentJointIds.end(),
                              [](int x) { return x == -1; });

  if (nroots == 0) {
    PUSH_ERROR_AND_RETURN(fmt::format(
        "Invalid Skel topology. No root joint found: {}", skel.name));
  }

  if (nroots != 1) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("Invalid Skel topology. Topology must be single-rooted, "
                    "but it has {} roots: {}",
                    nroots, skel.name));
  }

  // Build parent -> children map for O(n) hierarchy construction
  std::vector<std::vector<size_t>> childrenMap(joints.size());
  size_t rootIdx = 0;
  for (size_t i = 0; i < parentJointIds.size(); i++) {
    int parentId = parentJointIds[i];
    if (parentId < 0) {
      rootIdx = i;
    } else {
      childrenMap[size_t(parentId)].push_back(i);
    }
  }

  SkelNode root;
  root.joint_name = jointNames[rootIdx].str();
  root.joint_path = joints[rootIdx].str();
  root.joint_id = int(rootIdx);
  root.bind_transform = bindTransforms[rootIdx];
  root.rest_transform = restTransforms[rootIdx];

  DCOUT("parentJointIds = " << parentJointIds);

  // Construct hierarchy from children map.
  if (!detail::BuildSkelHierarchyImpl(root, childrenMap, joints, jointNames,
                                      bindTransforms, restTransforms,
                                      err)) {
    return false;
  }

  dst = root;

  return true;
}

namespace {

size_t CountSkelNodesIterative(const SkelNode &root) {
  size_t count = 0;
  StackVector<const SkelNode *, 4> stack;
  stack.reserve(64);
  stack.emplace_back(&root);

  while (!stack.empty()) {
    const SkelNode *node = stack.back();
    stack.pop_back();
    ++count;
    for (const auto &child : node->children) {
      stack.emplace_back(&child);
    }
  }

  return count;
}

// Iterative version of BuildSkelNameToIndexMap using explicit stack
void BuildSkelNameToIndexMapIterative(const SkelNode &root,
                                      SkelNameToIndexMap &m,
                                      size_t max_iter = kMaxDefaultTraversalLimit) {
  // Stack for DFS traversal
  StackVector<std::pair<const SkelNode *, size_t>, 4> stack;
  stack.reserve(64);
  stack.emplace_back(&root, 0);

  size_t iter = 0;
  while (!stack.empty()) {
    if (iter++ >= max_iter) break;
    std::pair<const SkelNode *, size_t> &entry = stack.back();
    const SkelNode *node = entry.first;
    size_t &child_idx = entry.second;

    // Process current node on first visit (child_idx == 0)
    if (child_idx == 0) {
      if (node->joint_id >= 0) {
        auto add_key = [&](const std::string &key) {
          if (key.empty()) {
            return;
          }
          // Keep the first authored mapping if duplicates appear.
          m.emplace(key, node->joint_id);
        };

        add_key(node->joint_name);
        add_key(node->joint_path);

        // Also register absolute/relative variants to handle mixed token forms
        // (e.g. "root/hip" vs "/root/hip") between Skeleton and SkelAnimation data.
        if (!node->joint_path.empty()) {
          if (node->joint_path[0] == '/') {
            add_key(node->joint_path.substr(1));
          } else {
            add_key("/" + node->joint_path);
          }
        }
      }
    }

    // Process children
    if (child_idx < node->children.size()) {
      size_t idx = child_idx++;
      stack.emplace_back(&node->children[idx], 0);
    } else {
      stack.pop_back();
    }
  }
}

} // namespace

SkelNameToIndexMap BuildSkelNameToIndexMap(const SkelHierarchy &skel) {

  SkelNameToIndexMap m;
  m.reserve(CountSkelNodesIterative(skel.root_node) * 3);

  BuildSkelNameToIndexMapIterative(skel.root_node, m);

  return m;
}

//
// Skeletal mesh extent computation
//

bool ComputeJointsExtent(
    const std::vector<value::matrix4d> &jointXforms,
    Extent *extent,
    float padding,
    const value::matrix4d *rootXform) {

  if (!extent) {
    return false;
  }

  if (jointXforms.empty()) {
    return false;
  }

  Extent e;  // initialized to +inf/-inf

  for (const auto &xf : jointXforms) {
    // Extract translation (pivot) from joint transform.
    // Row-major layout: translation is in row 3.
    value::float3 pivot;
    pivot[0] = float(xf.m[3][0]);
    pivot[1] = float(xf.m[3][1]);
    pivot[2] = float(xf.m[3][2]);

    if (rootXform) {
      // Transform pivot through rootXform: pivot * rootXform
      double px = double(pivot[0]);
      double py = double(pivot[1]);
      double pz = double(pivot[2]);

      double rx = px * rootXform->m[0][0] + py * rootXform->m[1][0] + pz * rootXform->m[2][0] + rootXform->m[3][0];
      double ry = px * rootXform->m[0][1] + py * rootXform->m[1][1] + pz * rootXform->m[2][1] + rootXform->m[3][1];
      double rz = px * rootXform->m[0][2] + py * rootXform->m[1][2] + pz * rootXform->m[2][2] + rootXform->m[3][2];
      double rw = px * rootXform->m[0][3] + py * rootXform->m[1][3] + pz * rootXform->m[2][3] + rootXform->m[3][3];

      if (std::abs(rw) > 1e-10) {
        rx /= rw;
        ry /= rw;
        rz /= rw;
      }

      pivot[0] = float(rx);
      pivot[1] = float(ry);
      pivot[2] = float(rz);
    }

    e.union_with(pivot);
  }

  if (padding > 0.0f) {
    e.lower[0] -= padding;
    e.lower[1] -= padding;
    e.lower[2] -= padding;
    e.upper[0] += padding;
    e.upper[1] += padding;
    e.upper[2] += padding;
  }

  *extent = e;
  return true;
}

float ComputeSkinnedExtentPadding(
    const std::vector<value::matrix4d> &restJointXforms,
    const Extent &meshRestExtent,
    const value::matrix4d &geomBindTransform) {

  if (restJointXforms.empty() || !meshRestExtent.is_valid()) {
    return 0.0f;
  }

  // Compute pivot extent from rest-pose joints
  Extent jointExtent;
  if (!ComputeJointsExtent(restJointXforms, &jointExtent)) {
    return 0.0f;
  }

  // Transform mesh rest extent corners by geomBindTransform
  // We need the 8 corners of the AABB transformed, then compute the new AABB
  const value::float3 &lo = meshRestExtent.lower;
  const value::float3 &hi = meshRestExtent.upper;

  Extent transformedMeshExtent;

  for (int i = 0; i < 8; i++) {
    float cx = (i & 1) ? hi[0] : lo[0];
    float cy = (i & 2) ? hi[1] : lo[1];
    float cz = (i & 4) ? hi[2] : lo[2];

    double px = double(cx);
    double py = double(cy);
    double pz = double(cz);

    // point * matrix (row-major, row-vector convention)
    double rx = px * geomBindTransform.m[0][0] + py * geomBindTransform.m[1][0] + pz * geomBindTransform.m[2][0] + geomBindTransform.m[3][0];
    double ry = px * geomBindTransform.m[0][1] + py * geomBindTransform.m[1][1] + pz * geomBindTransform.m[2][1] + geomBindTransform.m[3][1];
    double rz = px * geomBindTransform.m[0][2] + py * geomBindTransform.m[1][2] + pz * geomBindTransform.m[2][2] + geomBindTransform.m[3][2];
    double rw = px * geomBindTransform.m[0][3] + py * geomBindTransform.m[1][3] + pz * geomBindTransform.m[2][3] + geomBindTransform.m[3][3];

    if (std::abs(rw) > 1e-10) {
      rx /= rw;
      ry /= rw;
      rz /= rw;
    }

    value::float3 tp;
    tp[0] = float(rx);
    tp[1] = float(ry);
    tp[2] = float(rz);
    transformedMeshExtent.union_with(tp);
  }

  // Padding = max distance that the mesh extent exceeds the joint extent
  // on any axis in any direction
  float padding = 0.0f;

  for (size_t i = 0; i < 3; i++) {
    float diffLo = jointExtent.lower[i] - transformedMeshExtent.lower[i];
    float diffHi = transformedMeshExtent.upper[i] - jointExtent.upper[i];

    padding = (std::max)(padding, (std::max)(diffLo, 0.0f));
    padding = (std::max)(padding, (std::max)(diffHi, 0.0f));
  }

  return padding;
}

bool SkinPointsLBS(
    const std::vector<value::point3f> &restPoints,
    const value::matrix4d &geomBindTransform,
    const std::vector<value::matrix4d> &jointXforms,
    const std::vector<int> &jointIndices,
    const std::vector<float> &jointWeights,
    int numInfluencesPerPoint,
    std::vector<value::point3f> *skinnedPoints,
    std::string *err) {

  if (!skinnedPoints) {
    if (err) { *err = "skinnedPoints is null."; }
    return false;
  }

  if (numInfluencesPerPoint < 1) {
    if (err) { *err = "numInfluencesPerPoint must be >= 1."; }
    return false;
  }

  size_t numPoints = restPoints.size();
  size_t expectedSize = numPoints * size_t(numInfluencesPerPoint);

  if (jointIndices.size() != expectedSize) {
    if (err) {
      *err = "jointIndices size mismatch: expected " +
             std::to_string(expectedSize) + ", got " +
             std::to_string(jointIndices.size()) + ".";
    }
    return false;
  }

  if (jointWeights.size() != expectedSize) {
    if (err) {
      *err = "jointWeights size mismatch: expected " +
             std::to_string(expectedSize) + ", got " +
             std::to_string(jointWeights.size()) + ".";
    }
    return false;
  }

  int numJoints = int(jointXforms.size());

  skinnedPoints->resize(numPoints);

  for (size_t pi = 0; pi < numPoints; pi++) {
    // Transform rest point into skeleton space via geomBindTransform
    const value::point3f &rp = restPoints[pi];
    double px = double(rp.x);
    double py = double(rp.y);
    double pz = double(rp.z);

    double sx = px * geomBindTransform.m[0][0] + py * geomBindTransform.m[1][0] + pz * geomBindTransform.m[2][0] + geomBindTransform.m[3][0];
    double sy = px * geomBindTransform.m[0][1] + py * geomBindTransform.m[1][1] + pz * geomBindTransform.m[2][1] + geomBindTransform.m[3][1];
    double sz = px * geomBindTransform.m[0][2] + py * geomBindTransform.m[1][2] + pz * geomBindTransform.m[2][2] + geomBindTransform.m[3][2];
    double sw = px * geomBindTransform.m[0][3] + py * geomBindTransform.m[1][3] + pz * geomBindTransform.m[2][3] + geomBindTransform.m[3][3];

    if (std::abs(sw) > 1e-10) {
      sx /= sw;
      sy /= sw;
      sz /= sw;
    }

    // Accumulate weighted joint transforms
    double outx = 0.0, outy = 0.0, outz = 0.0;

    size_t base = pi * size_t(numInfluencesPerPoint);
    for (int ji = 0; ji < numInfluencesPerPoint; ji++) {
      int idx = jointIndices[base + size_t(ji)];
      float w = jointWeights[base + size_t(ji)];

      if (w == 0.0f || idx < 0 || idx >= numJoints) {
        continue;
      }

      const value::matrix4d &jx = jointXforms[size_t(idx)];

      // skelPoint * jointXform
      double tx = sx * jx.m[0][0] + sy * jx.m[1][0] + sz * jx.m[2][0] + jx.m[3][0];
      double ty = sx * jx.m[0][1] + sy * jx.m[1][1] + sz * jx.m[2][1] + jx.m[3][1];
      double tz = sx * jx.m[0][2] + sy * jx.m[1][2] + sz * jx.m[2][2] + jx.m[3][2];

      outx += double(w) * tx;
      outy += double(w) * ty;
      outz += double(w) * tz;
    }

    (*skinnedPoints)[pi].x = float(outx);
    (*skinnedPoints)[pi].y = float(outy);
    (*skinnedPoints)[pi].z = float(outz);
  }

  return true;
}

bool ComputeSkinnedMeshExtent(
    const std::vector<value::matrix4d> &jointXforms,
    const std::vector<value::matrix4d> &restJointXforms,
    const Extent &meshRestExtent,
    const value::matrix4d &geomBindTransform,
    Extent *extent,
    const value::matrix4d *rootXform) {

  if (!extent) {
    return false;
  }

  float padding = ComputeSkinnedExtentPadding(
      restJointXforms, meshRestExtent, geomBindTransform);

  return ComputeJointsExtent(jointXforms, extent, padding, rootXform);
}

//
// Skeleton transform utilities (ported from OpenUSD UsdSkelUtils)
//

bool ConcatJointTransforms(
    const std::vector<int> &topology,
    const std::vector<value::matrix4d> &localXforms,
    std::vector<value::matrix4d> *worldXforms,
    const value::matrix4d *rootXform) {

  if (!worldXforms) {
    return false;
  }

  size_t numJoints = topology.size();
  if (localXforms.size() != numJoints) {
    return false;
  }

  worldXforms->resize(numJoints);

  // Topology guarantees parent index < child index, so a single forward pass
  // computes all world-space transforms.
  for (size_t i = 0; i < numJoints; i++) {
    int parent = topology[i];
    if (parent < 0) {
      // Root joint
      if (rootXform) {
        (*worldXforms)[i] = localXforms[i] * (*rootXform);
      } else {
        (*worldXforms)[i] = localXforms[i];
      }
    } else if (size_t(parent) < numJoints) {
      (*worldXforms)[i] = localXforms[i] * (*worldXforms)[size_t(parent)];
    } else {
      // Invalid parent - treat as root
      (*worldXforms)[i] = localXforms[i];
    }
  }

  return true;
}

bool ComputeJointLocalTransforms(
    const std::vector<int> &topology,
    const std::vector<value::matrix4d> &worldXforms,
    std::vector<value::matrix4d> *localXforms,
    const value::matrix4d *inverseRootXform) {

  if (!localXforms) {
    return false;
  }

  size_t numJoints = topology.size();
  if (worldXforms.size() != numJoints) {
    return false;
  }

  localXforms->resize(numJoints);

  for (size_t i = 0; i < numJoints; i++) {
    int parent = topology[i];
    if (parent < 0) {
      // Root joint
      if (inverseRootXform) {
        (*localXforms)[i] = worldXforms[i] * (*inverseRootXform);
      } else {
        (*localXforms)[i] = worldXforms[i];
      }
    } else if (size_t(parent) < numJoints) {
      value::matrix4d parentInv = inverse(worldXforms[size_t(parent)]);
      (*localXforms)[i] = worldXforms[i] * parentInv;
    } else {
      (*localXforms)[i] = worldXforms[i];
    }
  }

  return true;
}

value::matrix4d SkelMakeTransform(
    const value::float3 &translation,
    const value::quatf &rotation,
    const value::half3 &scale) {

  // Build rotation matrix from quaternion
  value::matrix4d rotMat = to_matrix(rotation);

  // Apply scale to the rotation matrix (upper-left 3x3)
  double sx = double(half_to_float(scale[0]));
  double sy = double(half_to_float(scale[1]));
  double sz = double(half_to_float(scale[2]));

  rotMat.m[0][0] *= sx; rotMat.m[0][1] *= sx; rotMat.m[0][2] *= sx;
  rotMat.m[1][0] *= sy; rotMat.m[1][1] *= sy; rotMat.m[1][2] *= sy;
  rotMat.m[2][0] *= sz; rotMat.m[2][1] *= sz; rotMat.m[2][2] *= sz;

  // Set translation
  rotMat.m[3][0] = double(translation[0]);
  rotMat.m[3][1] = double(translation[1]);
  rotMat.m[3][2] = double(translation[2]);

  return rotMat;
}

bool SkinNormalsLBS(
    const std::vector<value::normal3f> &restNormals,
    const value::matrix4d &geomBindTransform,
    const std::vector<value::matrix4d> &jointXforms,
    const std::vector<int> &jointIndices,
    const std::vector<float> &jointWeights,
    int numInfluencesPerPoint,
    std::vector<value::normal3f> *skinnedNormals,
    std::string *err) {

  if (!skinnedNormals) {
    if (err) { *err = "skinnedNormals is null."; }
    return false;
  }

  if (numInfluencesPerPoint < 1) {
    if (err) { *err = "numInfluencesPerPoint must be >= 1."; }
    return false;
  }

  size_t numPoints = restNormals.size();
  size_t expectedSize = numPoints * size_t(numInfluencesPerPoint);

  if (jointIndices.size() != expectedSize) {
    if (err) {
      *err = "jointIndices size mismatch: expected " +
             std::to_string(expectedSize) + ", got " +
             std::to_string(jointIndices.size()) + ".";
    }
    return false;
  }

  if (jointWeights.size() != expectedSize) {
    if (err) {
      *err = "jointWeights size mismatch: expected " +
             std::to_string(expectedSize) + ", got " +
             std::to_string(jointWeights.size()) + ".";
    }
    return false;
  }

  int numJoints = int(jointXforms.size());

  skinnedNormals->resize(numPoints);

  // For normals, we use inverse-transpose of the skinning matrix.
  // Since we accumulate the weighted skinning matrix per vertex first,
  // we can compute its inverse-transpose at the end. But for LBS where
  // weights sum to 1, we can skin normals with the upper-left 3x3
  // (direction only, no translation) and then renormalize.

  for (size_t pi = 0; pi < numPoints; pi++) {
    const value::normal3f &rn = restNormals[pi];
    double nx = double(rn[0]);
    double ny = double(rn[1]);
    double nz = double(rn[2]);

    // Transform normal into skeleton space via geomBindTransform (direction only)
    double snx = nx * geomBindTransform.m[0][0] + ny * geomBindTransform.m[1][0] + nz * geomBindTransform.m[2][0];
    double sny = nx * geomBindTransform.m[0][1] + ny * geomBindTransform.m[1][1] + nz * geomBindTransform.m[2][1];
    double snz = nx * geomBindTransform.m[0][2] + ny * geomBindTransform.m[1][2] + nz * geomBindTransform.m[2][2];

    // Accumulate weighted joint transforms (direction only)
    double outx = 0.0, outy = 0.0, outz = 0.0;

    size_t base = pi * size_t(numInfluencesPerPoint);
    for (int ji = 0; ji < numInfluencesPerPoint; ji++) {
      int idx = jointIndices[base + size_t(ji)];
      float w = jointWeights[base + size_t(ji)];

      if (w == 0.0f || idx < 0 || idx >= numJoints) {
        continue;
      }

      const value::matrix4d &jx = jointXforms[size_t(idx)];

      // skelNormal * jointXform (3x3 only for directions)
      double tx = snx * jx.m[0][0] + sny * jx.m[1][0] + snz * jx.m[2][0];
      double ty = snx * jx.m[0][1] + sny * jx.m[1][1] + snz * jx.m[2][1];
      double tz = snx * jx.m[0][2] + sny * jx.m[1][2] + snz * jx.m[2][2];

      outx += double(w) * tx;
      outy += double(w) * ty;
      outz += double(w) * tz;
    }

    // Renormalize
    double len = std::sqrt(outx * outx + outy * outy + outz * outz);
    if (len > 1e-10) {
      outx /= len;
      outy /= len;
      outz /= len;
    }

    (*skinnedNormals)[pi][0] = float(outx);
    (*skinnedNormals)[pi][1] = float(outy);
    (*skinnedNormals)[pi][2] = float(outz);
  }

  return true;
}

bool ExpandConstantInfluencesToVarying(
    const std::vector<int> &indices,
    const std::vector<float> &weights,
    size_t numVertices,
    std::vector<int> *expandedIndices,
    std::vector<float> *expandedWeights) {

  if (!expandedIndices || !expandedWeights) {
    return false;
  }

  if (indices.size() != weights.size()) {
    return false;
  }

  size_t numInfluences = indices.size();
  if (numInfluences == 0 || numVertices == 0) {
    expandedIndices->clear();
    expandedWeights->clear();
    return true;
  }

  size_t totalSize = numVertices * numInfluences;
  expandedIndices->resize(totalSize);
  expandedWeights->resize(totalSize);

  for (size_t v = 0; v < numVertices; v++) {
    size_t offset = v * numInfluences;
    for (size_t i = 0; i < numInfluences; i++) {
      (*expandedIndices)[offset + i] = indices[i];
      (*expandedWeights)[offset + i] = weights[i];
    }
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
