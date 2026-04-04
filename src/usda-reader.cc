// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// USDA reader
//   - [ ] Refactor and unify Prim and PrimSpec related code.

#include <algorithm>
#include <atomic>
#include <cctype>
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
//#include "asset-resolution.hh"
#include "core/model-scope.hh"  // Model, Scope
#include "usdGeom.hh"
#include "usdSkel.hh"
#include "usdPhysics.hh"
#include "usdAR.hh"
#include "usdMedia.hh"
#include "mjcPhysics.hh"
#if defined(__wasi__)
#else
#include <mutex>
#include <thread>
#endif
#include <vector>

#include "usda-reader.hh"
#include "layer.hh"
#include "parser-timing.hh"
#include "enum-handlers.hh"

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
#include "pprint-enum.hh"
#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "prim-reconstruct.hh"
#include "primvar.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "value-types.hh"
#include "tiny-format.hh"

#include "common-macros.inc"

namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty) template<> bool ReconstructPrim<__ty>(const Specifier &spec, PropertyMap &, const ReferenceList &, __ty *, std::string *, std::string *, const PrimReconstructOptions &)

RECONSTRUCT_PRIM_DECL(Xform);
RECONSTRUCT_PRIM_DECL(Model);
RECONSTRUCT_PRIM_DECL(Scope);
RECONSTRUCT_PRIM_DECL(Skeleton);
RECONSTRUCT_PRIM_DECL(SkelRoot);
RECONSTRUCT_PRIM_DECL(SkelAnimation);
RECONSTRUCT_PRIM_DECL(BlendShape);
RECONSTRUCT_PRIM_DECL(DomeLight);
RECONSTRUCT_PRIM_DECL(SphereLight);
RECONSTRUCT_PRIM_DECL(CylinderLight);
RECONSTRUCT_PRIM_DECL(DiskLight);
RECONSTRUCT_PRIM_DECL(DistantLight);
RECONSTRUCT_PRIM_DECL(RectLight);
RECONSTRUCT_PRIM_DECL(GeometryLight);
RECONSTRUCT_PRIM_DECL(PortalLight);
RECONSTRUCT_PRIM_DECL(GPrim);
RECONSTRUCT_PRIM_DECL(GeomMesh);
RECONSTRUCT_PRIM_DECL(GeomSubset);
RECONSTRUCT_PRIM_DECL(GeomSphere);
RECONSTRUCT_PRIM_DECL(GeomPoints);
RECONSTRUCT_PRIM_DECL(GeomCone);
RECONSTRUCT_PRIM_DECL(GeomCube);
RECONSTRUCT_PRIM_DECL(GeomCylinder);
RECONSTRUCT_PRIM_DECL(GeomCapsule);
RECONSTRUCT_PRIM_DECL(GeomBasisCurves);
RECONSTRUCT_PRIM_DECL(GeomNurbsCurves);
RECONSTRUCT_PRIM_DECL(GeomCamera);
RECONSTRUCT_PRIM_DECL(GeomPointInstancer);
RECONSTRUCT_PRIM_DECL(Material);
RECONSTRUCT_PRIM_DECL(Shader);
RECONSTRUCT_PRIM_DECL(NodeGraph);
// UsdPhysics + mjcPhysics
RECONSTRUCT_PRIM_DECL(PhysicsScene);
RECONSTRUCT_PRIM_DECL(PhysicsRevoluteJoint);
RECONSTRUCT_PRIM_DECL(PhysicsPrismaticJoint);
RECONSTRUCT_PRIM_DECL(PhysicsSphericalJoint);
RECONSTRUCT_PRIM_DECL(PhysicsFixedJoint);
RECONSTRUCT_PRIM_DECL(PhysicsDistanceJoint);
RECONSTRUCT_PRIM_DECL(PhysicsCollisionGroup);
RECONSTRUCT_PRIM_DECL(MjcActuator);
RECONSTRUCT_PRIM_DECL(MjcTendon);
RECONSTRUCT_PRIM_DECL(MjcKeyframe);
// AR/Interactive (Apple Preliminary_*)
RECONSTRUCT_PRIM_DECL(Preliminary_PhysicsGravitationalForce);
RECONSTRUCT_PRIM_DECL(Preliminary_InfiniteColliderPlane);
RECONSTRUCT_PRIM_DECL(Preliminary_ReferenceImage);
RECONSTRUCT_PRIM_DECL(Preliminary_Behavior);
RECONSTRUCT_PRIM_DECL(Preliminary_Trigger);
RECONSTRUCT_PRIM_DECL(Preliminary_Action);
RECONSTRUCT_PRIM_DECL(Preliminary_Text);
// usdMedia
RECONSTRUCT_PRIM_DECL(SpatialAudio);

#undef RECONSTRUCT_PRIM_DECL

} // namespace prim

namespace usda {

constexpr auto kTag = "[USDA]";

namespace {

static std::string TrimTrailingNewlines(std::string s) {
  while (!s.empty() && ((s.back() == '\n') || (s.back() == '\r'))) {
    s.pop_back();
  }
  return s;
}

static bool IsStructuredErrorHeader(const std::string &line) {
  if (line.empty()) {
    return false;
  }

  if ((line.rfind("Error at line ", 0) == 0) ||
      (line.rfind("Syntax Error at line ", 0) == 0) ||
      (line.rfind("Semantic Error at line ", 0) == 0) ||
      (line.rfind("Validation Error at line ", 0) == 0) ||
      (line.rfind("IO Error at line ", 0) == 0)) {
    return true;
  }

  return (line.find("():") != std::string::npos) &&
         ((line.rfind("/", 0) == 0) || (line.rfind("[", 0) == 0));
}

static std::vector<std::string> SplitStructuredErrorBlocks(
    const std::string &text) {
  std::vector<std::string> blocks;
  std::stringstream input(text);
  std::string line;
  std::string current;

  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }

    if (IsStructuredErrorHeader(line) && !current.empty()) {
      blocks.emplace_back(TrimTrailingNewlines(current));
      current.clear();
    }

    if (!current.empty()) {
      current += "\n";
    }
    current += line;
  }

  if (!current.empty()) {
    blocks.emplace_back(TrimTrailingNewlines(current));
  }

  return blocks;
}

static std::vector<std::string> SplitBlockLines(const std::string &block) {
  std::vector<std::string> lines;
  std::stringstream input(block);
  std::string line;
  while (std::getline(input, line)) {
    lines.emplace_back(line);
  }
  return lines;
}

static std::string JoinBlockLines(const std::vector<std::string> &lines) {
  std::stringstream ss;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i) {
      ss << "\n";
    }
    ss << lines[i];
  }
  return ss.str();
}

static std::string GetBlockHeaderLine(const std::string &block) {
  size_t eol = block.find('\n');
  if (eol == std::string::npos) {
    return block;
  }

  return block.substr(0, eol);
}

static std::string ExtractStructuredMessage(const std::string &header_line) {
  size_t sig = header_line.rfind("):");
  if (sig == std::string::npos) {
    return header_line;
  }

  size_t space = header_line.find(' ', sig + 2);
  if (space == std::string::npos) {
    return header_line;
  }

  return header_line.substr(space + 1);
}

static std::string NormalizeRedundantMessage(std::string msg) {
  msg = TrimTrailingNewlines(msg);

  auto remove_prefix = [&](const std::string &prefix) {
    if (msg.rfind(prefix, 0) == 0) {
      msg = msg.substr(prefix.size());
      return true;
    }
    return false;
  };

  auto remove_suffix = [&](const std::string &suffix) {
    if ((msg.size() >= suffix.size()) &&
        (msg.compare(msg.size() - suffix.size(), suffix.size(), suffix) == 0)) {
      msg.resize(msg.size() - suffix.size());
      return true;
    }
    return false;
  };

  remove_prefix("Failed to parse ");
  remove_prefix("Failed to parse");
  remove_suffix(" parse failed.");
  remove_suffix(" parse failed");
  remove_suffix(" failed.");
  remove_suffix(" failed");

  std::string normalized;
  normalized.reserve(msg.size());
  for (char ch : msg) {
    if ((ch == '`') || (ch == '\'') || (ch == '"') || (ch == '.') ||
        (ch == ',') || (ch == ':')) {
      continue;
    }

    normalized.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  }

  return normalized;
}

static bool IsGenericReconstructWrapperMessage(const std::string &msg) {
  if (msg.rfind("Failed to reconstruct ", 0) != 0) {
    return false;
  }

  return (msg.find(" Prim") != std::string::npos) ||
         (msg.find(" prim") != std::string::npos);
}

static bool IsSpecificReconstructMessage(const std::string &msg) {
  return (msg.rfind("Failed to Reconstruct ", 0) == 0) ||
         (msg.rfind("Failed to reconstruct ", 0) == 0);
}

static bool HasTrailingReconstructPrimMarker(const std::string &msg) {
  return msg.find(": Failed to reconstruct Prim: ") != std::string::npos;
}

static std::string StripTrailingReconstructPrimMarker(
    const std::string &block) {
  std::vector<std::string> lines = SplitBlockLines(block);
  if (lines.empty()) {
    return block;
  }

  const std::string marker = ": Failed to reconstruct Prim: ";
  size_t pos = lines[0].find(marker);
  if (pos == std::string::npos) {
    return block;
  }

  lines[0] = lines[0].substr(0, pos);
  return JoinBlockLines(lines);
}

static bool ShouldStripTrailingReconstructPrimMarker(
    const std::string &parent_block, const std::string &child_block) {
  const std::string parent_msg =
      ExtractStructuredMessage(GetBlockHeaderLine(parent_block));
  const std::string child_msg =
      ExtractStructuredMessage(GetBlockHeaderLine(child_block));

  return HasTrailingReconstructPrimMarker(parent_msg) &&
         IsSpecificReconstructMessage(child_msg);
}

