// SPDX-License-Identifier: MIT
// Copyright 2020-Present Syoyo Fujita.
//
// USDC(Crate) reader
//
// TODO:
//
// - [ ] Refactor Reconstruct*** function
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "usdc-reader.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_READER)

#include <unordered_map>
#include <unordered_set>

#include "prim-types.hh"
#include "tinyusdz.hh"
#include "value-types.hh"
#if defined(__wasi__)
#else
#include <thread>
#endif

#include "crate-format.hh"
#include "crate-pprint.hh"
#include "crate-reader.hh"
#include "integerCoding.h"
#include "lz4-compression.hh"
#include "path-util.hh"
#include "pprinter.hh"
#include "prim-reconstruct.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "value-pprint.hh"
#include "tiny-format.hh"

//
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//
#include "common-macros.inc"


namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty) template<> bool ReconstructPrim<__ty>(const PropertyMap &, const ReferenceList &, __ty *, std::string *, std::string *)

RECONSTRUCT_PRIM_DECL(Xform);
RECONSTRUCT_PRIM_DECL(Model);
RECONSTRUCT_PRIM_DECL(Scope);
RECONSTRUCT_PRIM_DECL(GeomMesh);

#undef RECONSTRUCT_PRIM_DECL

} // namespace prim

namespace usdc {

constexpr auto kTag = "[USDC]";

struct PrimNode {
  value::Value prim;

  int64_t parent{-1};            // -1 = root node
  std::vector<size_t> children;  // index to USDCReader::Impl::._prim_nodes[]
};

class USDCReader::Impl {
 public:
  Impl(StreamReader *sr, const USDCReaderConfig &config) : _sr(sr) {
    _config = config;

#if defined(__wasi__)
    _config.numThreads = 1;
#else
    if (_config.numThreads == -1) {
      _config.numThreads =
          (std::max)(1, int(std::thread::hardware_concurrency()));
    }
    // Limit to 1024 threads.
    _config.numThreads = (std::min)(1024, _config.numThreads);
#endif
  }

  ~Impl() {
    delete crate_reader;
    crate_reader = nullptr;
  }

  bool ReadUSDC();

  using PathIndexToSpecIndexMap = std::unordered_map<uint32_t, uint32_t>;

#if 0
  bool ReconstructGeomSubset(const Node &node,
                             const FieldValuePairVector &fields,
                             const std::unordered_map<uint32_t, uint32_t>
                                 &path_index_to_spec_index_map,
                             GeomSubset *mesh);

  bool ReconstructGeomMesh(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           GeomMesh *mesh);

  bool ReconstructGeomBasisCurves(const Node &node,
                                  const FieldValuePairVector &fields,
                                  const std::unordered_map<uint32_t, uint32_t>
                                      &path_index_to_spec_index_map,
                                  GeomBasisCurves *curves);

  bool ReconstructMaterial(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           Material *material);

  bool ReconstructShader(const Node &node, const FieldValuePairVector &fields,
                         const std::unordered_map<uint32_t, uint32_t>
                             &path_index_to_spec_index_map,
                         Shader *shader);

  bool ReconstructPreviewSurface(const Node &node,
                                 const FieldValuePairVector &fields,
                                 const std::unordered_map<uint32_t, uint32_t>
                                     &path_index_to_spec_index_map,
                                 PreviewSurface *surface);

  bool ReconstructUVTexture(const Node &node,
                            const FieldValuePairVector &fields,
                            const std::unordered_map<uint32_t, uint32_t>
                                &path_index_to_spec_index_map,
                            UVTexture *uvtex);

  bool ReconstructPrimvarReader_float2(
      const Node &node, const FieldValuePairVector &fields,
      const std::unordered_map<uint32_t, uint32_t>
          &path_index_to_spec_index_map,
      PrimvarReader_float2 *preader);

  bool ReconstructSkelRoot(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           SkelRoot *skelRoot);

  bool ReconstructSkeleton(const Node &node, const FieldValuePairVector &fields,
                           const std::unordered_map<uint32_t, uint32_t>
                               &path_index_to_spec_index_map,
                           Skeleton *skeleton);

#endif

  ///
  /// Construct Property(Attribute, Relationship/Connection) from
  /// FieldValuePairs
  ///
  bool ParseProperty(const crate::FieldValuePairVector &fvs, Property *prop);

  // For simple, non animatable and non `.connect` types. e.g. "token[]"
  template <typename T>
  bool ReconstructSimpleAttribute(int parent,
                                  const crate::FieldValuePairVector &fvs,
                                  T *attr, bool *custom_out = nullptr,
                                  Variability *variability_out = nullptr);

  // For attribute which maybe a value, connection or TimeSamples.
  template <typename T>
  bool ReconstructTypedAttribute(int parent,
                                 const crate::FieldValuePairVector &fvs,
                                 TypedAttribute<T> *attr);

  template <typename T>
  bool ReconstructPrim(const crate::CrateReader::Node &node,
                       const crate::FieldValuePairVector &fvs,
                       const PathIndexToSpecIndexMap &psmap, T *prim);

  bool ReconstructPrimRecursively(int parent_id, int current_id, int level,
                                  const PathIndexToSpecIndexMap &psmap,
                                  Stage *stage);

  bool ReconstructStage(Stage *stage);

  ///
  /// --------------------------------------------------
  ///

  void PushError(const std::string &s) { _err += s; }

  void PushWarn(const std::string &s) { _warn += s; }

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const { return memory_used / (1024 * 1024); }

 private:
  ///
  /// Builds std::map<std::string, Property> from the list of Path(Spec)
  /// indices.
  ///
  bool BuildPropertyMap(const std::vector<size_t> &pathIndices,
                        const PathIndexToSpecIndexMap &psmap,
                        prim::PropertyMap *props);

  bool ReconstrcutStageMeta(const crate::FieldValuePairVector &fvs,
                            StageMetas *out,
                            std::vector<value::token> *primChildrenOut);

  crate::CrateReader *crate_reader{nullptr};

  StreamReader *_sr = nullptr;
  std::string _err;
  std::string _warn;

  USDCReaderConfig _config;

  // Tracks the memory used(In advisorily manner since counting memory usage is
  // done by manually, so not all memory consumption could be tracked)
  size_t memory_used{0};  // in bytes.

