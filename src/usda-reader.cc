// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// USDA reader
// TODO:
//   - [ ] Use common base code for Reconstruct**** with USDC reader

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stack>

#include "ascii-parser.hh"
#include "usdGeom.hh"
#if defined(__wasi__)
#else
#include <mutex>
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
#include "primvar.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "usdObj.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "value-types.hh"

#include "common-macros.inc"

namespace tinyusdz {
namespace usda {

constexpr auto kTag = "[USDA]";

namespace {

struct PrimNode {
  value::Value prim;

  int64_t parent{-1};            // -1 = root node
  std::vector<size_t> children;  // index to USDAReader._prims[]
};

// TODO: Move to prim-types.hh?

template <typename T>
struct PrimTypeTrait;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

#define DEFINE_PRIM_TYPE(__dty, __name, __tyid)    \
  template <>                                      \
  struct PrimTypeTrait<__dty> {                    \
    using primt_type = __dty;                      \
    static constexpr uint32_t type_id = __tyid;    \
    static constexpr auto prim_type_name = __name; \
  }

DEFINE_PRIM_TYPE(Model, "Model", value::TYPE_ID_MODEL);

DEFINE_PRIM_TYPE(Xform, "Xform", value::TYPE_ID_GEOM_XFORM);
DEFINE_PRIM_TYPE(GeomMesh, "Mesh", value::TYPE_ID_GEOM_MESH);
DEFINE_PRIM_TYPE(GeomSphere, "Sphere", value::TYPE_ID_GEOM_SPHERE);
DEFINE_PRIM_TYPE(GeomCube, "Cube", value::TYPE_ID_GEOM_CUBE);
DEFINE_PRIM_TYPE(GeomCone, "Cone", value::TYPE_ID_GEOM_CONE);
DEFINE_PRIM_TYPE(GeomCapsule, "Capsule", value::TYPE_ID_GEOM_CAPSULE);
DEFINE_PRIM_TYPE(GeomCylinder, "Cylinder", value::TYPE_ID_GEOM_CYLINDER);
DEFINE_PRIM_TYPE(GeomBasisCurves, "BasisCurves",
                 value::TYPE_ID_GEOM_BASIS_CURVES);
DEFINE_PRIM_TYPE(GeomSubset, "GeomSubset", value::TYPE_ID_GEOM_GEOMSUBSET);
DEFINE_PRIM_TYPE(LuxSphereLight, "SphereLight", value::TYPE_ID_LUX_SPHERE);
DEFINE_PRIM_TYPE(LuxDomeLight, "DomeLight", value::TYPE_ID_LUX_DOME);
DEFINE_PRIM_TYPE(Material, "Material", value::TYPE_ID_MATERIAL);
DEFINE_PRIM_TYPE(Shader, "Shader", value::TYPE_ID_SHADER);
DEFINE_PRIM_TYPE(SkelRoot, "SkelRoot", value::TYPE_ID_SKEL_ROOT);
DEFINE_PRIM_TYPE(Skeleton, "Skeleton", value::TYPE_ID_SKELETON);
DEFINE_PRIM_TYPE(SkelAnimation, "SkelAnimation", value::TYPE_ID_SKELANIMATION);
DEFINE_PRIM_TYPE(BlendShape, "Blendshape", value::TYPE_ID_BLENDSHAPE);
DEFINE_PRIM_TYPE(Scope, "Scope", value::TYPE_ID_SCOPE);
DEFINE_PRIM_TYPE(GeomCamera, "Camera", value::TYPE_ID_GEOM_CAMERA);

DEFINE_PRIM_TYPE(GPrim, "GPrim", value::TYPE_ID_GPRIM);

// DEFINE_PRIM_TYPE(PreviewSurface, "PreviewSurface",
// value::TYPE_ID_IMAGING_PREVIEWSURFACE); DEFINE_PRIM_TYPE(UVTexture,
// "UVTexture", value::TYPE_ID_IMAGING_UVTEXTURE);
// DEFINE_PRIM_TYPE(PrimvarReader_float2, "PrimvarReaderUVTexture",
// value::TYPE_ID_IMAGING_UVTEXTURE);

#ifdef __clang__
#pragma clang diagnostic pop
#endif

}  // namespace

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

#if 1
// Empty allowedTokens = allow all
template <class E, size_t N>
static nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::array<std::pair<E, const char *>, N> &allowedTokens,
    const std::string &tok) {
  if (allowedTokens.empty()) {
    return true;
  }

  for (size_t i = 0; i < N; i++) {
    if (tok.compare(std::get<1>(allowedTokens[i])) == 0) {
      return true;
    }
  }

  std::vector<std::string> toks;
  for (size_t i = 0; i < N; i++) {
    toks.push_back(std::get<1>(allowedTokens[i]));
  }

  std::string s = join(", ", tinyusdz::quote(toks));

  return nonstd::make_unexpected("Allowed tokens are [" + s + "] but got " +
                                 quote(tok) + ".");
};

template <class E>
static nonstd::expected<bool, std::string> CheckAllowedTokens(
    const std::vector<std::pair<E, const char *>> &allowedTokens,
    const std::string &tok) {
  if (allowedTokens.empty()) {
    return true;
  }

  for (size_t i = 0; i < allowedTokens.size(); i++) {
    if (tok.compare(std::get<1>(allowedTokens[i])) == 0) {
      return true;
    }
  }

  std::vector<std::string> toks;
  for (size_t i = 0; i < allowedTokens.size(); i++) {
    toks.push_back(std::get<1>(allowedTokens[i]));
  }

  std::string s = join(", ", tinyusdz::quote(toks));

  return nonstd::make_unexpected("Allowed tokens are [" + s + "] but got " +
                                 quote(tok) + ".");
};
#endif

template <typename T>
nonstd::expected<T, std::string> EnumHandler(
    const std::string &prop_name, const std::string &tok,
    const std::vector<std::pair<T, const char *>> &enums) {
  auto ret = CheckAllowedTokens<T>(enums, tok);
  if (!ret) {
    return nonstd::make_unexpected(ret.error());
  }

  for (auto &item : enums) {
    if (tok == item.second) {
      return item.first;
    }
  }
  // Should never reach here, though.
  return nonstd::make_unexpected(
      quote(tok) + " is an invalid token for attribute `" + prop_name + "`");
}

class USDAReader::Impl {
 private:
  Stage _stage;

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

  void PushError(const std::string &s) {
    _err += s;
  }

