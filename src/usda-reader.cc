// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// USDA reader
// TODO:
//   - [ ] Use common base code for Reconstruct**** with USDC reader

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stack>

#include "ascii-parser.hh"
#if defined(__wasi__)
#else
#include <thread>
#endif
#include <vector>

#include "usda-reader.hh"

//
#if !defined(TINYUSDZ_DISABLE_MODULE_USDA_READER)

//

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external

//#include "ryu/ryu.h"
//#include "ryu/ryu_parse.h"

#include "fast_float/fast_float.h"
#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

//

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

// Tentative
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

#include "io-util.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
//#include "simple-type-reflection.hh"
#include "primvar.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "usdObj.hh"
#include "value-pprint.hh"
#include "value-type.hh"

#if 0
#define PUSH_PARSER_ERROR_AND_RETURN() \
  do {                                 \
    std::ostringstream ss;             \
    ss << _parser.GetError();          \
    _err += ss.str();                  \
    return false;                      \
  } while (0)
#endif

// s = std::string
#define PUSH_ERROR_AND_RETURN(s)                                   \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _err += ss.str();                                              \
    return false;                                                  \
  } while (0)

#define PUSH_WARN(s)                                               \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _err += ss.str();                                              \
  } while (0)

#if !defined(TINYUSDZ_PRODUCTION_BUILD)
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x)                                               \
  do {                                                         \
    std::cout << __FILE__ << ":" << __func__ << ":"            \
              << std::to_string(__LINE__) << " " << x << "\n"; \
  } while (false)
#else
#define DCOUT(x)
#endif

namespace tinyusdz {

namespace usda {

namespace {

// TODO: Move to prim-types.hh?

template<typename T>
struct PrimTypeTrait;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

#define DEFINE_PRIM_TYPE(__dty, __name, __tyid) \
  template<> \
  struct PrimTypeTrait<__dty> { \
    using primt_type = __dty; \
    static constexpr uint32_t type_id = __tyid; \
    static constexpr auto prim_type_name = __name; \
  }

DEFINE_PRIM_TYPE(Xform, "Xform", value::TYPE_ID_GEOM_XFORM);
DEFINE_PRIM_TYPE(GeomMesh, "Mesh", value::TYPE_ID_GEOM_MESH);
DEFINE_PRIM_TYPE(GeomSphere, "Sphere", value::TYPE_ID_GEOM_SPHERE);
DEFINE_PRIM_TYPE(GeomCube, "Cube", value::TYPE_ID_GEOM_CUBE);
DEFINE_PRIM_TYPE(GeomCone, "Cone", value::TYPE_ID_GEOM_CONE);
DEFINE_PRIM_TYPE(GeomCapsule, "Capsule", value::TYPE_ID_GEOM_CAPSULE);
DEFINE_PRIM_TYPE(GeomCylinder, "Cylinder", value::TYPE_ID_GEOM_CYLINDER);
DEFINE_PRIM_TYPE(GeomBasisCurves, "BasisCurves", value::TYPE_ID_GEOM_BASIS_CURVES);
DEFINE_PRIM_TYPE(GeomSubset, "GeomSubset", value::TYPE_ID_GEOM_GEOMSUBSET);
DEFINE_PRIM_TYPE(LuxSphereLight, "SphereLight", value::TYPE_ID_LUX_SPHERE);
DEFINE_PRIM_TYPE(LuxDomeLight, "DomeLight", value::TYPE_ID_LUX_DOME);
DEFINE_PRIM_TYPE(Material, "Material", value::TYPE_ID_MATERIAL);
DEFINE_PRIM_TYPE(Shader, "Shader", value::TYPE_ID_SHADER);
DEFINE_PRIM_TYPE(SkelRoot, "SkelRoot", value::TYPE_ID_SKEL_ROOT);
DEFINE_PRIM_TYPE(Skeleton, "Skeleton", value::TYPE_ID_SKELETON);
DEFINE_PRIM_TYPE(Scope, "Scope", value::TYPE_ID_SCOPE);
DEFINE_PRIM_TYPE(GeomCamera, "Camera", value::TYPE_ID_GEOM_CAMERA);

DEFINE_PRIM_TYPE(GPrim, "GPrim", value::TYPE_ID_GPRIM);

//DEFINE_PRIM_TYPE(PreviewSurface, "PreviewSurface", value::TYPE_ID_IMAGING_PREVIEWSURFACE);
//DEFINE_PRIM_TYPE(UVTexture, "UVTexture", value::TYPE_ID_IMAGING_UVTEXTURE);
//DEFINE_PRIM_TYPE(PrimvarReader_float2, "PrimvarReaderUVTexture", value::TYPE_ID_IMAGING_UVTEXTURE);

#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace

class VariableDef {
 public:
  std::string type;
  std::string name;

  VariableDef() = default;

  VariableDef(const std::string &t, const std::string &n) : type(t), name(n) {}

  VariableDef(const VariableDef &rhs) = default;

  VariableDef &operator=(const VariableDef &rhs) {
    type = rhs.type;
    name = rhs.name;

    return *this;
  }
};

namespace {

using ReferenceList = std::vector<std::pair<ListEditQual, Reference>>;

#if 0
// Extract array of References from Variable.
ReferenceList GetReferences(
    const std::tuple<ListEditQual, value::any_value> &_var) {
  ReferenceList result;

  ListEditQual qual = std::get<0>(_var);

  auto var = std::get<1>(_var);

  SDCOUT << "GetReferences. var.name = " << var.name << "\n";

  if (var.IsArray()) {
    DCOUT("IsArray");
    auto parr = var.as_array();
    if (parr) {
      DCOUT("parr");
      for (const auto &v : parr->values) {
        DCOUT("Maybe Value");
        if (v.IsValue()) {
          DCOUT("Maybe Reference");
          if (auto pref = nonstd::get_if<Reference>(v.as_value())) {
            DCOUT("Got it");
            result.push_back({qual, *pref});
          }
        }
      }
    }
  } else if (var.IsValue()) {
    DCOUT("IsValue");
    if (auto pv = var.as_value()) {
      DCOUT("Maybe Reference");
      if (auto pas = nonstd::get_if<Reference>(pv)) {
        DCOUT("Got it");
        result.push_back({qual, *pas});
      }
    }
  } else {
    DCOUT("Unknown var type: " + Variable::type_name(var));
  }

  return result;
}
#endif

}  // namespace

inline bool hasConnect(const std::string &str) {
  return endsWith(str, ".connect");
}

inline bool hasInputs(const std::string &str) {
  return startsWith(str, "inputs:");
}

inline bool hasOutputs(const std::string &str) {
  return startsWith(str, "outputs:");
}

class USDAReader::Impl {
 private:
  HighLevelScene scene_;