  nonstd::optional<Path> GetPath(crate::Index index) const {
    if (index.value <= _paths.size()) {
      return _paths[index.value];
    }

    return nonstd::nullopt;
  }

  nonstd::optional<Path> GetElemPath(crate::Index index) const {
    if (index.value <= _elemPaths.size()) {
      return _elemPaths[index.value];
    }

    return nonstd::nullopt;
  }

  // TODO: Do not copy data from crate_reader.
  std::vector<crate::CrateReader::Node> _nodes;
  std::vector<crate::Spec> _specs;
  std::vector<crate::Field> _fields;
  std::vector<crate::Index> _fieldset_indices;
  std::vector<crate::Index> _string_indices;
  std::vector<Path> _paths;
  std::vector<Path> _elemPaths;

  std::map<crate::Index, crate::FieldValuePairVector>
      _live_fieldsets;  // <fieldset index, List of field with unpacked Values>

  std::vector<PrimNode> _prim_nodes;

  // Check if given node_id is a prim node.
  std::set<int32_t> _prim_table;
};

//
// -- Impl
//

#if 0



bool USDCReader::Impl::ReconstructGeomBasisCurves(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomBasisCurves *curves) {
  bool has_position{false};

  DCOUT("Reconstruct Xform");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {

      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      auto ret = fv.second.get_value<std::vector<value::token>>();
      if (!ret) {
        PUSH_ERROR("Invalid `properties` data");
        return false;
      }

      for (size_t i = 0; i < ret.value().size(); i++) {
        if (ret.value()[i].str() == "points") {
          has_position = true;
        }
      }
    }
  }

  (void)has_position;

  //
  // NOTE: Currently we assume one deeper node has GeomMesh's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
#if 0
      _err += "GeomBasisCurves: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      _err += "Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")\n";
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
    std::cout << "Path prim part: " << path.GetPrimPart()
              << ", prop part: " << path.GetPropPart()
              << ", spec_index = " << spec_index << "\n";
#endif

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
      std::cout << "prop: " << prop_name << ", ret = " << ret << "\n";
#endif
      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "points") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "got point\n";
#endif
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //   curves->points = *p;
          // }
        } else if (prop_name == "extent") {
          // vec3f[2]
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //  if (p->size() == 2) {
          //    curves->extent.value.lower = (*p)[0];
          //    curves->extent.value.upper = (*p)[1];
          //  }
          //}
        } else if (prop_name == "normals") {
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //   curves->normals = (*p);
          // }
        } else if (prop_name == "widths") {
          // if (auto p = primvar::as_vector<float>(&attr.var)) {
          //   curves->widths = (*p);
          // }
        } else if (prop_name == "curveVertexCounts") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //   curves->curveVertexCounts = (*p);
          // }
        } else if (prop_name == "type") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          // std::cout << "type:" << attr.stringVal << "\n";