static bool AreRedundantStructuredBlocks(const std::string &parent_block,
                                         const std::string &child_block) {
  const std::string parent_header = GetBlockHeaderLine(parent_block);
  const std::string child_header = GetBlockHeaderLine(child_block);

  if ((parent_header.rfind("Error at line ", 0) != 0) ||
      (child_header.rfind("Error at line ", 0) != 0)) {
    return false;
  }

  const std::string parent_msg =
      NormalizeRedundantMessage(ExtractStructuredMessage(parent_header));
  const std::string child_msg =
      NormalizeRedundantMessage(ExtractStructuredMessage(child_header));

  if (parent_msg.empty() || child_msg.empty()) {
    return false;
  }

  return parent_msg == child_msg;
}

static std::string MergeParentDetailsIntoChild(const std::string &parent_block,
                                               const std::string &child_block) {
  std::vector<std::string> parent_lines = SplitBlockLines(parent_block);
  std::vector<std::string> child_lines = SplitBlockLines(child_block);
  if (child_lines.empty()) {
    return child_block;
  }

  std::vector<std::string> merged;
  merged.reserve(parent_lines.size() + child_lines.size());
  merged.emplace_back(child_lines[0]);

  for (size_t i = 1; i < parent_lines.size(); ++i) {
    if (parent_lines[i].empty()) {
      continue;
    }

    bool duplicate = false;
    for (size_t j = 1; j < child_lines.size(); ++j) {
      if (child_lines[j] == parent_lines[i]) {
        duplicate = true;
        break;
      }
    }

    if (!duplicate) {
      merged.emplace_back(parent_lines[i]);
    }
  }

  for (size_t i = 1; i < child_lines.size(); ++i) {
    merged.emplace_back(child_lines[i]);
  }

  return JoinBlockLines(merged);
}

static bool ShouldMergeReconstructWrapper(const std::string &parent_block,
                                          const std::string &child_block) {
  const std::string parent_msg =
      ExtractStructuredMessage(GetBlockHeaderLine(parent_block));
  const std::string child_msg =
      ExtractStructuredMessage(GetBlockHeaderLine(child_block));

  if (!IsGenericReconstructWrapperMessage(parent_msg)) {
    return false;
  }

  if (!IsSpecificReconstructMessage(child_msg)) {
    return false;
  }

  return NormalizeRedundantMessage(parent_msg) !=
         NormalizeRedundantMessage(child_msg);
}

static std::vector<std::string> DeduplicateStructuredBlocks(
    const std::vector<std::string> &blocks) {
  std::vector<std::string> deduped;
  deduped.reserve(blocks.size());

  size_t i = 0;
  while (i < blocks.size()) {
    std::string current = blocks[i];

    if ((i + 1) < blocks.size()) {
      std::string next = blocks[i + 1];

      if (ShouldStripTrailingReconstructPrimMarker(current, next)) {
        current = StripTrailingReconstructPrimMarker(current);
      }

      if (AreRedundantStructuredBlocks(current, next)) {
        ++i;
        continue;
      }

      if (ShouldMergeReconstructWrapper(current, next)) {
        deduped.emplace_back(MergeParentDetailsIntoChild(current, next));
        i += 2;
        continue;
      }
    }

    deduped.emplace_back(current);
    ++i;
  }

  return deduped;
}

static void AppendIndentedBlock(std::stringstream &ss,
                                const std::string &block,
                                size_t depth) {
  std::stringstream input(block);
  std::string line;
  bool first_line = true;
  const std::string indent(depth * 2, ' ');
  const std::string detail_indent((depth * 2) + 2, ' ');
  bool first_detail_line = true;

  while (std::getline(input, line)) {
    if (first_line) {
      ss << indent << "- " << line << "\n";
      first_line = false;
    } else {
      if (IsStructuredErrorHeader(line) && first_detail_line) {
        ss << detail_indent << "-> " << line << "\n";
      } else {
        ss << detail_indent << line << "\n";
      }
      first_detail_line = false;
    }
  }
}

static std::string FormatStructuredErrorStack(
    const std::vector<std::string> &blocks,
    size_t max_blocks = 8) {
  if (blocks.empty()) {
    return std::string();
  }

  std::stringstream ss;
  ss << "Error stack:\n";

  size_t display_count = blocks.size();
  bool snipped = false;
  if ((max_blocks > 0) && (display_count > max_blocks)) {
    display_count = max_blocks - 1;
    snipped = true;
  }

  for (size_t i = 0; i < display_count; ++i) {
    AppendIndentedBlock(ss, blocks[i], i);
  }

  if (snipped) {
    const size_t omitted = blocks.size() - display_count;
    ss << std::string(display_count * 2, ' ')
       << "- ... " << omitted << " more frame"
       << ((omitted == 1) ? "" : "s") << " omitted ...";
  }

  return TrimTrailingNewlines(ss.str());
}

static std::string AppendPrimPath(const std::string &msg,
                                  const std::string &prim_path) {
  if (prim_path.empty()) {
    return msg;
  }

  size_t newline_pos = msg.find('\n');
  if (newline_pos == std::string::npos) {
    return msg + "\nPrim path: " + prim_path;
  }

  std::string result = msg.substr(0, newline_pos);
  result += "\nPrim path: " + prim_path;
  result += msg.substr(newline_pos);
  return result;
}

static std::string BuildStructuredReadErrorReport(
    const std::string &read_frame,
    const std::string &parser_error,
    const std::string &reconstruct_error,
    size_t max_blocks) {
  std::vector<std::string> blocks;
  blocks.emplace_back(read_frame);

  std::vector<std::string> parser_blocks =
      SplitStructuredErrorBlocks(parser_error);
  std::reverse(parser_blocks.begin(), parser_blocks.end());
  for (const auto &block : parser_blocks) {
    blocks.emplace_back(block);
  }

  std::vector<std::string> reconstruct_blocks =
      SplitStructuredErrorBlocks(reconstruct_error);
  for (const auto &block : reconstruct_blocks) {
    blocks.emplace_back(block);
  }

  return FormatStructuredErrorStack(DeduplicateStructuredBlocks(blocks),
                                    max_blocks);
}

// intermediate data structure for VariantSet stmt
struct VariantNode {
  PrimMeta metas;
  std::map<std::string, Property> props;
  std::vector<int64_t> primChildren;
  int64_t variantPrimIdx{-1};
  std::map<std::string, std::map<std::string, VariantNode>> variantSets;
};

struct PrimNode {
  value::Value prim; // stores typed Prim value. Xform, GeomMesh, ...
  std::string elementName;
  std::string typeName; // Prim's typeName

  int64_t parent{-1};            // -1 = root node
  //bool parent_is_variant{false}; // True when this Prim is defined under variantSet stmt.
  std::vector<size_t> children;  // index to USDAReader._prims[] of childPrims. it contains variant's primChildren also.

  std::map<std::string, std::map<std::string, VariantNode>> variantNodeMap;
};

// For USD scene read for composition(read by references, subLayers, payloads)
struct PrimSpecNode {
  PrimSpec primSpec;

  int64_t parent{-1};            // -1 = root node
  //bool parent_is_variant{false}; // True when this Prim is defined under variantSet stmt.
  std::vector<size_t> children;  // index to USDAReader._primspecs[]

  std::map<std::string, std::map<std::string, VariantNode>> variantNodeMap;
};

// TODO: Move to prim-types.hh?

template <typename T>
struct PrimTypeTraits;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

#define DEFINE_PRIM_TYPE(__dty, __name, __tyid)    \
  template <>                                      \
  struct PrimTypeTraits<__dty> {                    \
    using primt_type = __dty;                      \
    static constexpr uint32_t type_id = __tyid;    \
    static constexpr auto prim_type_name = __name; \
  }

DEFINE_PRIM_TYPE(Model, "Model", value::TYPE_ID_MODEL);

DEFINE_PRIM_TYPE(Xform, kGeomXform, value::TYPE_ID_GEOM_XFORM);
DEFINE_PRIM_TYPE(GeomMesh, kGeomMesh, value::TYPE_ID_GEOM_MESH);
DEFINE_PRIM_TYPE(GeomPoints, kGeomPoints, value::TYPE_ID_GEOM_POINTS);
DEFINE_PRIM_TYPE(GeomSphere, kGeomSphere, value::TYPE_ID_GEOM_SPHERE);
DEFINE_PRIM_TYPE(GeomCube, kGeomCube, value::TYPE_ID_GEOM_CUBE);
DEFINE_PRIM_TYPE(GeomCone, kGeomCone, value::TYPE_ID_GEOM_CONE);
DEFINE_PRIM_TYPE(GeomCapsule, kGeomCapsule, value::TYPE_ID_GEOM_CAPSULE);
DEFINE_PRIM_TYPE(GeomCylinder, kGeomCylinder, value::TYPE_ID_GEOM_CYLINDER);
DEFINE_PRIM_TYPE(GeomBasisCurves, kGeomBasisCurves,
                 value::TYPE_ID_GEOM_BASIS_CURVES);
DEFINE_PRIM_TYPE(GeomNurbsCurves, kGeomNurbsCurves,
                 value::TYPE_ID_GEOM_NURBS_CURVES);