 public:
  Impl(StreamReader *sr) { _parser.SetStream(sr); }

  // Return the flag if the .usda is read from `references`
  bool IsReferenced() { return _referenced; }

  // Return the flag if the .usda is read from `subLayers`
  bool IsSubLayered() { return _sub_layered; }

  // Return the flag if the .usda is read from `payload`
  bool IsPayloaded() { return _payloaded; }

  // Return true if the .udsa is read in the top layer(stage)
  bool IsToplevel() {
    return !IsReferenced() && !IsSubLayered() && !IsPayloaded();
  }

  void SetBaseDir(const std::string &str) { _base_dir = str; }

  std::string GetCurrentPath() {
    if (_path_stack.empty()) {
      return "/";
    }

    return _path_stack.top();
  }

  bool PathStackDepth() { return _path_stack.size(); }

  void PushPath(const std::string &p) { _path_stack.push(p); }

  void PopPath() {
    if (!_path_stack.empty()) {
      _path_stack.pop();
    }
  }

#if 0  // TODO: remove
    if (prim_type.empty()) {
      if (IsToplevel()) {
        if (references.size()) {
          // Infer prim type from referenced asset.

          if (references.size() > 1) {
            LOG_ERROR("TODO: multiple references\n");
          }

          auto it = references.begin();
          const Reference &ref = it->second;
          std::string filepath = ref.asset_path;

          // usdOBJ?
          if (endsWith(filepath, ".obj")) {
            prim_type = "geom_mesh";
          } else {
            if (!io::IsAbsPath(filepath)) {
              filepath = io::JoinPath(_base_dir, ref.asset_path);
            }

            if (_reference_cache.count(filepath)) {
              LOG_ERROR("TODO: Use cached info");
            }

            DCOUT("Reading references: " + filepath);

            std::vector<uint8_t> data;
            std::string err;
            if (!io::ReadWholeFile(&data, &err, filepath,
                                   /* max_filesize */ 0)) {
              PUSH_ERROR_AND_RETURN("Failed to read file: " + filepath);
            }

            tinyusdz::StreamReader sr(data.data(), data.size(),
                                      /* swap endian */ false);
            tinyusdz::usda::USDAReader parser(&sr);

            std::string base_dir = io::GetBaseDir(filepath);

            parser.SetBaseDir(base_dir);

            {
              bool ret = parser.Parse(tinyusdz::usda::LOAD_STATE_REFERENCE);

              if (!ret) {
                PUSH_WARN("Failed to parse .usda: " << parser.GetError());
              } else {
                DCOUT("`references` load ok.");
              }
            }

            std::string defaultPrim = parser.GetDefaultPrimName();

            DCOUT("defaultPrim: " + parser.GetDefaultPrimName());

            const std::vector<GPrim> &root_nodes = parser.GetGPrims();
            if (root_nodes.empty()) {
              PUSH_WARN("USD file does not contain any Prim node.");
            } else {
              size_t default_idx =
                  0;  // Use the first element when corresponding defaultPrim
                      // node is not found.

              auto node_it = std::find_if(root_nodes.begin(), root_nodes.end(),
                                          [defaultPrim](const GPrim &a) {
                                            return !defaultPrim.empty() &&
                                                   (a.name == defaultPrim);
                                          });

              if (node_it != root_nodes.end()) {
                default_idx =
                    size_t(std::distance(root_nodes.begin(), node_it));
              }

              DCOUT("defaultPrim node: " + root_nodes[default_idx].name);
              for (size_t i = 0; i < root_nodes.size(); i++) {
                DCOUT("root nodes: " + root_nodes[i].name);
              }

              // Store result to cache
              _reference_cache[filepath] = {default_idx, root_nodes};

              prim_type = root_nodes[default_idx].prim_type;
              DCOUT("Infered prim type: " + prim_type);
            }
          }
        }
      } else {
        // Unknown or unresolved node type
        LOG_ERROR("TODO: unresolved node type\n");
      }
    }


    return true;
  }
#endif

  template <typename T>
  bool ReconstructPrim(
      const std::map<std::string, Property> &properties,
      const std::vector<std::pair<ListEditQual, Reference>> &references,
      T *out);

  // T = Prim class(e.g. Xform)
  template<typename T>
  bool RegisterReconstructCallback() {
    _parser.RegisterPrimConstructFunction(
        PrimTypeTrait<T>::prim_type_name,
        [&](const Path &path, const std::map<std::string, Property> &properties,
            const std::vector<std::pair<ListEditQual, Reference>> &references) {
          T prim;

          if (ReconstructPrim<T>(properties, references, &prim)) {
            // TODO
            PUSH_WARN("TODO: Implement " + std::string(PrimTypeTrait<T>::prim_type_name));
          }

          return true;
        });

    return true;
  }

  bool ReconstructPreviewSurface(
      const std::map<std::string, Property> &properties,
      const std::vector<std::pair<ListEditQual, Reference>> &references,
      PreviewSurface *surface);

  bool ReconstructUVTexture(
      const std::map<std::string, Property> &properties,
      const std::vector<std::pair<ListEditQual, Reference>> &references,
      UVTexture *texture);

  bool ReconstructPrimvarReader_float2(
      const std::map<std::string, Property> &properties,
      const std::vector<std::pair<ListEditQual, Reference>> &references,
      PrimvarReader_float2 *reader_float2);


  void ImportScene(tinyusdz::HighLevelScene &scene) { _imported_scene = scene; }

  bool HasPath(const std::string &path) {
    // TODO
    TokenizedPath tokPath(path);
    (void)tokPath;
    return false;
  }

  void StageMetaProcessor() {
    _parser.RegisterStageMetaProcessFunction(
        [&](const ascii::AsciiParser::StageMetas &metas) {
          DCOUT("StageMeta CB:");
          if (metas.upAxis) {
            DCOUT("upAxis = " << to_string(metas.upAxis.value()));
          }

          // HACK
          _upAxis = metas.upAxis;

          return true;  // ok
        });
  }