#endif
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //   if (p->compare("cubic") == 0) {
          //     curves->type = "cubic";
          //   } else if (p->compare("linear") == 0) {
          //     curves->type = "linear";
          //   } else {
          //     _err += "Unknown type: " + (*p) + "\n";
          //     return false;
          //   }
          // }
        } else if (prop_name == "basis") {
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          // std::cout << "basis:" << attr.stringVal << "\n";
#endif
#if 0
          if (auto p = nonstd::get_if<std::string>(&attr.var)) {
            if (p->compare("bezier") == 0) {
              curves->type = "bezier";
            } else if (p->compare("catmullRom") == 0) {
              curves->type = "catmullRom";
            } else if (p->compare("bspline") == 0) {
              curves->type = "bspline";
            } else if (p->compare("hermite") == 0) {
              _err += "`hermite` basis for BasisCurves is not supported in TinyUSDZ\n";
              return false;
            } else if (p->compare("power") == 0) {
              _err += "`power` basis for BasisCurves is not supported in TinyUSDZ\n";
              return false;
            } else {
              _err += "Unknown basis: " + (*p) + "\n";
              return false;
            }
          }
#endif
        } else if (prop_name == "wrap") {
#if 0
          if (auto p = nonstd::get_if<std::string>(&attr.var)) {
            if (p->compare("nonperiodic") == 0) {
              curves->type = "nonperiodic";
            } else if (p->compare("periodic") == 0) {
              curves->type = "periodic";
            } else if (p->compare("pinned") == 0) {
              curves->type = "pinned";
            } else {
              _err += "Unknown wrap: " + (*p) + "\n";
              return false;
            }
          }
#endif
        } else {
          // Assume Primvar.
          if (curves->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          curves->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructGeomSubset(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomSubset *geom_subset) {

  DCOUT("Reconstruct GeomSubset");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      //   // if (fv.second.GetStringArray()[i] == "points") {
      //   // }
      // }
    }
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // TODO: Should we report an error?
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "elementType") {
          auto p = attr.var.get_value<tinyusdz::value::token>();
          if (p) {
            std::string str = p->str();
            if (str == "face") {
              geom_subset->elementType = GeomSubset::ElementType::Face;
            } else {
              PUSH_ERROR("`elementType` must be `face`, but got `" + str + "`");
              return false;
            }
          } else {
            PUSH_ERROR("`elementType` must be token type, but got " +
                       value::GetTypeName(attr.var.type_id()));
            return false;
          }
        } else if (prop_name == "faces") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            geom_subset->faces = (*p);
          }

          DCOUT("faces.num = " << geom_subset->faces.size());

        } else {
          // Assume Primvar.
          if (geom_subset->attribs.count(prop_name)) {
            _err += "Duplicated property name found: " + prop_name + "\n";
            return false;
          }

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
          std::cout << "add [" << prop_name << "] to generic attrs\n";
#endif

          geom_subset->attribs[prop_name] = std::move(attr);
        }
      }
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructGeomMesh(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    GeomMesh *mesh) {
  bool has_position{false};

  DCOUT("Reconstruct GeomMesh");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      const auto arr = fv.second.get_value<std::vector<value::token>>();
      if (!arr) {
        return false;
      }
      for (size_t i = 0; i < arr.value().size(); i++) {
        if (arr.value()[i] == "points") {
          has_position = true;
        }
      }
    }
  }

  (void)has_position;

  // Disable has_position check for a while, since Mesh may not have "points",
  // but "position"

  // if (!has_position) {
  //  _err += "No `position` field exist for Mesh node: " + node.GetLocalPath()
  //  +
  //          ".\n";
  //  return false;
  //}

  //
  // NOTE: Currently we assume one deeper node has GeomMesh's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      DCOUT("No speciefier assigned to this child node: " << child_index);
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // TODO(syoyo): Support more prop names
        if (prop_name == "points") {
          auto p = attr.var.get_value<std::vector<value::point3f>>();
          if (p) {
            mesh->points = (*p);
          } else {
            PUSH_ERROR("`points` must be point3[] type, but got " +
                       value::GetTypeName(attr.var.type_id()));
            return false;
          }
          // if (auto p = primvar::as_vector<value::float3>(&attr.var)) {
          //   mesh->points = (*p);
          // }
        } else if (prop_name == "doubleSided") {
          auto p = attr.var.get_value<bool>();
          if (p) {
            mesh->doubleSided = (*p);
          }
        } else if (prop_name == "extent") {
          // vec3f[2]
          auto p = attr.var.get_value<std::vector<value::float3>>();
          if (p && p->size() == 2) {
            mesh->extent.value.lower = (*p)[0];
            mesh->extent.value.upper = (*p)[1];
          }
        } else if (prop_name == "normals") {
          mesh->normals = attr;
        } else if ((prop_name == "primvars:UVMap") &&
                   (attr.type_name == "texCoord2f[]")) {
          // Explicit UV coord attribute.
          // TODO(syoyo): Write PrimVar parser

          // Currently we only support vec2f for uv coords.
          // if (auto p = primvar::as_vector<Vec2f>(&attr.var)) {
          //  mesh->st.buffer = (*p);
          //  mesh->st.variability = attr.variability;
          //}
        } else if (prop_name == "faceVertexCounts") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            mesh->faceVertexCounts = (*p);
          }
          //}
        } else if (prop_name == "faceVertexIndices") {
          auto p = attr.var.get_value<std::vector<int>>();
          if (p) {
            mesh->faceVertexIndices = (*p);
          }

        } else if (prop_name == "holeIndices") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //     mesh->holeIndices = (*p);
          // }
        } else if (prop_name == "cornerIndices") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //     mesh->cornerIndices = (*p);
          // }
        } else if (prop_name == "cornerSharpnesses") {
          // if (auto p = primvar::as_vector<float>(&attr.var)) {
          //     mesh->cornerSharpnesses = (*p);
          // }
        } else if (prop_name == "creaseIndices") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //     mesh->creaseIndices = (*p);
          // }
        } else if (prop_name == "creaseLengths") {
          // if (auto p = primvar::as_vector<int>(&attr.var)) {
          //   mesh->creaseLengths = (*p);
          // }
        } else if (prop_name == "creaseSharpnesses") {
          // if (auto p = primvar::as_vector<float>(&attr.var)) {
          //     mesh->creaseSharpnesses = (*p);
          // }
        } else if (prop_name == "subdivisionScheme") {
          auto p = attr.var.get_value<value::token>();
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //   if (p->compare("none") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::None;
          //   } else if (p->compare("catmullClark") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::CatmullClark;
          //   } else if (p->compare("bilinear") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::Bilinear;
          //   } else if (p->compare("loop") == 0) {
          //     mesh->subdivisionScheme = SubdivisionScheme::Loop;
          //   } else {
          //     _err += "Unknown subdivision scheme: " + (*p) + "\n";
          //     return false;
          //   }
          // }
        } else if (prop_name.compare("material:binding") == 0) {
          // rel
          auto p =
              attr.var.get_value<std::string>();  // rel, but treat as sting
          if (p) {
            mesh->materialBinding.materialBinding = (*p);
          }
        } else {
          // Assume Primvar.
          if (mesh->props.count(prop_name)) {
            PUSH_ERROR("Duplicated property name found: " + prop_name);
            return false;
          }

          DCOUT("add [" << prop_name << "] to generic attrs.");

          // FIXME: Look-up schema to detect if the property is `custom` or not.
          bool is_custom{false};

          mesh->props.emplace(prop_name, Property(attr, is_custom));
        }
      }
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructMaterial(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Material *material) {
  (void)material;

  DCOUT("Parse mateiral");

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Material's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
#if 0
      _err += "Material: No specifier found for node id: " + std::to_string(child_index) +
              "\n";
      return false;
#else
      continue;
#endif
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    (void)child_fields;
    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      if (ret) {
        if (prop_name.compare("outputs:surface") == 0) {
          auto p = attr.var.get_value<std::string>();
          if (p) {
            material->outputs_surface = (*p);
          }
        }
#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
        std::cout << "prop: " << prop_name << "\n";
#endif
      }
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructShader(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Shader *shader) {
  (void)shader;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  //
  // Find shader type.
  //
  std::string shader_type;

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<value::token>();
          if (p) {
            shader_type = p.value().str();
          }
        }
      }
    }
  }

  if (shader_type.empty()) {
    PUSH_ERROR("`info:id` is missing in Shader.");
    return false;
  }

  return true;
}

