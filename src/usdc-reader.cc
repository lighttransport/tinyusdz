// SPDX-License-Identifier: MIT
// Copyright 2020-Present Syoyo Fujita.
//
// USDC(Crate) reader
//
// TODO:
//
// - [ ] Refactor Reconstruct*** function
// - [ ] Set `custom` in property by looking up schema.
// - [ ] And more...
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
#include "stream-reader.hh"
#include "value-pprint.hh"

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

#ifdef TINYUSDZ_PRODUCTION_BUILD
// Do not include full filepath for privacy.
#define PUSH_ERROR(s)                                               \
  {                                                                 \
    std::ostringstream ss;                                          \
    ss << "[usdc-parser] " << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                        \
    _err += ss.str() + "\n";                                        \
  }                                                                 \
  while (0)

#define PUSH_WARN(s)                                                \
  {                                                                 \
    std::ostringstream ss;                                          \
    ss << "[usdc-parser] " << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                        \
    _warn += ss.str() + "\n";                                       \
  }                                                                 \
  while (0)

#else

#if 0
#define PUSH_ERROR(s)                                              \
  {                                                                \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _err += ss.str() + "\n";                                       \
  }                                                                \
  while (0)

#define PUSH_WARN(s)                                               \
  {                                                                \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    _warn += ss.str() + "\n";                                      \
  }                                                                \
  while (0)
#endif

#endif

#include "common-macros.inc"

namespace tinyusdz {
namespace usdc {


class USDCReader::Impl {
 public:
  Impl(StreamReader *sr, int num_threads) : _sr(sr) {
    if (num_threads == -1) {
#if defined(__wasi__)
      num_threads = 1;
#else
      num_threads = (std::max)(1, int(std::thread::hardware_concurrency()));
#endif
    }

    // Limit to 1024 threads.
    _num_threads = (std::min)(1024, num_threads);
  }

  ~Impl() {
    delete crate_reader;
    crate_reader = nullptr;
  }

  bool ReadUSDC();

#if 0
  ///
  /// Parse node's attribute from FieldValuePairVector.
  ///
  bool ParseAttribute(const FieldValuePairVector &fvs, PrimAttrib *attr,
                      const std::string &prop_name);

  bool ReconstructXform(const Node &node, const FieldValuePairVector &fields,
                        const std::unordered_map<uint32_t, uint32_t>
                            &path_index_to_spec_index_map,
                        Xform *xform);

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

  bool ReconstructSceneRecursively(int parent_id, int level,
                                   const std::unordered_map<uint32_t, uint32_t>
                                       &path_index_to_spec_index_map,
                                   Scene *scene);
#endif

  bool ReconstructStage(Stage *stage);

  ///
  /// --------------------------------------------------
  ///

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const { return memory_used / (1024 * 1024); }

  //
  // APIs valid after successfull Parse()
  //

  //size_t NumPaths() const { return _paths.size(); }

 private:
  //bool ReadCompressedPaths(const uint64_t ref_num_paths);

  crate::CrateReader *crate_reader{nullptr};

  StreamReader *_sr = nullptr;
  std::string _err;
  std::string _warn;

  int _num_threads{1};