  void PushWarn(const std::string &s) {
    _err += s;
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

  ///
  /// Reconstruct properties with `xformOp:***` namespace by looking up
  /// tokens in `xformOpOrder` property.
  /// Name of processed xformOp properties are added to `table`
  ///
  bool ReconstructXformOpProperties(
    std::set<std::string> &table, /* inout */
    const std::map<std::string, Property> &properties,
    std::vector<XformOp> *xformOps);

  template <typename T>
  bool ReconstructPrim(
      const std::map<std::string, Property> &properties,
      const std::vector<std::pair<ListEditQual, Reference>> &references,
      T *out);

  ///
  /// TinyUSDZ reconstruct some frequently used shaders(e.g. UsdPreviewSurface)
  /// here, not in Tydra
  ///
  template <typename T>
  bool ReconstructShader(
      const std::map<std::string, Property> &properties,
      const std::vector<std::pair<ListEditQual, Reference>> &references,
      T *out);

  // T = Prim class(e.g. Xform)
  template <typename T>
  bool RegisterReconstructCallback() {
    _parser.RegisterPrimConstructFunction(
        PrimTypeTrait<T>::prim_type_name,
        [&](const Path &full_path, const Path &prim_name, const int64_t primIdx,
            const int64_t parentPrimIdx,
            const std::map<std::string, Property> &properties,
            const std::vector<std::pair<ListEditQual, Reference>> &references,
            const ascii::AsciiParser::PrimMetaInput &in_meta)
            -> nonstd::expected<bool, std::string> {
          if (!prim_name.IsValid()) {
            return nonstd::make_unexpected("Invalid Prim name: " +
                                           prim_name.full_path_name());
          }
          if (prim_name.IsAbsolutePath() || prim_name.IsRootPath()) {
            return nonstd::make_unexpected(
                "Prim name should not starts with '/' or contain `/`: Prim "
                "name = " +
                prim_name.full_path_name());
          }

          if (!prim_name.GetPropPart().empty()) {
            return nonstd::make_unexpected(
                "Prim path should not contain property part(`.`): Prim name "
                "= " +
                prim_name.full_path_name());
          }

          if (primIdx < 0) {
            return nonstd::make_unexpected(
                "Unexpected primIdx value. primIdx must be positive.");
          }

          T prim;

          if (!ReconstructPrimMeta(in_meta, &prim.meta)) {
            return nonstd::make_unexpected(
                "Failed to process Prim metadataum.");
          }

          DCOUT("primType = " << value::TypeTrait<T>::type_name()
                              << ", node.size "
                              << std::to_string(_prim_nodes.size())
                              << ", primIdx = " << primIdx
                              << ", parentPrimIdx = " << parentPrimIdx);

          DCOUT("full_path = " << full_path.full_path_name());
          DCOUT("primName = " << prim_name.full_path_name());

          bool ret = ReconstructPrim<T>(properties, references, &prim);

          if (!ret) {
            return nonstd::make_unexpected("Failed to reconstruct Prim: " +
                                           prim_name.full_path_name());
          }

          prim.name = prim_name.GetPrimPart();

          // Add to scene graph.
          // NOTE: Scene graph is constructed from bottom up manner(Children
          // first), so add this primIdx to parent's children.
          if (size_t(primIdx) >= _prim_nodes.size()) {
            _prim_nodes.resize(size_t(primIdx) + 1);
          }
          DCOUT("sz " << std::to_string(_prim_nodes.size())
                      << ", primIdx = " << primIdx);

          _prim_nodes[size_t(primIdx)].prim = std::move(prim);
          DCOUT("prim[" << primIdx << "].ty = "
                        << _prim_nodes[size_t(primIdx)].prim.type_name());
          _prim_nodes[size_t(primIdx)].parent = parentPrimIdx;

          if (parentPrimIdx == -1) {
            _toplevel_prims.push_back(size_t(primIdx));
          } else {
            _prim_nodes[size_t(parentPrimIdx)].children.push_back(
                size_t(primIdx));
          }

          return true;
        });

    return true;
  }

  void ImportScene(tinyusdz::Stage &scene) { _imported_scene = scene; }

  bool HasPath(const std::string &path) {
    Path p(path, "");
    TokenizedPath tokPath(p);
    (void)tokPath;
    PUSH_ERROR_AND_RETURN("TODO: HasPath()");
  }

  void StageMetaProcessor() {
    _parser.RegisterStageMetaProcessFunction(
        [&](const ascii::AsciiParser::StageMetas &metas) {
          DCOUT("StageMeta CB:");

          _stage.GetMetas().doc = metas.doc;
          if (metas.upAxis) {
            _stage.GetMetas().upAxis = metas.upAxis.value();
          }

          _stage.GetMetas().defaultPrim = metas.defaultPrim;
          if (metas.metersPerUnit) {
            _stage.GetMetas().metersPerUnit = metas.metersPerUnit.value();
          }

          if (metas.timeCodesPerSecond) {
            _stage.GetMetas().timeCodesPerSecond =
                metas.timeCodesPerSecond.value();
          }

          if (metas.startTimeCode) {
            _stage.GetMetas().startTimeCode = metas.startTimeCode.value();
          }

          if (metas.endTimeCode) {
            _stage.GetMetas().endTimeCode = metas.endTimeCode.value();
          }

          _stage.GetMetas().customLayerData = metas.customLayerData;

          _stage.GetMetas().stringData = metas.strings;

          return true;  // ok
        });
  }

  void RegisterPrimIdxAssignCallback() {
    _parser.RegisterPrimIdxAssignFunction([&](const int64_t parentPrimIdx) {
      size_t idx = _prim_nodes.size();

      DCOUT("parentPrimIdx: " << parentPrimIdx << ", idx = " << idx);

      _prim_nodes.resize(idx + 1);

      // if (parentPrimIdx < 0) { // root
      //   // allocate empty prim to reserve _prim_nodes[idx]
      //   _prim_nodes.resize(idx + 1);
      //   DCOUT("resize to : " << (idx + 1));
      // }

      return idx;
    });
  }

  bool ReconstructPrimMeta(const ascii::AsciiParser::PrimMetaInput &in_meta,
                           PrimMeta *out) {

    auto ApiSchemaHandler = [](const std::string &tok)
        -> nonstd::expected<APISchemas::APIName, std::string> {
      using EnumTy = std::pair<APISchemas::APIName, const char *>;
      const std::vector<EnumTy> enums = {
          std::make_pair(APISchemas::APIName::SkelBindingAPI, "SkelBindingAPI"),
          std::make_pair(APISchemas::APIName::MaterialBindingAPI,
                         "MaterialBindingAPI"),
      };
      return EnumHandler<APISchemas::APIName>("apiSchemas", tok, enums);
    };

    DCOUT("ReconstructPrimMeta");
    for (const auto &meta : in_meta) {
      DCOUT("meta.name = " << meta.first);

      const auto &listEditQual = std::get<0>(meta.second);
      const MetaVariable &var = std::get<1>(meta.second);

      if (meta.first == "active") {
        DCOUT("active. type = " << var.type);
        if (var.type == "bool") {
          if (auto pv = var.Get<bool>()) {
            out->active = pv.value();
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `active` metadataum is not type `bool`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `active` metadataum is not type `bool`. got `"
              << var.type << "`.");
        }

      } else if (meta.first == "kind") {
        // std::tuple<ListEditQual, MetaVariable>
        // TODO: list-edit qual
        DCOUT("kind. type = " << var.type);
        if (var.type == "token") {
          if (auto pv = var.Get<value::token>()) {
            const value::token tok = pv.value();
            if (tok.str() == "subcomponent") {
              out->kind = Kind::Subcomponent;
            } else if (tok.str() == "component") {
              out->kind = Kind::Component;
            } else if (tok.str() == "model") {
              out->kind = Kind::Model;
            } else if (tok.str() == "group") {
              out->kind = Kind::Group;
            } else if (tok.str() == "assembly") {
              out->kind = Kind::Assembly;
            } else {
              PUSH_ERROR_AND_RETURN("Invalid token for `kind` metadataum.");
            }
            DCOUT("Added kind: " << to_string(out->kind.value()));
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `kind` metadataum is not type `token`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `kind` metadataum is not type `token`. got `"
              << var.type << "`.");
        }
      } else if (meta.first == "customData") {
        DCOUT("customData. type = " << var.type);
        if (var.type == "dictionary") {
          if (auto pv = var.Get<CustomDataType>()) {
            out->customData = pv.value();
          } else {
            PUSH_ERROR_AND_RETURN_TAG(kTag,
                "(Internal error?) `customData` metadataum is not type "
                "`dictionary`. got type `"
                << var.type << "`");
          }

        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `customData` metadataum is not type "
              "`dictionary`. got type `"
              << var.type << "`");
        }
      } else if (meta.first == "apiSchemas") {
        DCOUT("apiSchemas. type = " << var.type);
        if (var.type == "token[]") {
          APISchemas apiSchemas;
          if (listEditQual != ListEditQual::Prepend) {
            PUSH_ERROR_AND_RETURN("(PrimMeta) " << "ListEdit op for `apiSchemas` must be `prepend` in TinyUSDZ, but got `" << to_string(listEditQual) << "`");
          }
          apiSchemas.qual = listEditQual;

          if (auto pv = var.Get<std::vector<value::token>>()) {

            for (const auto &item : pv.value()) {
              // TODO: Multi-apply schema(instance name)
              auto ret = ApiSchemaHandler(item.str());
              if (ret) {
                apiSchemas.names.push_back({ret.value(), /* instanceName */""});
              } else {
                PUSH_ERROR_AND_RETURN("(PrimMeta) " << ret.error());
              }
            }
          } else {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "(Internal error?) `apiSchemas` metadataum is not type "
            "`token[]`. got type `"
            << var.TypeName() << "`");
          }

          out->apiSchemas = std::move(apiSchemas);
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "(Internal error?) `apiSchemas` metadataum is not type "
          "`token[]`. got type `"
          << var.type << "`");
        }
      } else {
        // string-only data?
        if (auto pv = var.Get<StringData>()) {
          out->stringData.push_back(pv.value());
        } else {
          PUSH_WARN("TODO: Prim metadataum : " << meta.first);
        }
      }
    }

    return true;
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

  ///
  /// Valid after `ReconstructStage`.
  ///
  const Stage &GetStage() const { return _stage; }

  ///
  /// Valid after `Read`.
  ///
  bool ReconstructStage();

 private:
  bool stage_reconstructed_{false};

#if 0
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
#endif

  ///
  /// -- Members --
  ///

  // TODO: Remove
  // std::set<std::string> _node_types;

  std::stack<ParseState> parse_stack;

  std::string _base_dir;  // Used for importing another USD file

  nonstd::optional<tinyusdz::Stage> _imported_scene;  // Imported scene.

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
  std::vector<size_t> _toplevel_prims;  // index to _prim_nodes

  // Flattened array of prim nodes.
  std::vector<PrimNode> _prim_nodes;

  // Path(prim part only) -> index to _prim_nodes[]
  std::map<std::string, size_t> _primpath_to_prim_idx_map;

  // load flags
  bool _sub_layered{false};
  bool _referenced{false};
  bool _payloaded{false};

  std::string _defaultPrim;

  ascii::AsciiParser _parser;

};  // namespace usda


///
/// -- Impl reconstruct
//

namespace {

void ReconstructNodeRec(const size_t idx,
                        const std::vector<PrimNode> &prim_nodes, Prim &parent) {
  const auto &node = prim_nodes[idx];

  Prim prim(node.prim);
  DCOUT("prim[" << idx << "].type = " << node.prim.type_name());

  for (const auto &cidx : node.children) {
    ReconstructNodeRec(cidx, prim_nodes, prim);
  }

  parent.children.emplace_back(std::move(prim));
}

}  // namespace

bool USDAReader::Impl::ReconstructXformOpProperties(
  std::set<std::string> &table, /* inout */
  const std::map<std::string, Property> &properties,
  std::vector<XformOp> *xformOps)
{

  constexpr auto kTranslate = "xformOp:translate";
  constexpr auto kTransform = "xformOp:transform";
  constexpr auto kScale = "xformOp:scale";
  constexpr auto kRotateX = "xformOp:rotateX";
  constexpr auto kRotateY = "xformOp:rotateY";
  constexpr auto kRotateZ = "xformOp:rotateZ";
  constexpr auto kRotateXYZ = "xformOp:rotateXYZ";
  constexpr auto kRotateXZY = "xformOp:rotateXZY";
  constexpr auto kRotateYXZ = "xformOp:rotateYXZ";
  constexpr auto kRotateYZX = "xformOp:rotateYZX";
  constexpr auto kRotateZXY = "xformOp:rotateZXY";
  constexpr auto kRotateZYX = "xformOp:rotateZYX";
  constexpr auto kOrient = "xformOp:orient";

  // false : no prefix found.
  // true : return suffix(first namespace ':' is ommited.).
  // - "" for prefix only "xformOp:translate"
  // - "blender:pivot" for "xformOp:translate:blender:pivot"
  auto SplitXformOpToken =
      [](const std::string &s,
         const std::string &prefix) -> nonstd::optional<std::string> {
    if (startsWith(s, prefix)) {
      if (s.compare(prefix) == 0) {
        // prefix only.
        return std::string();  // empty suffix
      } else {
        std::string suffix = removePrefix(s, prefix);
        DCOUT("suffix = " << suffix);
        if (suffix.length() == 1) {  // maybe namespace only.
          return nonstd::nullopt;
        }

        // remove namespace ':'
        if (suffix[0] == ':') {
          // ok
          suffix.erase(0, 1);
        } else {
          return nonstd::nullopt;
        }

        return std::move(suffix);
      }
    }

    return nonstd::nullopt;
  };


  // Lookup xform values from `xformOpOrder`
  // TODO: TimeSamples, Connection
  if (properties.count("xformOpOrder")) {
    // array of string
    auto prop = properties.at("xformOpOrder");
    if (prop.IsRel()) {
      PUSH_ERROR_AND_RETURN("Relation for `xformOpOrder` is not supported.");
    } else if (auto pv =
                   prop.attrib.var.get_value<std::vector<value::token>>()) {
      // TODO: 'uniform' qualifier check?
      for (size_t i = 0; i < pv.value().size(); i++) {
        const auto &item = pv.value()[i];

        XformOp op;

        std::string tok = item.str();
        DCOUT("xformOp token = " << tok);

        if (startsWith(tok, "!resetXformStack!")) {
          if (tok.compare("!resetXformStack!") != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must be defined solely(not to be a prefix "
                "to \"xformOp:*\")");
          }

          if (i != 0) {
            PUSH_ERROR_AND_RETURN(
                "`!resetXformStack!` must appear at the first element of "
                "xformOpOrder list.");
          }

          op.op = XformOp::OpType::ResetXformStack;
          xformOps->emplace_back(op);

          // skip looking up property
          continue;
        }

        if (startsWith(tok, "!invert!")) {
          DCOUT("invert!");
          op.inverted = true;
          tok = removePrefix(tok, "!invert!");
          DCOUT("tok = " << tok);
        }

        auto it = properties.find(tok);
        if (it == properties.end()) {
          PUSH_ERROR_AND_RETURN("Property `" + tok + "` not found.");
        }
        if (it->second.IsConnection()) {
          PUSH_ERROR_AND_RETURN(
              "Connection(.connect) of xformOp property is not yet supported: "
              "`" +
              tok + "`");
        }
        const PrimAttrib &attr = it->second.attrib;

        // Check `xformOp` namespace
        if (auto xfm = SplitXformOpToken(tok, kTransform)) {
          op.op = XformOp::OpType::Transform;
          op.suffix = xfm.value();  // may contain nested namespaces

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::matrix4d>()) {
            op.set_scalar(pvd.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:transform` must be type `matrix4d`, but got type `" +
                attr.var.type_name() + "`.");
          }

        } else if (auto tx = SplitXformOpToken(tok, kTranslate)) {
          op.op = XformOp::OpType::Translate;
          op.suffix = tx.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:translate` must be type `double3` or `float3`, but "
                "got type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto scale = SplitXformOpToken(tok, kScale)) {
          op.op = XformOp::OpType::Scale;
          op.suffix = scale.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:scale` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotX = SplitXformOpToken(tok, kRotateX)) {
          op.op = XformOp::OpType::RotateX;
          op.suffix = rotX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateX` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotY = SplitXformOpToken(tok, kRotateY)) {
          op.op = XformOp::OpType::RotateY;
          op.suffix = rotX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateY` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotZ = SplitXformOpToken(tok, kRotateZ)) {
          op.op = XformOp::OpType::RotateY;
          op.suffix = rotZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<double>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<float>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZ` must be type `double` or `float`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateXYZ = SplitXformOpToken(tok, kRotateXYZ)) {
          op.op = XformOp::OpType::RotateXYZ;
          op.suffix = rotateXYZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateXZY = SplitXformOpToken(tok, kRotateXZY)) {
          op.op = XformOp::OpType::RotateXZY;
          op.suffix = rotateXZY.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateYXZ = SplitXformOpToken(tok, kRotateYXZ)) {
          op.op = XformOp::OpType::RotateYXZ;
          op.suffix = rotateYXZ.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateYZX = SplitXformOpToken(tok, kRotateYZX)) {
          op.op = XformOp::OpType::RotateYZX;
          op.suffix = rotateYZX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateZXY = SplitXformOpToken(tok, kRotateZXY)) {
          op.op = XformOp::OpType::RotateZXY;
          op.suffix = rotateZXY.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto rotateZYX = SplitXformOpToken(tok, kRotateZYX)) {
          op.op = XformOp::OpType::RotateZYX;
          op.suffix = rotateZYX.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::double3>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::float3>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else if (auto orient = SplitXformOpToken(tok, kOrient)) {
          op.op = XformOp::OpType::Orient;
          op.suffix = orient.value();

          if (attr.var.is_timesample()) {
            op.set_timesamples(attr.var.var);
          } else if (auto pvd = attr.var.get_value<value::quatf>()) {
            op.set_scalar(pvd.value());
          } else if (auto pvf = attr.var.get_value<value::quatd>()) {
            op.set_scalar(pvf.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                "type `" +
                attr.var.type_name() + "`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "token for xformOpOrder must have namespace `xformOp:***`, or .");
        }

        xformOps->emplace_back(op);
        table.insert(tok);
      }

    } else {
      PUSH_ERROR_AND_RETURN(
          "`xformOpOrder` must be type `token[]` but got type `"
          << prop.attrib.var.type_name() << "`.");
    }
  }