bool USDCReader::Impl::ReconstructPreviewSurface(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    PreviewSurface *shader) {
  (void)shader;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  //
  // NOTE: Currently we assume one deeper node has Shader's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      _err += "Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")\n";
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop: " << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            if (p->compare("UsdPreviewSurface") != 0) {
              PUSH_ERROR("`info:id` must be `UsdPreviewSurface`.");
              return false;
            }
          }
        } else if (prop_name.compare("outputs:surface") == 0) {
          // Surface shader output available
        } else if (prop_name.compare("outputs:displacement") == 0) {
          // Displacement shader output available
        } else if (prop_name.compare("inputs:roughness") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->roughness.value = (*p);
          }
        } else if (prop_name.compare("inputs:specular") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->specular.value = (*p);
          }
        } else if (prop_name.compare("inputs:ior") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->ior.value = (*p);
          }
        } else if (prop_name.compare("inputs:opacity") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->opacity.value = (*p);
          }
        } else if (prop_name.compare("inputs:clearcoat") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->clearcoat.value = (*p);
          }
        } else if (prop_name.compare("inputs:clearcoatRoughness") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->clearcoatRoughness.value = (*p);
          }
        } else if (prop_name.compare("inputs:metallic") == 0) {
          // type: float
          auto p = attr.var.get_value<float>();
          if (p) {
            shader->metallic.value = (*p);
          }
        } else if (prop_name.compare("inputs:metallic.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          auto p = attr.var.get_value<std::string>();
          if (p) {
            shader->metallic.path = *p;
          }
        } else if (prop_name.compare("inputs:diffuseColor") == 0) {
          auto p = attr.var.get_value<value::float3>();
          if (p) {
            shader->diffuseColor.color = (*p);

            DCOUT("diffuseColor: " << shader->diffuseColor.color[0] << ", "
                                   << shader->diffuseColor.color[1] << ", "
                                   << shader->diffuseColor.color[2]);
          }
        } else if (prop_name.compare("inputs:diffuseColor.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          auto p = attr.var.get_value<std::string>();
          if (p) {
            shader->diffuseColor.path = *p;
          }
        } else if (prop_name.compare("inputs:emissiveColor") == 0) {
          // if (auto p = primvar::as_basic<value::float3>(&attr.var)) {
          //  shader->emissiveColor.color = (*p);

          //}
        } else if (prop_name.compare("inputs:emissiveColor.connect") == 0) {
          // Currently we assume texture is assigned to this attribute.
          // if (auto p = primvar::as_basic<std::string>(&attr.var)) {
          //  shader->emissiveColor.path = *p;
          //}
        }
      }
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructSkelRoot(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    SkelRoot *skelRoot) {
  DCOUT("Parse skelRoot");
  (void)skelRoot;

  for (const auto &fv : fields) {
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ").");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      _err += "FieldSet id: " + std::to_string(spec.fieldset_index.value) +
              " must exist in live fieldsets.\n";
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop:" << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            // shader_type = (*p);
          }
        }
      }
    }
  }

  return true;
}

#endif

namespace {}

bool USDCReader::Impl::BuildPropertyMap(const std::vector<size_t> &pathIndices,
                      const PathIndexToSpecIndexMap &psmap,
                      prim::PropertyMap *props) {

  for (size_t i = 0; i < pathIndices.size(); i++) {
    int child_index = int(pathIndices[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    if (!psmap.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index = psmap.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    // Property must be Connection or RelationshipTarget
    if ((spec.spec_type == SpecType::Connection) ||
        (spec.spec_type == SpecType::RelationshipTarget)) {
      // OK
    } else {
      continue;
    }

    nonstd::optional<Path> path = GetPath(spec.path_index);

    if (!path) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
    }

    DCOUT("Path prim part: " << path.value().GetPrimPart()
                             << ", prop part: " << path.value().GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const crate::FieldValuePairVector &child_fvs =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.value().GetPropPart();

      Property prop;
      if (!ParseProperty(child_fvs, &prop)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "Failed to construct Property from FieldValuePairVector.");
      }

      props->emplace(prop_name, prop);
      DCOUT("Add property : " << prop_name);
    }
  }

  return true;
}

// TODO: Use template and move code to `value-types.hh`
static bool UpcastType(
  const std::string &reqType,
  value::Value &inout)
{ 
  
  if (reqType == "float") {
    float dst;
    if (auto pv = inout.get_value<value::half>()) {
      dst = half_to_float(pv.value());
      inout = dst;
      return true;
    }
  } else if (reqType == "float2") {
    value::float2 dst;
    if (auto pv = inout.get_value<value::half2>()) {
      value::half2 v = pv.value();
      dst[0] = half_to_float(v[0]);
      dst[1] = half_to_float(v[1]);
      inout = dst;
      return true;
    }
  } else if (reqType == "float3") {
    value::float3 dst;
    if (auto pv = inout.get_value<value::half3>()) {
      value::half3 v = pv.value();
      dst[0] = half_to_float(v[0]);
      dst[1] = half_to_float(v[1]);
      dst[2] = half_to_float(v[2]);
      inout = dst;
      return true;
    }
  } else if (reqType == "float4") {
    value::float4 dst;
    if (auto pv = inout.get_value<value::half4>()) {
      value::half4 v = pv.value();
      dst[0] = half_to_float(v[0]);
      dst[1] = half_to_float(v[1]);
      dst[2] = half_to_float(v[2]);
      dst[3] = half_to_float(v[3]);
      inout = dst;
      return true;
    }
  } else if (reqType == "double") {
    double dst;
    if (auto pv = inout.get_value<value::half>()) {
      dst = double(half_to_float(pv.value()));
      inout = dst;
      return true;
    }
  } else if (reqType == "double2") {
    value::double2 dst;
    if (auto pv = inout.get_value<value::half2>()) {
      value::half2 v = pv.value();
      dst[0] = double(half_to_float(v[0]));
      dst[1] = double(half_to_float(v[1]));
      inout = dst;
      return true;
    }
  } else if (reqType == "double3") {
    value::double3 dst;
    if (auto pv = inout.get_value<value::half3>()) {
      value::half3 v = pv.value();
      dst[0] = double(half_to_float(v[0]));
      dst[1] = double(half_to_float(v[1]));
      dst[2] = double(half_to_float(v[2]));
      inout = dst;
      return true;
    }
  } else if (reqType == "double4") {
    value::double4 dst;
    if (auto pv = inout.get_value<value::half4>()) {
      value::half4 v = pv.value();
      dst[0] = double(half_to_float(v[0]));
      dst[1] = double(half_to_float(v[1]));
      dst[2] = double(half_to_float(v[2]));
      dst[3] = double(half_to_float(v[3]));
      inout = dst;
      return true;
    }
  }

  return false;
}

bool USDCReader::Impl::ParseProperty(const crate::FieldValuePairVector &fvs,
                                     Property *prop) {
  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  bool custom{false};
  nonstd::optional<value::token> typeName;
  Property::Type propType{Property::Type::EmptyAttrib};
  PrimAttrib attr;

  bool is_scalar{false};
  value::Value scalar;

  // TODO: Rel, TimeSamples, Connection

  for (auto &fv : fvs) {
    DCOUT(" fv name " << fv.first << "(type = " << fv.second.type_name()
                      << ")");

    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        custom = pv.value();
        DCOUT("  custom = " << pv.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "variability") {
      if (auto pv = fv.second.get_value<Variability>()) {
        attr.variability = pv.value();
        DCOUT("  variability = " << to_string(attr.variability));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variability` field is not `varibility` type.");
      }
    } else if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("  typeName = " << pv.value().str());
        typeName = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    } else if (fv.first == "default") {
      propType = Property::Type::Attrib;

      // Set scalar
      // TODO: Easier CrateValue to PrimAttrib.var conversion
      scalar = fv.second.get_raw();
      is_scalar = true;

    } else if (fv.first == "timeSamples") {

      propType = Property::Type::Attrib;

      if (auto pv = fv.second.get_value<value::TimeSamples>()) {
        attr.var.var = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`timeSamples` is not TimeSamples data.");
      }
    } else {
      PUSH_WARN("TODO: " << fv.first);
      DCOUT("TODO: " << fv.first);
    }
  }

  if (is_scalar) {
    if (typeName) {
      // Some inlined? value uses less accuracy type(e.g. `half3`) than typeName(e.g. `float3`)
      // Use type specified in `typeName` as much as possible.
      std::string reqTy = typeName.value().str();
      std::string scalarTy = scalar.type_name();

      if (reqTy.compare(scalarTy) != 0) {
        bool ret = UpcastType(reqTy, scalar);
        if (ret) {
          DCOUT(fmt::format("Upcast type from {} to {}.", scalarTy, reqTy));
        }
      }
    }
    attr.var.var.values.push_back(scalar);
  }

  if (propType == Property::Type::EmptyAttrib) {
    (*prop) = Property(custom);
  } else if (propType == Property::Type::Attrib) {
    (*prop) = Property(attr, custom);
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "TODO:");
  }

  return true;
}

template <typename T>
bool USDCReader::Impl::ReconstructSimpleAttribute(
    int parent, const crate::FieldValuePairVector &fvs, T *attr,
    bool *custom_out, Variability *variability_out) {
  (void)attr;

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  bool valid{false};

  for (auto &fv : fvs) {
    // Some predefined fields
    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        if (custom_out) {
          (*custom_out) = pv.value();
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "variability") {
      if (auto pv = fv.second.get_value<Variability>()) {
        if (variability_out) {
          (*variability_out) = pv.value();
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variability` field is not `varibility` type.");
      }
    } else if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("typeName = " << pv.value().str());
        if (value::TypeTrait<T>::type_name() != pv.value().str()) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Property type mismatch. `"
                        << value::TypeTrait<T>::type_name()
                        << "` expected but got `" << pv.value().str() << "`.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    } else if (fv.first == "default") {
      if (fv.second.type_id() != value::TypeTrait<T>::type_id) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Property type mismatch. `"
                                            << value::TypeTrait<T>::type_name()
                                            << "` expected but got `"
                                            << fv.second.type_name() << "`.");
      }

      if (auto pv = fv.second.get_value<T>()) {
        (*attr) = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Type mismatch. Internal error.");
      }
      valid = true;
    }

    DCOUT("parent[" << parent << "] fv name " << fv.first
                    << "(type = " << fv.second.type_name() << ")");
  }

  if (!valid) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "`default` field not found.");
  }

  return true;
}