  ///
  /// Reader entry point
  /// TODO: Use callback function(visitor) so that Reconstruct**** function is
  /// invoked in the Parser context.
  ///
  bool Read(ascii::LoadState state = ascii::LoadState::TOPLEVEL);

  // std::vector<GPrim> GetGPrims() { return _gprims; }

  std::string GetDefaultPrimName() const { return _defaultPrim; }

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

 private:
  void RegisterNodeTypes() {
    _node_types.insert(PrimTypeTrait<Xform>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomSphere>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomCube>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomCylinder>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomBasisCurves>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomMesh>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomSubset>::prim_type_name);
    _node_types.insert(PrimTypeTrait<Scope>::prim_type_name);

    _node_types.insert(PrimTypeTrait<Material>::prim_type_name);
    _node_types.insert(PrimTypeTrait<Shader>::prim_type_name);

    //_node_types.insert("NodeGraph");

    _node_types.insert(PrimTypeTrait<LuxSphereLight>::prim_type_name);
    _node_types.insert(PrimTypeTrait<LuxDomeLight>::prim_type_name);
    _node_types.insert(PrimTypeTrait<GeomCamera>::prim_type_name);
    _node_types.insert(PrimTypeTrait<SkelRoot>::prim_type_name);
    _node_types.insert(PrimTypeTrait<Skeleton>::prim_type_name);
  }

  ///
  /// -- Iterators --
  ///
  class PrimIterator {
   public:
    PrimIterator(const std::vector<size_t> &indices,
                 const std::vector<value::Value> &values, size_t idx = 0)
        : _indices(indices), _values(values), _idx(idx) {}

    const value::Value &operator*() const { return _values[_indices[_idx]]; }

    PrimIterator &operator++() {
      _idx++;
      return *this;
    }
    bool operator!=(const PrimIterator &rhs) { return _idx != rhs._idx; }

   private:
    const std::vector<size_t> &_indices;
    const std::vector<value::Value> &_values;
    size_t _idx{0};
  };
  friend class PrimIterator;

  // currently const only
  using const_prim_iterator = const PrimIterator;

  // Iterate over toplevel prims
  const_prim_iterator PrimBegin() {
    return PrimIterator(_toplevel_prims, _prims);
  }
  const_prim_iterator PrimEnd() {
    return PrimIterator(_toplevel_prims, _prims, _toplevel_prims.size());
  }
  size_t PrimSize() { return _toplevel_prims.size(); }

  ///
  /// -- Members --
  ///

  std::set<std::string> _node_types;

  std::stack<ParseState> parse_stack;

  std::string _base_dir;  // Used for importing another USD file

  nonstd::optional<tinyusdz::HighLevelScene>
      _imported_scene;  // Imported scene.

  // "class" defs
  std::map<std::string, Klass> _klasses;

  std::stack<std::string> _path_stack;

  std::string _err;
  std::string _warn;

  // Cache of loaded `references`
  // <filename, {defaultPrim index, list of root nodes in referenced usd file}>
  std::map<std::string, std::pair<uint32_t, std::vector<GPrim>>>
      _reference_cache;

  // toplevel prims
  std::vector<size_t> _toplevel_prims;  // index to _prims

  // Flattened array of prim nodes.
  std::vector<value::Value> _prims;

  // Path(prim part only) -> index to _prims[]
  std::map<std::string, size_t> _primpath_to_prim_idx_map;

  // load flags
  bool _sub_layered{false};
  bool _referenced{false};
  bool _payloaded{false};

  std::string _defaultPrim;

  ascii::AsciiParser _parser;