DEFINE_PRIM_TYPE(GeomSubset, kGeomSubset, value::TYPE_ID_GEOM_GEOMSUBSET);
DEFINE_PRIM_TYPE(SphereLight, kSphereLight, value::TYPE_ID_LUX_SPHERE);
DEFINE_PRIM_TYPE(DomeLight, kDomeLight, value::TYPE_ID_LUX_DOME);
DEFINE_PRIM_TYPE(DiskLight, kDiskLight, value::TYPE_ID_LUX_DISK);
DEFINE_PRIM_TYPE(DistantLight, kDistantLight, value::TYPE_ID_LUX_DISTANT);
DEFINE_PRIM_TYPE(CylinderLight, kCylinderLight, value::TYPE_ID_LUX_CYLINDER);
DEFINE_PRIM_TYPE(RectLight, kRectLight, value::TYPE_ID_LUX_RECT);
DEFINE_PRIM_TYPE(GeometryLight, kGeometryLight, value::TYPE_ID_LUX_GEOMETRY);
DEFINE_PRIM_TYPE(PortalLight, kPortalLight, value::TYPE_ID_LUX_PORTAL);
DEFINE_PRIM_TYPE(Material, kMaterial, value::TYPE_ID_MATERIAL);
DEFINE_PRIM_TYPE(Shader, kShader, value::TYPE_ID_SHADER);
DEFINE_PRIM_TYPE(NodeGraph, kNodeGraph, value::TYPE_ID_NODEGRAPH);
DEFINE_PRIM_TYPE(SkelRoot, kSkelRoot, value::TYPE_ID_SKEL_ROOT);
DEFINE_PRIM_TYPE(Skeleton, kSkeleton, value::TYPE_ID_SKELETON);
DEFINE_PRIM_TYPE(SkelAnimation, kSkelAnimation, value::TYPE_ID_SKELANIMATION);
DEFINE_PRIM_TYPE(BlendShape, kBlendShape, value::TYPE_ID_BLENDSHAPE);
DEFINE_PRIM_TYPE(GeomCamera, kGeomCamera, value::TYPE_ID_GEOM_CAMERA);
DEFINE_PRIM_TYPE(GeomPointInstancer, kPointInstancer, value::TYPE_ID_GEOM_POINT_INSTANCER);
// UsdPhysics + mjcPhysics
DEFINE_PRIM_TYPE(PhysicsScene, kPhysicsScene, value::TYPE_ID_PHYSICS_SCENE);
DEFINE_PRIM_TYPE(PhysicsRevoluteJoint, kPhysicsRevoluteJoint, value::TYPE_ID_PHYSICS_REVOLUTE_JOINT);
DEFINE_PRIM_TYPE(PhysicsPrismaticJoint, kPhysicsPrismaticJoint, value::TYPE_ID_PHYSICS_PRISMATIC_JOINT);
DEFINE_PRIM_TYPE(PhysicsSphericalJoint, kPhysicsSphericalJoint, value::TYPE_ID_PHYSICS_SPHERICAL_JOINT);
DEFINE_PRIM_TYPE(PhysicsFixedJoint, kPhysicsFixedJoint, value::TYPE_ID_PHYSICS_FIXED_JOINT);
DEFINE_PRIM_TYPE(PhysicsDistanceJoint, kPhysicsDistanceJoint, value::TYPE_ID_PHYSICS_DISTANCE_JOINT);
DEFINE_PRIM_TYPE(PhysicsCollisionGroup, kPhysicsCollisionGroup, value::TYPE_ID_PHYSICS_COLLISION_GROUP);
DEFINE_PRIM_TYPE(MjcActuator, kMjcActuator, value::TYPE_ID_MJC_ACTUATOR);
DEFINE_PRIM_TYPE(MjcTendon, kMjcTendon, value::TYPE_ID_MJC_TENDON);
DEFINE_PRIM_TYPE(MjcKeyframe, kMjcKeyframe, value::TYPE_ID_MJC_KEYFRAME);
// AR/Interactive (Apple Preliminary_*)
DEFINE_PRIM_TYPE(Preliminary_PhysicsGravitationalForce, kPreliminary_PhysicsGravitationalForce, value::TYPE_ID_PRELIMINARY_GRAVITATIONAL_FORCE);
DEFINE_PRIM_TYPE(Preliminary_InfiniteColliderPlane, kPreliminary_InfiniteColliderPlane, value::TYPE_ID_PRELIMINARY_INFINITE_COLLIDER_PLANE);
DEFINE_PRIM_TYPE(Preliminary_ReferenceImage, kPreliminary_ReferenceImage, value::TYPE_ID_PRELIMINARY_REFERENCE_IMAGE);
DEFINE_PRIM_TYPE(Preliminary_Behavior, kPreliminary_Behavior, value::TYPE_ID_PRELIMINARY_BEHAVIOR);
DEFINE_PRIM_TYPE(Preliminary_Trigger, kPreliminary_Trigger, value::TYPE_ID_PRELIMINARY_TRIGGER);
DEFINE_PRIM_TYPE(Preliminary_Action, kPreliminary_Action, value::TYPE_ID_PRELIMINARY_ACTION);
DEFINE_PRIM_TYPE(Preliminary_Text, kPreliminary_Text, value::TYPE_ID_PRELIMINARY_TEXT);
// usdMedia
DEFINE_PRIM_TYPE(SpatialAudio, kSpatialAudio, value::TYPE_ID_SPATIAL_AUDIO);
DEFINE_PRIM_TYPE(Scope, "Scope", value::TYPE_ID_SCOPE);

DEFINE_PRIM_TYPE(GPrim, "GPrim", value::TYPE_ID_GPRIM);

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

inline bool hasConnect(const std::string &str) {
  return endsWith(str, ".connect");
}

inline bool hasInputs(const std::string &str) {
  return startsWith(str, "inputs:");
}

inline bool hasOutputs(const std::string &str) {
  return startsWith(str, "outputs:");
}

// NOTE: CheckAllowedTokens and EnumHandler templates removed.
// Use centralized handlers from enum-handlers.hh instead.

class USDAReader::Impl {
 private:
  Stage _stage;

 public:
  Impl(StreamReader *sr) { _parser.SetStream(sr); }


  void SetBaseDir(const std::string &str) { _base_dir = str; }

  void SetFilename(const std::string &str) { _filename = str; }


  void set_reader_config(const USDAReaderConfig &config) {
    _config = config;
    _parser.SetMaxMemoryLimit(config.max_memory_limit_in_mb);
  }

  const USDAReaderConfig get_reader_config() const {
    return _config;
  }