  return true;
}


bool USDAReader::Impl::ReconstructStage() {
  _stage.GetRootPrims().clear();

  for (const auto &idx : _toplevel_prims) {
    DCOUT("Toplevel prim idx: " << std::to_string(idx));

    const auto &node = _prim_nodes[idx];

    Prim prim(node.prim);
    DCOUT("prim[" << idx << "].type = " << node.prim.type_name());

    for (const auto &cidx : node.children) {
#if 0
      const auto &child_node = _prim_nodes[cidx];
      DCOUT("prim[" << idx << "].children = " << cidx << ", type = " << child_node.prim.type_name());

      prim.children.emplace_back(std::move(child_node.prim));
#else
      ReconstructNodeRec(cidx, _prim_nodes, prim);
#endif
    }

    DCOUT("prim[" << idx << "].num_children = " << prim.children.size());

    size_t sz = _stage.GetRootPrims().size();
    _stage.GetRootPrims().emplace_back(std::move(prim));

    DCOUT("num_children = " << _stage.GetRootPrims()[sz].children.size());
  }

  return true;
}


template <typename T>
struct AttribType;

template <typename T>
struct AttribType {
  using type = T;
  static std::string type_name() { return value::TypeTrait<T>::type_name(); }
};

// `nonstd::optional<Connection<T>>` -> T
template <typename T>
struct AttribType<nonstd::optional<Connection<T>>> {
  using type = T;
  static std::string type_name() { return value::TypeTrait<T>::type_name(); }
};

// `nonstd::optional<T>` -> T
template <typename T>
struct AttribType<nonstd::optional<T>> {
  using type = T;
  static std::string type_name() { return value::TypeTrait<T>::type_name(); }
};

template <typename T>
struct AttribType<TypedAttribute<T>> {
  using type = T;
  static std::string type_name() { return value::TypeTrait<T>::type_name(); }
};

// `AttribWithFallback<T>` -> T
template <typename T>
struct AttribType<AttribWithFallback<T>> {
  using type = T;
  static std::string type_name() { return value::TypeTrait<T>::type_name(); }
};


template<typename T>
static nonstd::optional<Animatable<T>> ConvertToAnimatable(const primvar::PrimVar &var)
{
  Animatable<T> dst;

  if (!var.is_valid()) {
    DCOUT("is_valid failed");
    return nonstd::nullopt;
  }

  if (var.is_scalar()) {

    if (auto pv = var.get_value<T>()) {
      dst.value = pv.value();
      dst.blocked = false;

      return std::move(dst);
    }
  } else if (var.is_timesample()) {
    for (size_t i = 0; i < var.var.times.size(); i++) {
      double t = var.var.times[i];

      // Attribute Block?
      if (auto pvb = var.get_ts_value<value::Block>(i)) {
        dst.ts.AddBlockedSample(t);
      } else if (auto pv = var.get_ts_value<T>(i)) {
        dst.ts.AddSample(t, pv.value());
      } else {
        // Type mismatch
        DCOUT(i << "/" << var.var.times.size() << " type mismatch.");
        return nonstd::nullopt;
      }
    }

    return std::move(dst);
  }

  DCOUT("???");
  return nonstd::nullopt;
}

struct ParseResult
{
  enum class ResultCode
  {
    Success,
    Unmatched,
    AlreadyProcessed,
    TypeMismatch,
    InternalError,
  };