template <typename T>
bool USDCReader::Impl::ReconstructTypedAttribute(
    int parent, const crate::FieldValuePairVector &fvs,
    TypedAttribute<T> *attr) {
  (void)attr;

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  for (auto &fv : fvs) {
    // Some predefined fields
    if (fv.first == "custom") {
      if (auto pv = fv.second.get_value<bool>()) {
        attr->custom = pv.value();
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "`custom` field is not `bool` type.");
      }
    } else if (fv.first == "typeName") {
      if (auto pv = fv.second.get_value<value::token>()) {
        DCOUT("typeName = " << pv.value().str());
        if (value::TypeTrait<T>::type_name() != pv.value().str()) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Property type mismatch. `"
                        << value::TypeTrait<T>::type_name()
                        << "` expected but got `" << pv.value().str() << "`.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`typeName` field is not `token` type.");
      }
    } else if (fv.first == "default") {
      if (fv.second.type_id() != value::TypeTrait<T>::type_id) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Property type mismatch. `"
                                            << value::TypeTrait<T>::type_name()
                                            << "` expected but got `"
                                            << fv.second.type_name() << "`.");
      }

      if (auto pv = fv.second.get_value<T>()) {
        Animatable<T> anim;
        anim.value = pv.value();
        attr->value = anim;
      }
    }

    DCOUT("parent[" << parent << "] fv name " << fv.first
                    << "(type = " << fv.second.type_name() << ")");
  }

  return true;
}

//
// -- specialization of ReconstructPrim
//
#define FIELDVALUE_DATATYPE_CHECK(__fv, __name, __req_type)                 \
  {                                                                         \
    if (__fv.first == __name) {                                             \
      if (__fv.second.type_id() != value::TypeTrait<__req_type>::type_id) { \
        PUSH_ERROR_AND_RETURN_TAG(                                          \
            kTag, "`" << __name << "` attribute must be "                   \
                      << value::TypeTrait<__req_type>::type_name()          \
                      << " type, but got " << __fv.second.type_name());     \
      }                                                                     \
    }                                                                       \
  }

template <>
bool USDCReader::Impl::ReconstructPrim<Xform>(
    const crate::CrateReader::Node &node,
    const crate::FieldValuePairVector &fvs,
    const PathIndexToSpecIndexMap &psmap, Xform *xform) {
  // TODO:
  //  * [ ] !invert! suffix
  //  * [ ] !resetXformStack! suffix

  (void)xform;
  (void)fvs;

  prim::PropertyMap properties;
  if (!BuildPropertyMap(node.GetChildren(), psmap, &properties)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
  }

  prim::ReferenceList refs; // TODO:
  std::string err;

  DCOUT("Reconstruct Xform ====");

  if (!prim::ReconstructPrim<Xform>(properties, refs, xform, &_warn, &err)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, err);
  }


