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
#include "usdSkel.hh"
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
#include "prim-reconstruct.hh"
#include "primvar.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "usdObj.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"

#include "common-macros.inc"

namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty) template<> bool ReconstructPrim<__ty>(const PropertyMap &, const ReferenceList &, __ty *, std::string *, std::string *)

RECONSTRUCT_PRIM_DECL(Xform);
RECONSTRUCT_PRIM_DECL(Model);
RECONSTRUCT_PRIM_DECL(Scope);
RECONSTRUCT_PRIM_DECL(Skeleton);
RECONSTRUCT_PRIM_DECL(SkelRoot);
RECONSTRUCT_PRIM_DECL(SkelAnimation);
RECONSTRUCT_PRIM_DECL(BlendShape);
RECONSTRUCT_PRIM_DECL(LuxDomeLight);
RECONSTRUCT_PRIM_DECL(LuxSphereLight);
RECONSTRUCT_PRIM_DECL(GeomMesh);
RECONSTRUCT_PRIM_DECL(GeomSphere);
RECONSTRUCT_PRIM_DECL(GeomPoints);
RECONSTRUCT_PRIM_DECL(GeomCone);
RECONSTRUCT_PRIM_DECL(GeomPoints);
RECONSTRUCT_PRIM_DECL(GeomCube);
RECONSTRUCT_PRIM_DECL(GeomCylinder);
RECONSTRUCT_PRIM_DECL(GeomCapsule);
RECONSTRUCT_PRIM_DECL(GeomBasisCurves);
RECONSTRUCT_PRIM_DECL(GeomCamera);
RECONSTRUCT_PRIM_DECL(Material);
RECONSTRUCT_PRIM_DECL(Shader);

#undef RECONSTRUCT_PRIM_DECL

} // namespace prim

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

  template <typename T>
  bool ReconstructPrim(
      const prim::PropertyMap &properties,
      const ReferenceList &references,
      T *out);

#if 0
  ///
  /// TinyUSDZ reconstruct some frequently used shaders(e.g. UsdPreviewSurface)
  /// here, not in Tydra
  ///
  template <typename T>
  bool ReconstructShader(
      const prim::PropertyMap &properties,
      const ReferenceList &references,
      T *out);
#endif

  // T = Prim class(e.g. Xform)
  template <typename T>
  bool RegisterReconstructCallback() {
    _parser.RegisterPrimConstructFunction(
        PrimTypeTrait<T>::prim_type_name,
        [&](const Path &full_path, const Path &prim_name, const int64_t primIdx,
            const int64_t parentPrimIdx,
            const prim::PropertyMap &properties,
            const ReferenceList &references,
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
  //bool stage_reconstructed_{false};

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

template <>
bool USDAReader::Impl::ReconstructPrim(
    const prim::PropertyMap &properties,
    const ReferenceList &references,
    Xform *xform) {

  std::string err;
  if (!prim::ReconstructPrim(properties, references, xform, &_warn, &err)) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Xform Prim: " << err);
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
      [&](const Path &path, const PropertyMap &properties,
          ReferenceList &references) {
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
          const prim::PropertyMap &properties,
          const ReferenceList &references,
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
            if (auto pv = item.second.attrib.get_value<value::token>()) {
              if (item.second.attrib.variability == Variability::Uniform) {
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

            if (auto pv = item.second.attrib.get_value<value::token>()) {
              if (item.second.attrib.variability == Variability::Uniform) {
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
                    item.second.attrib.get_value<std::vector<int>>()) {
              // int -> uint
              std::transform(pv.value().begin(), pv.value().end(),
                             std::back_inserter(subset.indices),
                             [](int a) { return uint32_t(a); });
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`indices` property must be `int[]` type, but got `" +
                  item.second.attrib.type_name() + "`");
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

            if (auto pv = item.second.attrib.get_value<value::token>()) {
              subset.familyName = pv.value();
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`familyName` property must be `token` type, but got `" +
                  item.second.attrib.type_name() + "`");
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
    const prim::PropertyMap &properties,
    const ReferenceList &references,
    GPrim *gprim) {
  (void)gprim;

  DCOUT("TODO: Reconstruct GPrim.");

  PUSH_WARN("TODO: Reconstruct GPrim.");

  return true;
}


template <>
bool USDAReader::Impl::ReconstructPrim<NodeGraph>(
    const prim::PropertyMap &properties,
    const ReferenceList &references,
    NodeGraph *graph) {
  (void)properties;
  (void)references;
  (void)graph;

  PUSH_WARN("TODO: reconstruct NodeGrah.");

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim<Material>(
    const prim::PropertyMap &properties,
    const ReferenceList &references,
    Material *material) {
  (void)properties;
  (void)references;
  (void)material;

  PUSH_WARN("TODO: Implement Material.");

  return true;
}

// Generic Prim handler. T = Xform, GeomMesh, ...
template <typename T>
bool USDAReader::Impl::ReconstructPrim(
    const prim::PropertyMap &properties,
    const ReferenceList &references,
    T *prim) {

  std::string err;
  if (!prim::ReconstructPrim(properties, references, prim, &_warn, &err)) {
    PUSH_ERROR_AND_RETURN(fmt::format("Failed to reconstruct {} Prim: {}", value::TypeTrait<T>::type_name(), err));
  }
  return true;
}

///
/// -- Impl callback specializations
///

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