  // Tracks the memory used(In advisorily manner since counting memory usage is
  // done by manually, so not all memory consumption could be tracked)
  size_t memory_used{0};  // in bytes.

};

//
// -- Impl
//

#if 0

#define FIELDVALUE_DATATYPE_CHECK(__fv, __name, __req_type) { \
  if (__fv.first == __name) { \
    if (__fv.second.GetTypeName() != __req_type) { \
      PUSH_ERROR("`" << __name << "` attribute must be " << __req_type << " type, but got " << __fv.second.GetTypeName()); \
      return false; \
    } \
  } \
}

bool USDCReader::Impl::ReconstructXform(
    uint64_t node_idx,
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Xform *xform) {
  // TODO:
  //  * [ ] !invert! suffix
  //  * [ ] !resetXformStack! suffix
  //  * [ ] maya:pivot support?

  (void)xform;

  DCOUT("Reconstruct Xform");

  for (const auto &fv : fields) {
    DCOUT("field = " << fv.first << ", type = " << fv.second.GetTypeName());

    FIELDVALUE_DATATYPE_CHECK(fv, "properties", crate::kTokenVector)

  }

  //
  // NOTE: Currently we assume one deeper node has Xform's attribute
  //
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    int child_index = int(node.GetChildren()[i]);
    if ((child_index < 0) || (child_index >= int(_nodes.size()))) {
      PUSH_ERROR("Invalid child node id: " + std::to_string(child_index) +
              ". Must be in range [0, " + std::to_string(_nodes.size()) + ")");
      return false;
    }

    if (!path_index_to_spec_index_map.count(uint32_t(child_index))) {
      // No specifier assigned to this child node.
      // Should we report an error?
      continue;
    }

    uint32_t spec_index =
        path_index_to_spec_index_map.at(uint32_t(child_index));
    if (spec_index >= _specs.size()) {
      PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
              ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
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
      DCOUT("Xform: prop: " << prop_name << ", ret = " << ret);
      if (ret) {
        // TODO(syoyo): Implement
        PUSH_ERROR("TODO: Implemen Xform prop: " + prop_name);
      }
    }
  }

  return true;
}

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

  DCOUT("Reconstruct Xform");

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

  DCOUT("Reconstruct Xform");

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

bool USDCReader::Impl::ReconstructSkeleton(
    const Node &node, const FieldValuePairVector &fields,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Skeleton *skeleton) {
  DCOUT("Parse skeleton");
  (void)skeleton;

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

  return true;
}

bool USDCReader::Impl::ReconstructSceneRecursively(
    int parent, int level,
    const std::unordered_map<uint32_t, uint32_t> &path_index_to_spec_index_map,
    Scene *scene) {

  DCOUT("ReconstructSceneRecursively: parent = " << std::to_string(parent) << ", level = " << std::to_string(level));

  if ((parent < 0) || (parent >= int(_nodes.size()))) {
    PUSH_ERROR("Invalid parent node id: " + std::to_string(parent) +
               ". Must be in range [0, " + std::to_string(_nodes.size()) + ")");
    return false;
  }

  const Node &node = _nodes[size_t(parent)];

#ifdef TINYUSDZ_LOCAL_DEBUG_PRINT
  auto IndentStr = [](int l) -> std::string {
    std::string indent;
    for (size_t i = 0; i < size_t(l); i++) {
      indent += "  ";
    }

    return indent;
  };
  std::cout << IndentStr(level) << "lv[" << level << "] node_index[" << parent
            << "] " << node.GetLocalPath() << " ==\n";
  std::cout << IndentStr(level) << " childs = [";
  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    std::cout << node.GetChildren()[i];
    if (i != (node.GetChildren().size() - 1)) {
      std::cout << ", ";
    }
  }
  std::cout << "]\n";