#if 0
  //
  // Process Xform's specific prop
  //
  std::set<std::string> table;
  std::string err;
  if (!prim::ReconstructXformOpsFromProperties(table, properties,
                                               &xform->xformOps, &err)) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct xformOp data: " << err);
  }
#endif

  DCOUT("End Reconstruct Xform ====");

  return true;
}

template <>
bool USDCReader::Impl::ReconstructPrim<Scope>(
    const crate::CrateReader::Node &node,
    const crate::FieldValuePairVector &fvs,
    const PathIndexToSpecIndexMap &psmap, Scope *scope) {
  (void)scope;

  DCOUT("Reconstruct Scope ====");

  for (const auto &fv : fvs) {
    DCOUT("field = " << fv.first << ", type = " << fv.second.type_name());

    FIELDVALUE_DATATYPE_CHECK(fv, "properties", std::vector<value::token>)
  }

  // Properties are stored in Children node

  //
  // NOTE: Currently we assume one deeper node has Xform's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    if (!psmap.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index = psmap.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    nonstd::optional<Path> path = GetPath(spec.path_index);

    if (!path) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
    }

    DCOUT("Path prim part: " << path.value().GetPrimPart()
                             << ", prop part: " << path.value().GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const crate::FieldValuePairVector &child_fvs =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.value().GetPropPart();

      DCOUT("prop.name = " << prop_name);
      (void)child_fvs;
#if 0
      PrimAttrib attr;
      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("Xform: prop: " << prop_name << ", ret = " << ret);
      if (ret) {
        // TODO(syoyo): Implement
        PUSH_ERROR("TODO: Implemen Xform prop: " + prop_name);
      }
#endif
    }
  }

  DCOUT("End Reconstruct Scope ====");

  return true;
}