  void SetProgressCallback(std::function<bool(float progress, void *userptr)> callback, void *userptr) {
    _parser.SetProgressCallback(callback, userptr);
  }

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
    if (!_err.empty()) {
      _err += "\n";
    }
    _err += s;
  }

  void PushWarn(const std::string &s) {
    if (!_warn.empty()) {
      _warn += "\n";
    }
    _warn += s;
  }

  template <typename T>
  bool ReconstructPrim(
      const Path &full_path,
      const Specifier &spec,
      prim::PropertyMap &properties,
      const prim::ReferenceList &references,
      T *out);

  bool ProcessVariantSetContent(const uint32_t depth, const std::map<std::string, ascii::AsciiParser::VariantSetContent> &in_variants, std::map<std::string, std::map<std::string, VariantNode>> &dst) {
    if (depth > 512) {
      PUSH_ERROR_AND_RETURN("VariantSet nesting too deep (> 512).");
    }

    //
    // variantSet
    // NOTE: variantChildren setup is delayed. It will be processed in ConstructPrimSpecTreeRec
    //
    std::map<std::string, std::map<std::string, VariantNode>> variantSets;
    for (const auto &variantContext : in_variants) {
      const std::string variant_name = variantContext.first;

      DCOUT("variantName: " << variant_name);

      // Convert VariantContent -> VariantNode
      std::map<std::string, VariantNode> variantNodes;
      for (const auto &item : variantContext.second.variantSets) {

        // process child variantSet first.
        std::map<std::string, std::map<std::string, VariantNode>> childVariantSets;
        if (!ProcessVariantSetContent(depth+1, item.second.variantSets, childVariantSets))
        {
          return false;
        }

        VariantNode variant;

        DCOUT("variantPrimIdx = " << variantContext.second.variantPrimIdx);
        variant.variantPrimIdx = variantContext.second.variantPrimIdx;
        DCOUT("child variantSets.size " << childVariantSets.size());
        variant.variantSets = std::move(childVariantSets);

        if (!ReconstructPrimMeta(item.second.metas, &variant.metas)) {
          PUSH_ERROR_AND_RETURN(fmt::format("Failed to process Prim metadataum in variantSet {} item {} ", variant_name, item.first));
        }
        variant.props = item.second.props;

        // child Prim should be already reconstructed.
        for (const auto &childPrimIdx : item.second.primIndices) {
          if (childPrimIdx < 0) {
            PUSH_ERROR_AND_RETURN(fmt::format("[InternalError] Invalid primIndex found within VariantSet."));
          }

          if (size_t(childPrimIdx) >= _prim_nodes.size()) {
            PUSH_ERROR_AND_RETURN(fmt::format("[InternalError] Invalid primIndex found within VariantSet. variantChildPrimIdsx {} Exceeds _prim_nodes.size() {}", childPrimIdx, _prim_nodes.size()));
          }

          variant.primChildren.push_back(childPrimIdx);

          //_prim_nodes[size_t(childPrimIdx)].parent_is_variant = true;
        }
        DCOUT("Add variant: " << item.first);
        variantNodes[item.first] = std::move(variant);
      }

      DCOUT("Add variantSet: " << variant_name);
      variantSets[variant_name] = std::move(variantNodes);
    }

    DCOUT("variantSets.size = " << variantSets.size());
    dst = std::move(variantSets);

    return true;
  }

  template <typename T>
  bool RegisterReconstructCallback() {
    _parser.RegisterPrimConstructFunction(
        PrimTypeTraits<T>::prim_type_name,
        [&](const Path &full_path, const Specifier spec, const std::string &_primTypeName, const Path &prim_name, const int64_t primIdx,
            const int64_t parentPrimIdx,
            prim::PropertyMap &properties,
            const ascii::AsciiParser::PrimMetaMap &in_meta,
            const ascii::AsciiParser::VariantSetList &in_variants)
            -> nonstd::expected<bool, std::string> {

          std::string primTypeName = _primTypeName;
          if (primTypeName == "__AnyType__") {
            primTypeName = ""; // Make empty
          }

          if (!prim_name.is_valid()) {
            return nonstd::make_unexpected("Invalid Prim name: " +
                                           prim_name.full_path_name());
          }
          if (prim_name.is_absolute_path() || prim_name.is_root_path()) {
            return nonstd::make_unexpected(
                "Prim name should not starts with '/' or contain `/`: Prim "
                "name = " +
                prim_name.full_path_name());
          }

          if (!prim_name.prop_part().empty()) {
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

          DCOUT("primType = " << value::TypeTraits<T>::type_name()
                              << ", node.size "
                              << std::to_string(_prim_nodes.size())
                              << ", primIdx = " << primIdx
                              << ", parentPrimIdx = " << parentPrimIdx);

          DCOUT("full_path = " << full_path.full_path_name());
          DCOUT("primName = " << prim_name.full_path_name());

          prim::ReferenceList references;
          if (prim.meta.references) {
            references = prim.meta.references.value();
          }

          bool ret = ReconstructPrim<T>(full_path, spec, properties, references, &prim);

          if (!ret) {
            return nonstd::make_unexpected("Failed to reconstruct Prim: " +
                                           prim_name.full_path_name());
          }

          prim.spec = spec;
          prim.name = prim_name.prim_part();

          //
          // variants
          // NOTE: variantChildren setup is delayed. It will be processed in ConstructPrimSpecTreeRec
          //
          std::map<std::string, std::map<std::string, VariantNode>> variantSets;
          if (!ProcessVariantSetContent(0, in_variants, variantSets)) {
            return nonstd::make_unexpected(fmt::format("[InternalError] Failed to process VariantSet"));
          }

          // Add to scene graph.
          // NOTE: Scene graph is constructed from bottom up manner(Children
          // first), so add this primIdx to parent's children.
          if (size_t(primIdx) >= _prim_nodes.size()) {
            _prim_nodes.resize(size_t(primIdx) + 1);
          }
          DCOUT("sz " << std::to_string(_prim_nodes.size())
                      << ", primIdx = " << primIdx);

          _prim_nodes[size_t(primIdx)].prim = std::move(prim);
          _prim_nodes[size_t(primIdx)].typeName = primTypeName;
          _prim_nodes[size_t(primIdx)].variantNodeMap = variantSets;


          // Store actual Prim typeName also for Model Prim type.
          // TODO: Find more better way.
          {
            value::Value *p = &(_prim_nodes[size_t(primIdx)].prim);
            Model *model = p->as<Model>();
            if (model) {
              DCOUT("Set prim typeName " << primTypeName << " to Model Prim[" << primIdx << "]");
              model->prim_type_name = primTypeName;
            }
          }

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

  void RegisterPrimSpecHandler() {
    _parser.RegisterPrimSpecFunction(
         [&](const Path &full_path, const Specifier spec, const std::string &typeName, const Path &prim_name, const int64_t primIdx,
            const int64_t parentPrimIdx,
            const prim::PropertyMap &properties,
            const ascii::AsciiParser::PrimMetaMap &in_meta,
            const ascii::AsciiParser::VariantSetList &in_variants)
            -> nonstd::expected<bool, std::string> {

          if (!prim_name.is_valid()) {
            return nonstd::make_unexpected("Invalid Prim name: " +
                                           prim_name.full_path_name());
          }
          if (prim_name.is_absolute_path() || prim_name.is_root_path()) {
            return nonstd::make_unexpected(
                "Prim name should not starts with '/' or contain `/`: Prim "
                "name = " +
                prim_name.full_path_name());
          }

          if (!prim_name.prop_part().empty()) {
            return nonstd::make_unexpected(
                "Prim path should not contain property part(`.`): Prim name "
                "= " +
                prim_name.full_path_name());
          }

          if (primIdx < 0) {
            return nonstd::make_unexpected(
                "Unexpected primIdx value. primIdx must be positive.");
          }

          if (prim_name.prim_part().empty()) {
            return nonstd::make_unexpected("Prim's name should not be empty ");
          }

          PrimSpec primspec;
          primspec.name() = prim_name.prim_part();
          primspec.specifier() = spec;
          primspec.typeName() = typeName;

          DCOUT("primspec name, primType = " << prim_name.prim_part() << ", " << typeName);

          if (!ReconstructPrimMeta(in_meta, &primspec.metas())) {
            return nonstd::make_unexpected(
                "Failed to process Prim metadataum.");
          }

          primspec.props() = properties;

          //
          // variants
          // NOTE: variantChildren setup is delayed. It will be processed ConstructPrimTreeRec()
          //
          std::map<std::string, std::map<std::string, VariantNode>> variantSets;
          for (const auto &variantContext : in_variants) {
            const std::string variant_name = variantContext.first;

            // Convert VariantContent -> VariantNode
            std::map<std::string, VariantNode> variantNodes;
            for (const auto &item : variantContext.second.variantSets) {
              VariantNode variant;
              if (!ReconstructPrimMeta(item.second.metas, &variant.metas)) {
                return nonstd::make_unexpected(fmt::format("Failed to process Prim metadataum in variantSet {} item {} ", variant_name, item.first));
              }
              variant.props = item.second.props;

              // child Prim should be already reconstructed.
              for (const auto &childPrimIdx : item.second.primIndices) {
                if (childPrimIdx < 0) {
                  return nonstd::make_unexpected(fmt::format("[InternalError] Invalid primIndex found within VariantSet."));
                }

                if (size_t(childPrimIdx) >= _primspec_nodes.size()) {
                  return nonstd::make_unexpected(fmt::format("[InternalError] Invalid primIndex found within VariantSet. variantChildPrimIdsx {} Exceeds _prim_nodes.size() {}", childPrimIdx, _primspec_nodes.size()));
                }

                variant.primChildren.push_back(childPrimIdx);

                //_primspec_nodes[size_t(childPrimIdx)].parent_is_variant = true;
              }
              DCOUT("Add variant: " << item.first);
              variantNodes.emplace(item.first, std::move(variant));
            }

            DCOUT("Add variantSet: " << variant_name);
            variantSets.emplace(variant_name, std::move(variantNodes));
          }


          // Assign index for PrimSpec
          // TODO: Use sample id table(= _prim_nodes)

          if (size_t(primIdx) >= _primspec_nodes.size()) {
            _primspec_nodes.resize(size_t(primIdx) + 1);
          }
          DCOUT("sz " << std::to_string(_primspec_nodes.size())
                      << ", primIdx = " << primIdx);

          _primspec_nodes[size_t(primIdx)].primSpec = std::move(primspec);
          DCOUT("primspec[" << primIdx << "].ty = "
                        << _primspec_nodes[size_t(primIdx)].primSpec.typeName());
          _primspec_nodes[size_t(primIdx)].parent = parentPrimIdx;
          _primspec_nodes[size_t(primIdx)].variantNodeMap = variantSets;

          if (parentPrimIdx == -1) {
            _toplevel_primspecs.push_back(size_t(primIdx));
          } else {
            _primspec_nodes[size_t(parentPrimIdx)].children.push_back(
                size_t(primIdx));
            return true;
          }

          return true;
      }
    );

  }

  void StageMetaProcessor() {
    _parser.RegisterStageMetaProcessFunction(
        [&](const ascii::AsciiParser::StageMetas &metas) {
          DCOUT("StageMeta CB:");

          _stage.metas().doc = metas.doc;
          if (metas.upAxis) {
            _stage.metas().upAxis = metas.upAxis.value();
          }

          _stage.metas().comment = metas.comment;

          if (metas.subLayers.size()) {
            // TODO subLayer offset.
            std::vector<SubLayer> sublayers;
            for (size_t i = 0; i < metas.subLayers.size(); i++) {
              SubLayer sublayer;
              sublayer.assetPath = metas.subLayers[i];
              sublayers.push_back(sublayer);
            }
            _stage.metas().subLayers = sublayers;
          }

          _stage.metas().defaultPrim = metas.defaultPrim;
          if (metas.metersPerUnit) {
            _stage.metas().metersPerUnit = metas.metersPerUnit.value();
          }

          if (metas.kilogramsPerUnit) {
            _stage.metas().kilogramsPerUnit = metas.kilogramsPerUnit.value();
          }

          if (metas.timeCodesPerSecond) {
            _stage.metas().timeCodesPerSecond =
                metas.timeCodesPerSecond.value();
          }

          if (metas.startTimeCode) {
            _stage.metas().startTimeCode = metas.startTimeCode.value();
          }

          if (metas.endTimeCode) {
            _stage.metas().endTimeCode = metas.endTimeCode.value();
          }

          if (metas.framesPerSecond) {
            _stage.metas().framesPerSecond = metas.framesPerSecond.value();
          }

          if (metas.autoPlay) {
            _stage.metas().autoPlay = metas.autoPlay.value();
          }

          if (metas.playbackMode) {
            value::token tok = metas.playbackMode.value();
            if (tok.str() == "none") {
              _stage.metas().playbackMode = StageMetas::PlaybackMode::PlaybackModeNone;
            } else if (tok.str() == "loop") {
              _stage.metas().playbackMode = StageMetas::PlaybackMode::PlaybackModeLoop;
            } else {
              PUSH_ERROR_AND_RETURN("Unsupported playbackMode: " + tok.str());
            }
          }

          _stage.metas().customLayerData = metas.customLayerData;
          _stage.metas().customLayerDataAuthored = metas.customLayerDataAuthored;

          // AOUSD Core Spec layer metadata
          if (metas.colorConfiguration) {
            _stage.metas().colorConfiguration = metas.colorConfiguration.value();
          }
          if (metas.colorManagementSystem) {
            _stage.metas().colorManagementSystem = metas.colorManagementSystem.value();
          }
          if (metas.owner) {
            _stage.metas().owner = metas.owner.value();
          }
          if (metas.hasOwnedSubLayers) {
            _stage.metas().hasOwnedSubLayers = metas.hasOwnedSubLayers.value();
          }
          if (metas.expressionVariables) {
            _stage.metas().expressionVariables = metas.expressionVariables.value();
          }

          // AOUSD Core Spec 10.3.2.6: relocates
          if (!metas.relocates.empty()) {
            _stage.metas().layerRelocates = metas.relocates;
          }

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

  bool ReconstructPrimMeta(const ascii::AsciiParser::PrimMetaMap &in_meta,
                           PrimMeta *out) {

    // Use centralized handler from enum-handlers.hh
    auto ApiSchemaHandler = enum_handler::APISchemaName;

    auto BuildVariants = [](const Dictionary &dict) -> nonstd::expected<VariantSelectionMap, std::string> {

      // Allow empty dict.

      VariantSelectionMap m;

      for (const auto &item : dict) {
        // TODO: duplicated key check?
        if (auto pv = item.second.get_value<std::string>()) {
          m[item.first] = pv.value();
        } else if (auto pvs = item.second.get_value<value::StringData>()) {
          // TODO: store triple-quote info
          m[item.first] = pvs.value().value;
        } else {
          return nonstd::make_unexpected(fmt::format("TinyUSDZ only accepts `string` value for `variants` element, but got type `{}`(type_id {}).", item.second.type_name(), item.second.type_id()));
        }
      }

      return std::move(m);

    };

    DCOUT("ReconstructPrimMeta");
    for (const auto &meta : in_meta) {
      DCOUT("meta.name = " << meta.first);

      const auto &listEditQual = std::get<0>(meta.second);
      const MetaVariable &var = std::get<1>(meta.second);

      if (meta.first == "active") {
        DCOUT("active. type = " << var.type_name());
        if (var.type_name() == "bool") {
          if (auto pv = var.get_value<bool>()) {
            out->set_active(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `active` metadataum is not type `bool`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `active` metadataum is not type `bool`. got `"
              << var.type_name() << "`.");
        }
      } else if (meta.first == "hidden") {
        DCOUT("hidden. type = " << var.type_name());
        if (var.type_name() == "bool") {
          if (auto pv = var.get_value<bool>()) {
            out->set_hidden(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `hidden` metadataum is not type `bool`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `hidden` metadataum is not type `bool`. got `"
              << var.type_name() << "`.");
        }

      } else if (meta.first == "instanceable") {
        DCOUT("instanceable. type = " << var.type_name());
        if (var.type_name() == "bool") {
          if (auto pv = var.get_value<bool>()) {
            out->set_instanceable(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `instanceable` metadataum is not type `bool`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `instanceable` metadataum is not type `bool`. got `"
              << var.type_name() << "`.");
        }

      } else if (meta.first == "sceneName") {
        DCOUT("sceneName. type = " << var.type_name());
        if (var.type_name() == value::kString) {
          if (auto pv = var.get_value<std::string>()) {
            out->set_sceneName(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `sceneName` metadataum is not type `string`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `sceneName` metadataum is not type `string`. got `"
              << var.type_name() << "`.");
        }
      } else if (meta.first == "displayName") {
        DCOUT("displayName. type = " << var.type_name());
        if (var.type_name() == value::kString) {
          if (auto pv = var.get_value<std::string>()) {
            out->set_displayName(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `displayName` metadataum is not type `string`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `displayName` metadataum is not type `string`. got `"
              << var.type_name() << "`.");
        }
      } else if (meta.first == "kind") {
        // std::tuple<ListEditQual, MetaVariable>
        // TODO: list-edit qual
        DCOUT("kind. type = " << var.type_name());
        if (var.type_name() == "token") {
          if (auto pv = var.get_value<value::token>()) {
            const value::token tok = pv.value();
            if (tok.str() == "subcomponent") {
              out->set_kind(Kind::Subcomponent);
            } else if (tok.str() == "component") {
              out->set_kind(Kind::Component);
            } else if (tok.str() == "model") {
              out->set_kind(Kind::Model);
            } else if (tok.str() == "group") {
              out->set_kind(Kind::Group);
            } else if (tok.str() == "assembly") {
              out->set_kind(Kind::Assembly);
            } else if (tok.str() == "sceneLibrary") {
              // USDZ specific: https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/scenelibrary
              out->set_kind(Kind::SceneLibrary);
            } else {
              // NOTE: empty token allowed.
              // For user-defined kind, store the string directly
              out->set_kind(tok.str());
            }
            DCOUT("Added kind: " << out->get_kind_str());
          } else {
            PUSH_ERROR_AND_RETURN(
                "(Internal error?) `kind` metadataum is not type `token`.");
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `kind` metadataum is not type `token`. got `"
              << var.type_name() << "`.");
        }
      } else if (meta.first == "sdrMetadata") {
        DCOUT("sdrMetadata. type = " << var.type_name());
        if (var.type_id() == value::TypeTraits<Dictionary>::type_id()) {
          if (auto pv = var.get_value<Dictionary>()) {
            // TODO: Check if all items are string type.
            out->set_sdrMetadata(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN_TAG(kTag,
                "(Internal error?) `sdrMetadata` metadataum is not type "
                "`dictionary`. got type `"
                << var.type_name() << "`");
          }

        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `sdrMetadata` metadataum is not type "
              "`dictionary`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "customData") {
        DCOUT("customData. type = " << var.type_name());
        if (var.type_id() == value::TypeTraits<Dictionary>::type_id()) {
          if (auto pv = var.get_value<Dictionary>()) {
            out->set_customData(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN_TAG(kTag,
                "(Internal error?) `customData` metadataum is not type "
                "`dictionary`. got type `"
                << var.type_name() << "`");
          }

        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `customData` metadataum is not type "
              "`dictionary`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "clips") {
        DCOUT("clips. type = " << var.type_name());
        if (var.type_id() == value::TypeTraits<Dictionary>::type_id()) {
          if (auto pv = var.get_value<Dictionary>()) {
            out->set_clips(pv.value());
          } else {
            PUSH_ERROR_AND_RETURN_TAG(kTag,
                "(Internal error?) `clips` metadataum is not type "
                "`dictionary`. got type `"
                << var.type_name() << "`");
          }

        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `clips` metadataum is not type "
              "`dictionary`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "assetInfo") {
        DCOUT("assetInfo. type = " << var.type_name());
        if (auto pv = var.get_value<Dictionary>()) {
          out->set_assetInfo(pv.value());
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
              "(Internal error?) `assetInfo` metadataum is not type "
              "`dictionary`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "variants") {
        if (auto pv = var.get_value<Dictionary>()) {
          auto pm = BuildVariants(pv.value());
          if (!pm) {
            PUSH_ERROR_AND_RETURN(pm.error());
          }
          out->variants = (*pm);
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `variants` metadataum is not type "
              "`dictionary`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "inherits") {
        // Initialize vector if not present
        if (!out->inherits) {
          out->inherits = std::vector<std::pair<ListEditQual, std::vector<Path>>>();
        }
        if (auto pvb = var.get_value<value::ValueBlock>()) {
          if (listEditQual != ListEditQual::ResetToExplicit) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->inherits->push_back(std::make_pair(listEditQual, std::vector<Path>()));
        } else if (auto pv = var.get_value<std::vector<Path>>()) {
          if (pv.value().empty() && (listEditQual != ListEditQual::ResetToExplicit)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->inherits->push_back(std::make_pair(listEditQual, pv.value()));
        } else if (auto pvp = var.get_value<Path>()) {
          std::vector<Path> vs;
          vs.push_back(pvp.value());
          out->inherits->push_back(std::make_pair(listEditQual, vs));
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `inherits` metadataum should be either `path` or `path[]`. "
              "got type `"
              << var.type_name() << "`");
        }

      } else if (meta.first == "specializes") {
        // Initialize vector if not present
        if (!out->specializes) {
          out->specializes = std::vector<std::pair<ListEditQual, std::vector<Path>>>();
        }
        if (auto pvb = var.get_value<value::ValueBlock>()) {
          if (listEditQual != ListEditQual::ResetToExplicit) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->specializes->push_back(std::make_pair(listEditQual, std::vector<Path>()));
        } else if (auto pv = var.get_value<std::vector<Path>>()) {
          if (pv.value().empty() && (listEditQual != ListEditQual::ResetToExplicit)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->specializes->push_back(std::make_pair(listEditQual, pv.value()));
        } else if (auto pvp = var.get_value<Path>()) {
          std::vector<Path> vs;
          vs.push_back(pvp.value());
          out->specializes->push_back(std::make_pair(listEditQual, vs));
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `specializes` metadataum should be either `path` or `path[]`. "
              "got type `"
              << var.type_name() << "`");
        }

      } else if (meta.first == "variantSets") {
        // Initialize vector if not present
        if (!out->variantSets) {
          out->variantSets = std::vector<std::pair<ListEditQual, std::vector<std::string>>>();
        }
        // treat as `string`
        if (auto pvb = var.get_value<value::ValueBlock>()) {
          if (listEditQual != ListEditQual::ResetToExplicit) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->variantSets->push_back(std::make_pair(listEditQual, std::vector<std::string>()));
        } else if (auto pv = var.get_value<value::StringData>()) {
          std::vector<std::string> vs;
          vs.push_back(pv.value().value);
          out->variantSets->push_back(std::make_pair(listEditQual, vs));
        } else if (auto pvs = var.get_value<std::string>()) {
          std::vector<std::string> vs;
          vs.push_back(pvs.value());
          out->variantSets->push_back(std::make_pair(listEditQual, vs));
        } else if (auto pva = var.get_value<std::vector<std::string>>()) {
          if (pva.value().empty() && (listEditQual != ListEditQual::ResetToExplicit)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->variantSets->push_back(std::make_pair(listEditQual, pva.value()));
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `variantSets` metadataum is not type "
              "`string` or `string[]`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "apiSchemas") {
        DCOUT("apiSchemas. type = " << var.type_name());
        if (var.type_name() == "token[]") {
          APISchemas apiSchemas;
          if ((listEditQual != ListEditQual::Prepend) && (listEditQual != ListEditQual::ResetToExplicit)) {
            PUSH_ERROR_AND_RETURN("(PrimMeta) " << "ListEdit op for `apiSchemas` must be empty or `prepend` in TinyUSDZ, but got `" << to_string(listEditQual) << "`");
          }
          apiSchemas.listOpQual = listEditQual;

          if (auto pv = var.get_value<std::vector<value::token>>()) {

            for (const auto &item : pv.value()) {
              // TODO: Multi-apply schema(instance name)
              auto ret = ApiSchemaHandler(item.str());
              if (ret) {
                apiSchemas.names.push_back({ret.value(), /* instanceName */""});
              } else if (_config.allow_unknown_apiSchema) {
                // Store unknown schema instead of just warning
                std::string instanceName = "";  // TODO: parse instance name if present
                apiSchemas.unknownSchemas.push_back({item.str(), instanceName});
                PUSH_WARN("(PrimMeta) Preserving unknown API schema: " << item.str());
              } else {
                PUSH_ERROR_AND_RETURN("Unknown or invalid apiSchema: " + ret.error());
              }
            }
          } else {
            PUSH_ERROR_AND_RETURN_TAG(kTag, "(Internal error?) `apiSchemas` metadataum is not type "
            "`token[]`. got type `"
            << var.type_name() << "`");
          }

          out->set_apiSchemas(std::move(apiSchemas));
        } else {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "(Internal error?) `apiSchemas` metadataum is not type "
          "`token[]`. got type `"
          << var.type_name() << "`");
        }
      } else if (meta.first == "references") {
        // Initialize vector if not present
        if (!out->references) {
          out->references = std::vector<std::pair<ListEditQual, std::vector<Reference>>>();
        }
        if (var.is_blocked()) {
          // Treat as empty list
          // empty list must be qualified as 'explicit'
          if (listEditQual != ListEditQual::ResetToExplicit) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          std::vector<Reference> refs;
          out->references->push_back(std::make_pair(listEditQual, refs));
        } else if (auto pv = var.get_value<Reference>()) {
          // To Reference
          std::vector<Reference> refs;
          refs.emplace_back(pv.value());
          out->references->push_back(std::make_pair(listEditQual, refs));
        } else if (auto pva = var.get_value<std::vector<Reference>>()) {
          if (pva.value().empty() && (listEditQual != ListEditQual::ResetToExplicit)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->references->push_back(std::make_pair(listEditQual, pva.value()));
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `references` metadataum is not type "
              "`Reference`. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "payload") {
        // Initialize vector if not present
        if (!out->payload) {
          out->payload = std::vector<std::pair<ListEditQual, std::vector<Payload>>>();
        }
        if (var.is_blocked()) {
          if (listEditQual != ListEditQual::ResetToExplicit) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          // make empty
          std::vector<Payload> refs;
          out->payload->push_back(std::make_pair(listEditQual, refs));
        } else if (auto pv = var.get_value<Payload>()) {
          // To Payload
          std::vector<Payload> pls;
          pls.emplace_back(pv.value());
          out->payload->push_back(std::make_pair(listEditQual, pls));
        } else if (auto pva = var.get_value<std::vector<Payload>>()) {
          if (pva.value().empty() && (listEditQual != ListEditQual::ResetToExplicit)) {
            PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("None or Empty list must be `explicit`(no qualifier), but has qualifier `{}`", to_string(listEditQual)));
          }
          out->payload->push_back(std::make_pair(listEditQual, pva.value()));
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error) `payload` metadataum is not type "
              "Payload. got type `"
              << var.type_name() << "`");
        }
      } else if (meta.first == "doc") {
        if (auto pv = var.get_value<value::StringData>()) {
          out->set_doc(pv.value());
        } else if (auto spv = var.get_value<std::string>()) {
          out->set_doc(spv.value());
        } else {
          PUSH_ERROR_AND_RETURN(
              "(Internal error?) `doc` metadataum is not type `string`. got `"
              << var.type_name() << "`.");
        }
      } else if (meta.first == "comment") {
        if (auto pv = var.get_value<value::StringData>()) {
          // Preserve full StringData including has_comment_prefix flag
          out->set_comment(pv.value());
        } else if (auto spv = var.get_value<std::string>()) {
          value::StringData sdata;
          sdata.value = spv.value();
          out->set_comment(sdata);
        }
      } else {
        // Store unregistered metadata as raw string (OpenUSD-compatible).
        // The value is stored verbatim and written back unquoted to USDA.
        if (auto spv = var.get_value<std::string>()) {
          out->unregisteredMetas[meta.first] = spv.value();
        } else {
          // Convert non-string values to their string representation
          out->unregisteredMetas[meta.first] = value::pprint_value(var.get_raw_value());
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
  bool Read(const uint32_t state_flags, bool as_primspec);

  // std::vector<GPrim> GetGPrims() { return _gprims; }

  std::string GetDefaultPrimName() const { return _defaultPrim; }

  std::string GetError() { return _err; }

  std::string GetWarning() { return _warn; }

  ///
  /// Valid after `Read`.
  ///
  bool GetAsLayer(Layer *layer);

  ///
  /// Valid after `Read`.
  ///
  bool ReconstructStage();

  ///
  /// Valid after `ReconstructStage`.
  ///
  const Stage &GetStage() const { return _stage; }

 private:
  //bool stage_reconstructed_{false};


  ///
  /// -- Members --
  ///

  // TODO: Remove
  // std::set<std::string> _node_types;

  std::stack<ParseState> parse_stack;

  std::string _base_dir;  // Used for importing another USD file
  std::string _filename;  // Used for displaying error context from source file
  //AssetResolutionResolver _arr;


  // "class" defs
  //std::map<std::string, Klass> _klasses;

  std::stack<std::string> _path_stack;

  std::string _err;
  std::string _warn;

  // Cache of loaded `references`
  // <filename, {defaultPrim index, Layer(PrimSpec data of usd file)}>
  std::map<std::string, std::pair<uint32_t, Layer>>
      _reference_cache;

  // toplevel prims
  std::vector<size_t> _toplevel_prims;  // index to _prim_nodes

  // 1D Linearized array of prim nodes.
  std::vector<PrimNode> _prim_nodes;

  // Path(prim part only) -> index to _prim_nodes[]
  std::map<std::string, size_t> _primpath_to_prim_idx_map;


  // toplevel primspecs
  std::vector<size_t> _toplevel_primspecs;  // index to _prim_nodes

  // Flattened array of primspec nodes.
  std::vector<PrimSpecNode> _primspec_nodes;
  // Path(prim part only) -> index to _primspec_nodes[]
  std::map<std::string, size_t> _primpath_to_primspec_idx_map;
  bool _primspec_invalidated{false};

  std::string _defaultPrim;

  // Used for Ascii parser option
  USDAReaderConfig _config;

  ascii::AsciiParser _parser;

};  // namespace usda

namespace {

// bottom up conversion.
bool ToPrimSpecRec(const size_t primSpecIdx,
                        std::vector<PrimSpecNode> &primspec_nodes, PrimSpec &parent, std::string *err) {

  if (primSpecIdx >= primspec_nodes.size()) {
    if (err) {
      (*err) += "Internal error; primSpecIdx exceeds primspec_nodes.size.";
    }
    return false;
  }

  const PrimSpecNode &node = primspec_nodes[primSpecIdx];

  PrimSpec primspec = node.primSpec;

  // Firstly process variants.
  std::set<int64_t> variantChildrenIndices; // record variantChildren indices
  {

    std::map<std::string, VariantSetSpec> variantSets;
    for (const auto &variantNodes : node.variantNodeMap) {
      DCOUT("variantSet " << variantNodes.first);
      VariantSetSpec variantSet;
      for (const auto &item : variantNodes.second) {
        DCOUT("variant " << item.first);
        PrimSpec variant; // variantNode can be represented as PrimSpec.
        for (const int64_t vidx : item.second.primChildren) {
          if (variantChildrenIndices.count(vidx)) {
            // Duplicated variant childrenIndices
            if (err) {
              (*err) = fmt::format("variant primIdx {} is referenced multiple times.\n", vidx);
            }
            return false;
          } else {
            // Add prim to variants
            if ((vidx >= 0) && (size_t(vidx) <= primspec_nodes.size())) {

              PrimSpec variantChildPrim; // dummy
              if (!ToPrimSpecRec(size_t(vidx), primspec_nodes, variantChildPrim, err)) {
                return false;
              }

              DCOUT(fmt::format("Added prim {} to variantSet {} : variant {}", variantChildPrim.name(), variantNodes.first, item.first));
              variant.children().emplace_back(variantChildPrim);
            } else {
              if (err) {
                (*err) = "primIndex exceeds prim_nodes.size()\n";
              }
              return false;
            }

            variantChildrenIndices.insert(vidx);
          }
        }

        variant.metas() = std::move(item.second.metas);
        variant.props() = std::move(item.second.props);

        variantSet.name = variantNodes.first;
        variantSet.variantSet.emplace(item.first, std::move(variant));
      }
      DCOUT(fmt::format("Add {} to variantSet", variantNodes.first));
      variantSets.emplace(variantNodes.first, std::move(variantSet));
    }
    primspec.variantSets() = std::move(variantSets);
  }

  for (const auto &cidx : node.children) {

    if (variantChildrenIndices.count(int64_t(cidx))) {
      // PrimSpec is already processed
      continue;
    }

    PrimSpec childPrimSpec;
    if (!ToPrimSpecRec(cidx, primspec_nodes, childPrimSpec, err)) {
      return false;
    }
    primspec.children().emplace_back(std::move(childPrimSpec));
  }

  parent = std::move(primspec);

  return true;
}

}  // namespace

bool USDAReader::Impl::GetAsLayer(Layer *layer) {

  if (!layer) {
    PUSH_ERROR_AND_RETURN("layer arg is nullptr.");
  }

  if (_primspec_invalidated) {
    PUSH_ERROR_AND_RETURN("PrimSpec data is invalid. USD data is not loaded or there was an error in earlier GetAsLayer call, or GetAsLayer was invoked multiple times.");
  }

  layer->clear_primspecs();
  DCOUT("# of subLayers = " << _stage.metas().subLayers.size());
  layer->metas() = _stage.metas();

  for (const auto &idx : _toplevel_primspecs) {
    DCOUT("Toplevel primspec idx: " << std::to_string(idx));

    if (idx >= _primspec_nodes.size()) {
      PUSH_ERROR_AND_RETURN("[Internal Error] out-of-bounds access.");
    }

    auto &node = _primspec_nodes[idx];
    PrimSpec &primSpec = node.primSpec;

    DCOUT("primspec[" << idx << "].typeName = " << primSpec.typeName());
    DCOUT("primspec[" << idx << "].name = " << primSpec.name());
    DCOUT("root prim[" << idx << "].num_children = " << primSpec.children().size());

    if (!ToPrimSpecRec(idx, _primspec_nodes, /* inout */primSpec, &_err)) {
      _primspec_invalidated = true;
      PUSH_ERROR_AND_RETURN("Construct PrimSpec tree failed.");
    }

    if (!layer->emplace_primspec(primSpec.name(), std::move(_primspec_nodes[idx].primSpec))) {
      PUSH_ERROR_AND_RETURN(fmt::format("Construct PrimSpec tree failed: PrimSpec.name = {}", primSpec.name()));
    }
  }

  // NOTE: _toplevel_primspecs are destroyed(std::move'ed)
  _primspec_invalidated = true;

  return true;
}

///
/// -- Impl reconstruct
//

namespace {

//
// TODO: Refeactor ConstructPrimTreeRec and ConstructVariantPrimTreeRec
//
bool ConstructPrimTreeRec(const size_t primIdx,
                        const std::vector<PrimNode> &prim_nodes,
                        const bool parent_is_variant,
                        Prim *destPrim,
                        std::string *err);

//
// Construct VariantPrim from with botom-up approach
//
bool ConstructVariantPrimTreeRec(const size_t variantPrimIdx,
                        const std::vector<PrimNode> &prim_nodes,
                        const std::string &variantName,
                        const std::map<std::string, VariantNode> &variantNodeMap,
                        std::map<std::string, VariantSet> &destVariantSets, /* inout */
                        std::string *err) {

  if (variantPrimIdx >= prim_nodes.size()) {
    if (err) {
      (*err) = "primIndex exceeds prim_nodes.size()\n";
    }
    return false;
  }

  const auto &node = prim_nodes[variantPrimIdx];

  std::set<int64_t> variantChildrenIndices; // record variantChildren indices

  std::map<std::string, VariantSet> variantSets;
  VariantSet variantSet;
  for (const auto &item : variantNodeMap) {

      DCOUT("variant " << item.first);
      Variant variant;

      // Firstly process nested variants.
      for (const auto &childVariantNode: item.second.variantSets) {
        DCOUT("variantSet child " << childVariantNode.first);
        DCOUT("  variantPrimIdx " << variantPrimIdx);

        const std::string childVariantName = childVariantNode.first;
        Prim variantChildPrim(value::Value(nullptr)); // dummy
        if (!ConstructVariantPrimTreeRec(size_t(variantPrimIdx), prim_nodes, childVariantName, childVariantNode.second, variant.variantSets(), err)) {
          return false;
        }

      }

      for (const int64_t vidx : item.second.primChildren) {
        if (variantChildrenIndices.count(vidx)) {
          // Duplicated variant childrenIndices
          if (err) {
            (*err) = fmt::format("variant primIdx {} is referenced multiple times.\n", vidx);
          }
          return false;
        } else {
          // Add prim to variants
          if ((vidx >= 0) && (size_t(vidx) <= prim_nodes.size())) {

            Prim variantChildPrim(value::Value(nullptr)); // dummy
            if (!ConstructPrimTreeRec(size_t(vidx), prim_nodes, /* parent_is_variant */true, &variantChildPrim, err)) {
              return false;
            }

            variant.primChildren().emplace_back(variantChildPrim);
          } else {
            if (err) {
              (*err) = "primIndex exceeds prim_nodes.size()\n";
            }
            return false;
          }

          variantChildrenIndices.insert(vidx);
        }
      }
      variant.metas() = std::move(item.second.metas);
      variant.properties() = std::move(item.second.props);

      variantSet.name = item.first;
      variantSet.variantSet[item.first] = std::move(variant);
    }

  destVariantSets[variantName] = std::move(variantSet);

  for (const auto &cidx : node.children) {
    DCOUT("parent: " << variantPrimIdx << ", child: " << cidx);
    if (variantChildrenIndices.count(int64_t(cidx))) {
      DCOUT("primIdx " << cidx << " processed");
      // Prim is processed
      continue;
    }

    Prim childPrim(value::Value(nullptr)); // dummy
    if (!ConstructPrimTreeRec(cidx, prim_nodes, /*parent_is_variant*/true, &childPrim, err)) {
      return false;
    }

    //DCOUT("Add childPrim " << childPrim.element_name() << " to Prim " << prim.element_name());
    //prim.children().emplace_back(std::move(childPrim));
  }

  //prim.variantSets() = std::move(variantSets);
  //(*destPrim) = std::move(prim);

  return true;
}

//
// Construct Prim from PrimNode with botom-up approach
//
bool ConstructPrimTreeRec(const size_t primIdx,
                        const std::vector<PrimNode> &prim_nodes,
                        const bool parent_is_variant,
                        Prim *destPrim,
                        std::string *err) {

  if (!destPrim) {
    if (err) {
      (*err) = "`destPrim` is nullptr.\n";
    }
    return false;
  }

  if (primIdx >= prim_nodes.size()) {
    if (err) {
      (*err) = "primIndex exceeds prim_nodes.size()\n";
    }
    return false;
  }

  const auto &node = prim_nodes[primIdx];

  Prim prim(node.prim);
  prim.prim_type_name() = node.typeName;

  DCOUT("prim[" << primIdx << "].name = " << prim.element_name());
  DCOUT("prim[" << primIdx << "].type = " << node.prim.type_name());
  DCOUT("prim[" << primIdx << "].variantNodeMap.size = " << node.variantNodeMap.size());
  //prim.prim_id() = int64_t(idx);

  // Firstly process variants.
  std::set<int64_t> variantChildrenIndices; // record variantChildren indices

  std::map<std::string, VariantSet> variantSets;
  for (const auto &variantNodes : node.variantNodeMap) {
    DCOUT("variantSet " << variantNodes.first);
    VariantSet variantSet;
    for (const auto &item : variantNodes.second) {
      DCOUT("variant " << item.first);
      Variant variant;

      int64_t variantPrimIdx = item.second.variantPrimIdx;
      if (item.second.variantSets.size() && (variantPrimIdx < 0)) {
        if (err) {
          (*err) = "variantPrimIdx is not set.\n";
        }
        return false;
      }

      DCOUT("# of child variantSet " << item.second.variantSets.size());
      for (const auto &childVariantNode: item.second.variantSets) {
        DCOUT("variantSet node " << childVariantNode.first);
        DCOUT("  variantPrimIdx " << variantPrimIdx);

        Prim variantChildPrim(value::Value(nullptr)); // dummy
        if (!ConstructVariantPrimTreeRec(size_t(variantPrimIdx), prim_nodes, childVariantNode.first, childVariantNode.second, variant.variantSets(), err)) {
          return false;
        }

      }

      for (const int64_t vidx : item.second.primChildren) {
        if (variantChildrenIndices.count(vidx)) {
          // Duplicated variant childrenIndices
          if (err) {
            (*err) = fmt::format("variant primIdx {} is referenced multiple times.\n", vidx);
          }
          return false;
        } else {
          // Add prim to variants
          if ((vidx >= 0) && (size_t(vidx) <= prim_nodes.size())) {

            Prim variantChildPrim(value::Value(nullptr)); // dummy
            if (!ConstructPrimTreeRec(size_t(vidx), prim_nodes, /* parent_is_variant */true, &variantChildPrim, err)) {
              return false;
            }

            DCOUT(fmt::format("Added prim {} to variantSet {} : variant {}", variantChildPrim.element_name(), variantNodes.first, item.first));
            variant.primChildren().emplace_back(variantChildPrim);
          } else {
            if (err) {
              (*err) = "primIndex exceeds prim_nodes.size()\n";
            }
            return false;
          }

          variantChildrenIndices.insert(vidx);
        }
      }
      variant.metas() = std::move(item.second.metas);
      variant.properties() = std::move(item.second.props);

      variantSet.name = variantNodes.first;
      variantSet.variantSet[item.first] = std::move(variant);
    }
    variantSets[variantNodes.first] = std::move(variantSet);
  }

  for (const auto &cidx : node.children) {
    DCOUT("parent: " << primIdx << ", child: " << cidx);
    if (variantChildrenIndices.count(int64_t(cidx))) {
      DCOUT("primIdx " << cidx << " processed");
      // Prim is processed
      continue;
    }

    Prim childPrim(value::Value(nullptr)); // dummy
    // inherit `parent_is_variant`
    if (!ConstructPrimTreeRec(cidx, prim_nodes, parent_is_variant, &childPrim, err)) {
      return false;
    }

    DCOUT("Add childPrim " << childPrim.element_name() << " to Prim " << prim.element_name());
    prim.children().emplace_back(std::move(childPrim));
  }

  prim.variantSets() = std::move(variantSets);
  (*destPrim) = std::move(prim);

  return true;
}

}  // namespace



bool USDAReader::Impl::ReconstructStage() {
  _stage.root_prims().clear();

  for (const auto &idx : _toplevel_prims) {
    DCOUT("Toplevel prim idx: " << std::to_string(idx));

    Prim prim(value::Value(nullptr)); // init with dummy Prim
    if (!ConstructPrimTreeRec(idx, _prim_nodes, /* parent_is_variant */false, &prim, &_err)) {
      return false;
    }

    _stage.root_prims().emplace_back(std::move(prim));

    DCOUT("num_children = " << _stage.root_prims()[size_t(_stage.root_prims().size() - 1)].children().size());
  }

  // Compute Abs Path from built Prim tree and Assign prim id.
  _stage.compute_absolute_prim_path_and_assign_prim_id();

  return true;
}

template <>
bool USDAReader::Impl::ReconstructPrim(
    const Path &full_path,
    const Specifier &spec,
    prim::PropertyMap &properties,
    const prim::ReferenceList &references,
    Xform *xform) {

  prim::PrimReconstructOptions options;
  const int source_column_width = _config.error_detail ? (1024 * 1024) : 40;
  options.format_property_source_diagnostic =
      [&](const std::string &property_name) {
        return _parser.FormatPrimAttrSourceDiagnostic(
            full_path.full_path_name(), property_name, source_column_width);
      };
  options.format_property_path =
      [&](const std::string &property_name) {
        return full_path.full_path_name() + "." + property_name;
      };
  options.format_prim_source_diagnostic = [&]() {
    return _parser.FormatPrimSourceDiagnostic(full_path.full_path_name(),
                                             source_column_width);
  };
  options.format_prim_path = [&]() {
    return full_path.full_path_name();
  };

  std::string err;
  if (!prim::ReconstructPrim(spec, properties, references, xform, &_warn, &err,
                             options)) {
    PUSH_ERROR_AND_RETURN(
        AppendPrimPath("Failed to reconstruct `Xform` prim:\n" + err,
                       full_path.full_path_name()));
  }
  return true;
}


// Generic Prim handler. T = Xform, GeomMesh, ...
template <typename T>
bool USDAReader::Impl::ReconstructPrim(
    const Path &full_path,
    const Specifier &spec,
    prim::PropertyMap &properties,
    const prim::ReferenceList &references,
    T *prim) {

  prim::PrimReconstructOptions options;
  const int source_column_width = _config.error_detail ? (1024 * 1024) : 40;
  options.strict_allowedToken_check = _config.strict_allowedToken_check;
  // MaterialX validation options
  options.validate_mtlx_connection_types = _config.validate_mtlx_connection_types || _config.strict_mtlx_check;
  options.validate_mtlx_info_id = _config.validate_mtlx_info_id || _config.strict_mtlx_check;
  options.validate_mtlx_connection_targets = _config.validate_mtlx_connection_targets || _config.strict_mtlx_check;
  options.validate_mtlx_duplicate_names = _config.validate_mtlx_duplicate_names || _config.strict_mtlx_check;
  options.validate_mtlx_index_bounds = _config.validate_mtlx_index_bounds || _config.strict_mtlx_check;
  options.strict_mtlx_check = _config.strict_mtlx_check;
  options.format_property_source_diagnostic =
      [&](const std::string &property_name) {
        return _parser.FormatPrimAttrSourceDiagnostic(
            full_path.full_path_name(), property_name, source_column_width);
      };
  options.format_property_path =
      [&](const std::string &property_name) {
        return full_path.full_path_name() + "." + property_name;
      };
  options.format_prim_source_diagnostic = [&]() {
    return _parser.FormatPrimSourceDiagnostic(full_path.full_path_name(),
                                             source_column_width);
  };
  options.format_prim_path = [&]() {
    return full_path.full_path_name();
  };
  DCOUT("strict_allowedToken_check " << options.strict_allowedToken_check);

  std::string err;
  if (!prim::ReconstructPrim(spec, properties, references, prim, &_warn, &err, options)) {
    PUSH_ERROR_AND_RETURN(
        AppendPrimPath(
            fmt::format("Failed to reconstruct `{}` prim:\n{}",
                        value::TypeTraits<T>::type_name(), err),
            full_path.full_path_name()));
  }
  return true;
}

///
/// -- Impl callback specializations
///

///
/// -- Impl Read
///

bool USDAReader::Impl::Read(const uint32_t state_flags, bool as_primspec) {
  TINYUSDZ_PROFILE_FUNCTION("usda-reader");

  ///
  /// Convert parser option.
  ///
  ascii::AsciiParserOption ascii_parser_option;
  ascii_parser_option.allow_unknown_prim = _config.allow_unknown_prims;
  ascii_parser_option.allow_unknown_apiSchema = _config.allow_unknown_apiSchema;
  ascii_parser_option.strict_allowedToken_check = _config.strict_allowedToken_check;

  ///
  /// Setup callbacks.
  ///
  StageMetaProcessor();

  RegisterPrimIdxAssignCallback();

  // For composition(as_primspec == true)
  RegisterPrimSpecHandler();

  // For direct Prim reconstruction(load state = Toplevel)
  RegisterReconstructCallback<Model>();  // Generic prim.

  RegisterReconstructCallback<GPrim>(); // Geometric prim

  RegisterReconstructCallback<Xform>();
  RegisterReconstructCallback<GeomCube>();
  RegisterReconstructCallback<GeomSphere>();
  RegisterReconstructCallback<GeomCone>();
  RegisterReconstructCallback<GeomPoints>();
  RegisterReconstructCallback<GeomCylinder>();
  RegisterReconstructCallback<GeomCapsule>();
  RegisterReconstructCallback<GeomMesh>();
  RegisterReconstructCallback<GeomSubset>();
  RegisterReconstructCallback<GeomBasisCurves>();
  RegisterReconstructCallback<GeomNurbsCurves>();
  RegisterReconstructCallback<GeomCamera>();
  RegisterReconstructCallback<GeomPointInstancer>();

  RegisterReconstructCallback<Material>();
  RegisterReconstructCallback<Shader>();
  RegisterReconstructCallback<NodeGraph>();

  RegisterReconstructCallback<Scope>();

  RegisterReconstructCallback<SphereLight>();
  RegisterReconstructCallback<DomeLight>();
  RegisterReconstructCallback<DiskLight>();
  RegisterReconstructCallback<DistantLight>();
  RegisterReconstructCallback<CylinderLight>();
  RegisterReconstructCallback<RectLight>();
  RegisterReconstructCallback<GeometryLight>();
  RegisterReconstructCallback<PortalLight>();

  RegisterReconstructCallback<SkelRoot>();
  RegisterReconstructCallback<Skeleton>();
  RegisterReconstructCallback<SkelAnimation>();
  RegisterReconstructCallback<BlendShape>();

  // UsdPhysics + mjcPhysics
  RegisterReconstructCallback<PhysicsScene>();
  RegisterReconstructCallback<PhysicsRevoluteJoint>();
  RegisterReconstructCallback<PhysicsPrismaticJoint>();
  RegisterReconstructCallback<PhysicsSphericalJoint>();
  RegisterReconstructCallback<PhysicsFixedJoint>();
  RegisterReconstructCallback<PhysicsDistanceJoint>();
  RegisterReconstructCallback<PhysicsCollisionGroup>();
  RegisterReconstructCallback<MjcActuator>();
  RegisterReconstructCallback<MjcTendon>();
  RegisterReconstructCallback<MjcKeyframe>();

  // AR/Interactive (Apple Preliminary_*)
  RegisterReconstructCallback<Preliminary_PhysicsGravitationalForce>();
  RegisterReconstructCallback<Preliminary_InfiniteColliderPlane>();
  RegisterReconstructCallback<Preliminary_ReferenceImage>();
  RegisterReconstructCallback<Preliminary_Behavior>();
  RegisterReconstructCallback<Preliminary_Trigger>();
  RegisterReconstructCallback<Preliminary_Action>();
  RegisterReconstructCallback<Preliminary_Text>();
  // usdMedia
  RegisterReconstructCallback<SpatialAudio>();

  _parser.set_primspec_mode(as_primspec);

  bool ret = _parser.Parse(state_flags, ascii_parser_option);

  std::string warn = _parser.GetWarning();
  if (!warn.empty()) {
    PUSH_WARN("<USDAParser> " + warn);
  }

  if (!ret) {
    std::string error_msg;
    const int source_column_width = _config.error_detail ? (1024 * 1024) : 40;
    if (!_filename.empty()) {
      error_msg = _parser.GetErrorWithSourceContext(_filename, 2,
                                                   source_column_width);
    }
    if (error_msg.empty()) {
      error_msg = _parser.GetError();
    }
    _err = BuildStructuredReadErrorReport(
        fmt::format("{}:Read():{} Failed to parse USDA", __FILE__, __LINE__),
        error_msg, _err, _config.error_detail ? size_t(0) : size_t(8));
    return false;
  }


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

bool USDAReader::read(const uint32_t state_flags, bool as_primspec) {
  return _impl->Read(state_flags, as_primspec);
}

void USDAReader::set_base_dir(const std::string &dir) {
  return _impl->SetBaseDir(dir);
}

void USDAReader::set_filename(const std::string &filename) {
  return _impl->SetFilename(filename);
}

// std::vector<GPrim> USDAReader::GetGPrims() { return _impl->GetGPrims(); }

//std::string USDAReader::GetDefaultPrimName() const {
//  return _impl->GetDefaultPrimName();
//}

std::string USDAReader::get_error() { return _impl->GetError(); }
std::string USDAReader::get_warning() { return _impl->GetWarning(); }

bool USDAReader::get_as_layer(Layer *layer) { return _impl->GetAsLayer(layer); }

bool USDAReader::reconstruct_stage() { return _impl->ReconstructStage(); }

const Stage &USDAReader::get_stage() const { return _impl->GetStage(); }

void USDAReader::set_reader_config(const USDAReaderConfig &config) {
  return _impl->set_reader_config(config);
}

const USDAReaderConfig USDAReader::get_reader_config() const {
  return _impl->get_reader_config();
}

void USDAReader::SetProgressCallback(std::function<bool(float progress, void *userptr)> callback, void *userptr) {
  _impl->SetProgressCallback(callback, userptr);
}

}  // namespace usda
}  // namespace tinyusdz

#else

namespace tinyusdz {
namespace usda {

USDAReader::USDAReader(StreamReader *sr) {
  _empty_stage = new Stage();
  (void)sr;
}

USDAReader::~USDAReader() {
  delete _empty_stage;
  _empty_stage = nullptr;
}

bool USDAReader::check_header() { return false; }

bool USDAReader::read(const LoadState state, bool as_primspec) {
  (void)state;
  (void)as_primspec;
  return false;
}

void USDAReader::set_base_dir(const std::string &dir) { (void)dir; }

//std::vector<GPrim> USDAReader::GetGPrims() { return {}; }

//std::string USDAReader::GetDefaultPrimName() const { return std::string{}; }

std::string USDAReader::get_error() {
  return "USDA parser feature is disabled in this build.\n";
}
std::string USDAReader::get_warning() { return std::string{}; }
bool USDAReader::reconstruct_stage() { return false; }

bool USDAReader::get_as_layer(Layer *layer) { return false; }

const Stage &USDAReader::get_stage() const {
  return *_empty_stage;
}

void USDAReader::set_reader_config(const USDAReaderConfig &config) {
  (void)config;
}

USDAReaderConfig USDAReader::get_reader_config() const {
  return USDAReaderConfig();
}

void USDAReader::SetProgressCallback(std::function<bool(float progress, void *userptr)> callback, void *userptr) {
  (void)callback;
  (void)userptr;
}

}  // namespace usda
}  // namespace tinyusdz

#endif