  ResultCode code;
  std::string err;
};

template<typename T>
static ParseResult ParseTypedAttribute(std::set<std::string> &table, /* inout */
  const std::string prop_name,
  const Property &prop,
  const std::string &name,
  TypedAttribute<T> &target) /* out */
{
  ParseResult ret;

  if (prop_name.compare(name + ".connect") == 0) {
    std::string propname = removeSuffix(name, ".connect");
    if (table.count(propname)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    if (auto pv = prop.GetConnectionTarget()) {
      target.target = pv.value();
      target.uniform = prop.attrib.uniform;
      target.meta = prop.attrib.meta;
      table.insert(propname);
      ret.code = ParseResult::ResultCode::Success;
      return ret;
    }
  } else if (prop_name.compare(name) == 0) {
    if (table.count(name)) {
      ret.code = ParseResult::ResultCode::AlreadyProcessed;
      return ret;
    }
    const PrimAttrib &attr = prop.attrib;

    DCOUT("attrib.type = " << AttribType<T>::type_name() << ", attr.var.type= " << attr.var.type_name());

    // Type info is stored in Attribute::type_name
    if (AttribType<T>::type_name() == attr.var.type_name()) {
      if (prop.type == Property::Type::EmptyAttrib) {
        target.define_only = true;
        target.uniform = attr.uniform;
        target.meta = attr.meta;
        table.insert(name);
      } else if (prop.type == Property::Type::Attrib) {
        DCOUT("Adding prop: " << name);

        Animatable<T> anim;

        if (attr.blocked) {
          anim.blocked = true;
        } else {
          if (auto av = ConvertToAnimatable<T>(attr.var)) {
            anim = av.value();
          } else {
            // Conversion failed.
            DCOUT("ConvertToAnimatable failed.");
            ret.code = ParseResult::ResultCode::InternalError;
            ret.err = "Converting Attribute data failed. Maybe TimeSamples have values with different types?";
            return ret;
          }
        }

        target.value = anim;
        target.uniform = attr.uniform;
        target.meta = attr.meta;
        table.insert(name);
        ret.code = ParseResult::ResultCode::Success;
        return ret;
      } else {
        DCOUT("Invalid Property.type");
        ret.err = "Invalid Property type(internal error)";
        ret.code = ParseResult::ResultCode::InternalError;
        return ret;
      }
    } else {
      DCOUT("tyname = " << AttribType<T>::type_name() << ", attr.type = " << attr.var.type_name());
      ret.code = ParseResult::ResultCode::TypeMismatch;
      std::stringstream ss;
      ss  << "Property type mismatch. " << name << " expects type `"
              << AttribType<T>::type_name()
              << "` but defined as type `" << attr.var.type_name() << "`";
      ret.err = ss.str();
      return ret;
    }
  }

  ret.code = ParseResult::ResultCode::Unmatched;
  return ret;
}



// TODO(syoyo): TimeSamples, Reference
#define PARSE_TYPED_PROPERTY(__table, __prop, __name, __klass, __target)       \
  if (__prop.first.compare(__name) == 0) {                                     \
    if (__table.count(__name)) { \
      continue; \
    } \
    const PrimAttrib &attr = __prop.second.attrib;                             \
    if (auto v = attr.var.get_value<AttribType<decltype(__target)>::type>()) { \
      DCOUT("Add prop: " << __name);                                           \
      __target.value = v.value();                                              \
      __target.uniform = attr.uniform; \
      __target.meta = attr.meta;                                               \
      __table.insert(__name);                                                  \
    } else {                                                                   \
      PUSH_ERROR_AND_RETURN(                                                   \
          "(" << value::TypeTrait<__klass>::type_name()                        \
              << ") Property type mismatch. " << __name << " expects type `"   \
              << AttribType<decltype(__target)>::type_name()                   \
              << "` but defined as type `" << attr.var.type_name() << "`");    \
    }                                                                          \
  } else

#if 0
// e.g. "float2 inputs:st"
// Attribute type is EmptyAttrib, Attrib(fallback) or Connection(`target` is
// Path)
#define PARSE_TYPED_ATTRIBUTE(__table, __prop, __name, __klass, __target)      \
  if (__prop.first.compare(__name ".connect") == 0) {                          \
    std::string propname = removeSuffix(__name, ".connect");                   \
    if (__table.count(propname)) { \
      continue; \
    } \
    const Property &p = __prop.second;                                         \
    if (auto pv = p.GetConnectionTarget()) {                                   \
      __target.value = pv.value();                                             \
      __target.uniform = p.attrib.uniform; \
      __target.meta = p.attrib.meta;                             \
      __table.insert(propname);                                                \
    } else {                                                                   \
      PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()      \
                                << ") No connection target or invalid syntax " \
                                   "of connection target for attribute `"      \
                                << propname << "`.");                          \
    }                                                                          \
  } else if (__prop.first == __name) {                                         \
    if (__table.count(__name)) { \
      continue; \
    } \
    const Property &p = __prop.second;                                         \
    const PrimAttrib &attr = p.attrib;                                         \
    /* Type info is stored in attrib.type_name */                              \
    if (AttribType<decltype(__target)>::type_name() == attr.type_name) {       \
      if (auto pv = p.GetConnectionTarget()) {                                 \
        __target.value = pv.value();                                           \
        __target.uniform = attr.uniform; \
        __target.meta = attr.meta;                                             \
        __table.insert(__name);                                                \
      } else if (p.type == Property::Type::EmptyAttrib) {                      \
        __target.value = tinyusdz::monostate();                                \
        __target.uniform = attr.uniform; \
        __target.meta = attr.meta;                                             \
        __table.insert(__name);                                                \
      } else {                                                                 \
        PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()    \
                                  << ") TODO: Connection Property `" << __name \
                                  << "` must not be value assigned.");         \
      }                                                                        \
    } else {                                                                   \
      PUSH_ERROR_AND_RETURN(                                                   \
          "(" << value::TypeTrait<__klass>::type_name()                        \
              << ") Property type mismatch. " << __name << " expects type `"   \
              << AttribType<decltype(__target)>::type_name()                   \
              << "` but defined as type `" << attr.type_name << "`");          \
    }                                                                          \
  } else
#else

#define PARSE_TYPED_ATTRIBUTE(__table, __prop, __name, __klass, __target) { \
  ParseResult ret = ParseTypedAttribute(__table, __prop.first, __prop.second, __name, __target); \
  if (ret.code == ParseResult::ResultCode::Success || ret.code == ParseResult::ResultCode::AlreadyProcessed) { \
    continue; /* got it */\
  } else if (ret.code == ParseResult::ResultCode::TypeMismatch) { \
    PUSH_ERROR_AND_RETURN(                                                   \
        "(" << value::TypeTrait<__klass>::type_name()                        \
            << ") " << ret.err); \
  } else if (ret.code == ParseResult::ResultCode::InternalError) { \
    PUSH_ERROR_AND_RETURN("Internal error: " + ret.err); \
  } else { \
    /* go next */ \
  } \
}

#endif

// e.g. "float3 outputs:rgb"
// Attribute type must be EmptyAttrib(No value or connection assigned)
#define PARSE_TYPED_OUTPUT_ATTRIBUTE(__table, __prop, __name, __klass,      \
                                      __target)                              \
  if (__prop.first == __name) {                                              \
    if (__table.count(__name)) { \
      continue; \
    } \
    const Property &p = __prop.second;                                       \
    const PrimAttrib &attr = p.attrib;                                       \
    /* Type info is stored in attrib.type_name */                            \
    if (AttribType<decltype(__target)>::type_name() == attr.type_name) {     \
      if (p.type == Property::Type::EmptyAttrib) {                    \
        __target.meta = attr.meta; \
        __target.set_define_only(); \
        __table.insert(__name);                                              \
      } else {                                                               \
        PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()  \
                                  << ") Attribute `" << __name     \
                                  << "` must be `define-only`(no valur or connection assigned).");       \
      }                                                                      \
    } else {                                                                 \
      PUSH_ERROR_AND_RETURN(                                                 \
          "(" << value::TypeTrait<__klass>::type_name()                      \
              << ") Property type mismatch. " << __name << " expects type `" \
              << AttribType<decltype(__target)>::type_name()                 \
              << "` but defined as type `" << attr.type_name << "`");        \
    }                                                                        \
  } else