#endif

  if (!path_index_to_spec_index_map.count(uint32_t(parent))) {
    // No specifier assigned to this node.
    DCOUT("No specifier assigned to this node: " << parent);
    return true;  // would be OK.
  }

  uint32_t spec_index = path_index_to_spec_index_map.at(uint32_t(parent));
  if (spec_index >= _specs.size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs.size()) + ")");
    return false;
  }

  const crate::Spec &spec = _specs[spec_index];

  DCOUT(Indent(uint32_t(level)) << "  specTy = " << to_string(spec.spec_type));
  DCOUT(Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if (!_live_fieldsets.count(spec.fieldset_index)) {
    PUSH_ERROR("FieldSet id: " + std::to_string(spec.fieldset_index.value) +
               " must exist in live fieldsets.");
    return false;
  }

  const FieldValuePairVector &fields = _live_fieldsets.at(spec.fieldset_index);

  // root only attributes.
  if (parent == 0) {
    for (const auto &fv : fields) {
      if (fv.first == "upAxis") {
        auto vt = fv.second.get_value<value::token>();
        if (!vt) {
          PUSH_ERROR("`upAxis` must be `token` type.");
          return false;
        }

        std::string v = vt.value().str();
        if ((v != "Y") && (v != "Z") && (v != "X")) {
          PUSH_ERROR("`upAxis` must be 'X', 'Y' or 'Z' but got '" + v + "'");
          return false;
        }
        DCOUT("upAxis = " << v);
        scene->upAxis = std::move(v);

      } else if (fv.first == "metersPerUnit") {
        if (auto vf = fv.second.get_value<float>()) {
          scene->metersPerUnit = double(vf.value());
        } else if (auto vd = fv.second.get_value<double>()) {
          scene->metersPerUnit = vd.value();
        } else {
          PUSH_ERROR(
              "`metersPerUnit` value must be double or float type, but got '" +
              fv.second.GetTypeName() + "'");
          return false;
        }
        DCOUT("metersPerUnit = " << scene->metersPerUnit);
      } else if (fv.first == "timeCodesPerSecond") {
        if (auto vf = fv.second.get_value<float>()) {
          scene->timeCodesPerSecond = double(vf.value());
        } else if (auto vd = fv.second.get_value<double>()) {
          scene->timeCodesPerSecond = vd.value();
        } else {
          PUSH_ERROR(
              "`timeCodesPerSecond` value must be double or float "
              "type, but got '" +
              fv.second.GetTypeName() + "'");
          return false;
        }
        DCOUT("timeCodesPerSecond = " << scene->timeCodesPerSecond);
      } else if ((fv.first == "defaultPrim")) {
        auto v = fv.second.get_value<value::token>();
        if (!v) {
          PUSH_ERROR("`defaultPrim` must be `token` type.");
          return false;
        }

        scene->defaultPrim = v.value().str();
        DCOUT("defaultPrim = " << scene->defaultPrim);
      } else if (fv.first == "customLayerData") {
        if (auto v = fv.second.get_value<crate::CrateValue::Dictionary>()) {
          auto dict = v.value();
          (void)dict;
          PUSH_WARN("TODO: Store customLayerData.");
          // scene->customLayerData = fv.second.GetDictionary();
        } else {
          PUSH_ERROR("customLayerData must be `dict` type.");
          return false;
        }
      } else if (fv.first == "primChildren") {
        auto v = fv.second.get_value<std::vector<value::token>>();
        if (!v) {
          PUSH_ERROR("Type must be TokenArray for `primChildren`, but got " +
                     fv.second.GetTypeName() + "\n");
          return false;
        }

        // convert to string.
        std::vector<std::string> children;
        for (const auto &item : v.value()) {
          children.push_back(item.str());
        }
        scene->primChildren = children;
        DCOUT("primChildren = " << children);
      } else if (fv.first == "documentation") {  // 'doc'

        auto v = fv.second.get_value<std::string>();
        if (!v) {
          PUSH_ERROR("Type must be String for `documentation`, but got " +
                     fv.second.GetTypeName() + "\n");
          return false;
        }
        scene->doc = v.value();
        DCOUT("doc = " << v.value());
      } else {
        PUSH_WARN("TODO: " + fv.first + "\n");
        //_err += "TODO: " + fv.first + "\n";
        return false;
        // TODO(syoyo):
      }
    }
  }

  std::string node_type;
  crate::CrateValue::Dictionary assetInfo;

  auto GetTypeName = [](const FieldValuePairVector &fvs) -> nonstd::optional<std::string> {
    for (const auto &fv : fvs) {
      DCOUT(" fv.first = " << fv.first << ", type = " << fv.second.GetTypeName());
      if (fv.first == "typeName") {
        auto v = fv.second.get_value<value::token>();
        if (v) {
          return v.value().str();
        }
      }
    }

    return nonstd::nullopt;
  };

  DCOUT("---");

  if (auto v = GetTypeName(fields)) {
    node_type = v.value();
  }

  DCOUT("===");

#if 0  // TODO: Refactor)
  for (const auto &fv : fields) {
    DCOUT(IndentStr(level) << "  \"" << fv.first
                           << "\" : ty = " << fv.second.GetTypeName());

    if (fv.second.GetTypeId() == VALUE_TYPE_SPECIFIER) {
      DCOUT(IndentStr(level)
            << "    specifier = " << to_string(fv.second.GetSpecifier()));
    } else if ((fv.first == "primChildren") &&
               (fv.second.GetTypeName() == "TokenArray")) {
      // Check if TokenArray contains known child nodes
      const auto &tokens = fv.second.GetStringArray();

      // bool valid = true;
      for (const auto &token : tokens) {
        if (!node.GetPrimChildren().count(token)) {
          _err += "primChild '" + token + "' not found in node '" +
                  node.GetPath().full_path_name() + "'\n";
          // valid = false;
          break;
        }
      }
    } else if (fv.second.GetTypeName() == "TokenArray") {
      assert(fv.second.IsArray());
      const auto &strs = fv.second.GetStringArray();
      for (const auto &str : strs) {
        (void)str;
        DCOUT(IndentStr(level + 2) << str);
      }
    } else if ((fv.first == "customLayerData") &&
               (fv.second.GetTypeName() == "Dictionary")) {
      const auto &dict = fv.second.GetDictionary();

      for (const auto &item : dict) {
        if (item.second.GetTypeName() == "String") {
          scene->customLayerData[item.first] = item.second.GetString();
        } else if (item.second.GetTypeName() == "IntArray") {
          const auto arr = item.second.GetIntArray();
          scene->customLayerData[item.first] = arr;
        } else {
          PUSH_WARN("TODO(customLayerData): name " + item.first + ", type " +
                    item.second.GetTypeName());
        }
      }

    } else if (fv.second.GetTypeName() == "TokenListOp") {
      PUSH_WARN("TODO: name " + fv.first + ", type TokenListOp.");
    } else if (fv.second.GetTypeName() == "Vec3fArray") {
      PUSH_WARN("TODO: name: " + fv.first +
                ", type: " + fv.second.GetTypeName());

    } else if ((fv.first == "assetInfo") &&
               (fv.second.GetTypeName() == "Dictionary")) {
      node_type = "assetInfo";
      assetInfo = fv.second.GetDictionary();

    } else {
      PUSH_WARN("TODO: name: " + fv.first +
                ", type: " + fv.second.GetTypeName());
      // return false;
    }
  }
#endif

  DCOUT("node_type = " << node_type);


  if (node_type == "Xform") {
    Xform xform;
    if (!ReconstructXform(node, fields, path_index_to_spec_index_map, &xform)) {
      _err += "Failed to reconstruct Xform.\n";
      return false;
    }
    scene->xforms.push_back(xform);
  } else if (node_type == "BasisCurves") {
    GeomBasisCurves curves;
    if (!ReconstructGeomBasisCurves(node, fields, path_index_to_spec_index_map,
                                    &curves)) {
      _err += "Failed to reconstruct GeomBasisCurves.\n";
      return false;
    }
    curves.name = node.GetLocalPath();  // FIXME
    scene->geom_basis_curves.push_back(curves);
  } else if (node_type == "GeomSubset") {
    GeomSubset geom_subset;
    // TODO(syoyo): Pass Parent 'Geom' node.
    if (!ReconstructGeomSubset(node, fields, path_index_to_spec_index_map,
                               &geom_subset)) {
      _err += "Failed to reconstruct GeomSubset.\n";
      return false;
    }
    geom_subset.name = node.GetLocalPath();  // FIXME
    // TODO(syoyo): add GeomSubset to parent `Mesh`.
    _err += "TODO: Add GeomSubset to Mesh.\n";
    return false;

  } else if (node_type == "Mesh") {
    GeomMesh mesh;
    if (!ReconstructGeomMesh(node, fields, path_index_to_spec_index_map,
                             &mesh)) {
      PUSH_ERROR("Failed to reconstruct GeomMesh.");
      return false;
    }
    mesh.name = node.GetLocalPath();  // FIXME
    scene->geom_meshes.push_back(mesh);
  } else if (node_type == "Material") {
    Material material;
    if (!ReconstructMaterial(node, fields, path_index_to_spec_index_map,
                             &material)) {
      PUSH_ERROR("Failed to reconstruct Material.");
      return false;
    }
    material.name = node.GetLocalPath();  // FIXME
    scene->materials.push_back(material);
  } else if (node_type == "Shader") {
    Shader shader;
    if (!ReconstructShader(node, fields, path_index_to_spec_index_map,
                           &shader)) {
      PUSH_ERROR("Failed to reconstruct PreviewSurface(Shader).");
      return false;
    }

    shader.name = node.GetLocalPath();  // FIXME

    scene->shaders.push_back(shader);
  } else if (node_type == "Scope") {
    std::cout << "TODO: Scope\n";
  } else if (node_type == "assetInfo") {
    PUSH_WARN("TODO: Reconstruct dictionary value of `assetInfo`");
    //_nodes[size_t(parent)].SetAssetInfo(assetInfo);
  } else if (node_type == "Skeleton") {
    Skeleton skeleton;
    if (!ReconstructSkeleton(node, fields, path_index_to_spec_index_map,
                             &skeleton)) {
      PUSH_ERROR("Failed to reconstruct Skeleton.");
      return false;
    }

    skeleton.name = node.GetLocalPath();  // FIXME

    scene->skeletons.push_back(skeleton);

  } else if (node_type == "SkelRoot") {
    SkelRoot skelRoot;
    if (!ReconstructSkelRoot(node, fields, path_index_to_spec_index_map,
                             &skelRoot)) {
      PUSH_ERROR("Failed to reconstruct SkelRoot.");
      return false;
    }

    skelRoot.name = node.GetLocalPath();  // FIXME

    scene->skel_roots.push_back(skelRoot);
  } else {
    if (!node_type.empty()) {
      DCOUT("TODO or we can ignore this node: node_type: " << node_type);
    }
  }

  DCOUT("Children.size = " << node.GetChildren().size());

  for (size_t i = 0; i < node.GetChildren().size(); i++) {
    if (!ReconstructSceneRecursively(int(node.GetChildren()[i]), level + 1,
                                     path_index_to_spec_index_map, scene)) {
      return false;
    }
  }

  return true;
}
#endif