  // HACK
  nonstd::optional<Axis> _upAxis;

};  // namespace usda

#if 1
// Empty allowedTokens = allow all
template<class E, size_t N>
static nonstd::expected<bool, std::string> CheckAllowedTokens(const std::array<std::pair<E, const char *>, N> &allowdTokens, const std::string &tok) {

  if (allowdTokens.empty()) {
    return true;
  }

  for (size_t i = 0; i < N; i++) {
    if (tok.compare(std::get<1>(allowdTokens[i])) == 0) {
      return true;
    }
  }

  std::vector<std::string> toks;
  for (size_t i = 0; i < N; i++) {
    toks.push_back(std::get<1>(allowdTokens[i]));
  }

  std::string s = join(", ", quote(toks));

  return nonstd::make_unexpected("Allowed tokens are [" + s + "] but got " + quote(tok) + ".");
};
#endif

///
/// -- Impl reconstruct
//
template <>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Xform *xform) {
  (void)xform;

  auto CheckAllowedTypeOfXformOp =
      [](const PrimAttrib &attr,
         const std::vector<value::TypeId> &allowed_type_ids)
      -> nonstd::expected<bool, std::string> {
    for (size_t i = 0; i < allowed_type_ids.size(); i++) {
      if (attr.var.type_id() == allowed_type_ids[i]) {
        return true;
      }
    }

    std::stringstream ss;

    ss << "Allowed type for \"" << attr.name << "\"";
    if (allowed_type_ids.size() > 1) {
      ss << " are ";
    } else {
      ss << " is ";
    }

    for (size_t i = 0; i < allowed_type_ids.size(); i++) {
      ss << value::GetTypeName(allowed_type_ids[i]);
      if (i < (allowed_type_ids.size() - 1)) {
        ss << ", ";
      } else if (i == (allowed_type_ids.size() - 1)) {
        ss << " or ";
      }
    }
    ss << ", but got " << value::GetTypeName(attr.var.type_id());

    return nonstd::make_unexpected(ss.str());
  };

  // ret = (basename, suffix, isTimeSampled?)
  auto Split =
      [](const std::string &str) -> std::tuple<std::string, std::string, bool> {
    bool isTimeSampled{false};

    std::string s = str;

    const std::string tsSuffix = ".timeSamples";

    if (endsWith(s, tsSuffix)) {
      isTimeSampled = true;
      // rtrim
      s = s.substr(0, s.size() - tsSuffix.size());
    }

    // TODO: Support multiple namespace(e.g. xformOp:translate:pivot)
    std::string suffix;
    if (s.find_last_of(':') != std::string::npos) {
      suffix = s.substr(s.find_last_of(':') + 1);
    }

    std::string basename = s;
    if (s.find_last_of(':') != std::string::npos) {
      basename = s.substr(0, s.find_last_of(':'));
    }

    return std::make_tuple(basename, suffix, isTimeSampled);
  };

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  for (const auto &prop : properties) {
    DCOUT("prop.name = " << prop.first);
    if (startsWith(prop.first, "xformOp:translate")) {
      // TODO: Implement
      // using allowedTys = tinyusdz::variant<value::float3, value::double3>;
      std::vector<value::TypeId> ids;
      auto ret = CheckAllowedTypeOfXformOp(prop.second.attrib, ids);
      if (!ret) {
      }
    }
  }

  // Lookup xform values from `xformOpOrder`
  if (properties.count("xformOpOrder")) {
    // array of string
    auto prop = properties.at("xformOpOrder");
    if (prop.is_rel) {
      PUSH_WARN("TODO: Rel type for `xformOpOrder`");
    } else {
#if 0
      if (auto parr = value::as_vector<std::string>(&attrib->var)) {
        for (const auto &item : *parr) {
          // remove double-quotation
          std::string identifier = item;
          identifier.erase(
              std::remove(identifier.begin(), identifier.end(), '\"'),
              identifier.end());

          auto tup = Split(identifier);
          auto basename = std::get<0>(tup);
          auto suffix = std::get<1>(tup);
          auto isTimeSampled = std::get<2>(tup);
          (void)isTimeSampled;

          XformOp op;

          std::string target_name = basename;
          if (!suffix.empty()) {
            target_name += ":" + suffix;
          }

          if (!properties.count(target_name)) {
            PushError("Property '" + target_name +
                       "' not found in Xform node.");
            return false;
          }

          auto targetProp = properties.at(target_name);

          if (basename == "xformOp:rotateZ") {
            if (auto targetAttr = nonstd::get_if<PrimAttrib>(&targetProp)) {
              if (auto p = value::as_basic<float>(&targetAttr->var)) {
                std::cout << "xform got it "
                          << "\n";
                op.op = XformOp::OpType::ROTATE_Z;
                op.suffix = suffix;
                op.value = (*p);

                xform->xformOps.push_back(op);
              }
            }
          }
        }
      }
      PushError("`xformOpOrder` must be an array of string type.");
#endif
      (void)Split;
    }

  } else {
    // std::cout << "no xformOpOrder\n";
  }

  // For xformO
  // TinyUSDZ does no accept arbitrary types for variables with `xformOp` su
#if 0
    for (const auto &prop : properties) {


      if (prop.first == "xformOpOrder") {
        if (!prop.second.IsArray()) {
          PushError("`xformOpOrder` must be an array type.");
          return false;
        }

        for (const auto &item : prop.second.array) {
          if (auto p = nonstd::get_if<std::string>(&item)) {
            // TODO
            //XformOp op;
            //op.op =
          }
        }

      } else if (std::get<0>(tup) == "xformOp:rotateZ") {

        if (prop.second.IsTimeSampled()) {

        } else if (prop.second.IsFloat()) {
          if (auto p = nonstd::get_if<float>(&prop.second.value)) {
            XformOp op;
            op.op = XformOp::OpType::ROTATE_Z;
            op.precision = XformOp::PrecisionType::PRECISION_FLOAT;
            op.value = *p;

            std::cout << "rotateZ value = " << *p << "\n";

          } else {
            PushError("`xformOp:rotateZ` must be an float type.");
            return false;
          }
        } else {
          PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                     "\n");
        }

      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
#endif

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
    }
  }

  return true;
}

  ///
  /// -- RegisterReconstructCallback specializations
  ///
  template <>
  bool USDAReader::Impl::RegisterReconstructCallback<GPrim>() {
    _parser.RegisterPrimConstructFunction(
        PrimTypeTrait<GPrim>::prim_type_name,
        [&](const Path &path, const std::map<std::string, Property> &properties,
            std::vector<std::pair<ListEditQual, Reference>> &references) {
          // TODO: Implement
          GPrim gprim;

          //
          // Resolve prepend references
          //
          for (const auto &ref : references) {
            if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
            }
          }

          // Update props;
          for (auto item : properties) {
            if (item.second.is_rel) {
              PUSH_WARN("TODO: rel");
            } else {
              gprim.props[item.first].attrib = item.second.attrib;
            }
          }

          //
          // Resolve append references
          //
          for (const auto &ref : references) {
            if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
            }
          }

          return true;
        });

    return true;
  }

  template<>
  bool USDAReader::Impl::RegisterReconstructCallback<GeomSubset>() {
    _parser.RegisterPrimConstructFunction(
        "GeomSubset",
        [&](const Path &path, const std::map<std::string, Property> &properties,
            std::vector<std::pair<ListEditQual, Reference>> &references) {
          // Parent Prim must be GeomMesh
          const Path parent = path.GetParentPrim();
          if (!parent.IsValid()) {
            PUSH_ERROR_AND_RETURN("Invalid Prim path");
          }

          if (parent.IsRootPrim()) {
            PUSH_ERROR_AND_RETURN(
                "GeomSubset must be a child of GeomMesh prim.");
          }

          const std::string parent_primpath = parent.GetPrimPart();

          if (!_primpath_to_prim_idx_map.count(parent_primpath)) {
            PUSH_ERROR_AND_RETURN("Parent Prim not found.");
          }

          size_t prim_idx = _primpath_to_prim_idx_map[parent_primpath];
          auto pmesh = _prims[prim_idx].get_value<GeomMesh>();
          if (!pmesh) {
            PUSH_ERROR_AND_RETURN("Parent Prim must be GeomMesh, but got " +
                                  _prims[prim_idx].type_name());
          }
          GeomMesh &mesh = pmesh.value();

          GeomSubset subset;

          // uniform token elementType
          // uniform token familyName
          // int[] indices
          // rel material:binding

          if (references.size()) {
            PUSH_WARN("`references` support in GeomSubset is TODO");
          }

          // Update props;
          for (auto item : properties) {
            if (item.first == "elementType") {
              if (item.second.IsRel()) {
                PUSH_ERROR_AND_RETURN(
                    "`elementType` property as Relation is not supported.");
              }
              if (auto pv = item.second.attrib.var.get_value<value::token>()) {
                if (item.second.attrib.uniform) {
                  auto e = subset.SetElementType(pv.value().str());
                  if (!e) {
                    PUSH_ERROR_AND_RETURN(e.error());
                  }
                  continue;
                }
              }
              PUSH_ERROR_AND_RETURN(
                  "`elementType` property must be `uniform token` type.");
            } else if (item.first == "familyType") {
              if (item.second.IsRel()) {
                PUSH_ERROR_AND_RETURN(
                    "`familyType` property as Relation is not supported.");
              }

              if (auto pv = item.second.attrib.var.get_value<value::token>()) {
                if (item.second.attrib.uniform) {
                  auto e = subset.SetFamilyType(pv.value().str());
                  if (!e) {
                    PUSH_ERROR_AND_RETURN(e.error());
                  }
                  continue;
                }
              }
              PUSH_ERROR_AND_RETURN(
                  "`familyType` property must be `uniform token` type.");

            } else if (item.first == "indices") {
              if (item.second.IsRel()) {
                PUSH_ERROR_AND_RETURN(
                    "`indices` property as Relation is not supported.");
              }

              if (auto pv =
                      item.second.attrib.var.get_value<std::vector<int>>()) {
                // int -> uint
                std::transform(pv.value().begin(), pv.value().end(),
                               std::back_inserter(subset.indices),
                               [](int a) { return uint32_t(a); });
              }

              PUSH_ERROR_AND_RETURN(
                  "`indices` property must be `int[]` type, but got `" +
                  item.second.attrib.var.type_name() + "`");

            } else if (item.first == "material:binding") {
              if (!item.second.IsRel()) {
                PUSH_ERROR_AND_RETURN(
                    "`material:binding` property as Attribute is not "
                    "supported.");
              }
            } else {
              PUSH_WARN("GeomSubeet: TODO: " + item.first);
            }
          }

          mesh.geom_subset_children.emplace_back(subset);

          return true;
        });

    return true;
  }