// TODO(syoyo): TimeSamples, Reference
#define PARSE_PROPERTY(__table, __prop, __name, __klass, __target)             \
  if (__prop.first == __name) {                                                \
    if (__table.count(__name)) { continue; } \
    const PrimAttrib &attr = __prop.second.attrib;                             \
    if (auto v = attr.var.get_value<AttribType<decltype(__target)>::type>()) { \
      __target = v.value();                                                    \
      /* TODO: attr meta  __target.meta = attr.meta; */ \
      __table.insert(__name);                                                  \
    } else {                                                                   \
      PUSH_ERROR_AND_RETURN(                                                   \
          "(" << value::TypeTrait<__klass>::type_name()                        \
              << ") Property type mismatch. " << __name << " expects type `"   \
              << AttribType<decltype(__target)>::type_name()                   \
              << "` but defined as type `" << attr.var.type_name() << "`");    \
    }                                                                          \
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

#define PARSE_ENUM_PROPETY(__table, __prop, __name, __enum_handler, __klass, \
                           __target)                                         \
  if (__prop.first == __name) {                                              \
    if (__table.count(__name)) { continue; } \
    const PrimAttrib &attr = __prop.second.attrib;                           \
    if (auto tok = attr.var.get_value<value::token>()) {                     \
      auto e = __enum_handler(tok.value().str());                            \
      if (e) {                                                               \
        __target = e.value();                                                \
        /* TODO: attr meta __target.meta = attr.meta;  */                    \
        __table.insert(__name);                                              \
      } else {                                                               \
        PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()  \
                                  << ") " << e.error());                     \
      }                                                                      \
    } else {                                                                 \
      PUSH_ERROR_AND_RETURN("(" << value::TypeTrait<__klass>::type_name()    \
                                << ") Property type mismatch. " << __name    \
                                << " must be type `token`, but got `"        \
                                << attr.var.type_name() << "`.");            \
    }                                                                        \
  } else

// Add custom property(including property with "primvars" prefix)
// Please call this macro after listing up all predefined property using
// `PARSE_PROPERTY` and `PARSE_ENUM_PROPETY`
#define ADD_PROPERY(__table, __prop, __klass, __dst)         \
  /* Check if the property name is a predefined property */  \
  if (!__table.count(__prop.first)) {                        \
    DCOUT("custom property added: name = " << __prop.first); \
    __dst[__prop.first] = __prop.second;                     \
    __table.insert(__prop.first);                            \
  } else

// This code path should not be reached though.
#define PARSE_PROPERTY_END_MAKE_ERROR(__table, __prop)                      \
  if (!__table.count(__prop.first)) {                              \
    PUSH_ERROR_AND_RETURN("Unsupported/unimplemented property: " + \
                          __prop.first);                           \
  }

// This code path should not be reached though.
#define PARSE_PROPERTY_END_MAKE_WARN(__prop) \
  { PUSH_WARN("Unsupported/unimplemented property: " + __prop.first); }

template <>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Xform *xform) {
  (void)xform;


  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  std::set<std::string> table;

  if (!ReconstructXformOpProperties(table, properties, &xform->xformOps)) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct xformOps.");
    return false;
  }

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
#if 0
template <>
bool USDAReader::Impl::RegisterReconstructCallback<GPrim>() {
  // TODO: Move to ReconstructPrim
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
          if (item.second.IsRel()) {
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
#endif

template <>
bool USDAReader::Impl::RegisterReconstructCallback<GeomSubset>() {
  _parser.RegisterPrimConstructFunction(
      "GeomSubset",
      [&](const Path &full_path, const Path &prim_name, const int64_t primIdx,
          const int64_t parentPrimIdx,
          const std::map<std::string, Property> &properties,
          const std::vector<std::pair<ListEditQual, Reference>> &references,
          const ascii::AsciiParser::PrimMetaInput &in_meta)
          -> nonstd::expected<bool, std::string> {
        const Path &parent = full_path.GetParentPrim();
        if (!parent.IsValid()) {
          return nonstd::make_unexpected("Invalid Prim path.");
        }

        if (parent.IsRootPrim()) {
          return nonstd::make_unexpected(
              "GeomSubset must be defined as a child of GeomMesh prim.");
        }

        if (parentPrimIdx < 0) {
          return nonstd::make_unexpected(
              "GeomSubset muet be defined as a child of GeomMesh.");
        }

        if (_prim_nodes.size() < size_t(parentPrimIdx)) {
          return nonstd::make_unexpected(
              "Unexpected parentPrimIdx for GeomSubset.");
        }

        PrimMeta meta;
        if (!ReconstructPrimMeta(in_meta, &meta)) {
          return nonstd::make_unexpected("Failed to process Prim metadataum.");
        }

    // TODO: Construct GeomMesh first
#if 0
        const std::string parent_primpath = parent.GetPrimPart();

        const PrimNode &pnode = _prim_nodes[size_t(parentPrimIdx)];
        auto pmesh = pnode.prim.get_value<GeomMesh>();
        if (!pmesh) {
          return nonstd::make_unexpected(
              "Parent Prim must be GeomMesh, but got " +
              pnode.prim.type_name());
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
            PUSH_WARN("GeomSubset: TODO: " + item.first);
          }
        }

        mesh.geom_subset_children.emplace_back(subset);
#else

        // Add GeomSubset to _prim_nodes.

        GeomSubset subset;

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
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`indices` property must be `int[]` type, but got `" +
                  item.second.attrib.var.type_name() + "`");
            }

          } else if (item.first == "material:binding") {
            if (!item.second.IsRel()) {
              PUSH_ERROR_AND_RETURN(
                  "`material:binding` property as Attribute is not "
                  "supported.");
            }
          } else if (item.first == "familyName") {
            if (item.second.IsRel()) {
              PUSH_ERROR_AND_RETURN(
                  "`familyName` property as Relation is not supported.");
            }

            if (auto pv = item.second.attrib.var.get_value<value::token>()) {
              subset.familyName = pv.value();
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`familyName` property must be `token` type, but got `" +
                  item.second.attrib.var.type_name() + "`");
            }
          } else {
            PUSH_WARN("GeomSubset: TODO: " + item.first);
          }
        }

        subset.name = prim_name.GetPrimPart();
        subset.meta = meta;

        // Add to scene graph.
        // NOTE: Scene graph is constructed from bottom up manner(Children
        // first), so add this primIdx to parent's children.
        if (size_t(primIdx) >= _prim_nodes.size()) {
          _prim_nodes.resize(size_t(primIdx) + 1);
        }
        DCOUT("sz " << std::to_string(_prim_nodes.size())
                    << ", primIdx = " << primIdx);

        _prim_nodes[size_t(primIdx)].prim = std::move(subset);
        DCOUT("prim[" << primIdx << "].ty = "
                      << _prim_nodes[size_t(primIdx)].prim.type_name());
        _prim_nodes[size_t(primIdx)].parent = parentPrimIdx;

        if (parentPrimIdx == -1) {
          _toplevel_prims.push_back(size_t(primIdx));
        } else {
          _prim_nodes[size_t(parentPrimIdx)].children.push_back(
              size_t(primIdx));
        }

#endif

        return true;
      });

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GPrim *gprim) {
  (void)gprim;

  DCOUT("TODO: Reconstruct GPrim.");

  PUSH_WARN("TODO: Reconstruct GPrim.");

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomSphere *sphere) {
  (void)sphere;

  constexpr auto kMaterialBinding = "material:binding";

  DCOUT("Reconstruct Sphere.");

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

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomSphere, sphere->radius)
      ADD_PROPERY(table, prop, GeomSphere, sphere->props)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
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

template <>
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

  constexpr auto kMaterialBinding = "material:binding";

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            cone->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCone, cone->radius)
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCone, cone->height)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

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

template <>
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

  constexpr auto kMaterialBinding = "material:binding";

  //
  // pxrUSD says... "If you author size you must also author extent."
  //
  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            cube->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "size", GeomCube, cube->size)
      ADD_PROPERY(table, prop, GeomCube, cube->props)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

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

template <>
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
          if (prop.second.IsRel()) {
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

  constexpr auto kMaterialBinding = "material:binding";

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            capsule->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCapsule, capsule->radius)
      PARSE_TYPED_PROPERTY(table, prop, "height", GeomCapsule, capsule->height)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

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