bool USDCReader::Impl::ReconstructStage(Stage *stage) {
  (void)stage;

#if 0 // TODO
  if (_nodes.empty()) {
    PUSH_WARN("Empty scene.");
    return true;
  }

  std::unordered_map<uint32_t, uint32_t>
      path_index_to_spec_index_map;  // path_index -> spec_index

  {
    for (size_t i = 0; i < _specs.size(); i++) {
      if (_specs[i].path_index.value == ~0u) {
        continue;
      }

      // path_index should be unique.
      assert(path_index_to_spec_index_map.count(_specs[i].path_index.value) ==
             0);
      path_index_to_spec_index_map[_specs[i].path_index.value] = uint32_t(i);
    }
  }

  int root_node_id = 0;

  bool ret = ReconstructSceneRecursively(root_node_id, /* level */ 0,
                                         path_index_to_spec_index_map, scene);

  if (!ret) {
    _err += "Failed to reconstruct scene.\n";
    return false;
  }

  return true;
#else
  return false;
#endif
}


bool USDCReader::Impl::ReadUSDC() {
  if (crate_reader) {
    delete crate_reader;
  }

  crate_reader = new crate::CrateReader(_sr, _num_threads);

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

  // TODO
  return true;

}

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, int num_threads) {
  impl_ = new USDCReader::Impl(sr, num_threads);
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

bool USDCReader::ReadUSDC() {
  return impl_->ReadUSDC();
}

}  // namespace usdc
}  // namespace tinyusdz


#else

namespace tinyusdz {
namespace usdc {

//
// -- Interface --
//
USDCReader::USDCReader(StreamReader *sr, int num_threads) {
  (void)sr;
  (void)num_threads;
}

USDCReader::~USDCReader() {
}

bool USDCReader::ReconstructScene(Scene *scene) {
  (void)scene;
  DCOUT("Reconstruct Scene.");
  return false;
}

std::string USDCReader::GetError() {
  return "USDC reader feature is disabled in this build.\n";
}

std::string USDCReader::GetWarning() { return ""; }

}  // namespace usdc
}  // namespace tinyusdz

#endif