#if 0
  bool RegisterReconstructGeomMeshCallback() {
    _parser.RegisterPrimConstructFunction(
        kGeomMesh,
        [&](const Path &path, const std::map<std::string, Property> &properties,
            std::vector<std::pair<ListEditQual, Reference>> &references) {
          GeomMesh mesh;

          if (ReconstructGeomMesh(properties, references, &mesh)) {
            // TODO
            PUSH_WARN("TODO: Implement GeomMesh");
          }

          return true;
        });

    return true;
  }
#endif

template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomSphere *sphere) {
  (void)sphere;

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                sphere->radius = *p;
              }
            }
          }
#endif
        }
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      // if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
      //   sphere->materialBinding.materialBinding = prel->path;
      // } else {
      //   PushError("`material:binding` must be 'rel' type.");
      //   return false;
      // }
    } else {
      if (prop.second.is_rel) {
        PUSH_WARN("TODO: Rel");
      } else {
        if (prop.first == "radius") {
          // const tinyusdz::PrimAttrib &attr = prop.second.attrib;
          // if (auto p = value::as_basic<double>(&attr->var)) {
          //   sphere->radius = *p;
          // } else {
          //   PushError("`radius` must be double type.");
          //   return false;
          // }
        } else {
          PUSH_ERROR_AND_RETURN("TODO: type: " + prop.first);
        }
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
          // if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
          //   if (prop.first == "radius") {
          //     if (auto p = value::as_basic<double>(&attr->var)) {
          //       SDCOUT << "append reference radius = " << (*p) << "\n";
          //       sphere->radius = *p;
          //     }
          //   }
          // }
        }
      }
    }
  }

  return true;
}


template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCone *cone) {
  (void)properties;
  (void)cone;
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                cone->radius = *p;
              }
            } else if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference height = " << (*p) << "\n";
                cone->height = *p;
              }
            }
          }
#endif
        }
      }
    }
  }

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cone->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cone->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cone->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }
#endif

#if 0
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference radius = " << (*p) << "\n";
                cone->radius = *p;
              }
            } else if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference height = " << (*p) << "\n";
                cone->height = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}