template <>
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
#endif

  constexpr auto kMaterialBinding = "material:binding";

  std::set<std::string> table;
  for (const auto &prop : properties) {
    DCOUT("prop: " << prop.first);
    if (prop.second.IsRel()) {
      if (prop.first == kMaterialBinding) {
        if (auto pv = prop.second.attrib.var.get_value<Relation>()) {
          MaterialBindingAPI m;
#if 0
          if (auto pathv = pv.value().targets.get<Path>()) {
            m.binding = pathv.value();
            sphere->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#else
          if (pv.value().IsPath()) {
            m.binding = pv.value().targetPath;
            cylinder->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN(kMaterialBinding << " must be Path.");
          }
#endif
        } else {
          PUSH_WARN(kMaterialBinding << " must be Relationship ");
        }
      } else {
        PUSH_WARN("TODO:" << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "radius", GeomCylinder,
                           cylinder->radius)
      PARSE_TYPED_PROPERTY(table, prop, "height", GeomCylinder,
                           cylinder->height)
      PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
    }
  }

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

template <>
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
          if (prop.IsRel()) {
            PUSH_WARN("TODO: points Rel\n");
          } else {
            const PrimAttrib &attr = prop.attrib;
            // PrimVar
            DCOUT("points.type:" + attr.var.type_name());
            if (attr.var.is_scalar()) {
              auto p = attr.var.get_value<std::vector<value::point3f>>();
              if (p) {
                mesh->points.value = p.value();
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

  auto SubdivisioSchemeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::SubdivisionScheme, std::string> {
    using EnumTy = std::pair<GeomMesh::SubdivisionScheme, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::SubdivisionScheme::None, "none"),
        std::make_pair(GeomMesh::SubdivisionScheme::CatmullClark,
                       "catmullClark"),
        std::make_pair(GeomMesh::SubdivisionScheme::Loop, "loop"),
        std::make_pair(GeomMesh::SubdivisionScheme::Bilinear, "bilinear"),
    };
    return EnumHandler<GeomMesh::SubdivisionScheme>("subdivisionScheme", tok,
                                                    enums);
  };

  auto InterpolateBoundaryHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::InterpolateBoundary, std::string> {
    using EnumTy = std::pair<GeomMesh::InterpolateBoundary, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::InterpolateBoundary::None, "none"),
        std::make_pair(GeomMesh::InterpolateBoundary::EdgeAndCorner,
                       "edgeAndCorner"),
        std::make_pair(GeomMesh::InterpolateBoundary::EdgeOnly, "edgeOnly"),
    };
    return EnumHandler<GeomMesh::InterpolateBoundary>("interpolateBoundary",
                                                      tok, enums);
  };

  auto FacevaryingLinearInterpolationHandler = [](const std::string &tok)
      -> nonstd::expected<GeomMesh::FacevaryingLinearInterpolation,
                          std::string> {
    using EnumTy =
        std::pair<GeomMesh::FacevaryingLinearInterpolation, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::CornersPlus1,
                       "cornersPlus1"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::CornersPlus2,
                       "cornersPlus2"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::CornersOnly,
                       "cornersOnly"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::Boundaries,
                       "boundaries"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::None, "none"),
        std::make_pair(GeomMesh::FacevaryingLinearInterpolation::All, "all"),
    };
    return EnumHandler<GeomMesh::FacevaryingLinearInterpolation>(
        "facevaryingLinearInterpolation", tok, enums);
  };

  std::set<std::string> table;

  for (const auto &prop : properties) {
    if (prop.second.IsRel()) {
      if (prop.first == "material:binding") {
        // Must be relation of type Path.
        if (prop.second.IsRel() && prop.second.IsEmpty()) {
          PUSH_ERROR_AND_RETURN(
              "`material:binding` must be a Relation with Path target.");
        }

        {
          const Relation &rel = prop.second.rel;
          if (rel.IsPath()) {
            DCOUT("materialBinding");
            MaterialBindingAPI m;
            m.binding = rel.targetPath;
            mesh->materialBinding = m;
          } else {
            PUSH_ERROR_AND_RETURN("`material:binding` target must be Path.");
          }
        }
      } else if (prop.first == "skel:skeleton") {
        // Must be relation of type Path.
        if (prop.second.IsRel() && prop.second.IsEmpty()) {
          PUSH_ERROR_AND_RETURN(
              "`skel:skeleton` must be a Relation with Path target.");
        }

        {
          const Relation &rel = prop.second.rel;
          if (rel.IsPath()) {
            DCOUT("skelBinding");
            mesh->skeleton = rel.targetPath;
          } else {
            PUSH_ERROR_AND_RETURN("`skel:skeleton` target must be Path.");
          }
        }

      } else {
        PUSH_WARN("TODO: rel : " << prop.first);
      }
    } else {
      PARSE_TYPED_PROPERTY(table, prop, "points", GeomMesh, mesh->points)
      PARSE_TYPED_PROPERTY(table, prop, "normals", GeomMesh, mesh->normals)
      PARSE_TYPED_PROPERTY(table, prop, "faceVertexCounts", GeomMesh,
                           mesh->faceVertexCounts)
      PARSE_TYPED_PROPERTY(table, prop, "faceVertexIndices", GeomMesh,
                           mesh->faceVertexIndices)
      // Subd
      PARSE_TYPED_PROPERTY(table, prop, "cornerIndices", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "cornerSharpnesses", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "creaseIndices", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "creaseLengths", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "creaseSharpnesses", GeomMesh,
                           mesh->cornerIndices)
      PARSE_TYPED_PROPERTY(table, prop, "holeIndices", GeomMesh,
                           mesh->cornerIndices)
      //
      PARSE_PROPERTY(table, prop, "doubleSided", GeomMesh, mesh->doubleSided)
      PARSE_ENUM_PROPETY(table, prop, "subdivisionScheme",
                         SubdivisioSchemeHandler, GeomMesh,
                         mesh->subdivisionScheme)
      PARSE_ENUM_PROPETY(table, prop, "interpolateBoundary",
                         InterpolateBoundaryHandler, GeomMesh,
                         mesh->interpolateBoundary)
      PARSE_ENUM_PROPETY(table, prop, "facevaryingLinearInterpolation",
                         FacevaryingLinearInterpolationHandler, GeomMesh,
                         mesh->facevaryingLinearInterpolation)
      ADD_PROPERY(table, prop, GeomMesh, mesh->props)
      PARSE_PROPERTY_END_MAKE_WARN(prop)
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

template <>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomBasisCurves *curves) {
  DCOUT("GeomBasisCurves");

  auto BasisHandler = [](const std::string &tok)
      -> nonstd::expected<GeomBasisCurves::Basis, std::string> {
    using EnumTy = std::pair<GeomBasisCurves::Basis, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomBasisCurves::Basis::Bezier, "bezier"),
        std::make_pair(GeomBasisCurves::Basis::Bspline, "bspline"),
        std::make_pair(GeomBasisCurves::Basis::CatmullRom, "catmullRom"),
    };

    return EnumHandler<GeomBasisCurves::Basis>("basis", tok, enums);
  };

  auto TypeHandler = [](const std::string &tok)
      -> nonstd::expected<GeomBasisCurves::Type, std::string> {
    using EnumTy = std::pair<GeomBasisCurves::Type, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomBasisCurves::Type::Cubic, "cubic"),
        std::make_pair(GeomBasisCurves::Type::Linear, "linear"),
    };

    return EnumHandler<GeomBasisCurves::Type>("type", tok, enums);
  };

  auto WrapHandler = [](const std::string &tok)
      -> nonstd::expected<GeomBasisCurves::Wrap, std::string> {
    using EnumTy = std::pair<GeomBasisCurves::Wrap, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(GeomBasisCurves::Wrap::Nonperiodic, "nonperiodic"),
        std::make_pair(GeomBasisCurves::Wrap::Periodic, "periodic"),
        std::make_pair(GeomBasisCurves::Wrap::Pinned, "periodic"),
    };

    return EnumHandler<GeomBasisCurves::Wrap>("wrap", tok, enums);
  };

  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, "curveVertexCounts", GeomBasisCurves,
                         curves->curveVertexCounts)
    PARSE_TYPED_PROPERTY(table, prop, "points", GeomBasisCurves, curves->points)
    PARSE_TYPED_PROPERTY(table, prop, "velocities", GeomBasisCurves,
                         curves->velocities)
    PARSE_TYPED_PROPERTY(table, prop, "normals", GeomBasisCurves,
                         curves->normals)
    PARSE_TYPED_PROPERTY(table, prop, "accelerations", GeomBasisCurves,
                         curves->accelerations)
    PARSE_TYPED_PROPERTY(table, prop, "widths", GeomBasisCurves, curves->widths)
    PARSE_ENUM_PROPETY(table, prop, "type", TypeHandler, GeomBasisCurves,
                       curves->type)
    PARSE_ENUM_PROPETY(table, prop, "basis", BasisHandler, GeomBasisCurves,
                       curves->basis)
    PARSE_ENUM_PROPETY(table, prop, "wrap", WrapHandler, GeomBasisCurves,
                       curves->wrap)

    ADD_PROPERY(table, prop, GeomBasisCurves, curves->props);

    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCamera *camera) {
  auto ProjectionHandler = [](const std::string &tok)
      -> nonstd::expected<GeomCamera::Projection, std::string> {
    using EnumTy = std::pair<GeomCamera::Projection, const char *>;
    constexpr std::array<EnumTy, 2> enums = {
        std::make_pair(GeomCamera::Projection::perspective, "perspective"),
        std::make_pair(GeomCamera::Projection::orthographic, "orthographic"),
    };

    auto ret =
        CheckAllowedTokens<GeomCamera::Projection, enums.size()>(enums, tok);
    if (!ret) {
      return nonstd::make_unexpected(ret.error());
    }

    for (auto &item : enums) {
      if (tok == item.second) {
        return item.first;
      }
    }

    // Should never reach here, though.
    return nonstd::make_unexpected(
        quote(tok) + " is invalid token for `projection` propety");
  };

  std::set<std::string> table;
  for (const auto &prop : properties) {
    PARSE_PROPERTY(table, prop, "focalLength", GeomCamera, camera->focalLength)
    PARSE_PROPERTY(table, prop, "focusDistance", GeomCamera,
                   camera->focusDistance)
    PARSE_PROPERTY(table, prop, "exposure", GeomCamera, camera->exposure)
    PARSE_PROPERTY(table, prop, "fStop", GeomCamera, camera->fStop)
    PARSE_PROPERTY(table, prop, "horizontalAperture", GeomCamera,
                   camera->horizontalAperture)
    PARSE_PROPERTY(table, prop, "horizontalApertureOffset", GeomCamera,
                   camera->horizontalApertureOffset)
    PARSE_PROPERTY(table, prop, "horizontalApertureOffset", GeomCamera,
                   camera->horizontalApertureOffset)
    PARSE_PROPERTY(table, prop, "clippingRange", GeomCamera,
                   camera->clippingRange)
    PARSE_PROPERTY(table, prop, "clippingPlanes", GeomCamera,
                   camera->clippingPlanes)
    PARSE_PROPERTY(table, prop, "shutter:open", GeomCamera, camera->shutterOpen)
    PARSE_PROPERTY(table, prop, "shutter:close", GeomCamera,
                   camera->shutterClose)
    PARSE_ENUM_PROPETY(table, prop, "projection", ProjectionHandler, GeomCamera,
                       camera->projection)
    ADD_PROPERY(table, prop, GeomCamera, camera->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<LuxSphereLight>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxSphereLight *light) {
  std::set<std::string> table;
  for (const auto &prop : properties) {
    // PARSE_PROPERTY(prop, "inputs:colorTemperature", light->colorTemperature)
    PARSE_PROPERTY(table, prop, "inputs:color", LuxSphereLight, light->color)
    PARSE_PROPERTY(table, prop, "inputs:radius", LuxSphereLight, light->radius)
    PARSE_PROPERTY(table, prop, "inputs:intensity", LuxSphereLight,
                   light->intensity)
    ADD_PROPERY(table, prop, LuxSphereLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<LuxDomeLight>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxDomeLight *light) {
  std::set<std::string> table;

  for (const auto &prop : properties) {
    PARSE_PROPERTY(table, prop, "guideRadius", LuxDomeLight, light->guideRadius)
    PARSE_PROPERTY(table, prop, "inputs:diffuse", LuxDomeLight, light->diffuse)
    PARSE_PROPERTY(table, prop, "inputs:specular", LuxDomeLight,
                   light->specular)
    PARSE_PROPERTY(table, prop, "inputs:colorTemperature", LuxDomeLight,
                   light->colorTemperature)
    PARSE_PROPERTY(table, prop, "inputs:color", LuxDomeLight, light->color)
    PARSE_PROPERTY(table, prop, "inputs:intensity", LuxDomeLight,
                   light->intensity)
    ADD_PROPERY(table, prop, LuxDomeLight, light->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  DCOUT("Implement DomeLight");
  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<Model>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Model *model) {
  DCOUT("Model(`def` with no type)");

  std::set<std::string> table;
  for (const auto &prop : properties) {
    ADD_PROPERY(table, prop, Model, model->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<Scope>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Scope *scope) {
  // `Scope` is just a namespace in scene graph(no node xform)

  DCOUT("Scope");
  std::set<std::string> table;
  for (const auto &prop : properties) {
    ADD_PROPERY(table, prop, Scope, scope->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<SkelRoot>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    SkelRoot *root) {
  (void)root;


  std::set<std::string> table;
  if (!ReconstructXformOpProperties(table, properties, &root->xformOps)) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct xformOp data.");
  }

  // SkelRoot is something like a grouping node, having 1 Skeleton and possibly?
  // multiple Prim hierarchy containing GeomMesh.
  // No specific properties for SkelRoot(AFAIK)

  // custom props only
  for (const auto &prop : properties) {
    ADD_PROPERY(table, prop, SkelRoot, root->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<Skeleton>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Skeleton *skel) {

  constexpr auto kSkelAnimationSource = "skel:animationSource";

  std::set<std::string> table;
  for (auto &prop : properties) {

    // SkelBindingAPI
    if (prop.first == kSkelAnimationSource) {

      // Must be relation of type Path.
      if (prop.second.IsRel() && prop.second.rel.IsPath()) {
        {
          const Relation &rel = prop.second.rel;
          if (rel.IsPath()) {
            DCOUT(kSkelAnimationSource);
            skel->animationSource = rel.targetPath;
            table.insert(kSkelAnimationSource);
          } else {
            PUSH_ERROR_AND_RETURN("`" << kSkelAnimationSource << "` target must be Path.");
          }
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`" << kSkelAnimationSource << "` must be a Relation with Path target.");
      }
    }

    //

    PARSE_TYPED_PROPERTY(table, prop, "bindTransforms", Skeleton, skel->bindTransforms)
    PARSE_TYPED_PROPERTY(table, prop, "joints", Skeleton, skel->joints)
    PARSE_TYPED_PROPERTY(table, prop, "jointNames", Skeleton, skel->jointNames)
    PARSE_TYPED_PROPERTY(table, prop, "restTransforms", Skeleton, skel->restTransforms)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<SkelAnimation>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    SkelAnimation *skelanim) {


  std::set<std::string> table;
  for (auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "joints", SkelAnimation, skelanim->joints)
    PARSE_TYPED_ATTRIBUTE(table, prop, "translations", SkelAnimation, skelanim->translations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "rotations", SkelAnimation, skelanim->rotations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "scales", SkelAnimation, skelanim->scales)
    PARSE_TYPED_ATTRIBUTE(table, prop, "blendShapes", SkelAnimation, skelanim->blendShapes)
    PARSE_TYPED_ATTRIBUTE(table, prop, "blendShapeWeights", SkelAnimation, skelanim->blendShapeWeights)
    //PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<BlendShape>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    BlendShape *bs) {

  constexpr auto kOffsets = "offsets";
  constexpr auto kNormalOffsets = "normalOffsets";
  constexpr auto kPointIndices = "pointIndices";

  std::set<std::string> table;
  for (auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, kOffsets, BlendShape, bs->offsets)
    PARSE_TYPED_PROPERTY(table, prop, kNormalOffsets, BlendShape, bs->normalOffsets)
    PARSE_TYPED_PROPERTY(table, prop, kPointIndices, BlendShape, bs->pointIndices)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

  // `offsets` and `normalOffsets` are required property.
  if (!table.count(kOffsets)) {
    PUSH_ERROR_AND_RETURN("`offsets` property is missing. `uniform vector3f[] offsets` is a required property.");
  }
  if (!table.count(kNormalOffsets)) {
    PUSH_ERROR_AND_RETURN("`normalOffsets` property is missing. `uniform vector3f[] normalOffsets` is a required property.");
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdPreviewSurface>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdPreviewSurface *surface) {
  // TODO: references

  std::set<std::string> table;
  for (auto &prop : properties) {
    PARSE_TYPED_PROPERTY(table, prop, "inputs:diffuseColor", UsdPreviewSurface,
                         surface->diffuseColor)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:emissiveColor", UsdPreviewSurface,
                         surface->emissiveColor)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:roughness", UsdPreviewSurface,
                         surface->roughness)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:specularColor", UsdPreviewSurface,
                         surface->specularColor)  // specular workflow
    PARSE_TYPED_PROPERTY(table, prop, "inputs:metallic", UsdPreviewSurface,
                         surface->metallic)  // non specular workflow
    PARSE_TYPED_PROPERTY(table, prop, "inputs:clearcoat", UsdPreviewSurface,
                         surface->clearcoat)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:clearcoatRoughness",
                         UsdPreviewSurface, surface->clearcoatRoughness)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:opacity", UsdPreviewSurface,
                         surface->opacity)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:opacityThreshold",
                         UsdPreviewSurface, surface->opacityThreshold)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:ior", UsdPreviewSurface,
                         surface->ior)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:normal", UsdPreviewSurface,
                         surface->normal)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:dispacement", UsdPreviewSurface,
                         surface->displacement)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:occlusion", UsdPreviewSurface,
                         surface->occlusion)
    PARSE_TYPED_PROPERTY(table, prop, "inputs:useSpecularWorkflow",
                         UsdPreviewSurface, surface->useSpecularWorkflow)
    PARSE_PROPERTY(table, prop, "outputs:surface", UsdPreviewSurface,
                   surface->outputsSurface)
    PARSE_PROPERTY(table, prop, "outputs:displacement", UsdPreviewSurface,
                   surface->outputsDisplacement)
    ADD_PROPERY(table, prop, UsdPreviewSurface, surface->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdUVTexture>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdUVTexture *texture) {
  // TODO: references

  auto SourceColorSpaceHandler = [](const std::string &tok)
      -> nonstd::expected<UsdUVTexture::SourceColorSpace, std::string> {
    using EnumTy = std::pair<UsdUVTexture::SourceColorSpace, const char *>;
    const std::vector<EnumTy> enums = {
        std::make_pair(UsdUVTexture::SourceColorSpace::Auto, "auto"),
        std::make_pair(UsdUVTexture::SourceColorSpace::Raw, "raw"),
        std::make_pair(UsdUVTexture::SourceColorSpace::SRGB, "sRGB"),
    };

    return EnumHandler<UsdUVTexture::SourceColorSpace>(
        "inputs:sourceColorSpace", tok, enums);
  };

  std::set<std::string> table;

  for (auto &prop : properties) {
    PARSE_PROPERTY(table, prop, "inputs:file", UsdPreviewSurface, texture->file)
    PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:st", UsdPreviewSurface,
                          texture->st)
    PARSE_ENUM_PROPETY(table, prop, "inputs:sourceColorSpace",
                       SourceColorSpaceHandler, UsdPreviewSurface,
                       texture->sourceColorSpace)
    PARSE_TYPED_OUTPUT_ATTRIBUTE(table, prop, "outputs:r", UsdPreviewSurface,
                                  texture->outputsR)
    PARSE_TYPED_OUTPUT_ATTRIBUTE(table, prop, "outputs:g", UsdPreviewSurface,
                                  texture->outputsG)
    PARSE_TYPED_OUTPUT_ATTRIBUTE(table, prop, "outputs:b", UsdPreviewSurface,
                                  texture->outputsB)
    PARSE_TYPED_OUTPUT_ATTRIBUTE(table, prop, "outputs:a", UsdPreviewSurface,
                                  texture->outputsA)
    PARSE_TYPED_OUTPUT_ATTRIBUTE(table, prop, "outputs:rgb", UsdPreviewSurface,
                                  texture->outputsRGB)
    ADD_PROPERY(table, prop, UsdUVTexture, texture->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdPrimvarReader_int>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdPrimvarReader_int *preader) {
  // TODO:
  return false;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdPrimvarReader_float>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdPrimvarReader_float *preader) {
  // TODO:
  return false;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdPrimvarReader_float2>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdPrimvarReader_float2 *preader) {
  std::set<std::string> table;

  for (auto &prop : properties) {
    PARSE_PROPERTY(table, prop, "inputs:varname", UsdPrimvarReader_float2,
                   preader->varname)  // `token`
    PARSE_TYPED_OUTPUT_ATTRIBUTE(table, prop, "outputs:result",
                                  UsdPrimvarReader_float2, preader->result)
    ADD_PROPERY(table, prop, UsdPrimvarReader_float2, preader->props)
    PARSE_PROPERTY_END_MAKE_WARN(prop)
  }

  return true;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdPrimvarReader_float3>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdPrimvarReader_float3 *preader) {
  // TODO:
  return false;
}

template <>
bool USDAReader::Impl::ReconstructShader<UsdPrimvarReader_float4>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    UsdPrimvarReader_float4 *preader) {
  // TODO:
  return false;
}

template <>
bool USDAReader::Impl::ReconstructPrim<Shader>(
    const std::map<std::string, Property> &properties,
    const std::vector<std::pair<ListEditQual, Reference>> &references,
    Shader *shader) {
  constexpr auto kUsdPreviewSurface = "UsdPreviewSurface";
  constexpr auto kUsdUVTexture = "UsdUVTexture";
  constexpr auto kUsdPrimvarReader_int = "UsdPrimvarReader_int";
  constexpr auto kUsdPrimvarReader_float = "UsdPrimvarReader_float";
  constexpr auto kUsdPrimvarReader_float2 = "UsdPrimvarReader_float2";
  constexpr auto kUsdPrimvarReader_float3 = "UsdPrimvarReader_float3";
  constexpr auto kUsdPrimvarReader_float4 = "UsdPrimvarReader_float4";

  for (const auto &prop : properties) {
    if (prop.first == "info:id") {
      const PrimAttrib &attr = prop.second.attrib;

      auto pv = attr.var.get_value<value::token>();
      if (!pv) {
        PUSH_ERROR_AND_RETURN("`info:id` must be type `token`, but got type `"
                              << attr.var.type_name() << "`.");
      }

      std::string shader_type = pv.value().str();

      DCOUT("info:id = " << shader_type);

      if (shader_type.compare(kUsdPreviewSurface) == 0) {
        UsdPreviewSurface surface;
        if (!ReconstructShader<UsdPreviewSurface>(properties, references,
                                                  &surface)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kUsdPreviewSurface);
        }
        shader->info_id = kUsdPreviewSurface;
        shader->value = surface;
        DCOUT("info_id = " << shader->info_id);
      } else if (shader_type.compare(kUsdUVTexture) == 0) {
        UsdUVTexture texture;
        if (!ReconstructShader<UsdUVTexture>(properties, references,
                                             &texture)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct " << kUsdUVTexture);
        }
        shader->info_id = kUsdUVTexture;
        shader->value = texture;
      } else if (shader_type.compare(kUsdPrimvarReader_int) == 0) {
        UsdPrimvarReader_int preader;
        if (!ReconstructShader<UsdPrimvarReader_int>(properties, references,
                                                     &preader)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                                << kUsdPrimvarReader_int);
        }
        shader->info_id = kUsdPrimvarReader_int;
        shader->value = preader;
      } else if (shader_type.compare(kUsdPrimvarReader_float) == 0) {
        UsdPrimvarReader_float preader;
        if (!ReconstructShader<UsdPrimvarReader_float>(properties, references,
                                                       &preader)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                                << kUsdPrimvarReader_float);
        }
        shader->info_id = kUsdPrimvarReader_float;
        shader->value = preader;
      } else if (shader_type.compare(kUsdPrimvarReader_float2) == 0) {
        UsdPrimvarReader_float2 preader;
        if (!ReconstructShader<UsdPrimvarReader_float2>(properties, references,
                                                        &preader)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                                << kUsdPrimvarReader_float2);
        }
        shader->info_id = kUsdPrimvarReader_float2;
        shader->value = preader;
      } else if (shader_type.compare(kUsdPrimvarReader_float3) == 0) {
        UsdPrimvarReader_float3 preader;
        if (!ReconstructShader<UsdPrimvarReader_float3>(properties, references,
                                                        &preader)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                                << kUsdPrimvarReader_float3);
        }
        shader->info_id = kUsdPrimvarReader_float3;
        shader->value = preader;
      } else if (shader_type.compare(kUsdPrimvarReader_float4) == 0) {
        UsdPrimvarReader_float4 preader;
        if (!ReconstructShader<UsdPrimvarReader_float4>(properties, references,
                                                        &preader)) {
          PUSH_ERROR_AND_RETURN("Failed to Reconstruct "
                                << kUsdPrimvarReader_float4);
        }
        shader->info_id = kUsdPrimvarReader_float4;
        shader->value = preader;
      } else {
        // TODO: string, point, vector, matrix
        PUSH_ERROR_AND_RETURN(
            "Invalid or Unsupported Shader type. info:id = \"" + shader_type +
            "\n");
      }

    } else {
      // std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

template <>
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

template <>
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

///
/// -- Impl callback specializations
///
#if 0
template <>
bool USDAReader::Impl::RegisterReconstructCallback<Xform>() {
  // TODO: Move to ReconstructPrim
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
          size_t idx = _prim_nodes.size();
          PrimNode node;
          node.prim = xform;
          _prim_nodes.push_back(node);
          _toplevel_prims.push_back(idx);
        } else {
          PUSH_WARN("TODO: Implement xform");
        }

        return true;
      });

  return true;
}
#endif

///
/// -- Impl Read
///

bool USDAReader::Impl::Read(ascii::LoadState state) {
  ///
  /// Setup callbacks.
  ///
  StageMetaProcessor();

  RegisterPrimIdxAssignCallback();

  RegisterReconstructCallback<Model>();  // `def` with no type.

  RegisterReconstructCallback<GPrim>();

  RegisterReconstructCallback<Xform>();
  RegisterReconstructCallback<GeomCube>();
  RegisterReconstructCallback<GeomSphere>();
  RegisterReconstructCallback<GeomCone>();
  RegisterReconstructCallback<GeomCylinder>();
  RegisterReconstructCallback<GeomCapsule>();
  RegisterReconstructCallback<GeomMesh>();
  RegisterReconstructCallback<GeomSubset>();
  RegisterReconstructCallback<GeomBasisCurves>();

  RegisterReconstructCallback<Material>();
  RegisterReconstructCallback<Shader>();

  RegisterReconstructCallback<Scope>();

  RegisterReconstructCallback<LuxSphereLight>();
  RegisterReconstructCallback<LuxDomeLight>();

  RegisterReconstructCallback<SkelRoot>();
  RegisterReconstructCallback<Skeleton>();
  RegisterReconstructCallback<SkelAnimation>();
  RegisterReconstructCallback<BlendShape>();

  if (!_parser.Parse(state)) {
    std::string warn = _parser.GetWarning();
    if (!warn.empty()) {
      PUSH_WARN("<parser> " + warn);
    }

    PUSH_ERROR_AND_RETURN("Parse failed:" + _parser.GetError());
  }

#if 0
  DCOUT("# of toplevel prims = " << std::to_string(PrimSize()));

  {
    size_t i = 0;
    for (auto it = PrimBegin(); it != PrimEnd(); ++it, i++) {
      DCOUT("Prim[" << std::to_string(i) << "].type = " << (*it).type_name());
    }
  }
#endif

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

bool USDAReader::ReconstructStage() { return _impl->ReconstructStage(); }

const Stage &USDAReader::GetStage() const { return _impl->GetStage(); }

}  // namespace usda
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
bool USDAReader::ReconstructStage() { return false; }

const Stage &USDAReader::GetStage() const {
  static Stage empty;
  return empty;
}

}  // namespace usda
}  // namespace tinyusdz

#endif