template <>
bool USDCReader::Impl::ReconstructPrim<Skeleton>(
    const crate::CrateReader::Node &node,
    const crate::FieldValuePairVector &fvs,
    const PathIndexToSpecIndexMap &psmap, Skeleton *skeleton) {
  DCOUT("Parse skeleton");
  (void)skeleton;
  (void)node;
  (void)psmap;

  for (const auto &fv : fvs) {
    DCOUT("field: " << fv.first);
    if (fv.first == "properties") {
      FIELDVALUE_DATATYPE_CHECK(fv, "properties", std::vector<value::token>)

      // for (size_t i = 0; i < fv.second.GetStringArray().size(); i++) {
      // }
    }
  }

#if 0
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
                 ". Must be in range [0, " + std::to_string(_nodes.size()) +
                 ")");
      return false;
    }

    // const Node &child_node = _nodes[size_t(child_index)];

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      PUSH_ERROR("No specifier found for node id: " +
                 std::to_string(child_index));
      return false;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
                 ". Must be in range [0, " + std::to_string(_specs.size()) +
                 ")");
      return false;
    }

    const crate::Spec &spec = _specs[spec_index];

    Path path = GetPath(spec.path_index);
    DCOUT("Path prim part: " << path.GetPrimPart()
                             << ", prop part: " << path.GetPropPart()
                             << ", spec_index = " << spec_index);

    if (!_live_fieldsets.count(spec.fieldset_index)) {
      PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
                 " must exist in live fieldsets.");
      return false;
    }

    const FieldValuePairVector &child_fields =
        _live_fieldsets.at(spec.fieldset_index);

    {
      std::string prop_name = path.GetPropPart();

      PrimAttrib attr;

      bool ret = ParseAttribute(child_fields, &attr, prop_name);
      DCOUT("prop:" << prop_name << ", ret = " << ret);

      if (ret) {
        // Currently we only support predefined PBR attributes.

        if (prop_name.compare("info:id") == 0) {
          auto p = attr.var.get_value<std::string>();  // `token` type, but
                                                       // treat it as string
          if (p) {
            // shader_type = (*p);
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDCReader::Impl::ReconstrcutStageMeta(
    const crate::FieldValuePairVector &fvs, StageMetas *metas,
    std::vector<value::token> *primChildren) {
  /// Stage(toplevel layer) Meta fieldSet example.
  ///
  ///   specTy = SpecTypeRelationship
  ///
  ///     - customLayerData(dict)
  ///     - defaultPrim(token)
  ///     - metersPerUnit(double)
  ///     - timeCodesPerSecond(double)
  ///     - upAxis(token)
  ///     - primChildren(token[]) : Crate only. List of root prims
  ///     - documentation(string) : `doc`
  ///     - comment(string) : comment

  for (const auto &fv : fvs) {
    if (fv.first == "upAxis") {
      auto vt = fv.second.get_value<value::token>();
      if (!vt) {
        PUSH_ERROR_AND_RETURN("`upAxis` must be `token` type.");
      }

      std::string v = vt.value().str();
      if (v == "Y") {
        metas->upAxis = Axis::Y;
      } else if (v == "Z") {
        metas->upAxis = Axis::Z;
      } else if (v == "X") {
        metas->upAxis = Axis::X;
      } else {
        PUSH_ERROR_AND_RETURN("`upAxis` must be 'X', 'Y' or 'Z' but got '" + v +
                              "'(note: Case sensitive)");
      }
      DCOUT("upAxis = " << to_string(metas->upAxis.get()));

    } else if (fv.first == "metersPerUnit") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->metersPerUnit = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->metersPerUnit = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`metersPerUnit` value must be double or float type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("metersPerUnit = " << metas->metersPerUnit.get());
    } else if (fv.first == "timeCodesPerSecond") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->timeCodesPerSecond = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->timeCodesPerSecond = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`timeCodesPerSecond` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("timeCodesPerSecond = " << metas->timeCodesPerSecond.get());
    } else if (fv.first == "startTimeCode") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->startTimeCode = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->startTimeCode = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`startTimeCode` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("startimeCode = " << metas->startTimeCode.get());
    } else if (fv.first == "endTimeCode") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->startTimeCode = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->startTimeCode = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`endTimeCode` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("endTimeCode = " << metas->endTimeCode.get());
    } else if ((fv.first == "defaultPrim")) {
      auto v = fv.second.get_value<value::token>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("`defaultPrim` must be `token` type.");
      }

      metas->defaultPrim = v.value();
      DCOUT("defaultPrim = " << metas->defaultPrim.str());
    } else if (fv.first == "customLayerData") {
      if (auto v = fv.second.get_value<CustomDataType>()) {
        metas->customLayerData = v.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "customLayerData must be `dictionary` type, but got type `" +
            fv.second.type_name());
      }
    } else if (fv.first == "primChildren") {  // it looks only appears in USDC.
      auto v = fv.second.get_value<std::vector<value::token>>();
      if (!v) {
        PUSH_ERROR("Type must be `token[]` for `primChildren`, but got " +
                   fv.second.type_name() + "\n");
        return false;
      }

      if (primChildren) {
        (*primChildren) = v.value();
        DCOUT("primChildren = " << (*primChildren));
      }
    } else if (fv.first == "documentation") {  // 'doc'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR("Type must be `string` for `documentation`, but got " +
                   fv.second.type_name() + "\n");
        return false;
      }
      StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->doc = sdata;
      DCOUT("doc = " << metas->doc.value);
    } else {
      PUSH_WARN("[StageMeta] TODO: " + fv.first + "\n");
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructPrimRecursively(
    int parent, int current, int level, const PathIndexToSpecIndexMap &psmap,
    Stage *stage) {
  DCOUT("ReconstructPrimRecursively: parent = "
        << std::to_string(current) << ", level = " << std::to_string(level));

  if ((current < 0) || (current >= int(_nodes.size()))) {
    PUSH_ERROR("Invalid current node id: " + std::to_string(current) +
               ". Must be in range [0, " + std::to_string(_nodes.size()) + ")");
    return false;
  }

  const crate::CrateReader::Node &node = _nodes[size_t(current)];

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  std::cout << pprint::Indent(uint32_t(level)) << "lv[" << level
            << "] node_index[" << current << "] " << node.GetLocalPath()
            << " ==\n";
  std::cout << pprint::Indent(uint32_t(level)) << " childs = [";
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    std::cout << node.GetChildren()[i];
    if (i != (node.GetChildren().size() - 1)) {
      std::cout << ", ";
    }
  }
  std::cout << "]\n";
#endif

  if (!psmap.count(uint32_t(current))) {
    // No specifier assigned to this node.
    DCOUT("No specifier assigned to this node: " << current);
    return true;  // would be OK.
  }

  uint32_t spec_index = psmap.at(uint32_t(current));
  if (spec_index >= _specs.size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
    return false;
  }

  const crate::Spec &spec = _specs[spec_index];

  DCOUT(pprint::Indent(uint32_t(level))
        << "  specTy = " << to_string(spec.spec_type));
  DCOUT(pprint::Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if ((spec.spec_type == SpecType::Connection) ||
      (spec.spec_type == SpecType::RelationshipTarget)) {
    if (_prim_table.count(parent)) {
      // This node is a Properties node. These are processed in
      // ReconstructPrim(), so nothing to do here.
      return true;
    }
  }

  if (!_live_fieldsets.count(spec.fieldset_index)) {
    PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
               " must exist in live fieldsets.");
    return false;
  }

  const crate::FieldValuePairVector &fvs =
      _live_fieldsets.at(spec.fieldset_index);

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

  // DBG
  for (auto &fv : fvs) {
    DCOUT("parent[" << current << "] level [" << level << "] fv name "
                    << fv.first << "(type = " << fv.second.type_name() << ")");
  }

  nonstd::optional<Prim> prim;
  std::vector<value::token> primChildren;
  Path elemPath;

  // StageMeta = root only attributes.
  // TODO: Unify reconstrction code with USDAReder?
  if (current == 0) {

    if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
      DCOUT("Root element path: " << pv.value().full_path_name());
    } else {
      PUSH_ERROR_AND_RETURN(
          "(Internal error). Root Element Path not found.");
    }


    // Root layer(Stage) is Relationship for some reaon.
    if (spec.spec_type != SpecType::Relationship) {
      PUSH_ERROR_AND_RETURN(
          "SpecTypeRelationship expected for root layer(Stage) element.");
    }

    if (!ReconstrcutStageMeta(fvs, &stage->GetMetas(), &primChildren)) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct StageMeta.");
    }

    _prim_table.insert(current);

  } else {
    nonstd::optional<std::string> typeName;
    nonstd::optional<Specifier> specifier;
    std::vector<value::token> properties;

    ///
    ///
    /// Prim(Model) fieldSet example.
    ///
    ///
    ///   specTy = SpecTypePseudoRoot
    ///
    ///     - specifier(specifier) : e.g. `def`, `over`, ...
    ///     - kind(token) : kind metadataum
    ///     - optional: typeName(token) : type name of Prim(e.g. `Xform`). No
    ///     typeName = `def "mynode"`
    ///     - properties(token[]) : List of name of Prim properties(attributes)
    ///     - optional: primChildren(token[]): List of child prims.
    ///
    ///

    /// Attrib fieldSet example
    ///
    ///   specTyppe = SpecTypeConnection
    ///
    ///     - typeName(token) : type name of Attribute(e.g. `float`)
    ///     - custom(bool) : `custom` qualifier
    ///     - variability(variability) : Variability(meta?)
    ///     <value>
    ///       - default : Default(fallback) value.
    ///       - timeSample(TimeSamples) : `.timeSamples` data.
    ///       - connectionPaths(type = ListOpPath) : `.connect`
    ///       - (Empty) : Define only(Neiher connection nor value assigned. e.g.
    ///       "float outputs:rgb")

    DCOUT("---");

    for (const auto &fv : fvs) {
      if (fv.first == "typeName") {
        if (auto pv = fv.second.get_value<value::token>()) {
          typeName = pv.value().str();
          DCOUT("typeName = " << typeName.value());
        } else {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "`typeName` must be type `token`, but got type `"
                        << fv.second.type_name() << "`");
        }
      } else if (fv.first == "specifier") {
        if (auto pv = fv.second.get_value<Specifier>()) {
          specifier = pv.value();
          DCOUT("specifier = " << to_string(specifier.value()));
        }
      } else if (fv.first == "properties") {
        if (auto pv = fv.second.get_value<std::vector<value::token>>()) {
          properties = pv.value();
          DCOUT("properties = " << properties);
        }
      }
    }

    DCOUT("===");