template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCube *cube) {
  (void)properties;
  (void)cube;
#if 0
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "size") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference size = " << (*p) << "\n";
                cube->size = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cube->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "size") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cube->size = *p;
        } else {
          PushError("`size` must be double type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }
#endif

#if 0
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "size") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference size = " << (*p) << "\n";
                cube->size = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCapsule *capsule) {
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (prop.second.is_rel) {
            PUSH_WARN("TODO: Rel");
          } else {
            // const PrimAttrib &attrib = prop.second.attrib;
#if 0
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference height = " << (*p) << "\n";
                capsule->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                capsule->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  capsule->axis = Axis::X;
                } else if (p->value == "y") {
                  capsule->axis = Axis::Y;
                } else if (p->value == "z") {
                  capsule->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
#endif
          }
        }
      }
    }
  }

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        capsule->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          capsule->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          capsule->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "axis") {
        if (auto p = value::as_basic<Token>(&attr->var)) {
          if (p->value == "x") {
            capsule->axis = Axis::X;
          } else if (p->value == "y") {
            capsule->axis = Axis::Y;
          } else if (p->value == "z") {
            capsule->axis = Axis::Z;
          }
        } else {
          PushError("`axis` must be token type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference height = " << (*p) << "\n";
                capsule->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference radius = " << (*p) << "\n";
                capsule->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  capsule->axis = Axis::X;
                } else if (p->value == "y") {
                  capsule->axis = Axis::Y;
                } else if (p->value == "z") {
                  capsule->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCylinder *cylinder) {
#if 0
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference height = " << (*p) << "\n";
                cylinder->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "prepend reference radius = " << (*p) << "\n";
                cylinder->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  cylinder->axis = Axis::X;
                } else if (p->value == "y") {
                  cylinder->axis = Axis::Y;
                } else if (p->value == "z") {
                  cylinder->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cylinder->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cylinder->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cylinder->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "axis") {
        if (auto p = value::as_basic<Token>(&attr->var)) {
          if (p->value == "x") {
            cylinder->axis = Axis::X;
          } else if (p->value == "y") {
            cylinder->axis = Axis::Y;
          } else if (p->value == "z") {
            cylinder->axis = Axis::Z;
          }
        } else {
          PushError("`axis` must be token type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        DCOUT("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference height = " << (*p) << "\n";
                cylinder->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SDCOUT << "append reference radius = " << (*p) << "\n";
                cylinder->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SDCOUT << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  cylinder->axis = Axis::X;
                } else if (p->value == "y") {
                  cylinder->axis = Axis::Y;
                } else if (p->value == "z") {
                  cylinder->axis = Axis::Z;
                } else {
                  PUSH_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomMesh *mesh) {
  //
  // Resolve prepend references
  //

  for (const auto &ref : references) {
    DCOUT("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      if (endsWith(asset_ref.asset_path, ".obj")) {
        std::string err;
        GPrim gprim;

        // abs path.
        std::string filepath = asset_ref.asset_path;

        if (io::IsAbsPath(asset_ref.asset_path)) {
          // do nothing
        } else {
          if (!_base_dir.empty()) {
            filepath = io::JoinPath(_base_dir, filepath);
          }
        }

        DCOUT("Reading .obj file: " + filepath);

        if (!usdObj::ReadObjFromFile(filepath, &gprim, &err)) {
          PUSH_ERROR_AND_RETURN("Failed to read .obj(usdObj). err = " + err);
        }
        DCOUT("Loaded .obj file: " + filepath);

        mesh->visibility = gprim.visibility;
        mesh->doubleSided = gprim.doubleSided;
        mesh->orientation = gprim.orientation;

        if (gprim.props.count("points")) {
          DCOUT("points");
          const Property &prop = gprim.props.at("points");
          if (prop.is_rel) {
            PUSH_WARN("TODO: points Rel\n");
          } else {
            const PrimAttrib &attr = prop.attrib;
            // PrimVar
            DCOUT("points.type:" + attr.var.type_name());
            if (attr.var.is_scalar()) {
              auto p = attr.var.get_value<std::vector<value::point3f>>();
              if (p) {
                mesh->points = p.value();
              } else {
                PUSH_ERROR_AND_RETURN("TODO: points.type = " +
                                      attr.var.type_name());
              }
              // if (auto p = value::as_vector<value::float3>(&pattr->var)) {
              //   DCOUT("points. sz = " + std::to_string(p->size()));
              //   mesh->points = (*p);
              // }
            } else {
              PUSH_ERROR_AND_RETURN("TODO: timesample points.");
            }
          }
        }

      } else {
        DCOUT("Not a .obj file");
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.second.is_rel) {
      if (prop.first == "material:binding") {
        mesh->materialBinding.materialBinding = prop.second.rel.path;
      } else {
        PUSH_WARN("TODO: rel");
      }
    } else {
      const PrimAttrib &attr = prop.second.attrib;
      if (prop.first == "points") {
        auto p = attr.var.get_value<std::vector<value::point3f>>();
        if (p) {
          mesh->points = (*p);
        } else {
          PUSH_ERROR_AND_RETURN(
              "`GeomMesh::points` must be point3[] type, but got " +
              attr.var.type_name());
        }
      } else if (prop.first == "subdivisionScheme") {
        auto p = attr.var.get_value<std::string>();
        if (!p) {
          PUSH_ERROR_AND_RETURN(
              "Invalid type for \'subdivisionScheme\'. expected \'STRING\' but "
              "got " +
              attr.var.type_name());
        } else {
          DCOUT("subdivisionScheme = " + (*p));
          if (p->compare("none") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::None;
          } else if (p->compare("catmullClark") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::CatmullClark;
          } else if (p->compare("bilinear") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::Bilinear;
          } else if (p->compare("loop") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::Loop;
          } else {
            PUSH_ERROR_AND_RETURN("Unknown subdivision scheme: " + (*p));
          }
        }
      } else {
        PUSH_WARN(" TODO: prop: " + prop.first);
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      // TODO
    }
  }

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomBasisCurves *curves) {
  for (const auto &prop : properties) {
    if (prop.first == "points") {
      if (prop.second.is_rel) {
        PUSH_WARN("TODO: Rel");
      } else {
        // const PrimAttrib &attrib = prop.second.attrib;
#if 0  // TODO
        attrib.
        attrib.IsFloat3() && !prop.second.IsArray()) {
        PushError("`points` must be float3 array type.");
        return false;
      }

      const std::vector<float3> p =
          nonstd::get<std::vector<float3>>(prop.second.value);

      curves->points.resize(p.size() * 3);
      memcpy(curves->points.data(), p.data(), p.size() * 3);
#endif
      }

    } else if (prop.first == "curveVertexCounts") {
#if 0  // TODO
      if (!prop.second.IsInt() && !prop.second.IsArray()) {
        PushError("`curveVertexCounts` must be int array type.");
        return false;
      }

      const std::vector<int32_t> p =
          nonstd::get<std::vector<int32_t>>(prop.second.value);

      curves->curveVertexCounts.resize(p.size());
      memcpy(curves->curveVertexCounts.data(), p.data(), p.size());
#endif

    } else {
      PUSH_WARN("TODO: " << prop.first);
    }
  }

  return true;
}

// TODO(syoyo): TimeSamples, Reference
#define PARSE_PROPERTY(__prop, __name, __target)             \
  if (__prop.first == __name) {                               \
    const PrimAttrib &attr = __prop.second.attrib;                 \
    if (auto v = attr.var.get_value<decltype(__target)>()) {                     \
      __target = v.value();                                        \
    } else {                                                       \
      PUSH_ERROR_AND_RETURN("Type mismatch. "                      \
                            << __name << " expects "               \
                            << value::TypeTrait<decltype(__target)>::type_name()); \
    } \
  } else

//#define PARSE_TOKEN_PROPETY(__prop, __name, __ty, __allowed_tokens, __target)             \
//  if (__prop.first == __name) {                               \
//    const PrimAttrib &attr = __prop.second.attrib;                 \
//    if (auto v = attr.var.get_value<__ty>()) {                     \
//      __target = v.value();                                        \
//    } else {                                                       \
//      PUSH_ERROR_AND_RETURN("Type mismatch. "                      \
//                            << __name << " expects "               \
//                            << value::TypeTrait<__ty>::type_name()); \
//    } \
//  } else

#define PARSE_ENUM_PROPETY(__prop, __name, __enum_handler, __target)             \
  if (__prop.first == __name) {                               \
    const PrimAttrib &attr = __prop.second.attrib;                 \
    if (auto tok = attr.var.get_value<value::token>()) {                     \
      auto e = __enum_handler(tok.value().str()); \
      if (e) { \
        __target = e.value();                                        \
      } else { \
        PUSH_ERROR_AND_RETURN(e.error()); \
      } \
    } else {                                                       \
      PUSH_ERROR_AND_RETURN("Type mismatch. "                      \
                            << __name << " must be `token` type.");     \
    } \
  } else


#define PARSE_PROPERTY_END_MAKE_ERROR(__prop)  { \
      PUSH_ERROR_AND_RETURN("Unsupported/unimplemented property: " + __prop.first); \
    }

#define PARSE_PROPERTY_END_MAKE_WARN(__prop)  { \
      PUSH_WARN("Unsupported/unimplemented property: " + __prop.first); \
    }


template<>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCamera *camera) {

  auto ProjectionHandler = [](const std::string &tok) -> nonstd::expected<GeomCamera::Projection, std::string> {

    using EnumTy = std::pair<GeomCamera::Projection, const char *>;
    constexpr std::array<EnumTy, 2> enums = {
      std::make_pair(GeomCamera::Projection::perspective, "perspective"),
      std::make_pair(GeomCamera::Projection::orthographic, "orthographic"),
    };

    auto ret = CheckAllowedTokens<GeomCamera::Projection, enums.size()>(enums, tok);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }

    for (auto &item : enums) {
      if (tok == item.second) {
        return item.first;
      }
    }

    // Should never reach here, though.
    return nonstd::make_unexpected(quote(tok) + " is invalid token for `projection` propety");
  };

  for (const auto &prop : properties) {
    PARSE_PROPERTY(prop, "focalLength", camera->focalLength)
    PARSE_PROPERTY(prop, "focusDistance", camera->focusDistance)
    PARSE_PROPERTY(prop, "exposure", camera->exposure)
    PARSE_PROPERTY(prop, "fStop", camera->fStop)
    PARSE_PROPERTY(prop, "horizontalAperture", camera->horizontalAperture)
    PARSE_PROPERTY(prop, "horizontalApertureOffset", camera->horizontalApertureOffset)
    PARSE_PROPERTY(prop, "horizontalApertureOffset", camera->horizontalApertureOffset)
    PARSE_PROPERTY(prop, "clippingRange", camera->clippingRange)
    PARSE_PROPERTY(prop, "clippingPlanes", camera->clippingPlanes)
    PARSE_PROPERTY(prop, "shutter:open", camera->shutterOpen)
    PARSE_PROPERTY(prop, "shutter:close", camera->shutterClose)
    PARSE_ENUM_PROPETY(prop, "projection", ProjectionHandler, camera->projection)
    PARSE_PROPERTY_END_MAKE_ERROR(prop)
  }

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<LuxSphereLight>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxSphereLight *light) {

  for (const auto &prop : properties) {
    //PARSE_PROPERTY(prop, "inputs:colorTemperature", light->colorTemperature)
    PARSE_PROPERTY(prop, "inputs:color", light->color)
    PARSE_PROPERTY(prop, "inputs:radius", light->color)
    PARSE_PROPERTY(prop, "inputs:intensity", light->intensity)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}


template<>
bool USDAReader::Impl::ReconstructPrim<LuxDomeLight>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxDomeLight *light) {

  for (const auto &prop : properties) {
    PARSE_PROPERTY(prop, "guideRadius", light->guideRadius)
    PARSE_PROPERTY(prop, "inputs:diffuse", light->diffuse)
    PARSE_PROPERTY(prop, "inputs:specular", light->specular)
    PARSE_PROPERTY(prop, "inputs:colorTemperature", light->colorTemperature)
    PARSE_PROPERTY(prop, "inputs:color", light->color)
    PARSE_PROPERTY(prop, "inputs:intensity", light->intensity)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  DCOUT("Implement DomeLight");
  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<Scope>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references, Scope *scope) {

  // `Scope` is just a namespace in scene graph(no node xform)

  // TODO: support custom properties
  for (const auto &prop : properties) {
    PUSH_WARN("Unsupported/unimplemented property: " + prop.first);
  }

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<SkelRoot>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    SkelRoot *root) {
  (void)root;

  // TODO: Implement
  DCOUT("Implement SkelRoot");

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<Skeleton>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Skeleton *skel) {
  (void)skel;

  // TODO: Implement
  DCOUT("Implement Skeleton");

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<Shader>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Shader *shader) {
  for (const auto &prop : properties) {
    if (prop.first == "info:id") {
      const PrimAttrib &attr = prop.second.attrib;

      auto p = attr.var.get_value<std::string>();
      if (p) {
        if (p->compare("UsdPreviewSurface") == 0) {
          PreviewSurface surface;
          if (!ReconstructPreviewSurface(properties, references, &surface)) {
            PUSH_WARN("TODO: reconstruct PreviewSurface.");
          }
          shader->value = surface;
        } else if (p->compare("UsdUVTexture") == 0) {
          UVTexture texture;
          if (!ReconstructUVTexture(properties, references, &texture)) {
            PUSH_WARN("TODO: reconstruct UVTexture.");
          }
          shader->value = texture;
        } else if (p->compare("UsdPrimvarReader_float2") == 0) {
          PrimvarReader_float2 preader;
          if (!ReconstructPrimvarReader_float2(properties, references,
                                               &preader)) {
            PUSH_WARN("TODO: reconstruct PrimvarReader_float2.");
          }
          shader->value = preader;
        } else {
          PUSH_ERROR_AND_RETURN("Invalid or Unsupported Shader id: " + (*p));
        }
      }
    } else {
      // std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<NodeGraph>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    NodeGraph *graph) {
  (void)properties;
  (void)references;
  (void)graph;

  PUSH_WARN("TODO: reconstruct NodeGrah.");

  return true;
}

template<>
bool USDAReader::Impl::ReconstructPrim<Material>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Material *material) {
  (void)properties;
  (void)references;
  (void)material;

  PUSH_WARN("TODO: Implement Material.");

  return true;
}

bool USDAReader::Impl::ReconstructPreviewSurface(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    PreviewSurface *surface) {
  // TODO:
  return false;
}

bool USDAReader::Impl::ReconstructUVTexture(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UVTexture *texture) {
  // TODO:
  return false;
}

bool USDAReader::Impl::ReconstructPrimvarReader_float2(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    PrimvarReader_float2 *preader) {
  // TODO:
  return false;
}

///
/// -- Impl callback specializations
///
template <>
bool USDAReader::Impl::RegisterReconstructCallback<Xform>() {
  _parser.RegisterPrimConstructFunction(
      PrimTypeTrait<Xform>::prim_type_name,
      [&](const Path &path, const std::map<std::string, Property> &properties,
          std::vector<std::pair<ListEditQual, Reference>> &references) {
        Xform xform;

        DCOUT("Reconstruct Xform: Path.PrimPart = " << path.GetPrimPart());

        if (!ReconstructPrim<Xform>(properties, references, &xform)) {
          return false;
        }

        Path parent = path.GetParentPrim();
        if (parent.IsRootPrim()) {
          size_t idx = _prims.size();
          _prims.push_back(xform);
          _toplevel_prims.push_back(idx);
        } else {
          PUSH_WARN("TODO: Implement xform");
        }

        return true;
      });

  return true;
}

///
/// -- Impl Read
///

bool USDAReader::Impl::Read(ascii::LoadState state) {
  ///
  /// Setup callbacks.
  ///
  StageMetaProcessor();
  RegisterReconstructCallback<GPrim>();
  RegisterReconstructCallback<Xform>();
  RegisterReconstructCallback<GeomCube>();
  RegisterReconstructCallback<GeomSphere>();
  RegisterReconstructCallback<GeomCone>();
  RegisterReconstructCallback<GeomCylinder>();
  RegisterReconstructCallback<GeomCapsule>();
  RegisterReconstructCallback<GeomMesh>();
  RegisterReconstructCallback<GeomSubset>();

  RegisterReconstructCallback<Scope>();

  RegisterReconstructCallback<LuxSphereLight>();
  RegisterReconstructCallback<LuxDomeLight>();

  RegisterReconstructCallback<SkelRoot>();
  RegisterReconstructCallback<Skeleton>();

  if (!_parser.Parse(state)) {
    std::string warn = _parser.GetWarning();
    if (!warn.empty()) {
      PUSH_WARN("<parser> " + warn);
    }

    PUSH_ERROR_AND_RETURN("Parse failed:" + _parser.GetError());
  }

  // HACK
  if (_upAxis) {
    DCOUT("upAxis = " << to_string(_upAxis.value()));
  }

  DCOUT("# of toplevel prims = " << std::to_string(PrimSize()));

  {
    size_t i = 0;
    for (auto it = PrimBegin(); it != PrimEnd(); ++it, i++) {
      const auto &prim = (*it);
      DCOUT("Prim[" << std::to_string(i) << "].type = " << prim.type_name());
    }
  }

#if 0
    //_sub_layered = (state == LOAD_STATE_SUBLAYER);
    //_referenced = (state == LOAD_STATE_REFERENCE);
    //_payloaded = (state == LOAD_STATE_PAYLOAD);

    // Stage meta.
    if (!_parser.ParseStageMetas()) {
      PUSH_PARSER_ERROR_AND_RETURN();
      return false;
    }

    DCOUT("Done parsing Stage metas");

    // parse blocks
    while (!_parser.Eof()) {
      if (!_parser.SkipCommentAndWhitespaceAndNewline()) {
        PUSH_PARSER_ERROR_AND_RETURN();
      }

      if (_parser.Eof()) {
        // Whitespaces in the end of line.
        break;
      }

      // Look ahead token
      auto curr_loc = _parser.CurrLoc();
      DCOUT("loc = " << curr_loc);

      std::string tok;
      if (!_parser.ReadIdentifier(&tok)) {
        DCOUT("Failed to read identifier");
        PUSH_PARSER_ERROR_AND_RETURN();
      }
      DCOUT("tok = " << tok);

      // Rewind
      if (!_parser.SeekTo(curr_loc)) {
        PUSH_PARSER_ERROR_AND_RETURN();
      }

      if (tok == "def") {
        DCOUT("`def` block");
        bool block_ok = _parser.ParseDefBlock();
        if (!block_ok) {
          PUSH_PARSER_ERROR_AND_RETURN();
        }
      } else if (tok == "over") {
        DCOUT("`over` block");
        bool block_ok = _parser.ParseOverBlock();
        if (!block_ok) {
          PUSH_PARSER_ERROR_AND_RETURN();
        }
      } else if (tok == "class") {
        DCOUT("`class` block");
        bool block_ok = _parser.ParseClassBlock();
        if (!block_ok) {
          PUSH_PARSER_ERROR_AND_RETURN();
        }
      } else {
        PUSH_ERROR_AND_RETURN("Unknown identifier '" + tok + "' for Prim block statement.");
      }
    }
#endif
  return true;
}

//
// --
//

bool IsUSDA(const std::string &filename, size_t max_filesize) {
  // TODO: Read only first N bytes
  std::vector<uint8_t> data;
  std::string err;

  if (!io::ReadWholeFile(&data, &err, filename, max_filesize)) {
    return false;
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  return parser.CheckHeader();
}

///
/// -- USDAReader
///
USDAReader::USDAReader(StreamReader *sr) { _impl = new Impl(sr); }

USDAReader::~USDAReader() { delete _impl; }

bool USDAReader::Read(ascii::LoadState state) { return _impl->Read(state); }

void USDAReader::SetBaseDir(const std::string &dir) {
  return _impl->SetBaseDir(dir);
}

// std::vector<GPrim> USDAReader::GetGPrims() { return _impl->GetGPrims(); }

std::string USDAReader::GetDefaultPrimName() const {
  return _impl->GetDefaultPrimName();
}

std::string USDAReader::GetError() { return _impl->GetError(); }
std::string USDAReader::GetWarning() { return _impl->GetWarning(); }


}  // namespace tinyusdz
}  // namespace tinyusdz

#else

namespace tinyusdz {
namespace usda {

USDAReader::USDAReader(StreamReader *sr) { (void)sr; }

USDAReader::~USDAReader() {}

bool USDAReader::CheckHeader() { return false; }

bool USDAReader::Parse(ascii::AsciiParser::LoadState state) {
  (void)state;
  return false;
}

void USDAReader::SetBaseDir(const std::string &dir) { (void)dir; }

std::vector<GPrim> USDAReader::GetGPrims() { return {}; }

std::string USDAReader::GetDefaultPrimName() const { return std::string{}; }

std::string USDAReader::GetError() {
  return "USDA parser feature is disabled in this build.\n";
}
std::string USDAReader::GetWarning() { return std::string{}; }

}  // namespace usda
}  // namespace tinyusdz

#endif