#define RECONSTRUCT_PRIM(__primty, __node_ty, __prim_name)                   \
  if (__node_ty == value::TypeTrait<__primty>::type_name()) {                \
    __primty typed_prim;                                                     \
    if (!ReconstructPrim(node, fvs, psmap, &typed_prim)) {                   \
      PUSH_ERROR_AND_RETURN_TAG(kTag,                                        \
                                "Failed to reconstruct Prim " << __node_ty); \
    }                                                                        \
    typed_prim.name = __prim_name; \
    value::Value primdata = typed_prim;                                      \
    prim = Prim(primdata);                                                   \
    /* PrimNode pnode; */                                                    \
    /* pnode.prim = prim; */                                                 \
    /* _prim_nodes.push_back(pnode); */                                      \
  } else

    if (spec.spec_type == SpecType::PseudoRoot) {
      // Prim

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();
        DCOUT(fmt::format("Element path: {}", elemPath.full_path_name()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "(Internal errror) Element path not found.");
      }


      // Sanity check
      if (specifier) {
        if (specifier.value() != Specifier::Def) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Currently TinyUSDZ only supports `def` for `specifier`.");
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`specifier` field is missing for FieldSets "
                                  "with SpecType::PseudoRoot.");
      }

      if (!typeName) {
        PUSH_WARN("Treat this node as Model(where `typeName` is missing.");
        typeName = "Model";
      }

      if (typeName) {
        std::string prim_name = elemPath.GetPrimPart();

        RECONSTRUCT_PRIM(Xform, typeName.value(), prim_name)
        // RECONSTRUCT_PRIM(GeomMesh, typeName.value(), prim_name)
        RECONSTRUCT_PRIM(Scope, typeName.value(), prim_name) {
          PUSH_WARN(
              "TODO or we can ignore this typeName: " << typeName.value());
        }

        if (prim) {
          prim.value().elementPath = elemPath;
        }
      }

      _prim_table.insert(current);
    } else {
      PUSH_ERROR_AND_RETURN_TAG(kTag,
                                "TODO: specTy = " << to_string(spec.spec_type));
    }
  }

  {
    DCOUT("node.Children.size = " << node.GetChildren().size());
    for (size_t i = 0; i < node.GetChildren().size(); i++) {
      if (!ReconstructPrimRecursively(current, int(node.GetChildren()[i]),
                                      level + 1, psmap, stage)) {
        return false;
      }
    }
  }

  if (parent == 0) {  // root prim
    if (prim) {
      stage->GetRootPrims().emplace_back(std::move(prim.value()));
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructStage(Stage *stage) {
  (void)stage;

  // format test
  DCOUT(fmt::format("# of Paths = {}", crate_reader->NumPaths()));

  if (crate_reader->NumNodes() == 0) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  // TODO: Directly access data in crate_reader.
  _nodes = crate_reader->GetNodes();
  _specs = crate_reader->GetSpecs();
  _fields = crate_reader->GetFields();
  _fieldset_indices = crate_reader->GetFieldsetIndices();
  _paths = crate_reader->GetPaths();
  _elemPaths = crate_reader->GetElemPaths();
  _live_fieldsets = crate_reader->GetLiveFieldSets();

  PathIndexToSpecIndexMap
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs.size(); i++) {
      if (_specs[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      if (path_index_to_spec_index_map.count(_specs[i].path_index.value) != 0) {
        PUSH_ERROR_AND_RETURN("Multiple PathIndex found in Crate data.");
      }

      path_index_to_spec_index_map[_specs[i].path_index.value] = uint32_t(i);
    }
  }

  stage->GetRootPrims().clear();

  int root_node_id = 0;
  bool ret = ReconstructPrimRecursively(/* no further root for root_node */ -1,
                                        root_node_id, /* level */ 0,
                                        path_index_to_spec_index_map, stage);

  if (!ret) {
    PUSH_ERROR_AND_RETURN("Failed to reconstruct Stage(Prim hierarchy)");
  }

  return true;
}

bool USDCReader::Impl::ReadUSDC() {
  if (crate_reader) {
    delete crate_reader;
  }

  // TODO: Setup CrateReaderConfig.
  crate::CrateReaderConfig config;
  config.numThreads = _config.numThreads;

  crate_reader = new crate::CrateReader(_sr, config);

  if (!crate_reader->ReadBootStrap()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadTOC()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  // Read known sections

  if (!crate_reader->ReadTokens()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadStrings()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadFields()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadFieldSets()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadPaths()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  if (!crate_reader->ReadSpecs()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();
    return false;
  }

  // TODO(syoyo): Read unknown sections

  ///
  /// Reconstruct C++ representation of USD scene graph.
  ///
  DCOUT("BuildLiveFieldSets\n");
  if (!crate_reader->BuildLiveFieldSets()) {
    _warn = crate_reader->GetWarning();
    _err = crate_reader->GetError();

    return false;
  }

  DCOUT("Read Crate.\n");

  return true;
}

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, const USDCReaderConfig &config) {
  impl_ = new USDCReader::Impl(sr, config);
}

USDCReader::~USDCReader() {
  delete impl_;
  impl_ = nullptr;
}

bool USDCReader::ReconstructStage(Stage *stage) {
  DCOUT("Reconstruct Stage.");
  return impl_->ReconstructStage(stage);
}

std::string USDCReader::GetError() { return impl_->GetError(); }

std::string USDCReader::GetWarning() { return impl_->GetWarning(); }

bool USDCReader::ReadUSDC() { return impl_->ReadUSDC(); }

}  // namespace usdc
}  // namespace tinyusdz

#else

namespace tinyusdz {
namespace usdc {

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, USDCReaderConfig &config) {
  (void)sr;
  (void)config;
}

USDCReader::~USDCReader() {}

bool USDCReader::ReconstructStage(Stage *stage) {
  (void)scene;
  DCOUT("Reconstruct Stage.");
  return false;
}

std::string USDCReader::GetError() {
  return "USDC reader feature is disabled in this build.\n";
}

std::string USDCReader::GetWarning() { return ""; }

}  // namespace usdc
}  // namespace tinyusdz

#endif
