// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Reconstruct concrete Prim from PropertyMap or PrimSpec.
//
// TODO:
//   - [ ] Refactor code
//
#include "prim-reconstruct.hh"

#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "core/model-scope.hh"  // Model, Scope
#include "str-util.hh"
#include "io-util.hh"
#include "tiny-format.hh"
#include "enum-handlers.hh"
#include "prim-property-tables.hh"

#include "usdGeom.hh"
#include "usdSkel.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdMtlx.hh"

#include "common-macros.inc"
#include "value-types.hh"

// For PUSH_ERROR_AND_RETURN
#define PushError(s) \
  if (err) { \
    (*err) = (s) + (err->empty() ? std::string() : std::string("\n")) + (*err); \
  }
#define PushWarn(s) \
  if (warn) { \
    (*warn) = (s) + (warn->empty() ? std::string() : std::string("\n")) + (*warn); \
  }

// __VA_ARGS__ does not allow empty, thus # of args must be 2+
#define PUSH_WARN_F(s, ...) PUSH_WARN(fmt::format(s, __VA_ARGS__))

//
// NOTE:
//
// There are mainly 5 variant of Primtive property(relationship/attribute)
//
// - TypedAttribute<T> : Uniform only. `uniform T` or `uniform T var.connect`
// - TypedAttribute<Animatable<T>> : Varying. `T var`, `T var = val`, `T var.connect` or `T value.timeSamples`
// - optional<T> : For output attribute(Just author it. e.g. `float outputs:rgb`)
// - Relationship : Typeless relation(e.g. `rel material:binding`)
// - TypedConnection : Typed relation(e.g. `token outputs:result = </material/diffuse.rgb>`)

namespace tinyusdz {
namespace prim {

//constexpr auto kTag = "[PrimReconstruct]";

constexpr auto kProxyPrim = "proxyPrim";
constexpr auto kVisibility = "visibility";
constexpr auto kExtent = "extent";
constexpr auto kPurpose = "purpose";
constexpr auto kMaterialBinding = "material:binding";
constexpr auto kMaterialBindingCollection = "material:binding:collection";
constexpr auto kMaterialBindingPreview = "material:binding:preview";
constexpr auto kSkelSkeleton = "skel:skeleton";
constexpr auto kSkelAnimationSource = "skel:animationSource";
constexpr auto kSkelBlendShapes = "skel:blendShapes";
constexpr auto kSkelBlendShapeTargets = "skel:blendShapeTargets";
// kInputsVarname moved to prim-reconstruct-shader.cc

// MaterialX Validation Helpers moved to prim-reconstruct-shader.cc


///
/// TinyUSDZ reconstruct some frequently used shaders(e.g. UsdPreviewSurface)
/// here, not in Tydra
///
template <typename T>
bool ReconstructShader(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    T *out,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options);


#include "prim-reconstruct-common.inc"


static bool ReconstructXformOpFromToken(

  const std::string &token, int i,
  std::map<std::string, Property> &properties,
  std::set<std::string> &table, /* inout */
  std::vector<XformOp> *xformOps, std::string *err) {
  if (!xformOps) {
    PUSH_ERROR_AND_RETURN("Internal error: xformOps ptr is null");
  }

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

  std::string tok = token;
        XformOp op;

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

          op.op_type = XformOp::OpType::ResetXformStack;
          xformOps->emplace_back(std::move(op));

          // skip looking up property
          return true;
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
        if (it->second.is_attribute_connection()) {
          PUSH_ERROR_AND_RETURN(
              "Connection(.connect) for xformOp attribute is not yet supported: "
              "`" +
              tok + "`");
        }
        const Attribute &attr = it->second.get_attribute();

        // Check `xformOp` namespace
        if (auto xfm = SplitXformOpToken(tok, kTransform)) {
          op.op_type = XformOp::OpType::Transform;
          op.suffix = xfm.value();  // may contain nested namespaces

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::matrix4d>::type_id()) {
                value::matrix4d dummy{value::matrix4d::identity()};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:transform` must be type `matrix4d`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::matrix4d>()) {
              op.set_value(pvd.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:transform` must be type `matrix4d`, but got type `" +
                  attr.type_name() + "`.");
            }
          }

        } else if (auto tx = SplitXformOpToken(tok, kTranslate)) {
          op.op_type = XformOp::OpType::Translate;
          op.suffix = tx.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:translate` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:translate` must be type `double3` or `float3`, but "
                  "got type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto scale = SplitXformOpToken(tok, kScale)) {
          op.op_type = XformOp::OpType::Scale;
          op.suffix = scale.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:scale` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:scale` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotX = SplitXformOpToken(tok, kRotateX)) {
          op.op_type = XformOp::OpType::RotateX;
          op.suffix = rotX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateX` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateX` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotY = SplitXformOpToken(tok, kRotateY)) {
          op.op_type = XformOp::OpType::RotateY;
          op.suffix = rotY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateY` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateY` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotZ = SplitXformOpToken(tok, kRotateZ)) {
          op.op_type = XformOp::OpType::RotateZ;
          op.suffix = rotZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<double>::type_id()) {
                double dummy(0.0);
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<float>::type_id()) {
                float dummy(0.0f);
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZ` must be type `double` or `float`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<double>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<float>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZ` must be type `double` or `float`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateXYZ = SplitXformOpToken(tok, kRotateXYZ)) {
          op.op_type = XformOp::OpType::RotateXYZ;
          op.suffix = rotateXYZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateXYZ` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateXZY = SplitXformOpToken(tok, kRotateXZY)) {
          op.op_type = XformOp::OpType::RotateXZY;
          op.suffix = rotateXZY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateXZY` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateYXZ = SplitXformOpToken(tok, kRotateYXZ)) {
          op.op_type = XformOp::OpType::RotateYXZ;
          op.suffix = rotateYXZ.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateYXZ` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateYZX = SplitXformOpToken(tok, kRotateYZX)) {
          op.op_type = XformOp::OpType::RotateYZX;
          op.suffix = rotateYZX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateYZX` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateZXY = SplitXformOpToken(tok, kRotateZXY)) {
          op.op_type = XformOp::OpType::RotateZXY;
          op.suffix = rotateZXY.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZXY` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto rotateZYX = SplitXformOpToken(tok, kRotateZYX)) {
          op.op_type = XformOp::OpType::RotateZYX;
          op.suffix = rotateZYX.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::double3>::type_id()) {
                value::double3 dummy{0.0, 0.0, 0.0};
                op.set_value(dummy);
              } else if (attr.type_id() == value::TypeTraits<value::float3>::type_id()) {
                value::float3 dummy{0.0f, 0.0f, 0.0f};
                op.set_value(dummy);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::double3>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::float3>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:rotateZYX` must be type `double3` or `float3`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else if (auto orient = SplitXformOpToken(tok, kOrient)) {
          op.op_type = XformOp::OpType::Orient;
          op.suffix = orient.value();

          // Check if timeSamples were authored (even if empty)
          if (attr.get_var().has_timesamples() || attr.get_var().ts_raw().type_id() != 0) {
            op.set_timesamples(attr.get_var().ts_raw());
          }

          if (attr.get_var().has_default()) {
            if (attr.has_blocked()) {
              // Set dummy value for `op.get_value_type_id/op.get_value_type_name'
              if (attr.type_id() == value::TypeTraits<value::quatf>::type_id()) {
                value::quatf q;
                q.real = 1.0f;
                q.imag = {0.0f, 0.0f, 0.0f};
                op.set_value(q);
              } else if (attr.type_id() == value::TypeTraits<value::quatd>::type_id()) {
                value::quatd q;
                q.real = 1.0;
                q.imag = {0.0, 0.0, 0.0};
                op.set_value(q);
              } else {
                PUSH_ERROR_AND_RETURN(
                    "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                    "type `" +
                    attr.type_name() + "`.");
              }
              op.set_blocked(true);
            } else if (auto pvd = attr.get_value<value::quatf>()) {
              op.set_value(pvd.value());
            } else if (auto pvf = attr.get_value<value::quatd>()) {
              op.set_value(pvf.value());
            } else {
              PUSH_ERROR_AND_RETURN(
                  "`xformOp:orient` must be type `quatf` or `quatd`, but got "
                  "type `" +
                  attr.type_name() + "`.");
            }
          }
        } else {
          PUSH_ERROR_AND_RETURN(
              "token for xformOpOrder must have namespace `xformOp:***`, or .");
        }

        xformOps->emplace_back(std::move(op));
        table.insert(tok);

    return true;
  }


bool ReconstructXformOpsFromProperties(
  const Specifier &spec,
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  std::vector<XformOp> *xformOps,
  std::string *err)
{

  if (spec == Specifier::Class) {
    // Do not materialize xformOps here.
    return true;
  }


  // Lookup xform values from `xformOpOrder`
  // TODO: TimeSamples, Connection
  if (properties.count("xformOpOrder")) {
    // array of string
    auto prop = properties.at("xformOpOrder");

    // 'uniform' check
    if (prop.get_attribute().variability() != Variability::Uniform) {
      PUSH_ERROR_AND_RETURN("`xformOpOrder` must have `uniform` variability.");
    }

    //const Attribute &attr = prop.get_attribute();
    //const auto &v = attr.get_var();
    //TUSDZ_LOG_I("attr.value.type " << v.type_name());

    if (prop.is_relationship()) {
      PUSH_ERROR_AND_RETURN("Relationship for `xformOpOrder` is not supported.");
    } else if (auto tpv =
                   prop.get_attribute().get_value<TypedArray<value::token>>()) {


      for (size_t i = 0; i < tpv.value().size(); i++) {
        const auto &item = tpv.value()[i];

        if (!ReconstructXformOpFromToken(item.str(), int(i), properties, table, xformOps, err)) {
          return false;
        }
      }
    } else if (auto pv =
                   prop.get_attribute().get_value<std::vector<value::token>>()) {

      // 'uniform' check
      if (prop.get_attribute().variability() != Variability::Uniform) {
        PUSH_ERROR_AND_RETURN("`xformOpOrder` must have `uniform` variability.");
      }

      for (size_t i = 0; i < pv.value().size(); i++) {
        const auto &item = pv.value()[i];

        if (!ReconstructXformOpFromToken(item.str(), int(i), properties, table, xformOps, err)) {
          return false;
        }
      }

    } else {
      PUSH_ERROR_AND_RETURN(
          "`xformOpOrder` must be type `token[]` but got type `"
          << prop.get_attribute().type_name() << "`.");
    }
  }

  table.insert("xformOpOrder");
  return true;
}

namespace {

bool ReconstructMaterialBindingProperties(
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  MaterialBinding *mb, /* inout */
  std::string *err)
{

  if (!mb) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBinding, mb->materialBinding)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBindingPreview, mb->materialBindingPreview)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kMaterialBindingPreview, mb->materialBindingFull)
    // material:binding:collection
    if (prop.first == kMaterialBindingCollection) {

      if (table.count(prop.first)) {
         continue;
      }

      if (!prop.second.is_relationship()) {
        PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
      }

      const Relationship &rel = prop.second.get_relationship();

      mb->set_materialBindingCollection(value::token(""), value::token(""), rel);

      table.insert(prop.first);
      continue;
    }
    // material:binding:collection[:PURPOSE]:NAME
    if (startsWith(prop.first, kMaterialBindingCollection + std::string(":"))) {

      if (table.count(prop.first)) {
         continue;
      }

      if (!prop.second.is_relationship()) {
        PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
      }

      std::string collection_name = removePrefix(prop.first, kMaterialBindingCollection + std::string(":"));
      if (collection_name.empty()) {
        PUSH_ERROR_AND_RETURN("empty NAME is not allowed for 'mateirial:binding:collection'");
      }
      std::vector<std::string> names = split(collection_name, ":");
      if (names.size() > 2) {
        PUSH_ERROR_AND_RETURN("3 or more namespaces is not allowed for 'mateirial:binding:collection'");
      }
      value::token mat_purpose; // empty = all-purpose
      if (names.size() == 1) {
        collection_name = names[0];
      } else {
        mat_purpose = value::token(names[0]);
        collection_name = names[1];
      }

      const Relationship &rel = prop.second.get_relationship();

      mb->set_materialBindingCollection(value::token(collection_name), mat_purpose, rel);

      table.insert(prop.first);
      continue;
    }
    // material:binding:PURPOSE
    if (startsWith(prop.first, kMaterialBinding + std::string(":"))) {

      if (table.count(prop.first)) {
         continue;
      }

      if (!prop.second.is_relationship()) {
        PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
      }

      std::string purpose_name = removePrefix(prop.first, kMaterialBinding + std::string(":"));
      if (purpose_name.empty()) {
        PUSH_ERROR_AND_RETURN("empty PURPOSE is not allowed for 'mateirial:binding:'");
      }
      std::vector<std::string> names = split(purpose_name, ":");
      if (names.size() > 1) {
        PUSH_ERROR_AND_RETURN(fmt::format("PURPOSE `{}` must not have nested namespaces for 'mateirial:binding'", purpose_name));
      }
      value::token mat_purpose = value::token(names[0]);

      const Relationship &rel = prop.second.get_relationship();

      mb->set_materialBinding(rel, mat_purpose);

      table.insert(prop.first);
      continue;
    }
  }

  return true;
}

bool ReconstructCollectionProperties(
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  Collection *coll, /* inout */
  std::string *warn,
  std::string *err,
  bool strict_allowedToken_check)
{
  constexpr auto kCollectionPrefix = "collection:";

  // Use centralized enum handler
  std::function<nonstd::expected<CollectionInstance::ExpansionRule, std::string>(const std::string &)> ExpansionRuleEnumHandler = enum_handler::ExpansionRule;

  if (!coll) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    if (startsWith(prop.first, kCollectionPrefix)) {
      if (table.count(prop.first)) {
         continue;
      }

      std::string suffix = removePrefix(prop.first, kCollectionPrefix);
      std::vector<std::string> names = split(suffix, ":");
      if (names.size() != 2) {
        PUSH_ERROR_AND_RETURN(fmt::format("Invalid collection property name. Must be 'collection:INSTANCE_NAME:<prop_name>' but got '{}'",  prop.first));
      }
      if (names[0].empty()) {
        PUSH_ERROR_AND_RETURN("INSTANCE_NAME is empty for collection property name");
      }
      if (names[1].empty()) {
        PUSH_ERROR_AND_RETURN("Collection property name is empty");
      }

      std::string instance_name = names[0];

      if (names[1] == "includes") {

        if (!prop.second.is_relationship()) {
          PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
        }

        CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
        coll_instance.includes = prop.second.get_relationship();
        table.insert(prop.first);

      } else if (names[1] == "expansionRule") {

        TypedAttributeWithFallback<CollectionInstance::ExpansionRule> r{CollectionInstance::ExpansionRule::ExpandPrims};

        PARSE_UNIFORM_ENUM_PROPERTY_NOCONTINUE(table, prop, prop.first, CollectionInstance::ExpansionRule, ExpansionRuleEnumHandler, CollectionInstance,
                       r, strict_allowedToken_check)

        if (table.count(prop.first)) {
          CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
          coll_instance.expansionRule = r;  // Assign full TypedAttributeWithFallback to preserve authored state
        }
      } else if (names[1] == "includeRoot") {

        TypedAttributeWithFallback<Animatable<bool>> includeRoot{false};
        PARSE_TYPED_ATTRIBUTE_NOCONTINUE(table, prop, prop.first, CollectionInstance, includeRoot)

        if (table.count(prop.first)) {
          CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
          coll_instance.includeRoot = includeRoot;
        }
      } else if (names[1] == "excludes") {

        if (!prop.second.is_relationship()) {
          PUSH_ERROR_AND_RETURN(fmt::format("`{}` must be a Relationship", prop.first));
        }

        CollectionInstance &coll_instance = coll->get_or_add_instance(instance_name);
        coll_instance.excludes = prop.second.get_relationship();
        table.insert(prop.first);

      }
    }
  }

  return true;
}
// xformOps and built-in props
bool ReconstructGPrimProperties(
  const Specifier &spec,
  std::set<std::string> &table, /* inout */
  std::map<std::string, Property> &properties,
  GPrim *gprim, /* inout */
  std::string *warn,
  std::string *err,
  bool strict_allowedToken_check)
{

  (void)warn;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &gprim->xformOps, err)) {
    return false;
  }

  if (!prim::ReconstructMaterialBindingProperties(table, properties, gprim, err)) {
    return false;
  }

  if (!prim::ReconstructCollectionProperties(
    table, properties, gprim, warn, err, strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, kProxyPrim, gprim->proxyPrim)
    PARSE_TYPED_ATTRIBUTE(table, prop, "doubleSided", GPrim, gprim->doubleSided)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, GPrim,
                   gprim->visibility, strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "purpose", Purpose, PurposeEnumHandler, GPrim,
                       gprim->purpose, strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "orientation", Orientation, OrientationEnumHandler, GPrim,
                       gprim->orientation, strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, "extent", GPrim, gprim->extent)
  }

  return true;
}

} // namespace local


template <>
bool ReconstructPrim<Xform>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Xform *xform,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)options;
  (void)references;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, xform, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    ADD_PROPERTY(table, prop, Xform, xform->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Model>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Model *model,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  DCOUT("Model ");
  (void)spec;
  (void)references;
  (void)model;
  (void)err;
  (void)options;

  std::set<std::string> table;
  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    ADD_PROPERTY(table, prop, Model, model->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Scope>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Scope *scope,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  // `Scope` is just a namespace in scene graph(no node xform)

  (void)spec;
  (void)references;
  (void)scope;
  (void)err;
  (void)options;

  DCOUT("Scope");
  std::set<std::string> table;
  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, Scope,
                   scope->visibility, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, Scope, scope->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<SkelRoot>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SkelRoot *root,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;
  (void)options;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &root->xformOps, err)) {
    return false;
  }

  // SkelRoot is something like a grouping node, having 1 Skeleton and possibly
  // multiple Prim hierarchy containing GeomMesh.
  // SkelBindingAPI properties (skel:animationSource, skel:skeleton) can be
  // authored on SkelRoot and inherited by child prims per the USD spec.

  for (auto &prop : properties) {  // Non-const to allow move from property metadata

    // SkelBindingAPI: animationSource relationship
    if (prop.first == kSkelAnimationSource) {
      if (prop.second.is_relationship()) {
        const Relationship &rel = prop.second.get_relationship();
        if (rel.is_path() || rel.is_pathvector()) {
          root->animationSource = rel;
          table.insert(kSkelAnimationSource);
        } else {
          PUSH_WARN("`" << kSkelAnimationSource << "` target must be Path.");
        }
      }
    }

    // SkelBindingAPI: skeleton relationship
    if (prop.first == kSkelSkeleton) {
      if (prop.second.is_relationship()) {
        const Relationship &rel = prop.second.get_relationship();
        if (rel.is_path() || rel.is_pathvector()) {
          root->skeleton = rel;
          table.insert(kSkelSkeleton);
        } else {
          PUSH_WARN("`" << kSkelSkeleton << "` target must be Path.");
        }
      }
    }

    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, SkelRoot,
                   root->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, SkelRoot,
                       root->purpose, options.strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, SkelRoot, root->extent)
    ADD_PROPERTY(table, prop, SkelRoot, root->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<Skeleton>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    Skeleton *skel,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;

  std::set<std::string> table;
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &skel->xformOps, err)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ Skeleton
#define PRIM_PTR_ skel
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {

    // SkelBindingAPI: animationSource relationship
    if (prop.first == kSkelAnimationSource) {
      // Must be relation of type Path.
      if (prop.second.is_relationship() && prop.second.get_relationship().is_path()) {
        const Relationship &rel = prop.second.get_relationship();
        if (rel.is_path()) {
          skel->animationSource = rel;
          table.insert(kSkelAnimationSource);
        } else {
          PUSH_ERROR_AND_RETURN("`" << kSkelAnimationSource << "` target must be Path.");
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`" << kSkelAnimationSource << "` must be a Relationship with Path target.");
      }
    }

    SKELETON_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, Skeleton,
                   skel->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, "purpose", Purpose, PurposeEnumHandler, Skeleton,
                       skel->purpose, options.strict_allowedToken_check)
    PARSE_EXTENT_ATTRIBUTE(table, prop, "extent", Skeleton, skel->extent)
    ADD_PROPERTY(table, prop, Skeleton, skel->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_


  return true;
}

template <>
bool ReconstructPrim<SkelAnimation>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SkelAnimation *skelanim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)warn;
  (void)references;
  (void)options;

  std::set<std::string> table;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ SkelAnimation
#define PRIM_PTR_ skelanim
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    SKEL_ANIMATION_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, SkelAnimation, skelanim->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<BlendShape>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    BlendShape *bs,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)spec;
  (void)warn;
  (void)references;
  (void)options;

  DCOUT("Reconstruct BlendShape");

  std::set<std::string> table;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ BlendShape
#define PRIM_PTR_ bs
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    BLEND_SHAPE_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, BlendShape, bs->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_


  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GPrim *gprim,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)gprim;
  (void)err;

  (void)references;
  (void)properties;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, gprim, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomBasisCurves *curves,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;

  DCOUT("GeomBasisCurves");

  // Use centralized enum handlers
  auto BasisHandler = enum_handler::BasisCurvesBasis;
  auto TypeHandler = enum_handler::BasisCurvesType;
  auto WrapHandler = enum_handler::BasisCurvesWrap;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, curves, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomBasisCurves
#define PRIM_PTR_ curves
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_BASIS_CURVES_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_BASIS_CURVES_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomBasisCurves, curves->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomNurbsCurves *curves,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;
  (void)options;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, curves, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    PARSE_TYPED_ATTRIBUTE(table, prop, "curveVertexCounts", GeomNurbsCurves,
                         curves->curveVertexCounts)
    PARSE_TYPED_ATTRIBUTE(table, prop, "points", GeomNurbsCurves, curves->points)
    PARSE_TYPED_ATTRIBUTE(table, prop, "velocities", GeomNurbsCurves,
                          curves->velocities)
    PARSE_TYPED_ATTRIBUTE(table, prop, "normals", GeomNurbsCurves,
                  curves->normals)
    PARSE_TYPED_ATTRIBUTE(table, prop, "accelerations", GeomNurbsCurves,
                 curves->accelerations)
    PARSE_TYPED_ATTRIBUTE(table, prop, "widths", GeomNurbsCurves, curves->widths)

    //
    PARSE_TYPED_ATTRIBUTE(table, prop, "order", GeomNurbsCurves, curves->order)
    PARSE_TYPED_ATTRIBUTE(table, prop, "knots", GeomNurbsCurves, curves->knots)
    PARSE_TYPED_ATTRIBUTE(table, prop, "ranges", GeomNurbsCurves, curves->ranges)
    PARSE_TYPED_ATTRIBUTE(table, prop, "pointWeights", GeomNurbsCurves, curves->pointWeights)

    ADD_PROPERTY(table, prop, GeomBasisCurves, curves->props)

    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

// ============================================================================
// Generic macro for light prim reconstruction
// ============================================================================
// Consolidates the common pattern for all light types:
// SphereLight, RectLight, DiskLight, CylinderLight, DistantLight, GeometryLight, DomeLight
//
// IMPORTANT: Caller must define PRIM_CLASS_ and PRIM_PTR_ macros before calling
//            this macro, and undef them afterward. These are required by
//            EXPAND_TYPED_ATTR macros.
//
// Parameters:
//   LightClass: The light class (e.g., SphereLight, RectLight)
//   light_ptr: Pointer to the light instance
//   TYPED_ATTRS: Property table macro (e.g., SPHERE_LIGHT_TYPED_ATTRS)
//   COMMON_ATTRS: Light common attrs macro (LIGHT_COMMON_ATTRS_WITH_SHAPING or LIGHT_COMMON_ATTRS_NO_SHAPING)
//   EXTENT_HANDLING: Either PARSE_EXTENT_ATTRIBUTE(...) or /* no extent */
//   SPECIAL_HANDLING: Special attribute handling for exceptions like RectLight's texture:file or /* no special handling */
#define RECONSTRUCT_LIGHT_PRIM_BODY(LightClass, light_ptr, TYPED_ATTRS, COMMON_ATTRS, EXTENT_HANDLING, SPECIAL_HANDLING) \
  (void)references; \
  \
  std::set<std::string> table; \
  \
  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &light_ptr->xformOps, err)) { \
    return false; \
  } \
  \
  for (auto &prop : properties) { \
    SPECIAL_HANDLING \
    TYPED_ATTRS(EXPAND_TYPED_ATTR) \
    COMMON_ATTRS(EXPAND_TYPED_ATTR) \
    EXTENT_HANDLING \
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, LightClass, \
                       light_ptr->visibility, options.strict_allowedToken_check) \
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, LightClass, \
                       light_ptr->purpose, options.strict_allowedToken_check) \
    ADD_PROPERTY(table, prop, LightClass, light_ptr->props) \
    PARSE_PROPERTY_END_MAKE_WARN(table, prop) \
  } \
  \
  return true;

template <>
bool ReconstructPrim<SphereLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    SphereLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ SphereLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(SphereLight, light, SPHERE_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, SphereLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<RectLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    RectLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ RectLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  // Special case: texture:file uses UsdUVTexture type
  RECONSTRUCT_LIGHT_PRIM_BODY(RectLight, light, RECT_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, RectLight, light->extent),
                              PARSE_TYPED_ATTRIBUTE(table, prop, "inputs:texture:file", UsdUVTexture, light->file))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DiskLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DiskLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DiskLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DiskLight, light, DISK_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, DiskLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<CylinderLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    CylinderLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ CylinderLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(CylinderLight, light, CYLINDER_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_WITH_SHAPING,
                              PARSE_EXTENT_ATTRIBUTE(table, prop, kExtent, CylinderLight, light->extent),
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DistantLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DistantLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DistantLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DistantLight, light, DISTANT_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeometryLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeometryLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeometryLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(GeometryLight, light, GEOMETRY_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<PortalLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    PortalLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ PortalLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(PortalLight, light, GEOMETRY_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DomeLight>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DomeLight *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DomeLight
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  DCOUT("Implement DomeLight");
  RECONSTRUCT_LIGHT_PRIM_BODY(DomeLight, light, DOME_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              /* no special handling */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<DomeLight_1>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    DomeLight_1 *light,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ DomeLight_1
#define PRIM_PTR_ light
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_LIGHT_PRIM_BODY(DomeLight_1, light, DOME_LIGHT_TYPED_ATTRS, LIGHT_COMMON_ATTRS_NO_SHAPING,
                              /* no extent */,
                              PARSE_TYPED_ATTRIBUTE(table, prop, "poleAxis", DomeLight_1, light->poleAxis))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<LightFilter>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    LightFilter *filter,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;

  std::set<std::string> table;

  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &filter->xformOps, err)) {
    return false;
  }

  for (auto &prop : properties) {
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, LightFilter,
                       filter->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, LightFilter,
                       filter->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, LightFilter, filter->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

template <>
bool ReconstructPrim<PluginLightFilter>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    PluginLightFilter *filter,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;

  std::set<std::string> table;

  if (!prim::ReconstructXformOpsFromProperties(spec, table, properties, &filter->xformOps, err)) {
    return false;
  }

  for (auto &prop : properties) {
    PARSE_TYPED_ATTRIBUTE(table, prop, "light:shaderId", PluginLightFilter, filter->shaderId)
    PARSE_TIMESAMPLED_ENUM_PROPERTY(table, prop, kVisibility, Visibility, VisibilityEnumHandler, PluginLightFilter,
                       filter->visibility, options.strict_allowedToken_check)
    PARSE_UNIFORM_ENUM_PROPERTY(table, prop, kPurpose, Purpose, PurposeEnumHandler, PluginLightFilter,
                       filter->purpose, options.strict_allowedToken_check)
    ADD_PROPERTY(table, prop, PluginLightFilter, filter->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

  return true;
}

// ============================================================================
// Generic macro for simple geometry prim reconstruction
// ============================================================================
// Consolidates the common pattern for GeomSphere, GeomCone, GeomCylinder,
// GeomCapsule, GeomCube: ReconstructGPrimProperties + property loop
//
// IMPORTANT: Caller must define PRIM_CLASS_ and PRIM_PTR_ macros before calling
//            this macro, and undef them afterward. These are required by
//            EXPAND_TYPED_ATTR and EXPAND_UNIFORM_ENUM macros.
//
// Parameters:
//   PrimClass: The geometry class (e.g., GeomSphere, GeomCone)
//   prim_ptr: Pointer to the prim instance
//   TYPED_ATTRS: Property table macro (e.g., GEOM_SPHERE_TYPED_ATTRS)
//   ENUM_EXPANSION: Enum handling macro call or empty
//                   - For shapes without enums: /* empty */
//                   - For shapes with enums: GEOM_XXX_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
#define RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(PrimClass, prim_ptr, TYPED_ATTRS, ENUM_EXPANSION) \
  (void)references; \
  \
  std::set<std::string> table; \
  if (!ReconstructGPrimProperties(spec, table, properties, prim_ptr, warn, err, \
                                   options.strict_allowedToken_check)) { \
    return false; \
  } \
  \
  for (auto &prop : properties) { \
    TYPED_ATTRS(EXPAND_TYPED_ATTR) \
    ENUM_EXPANSION \
    ADD_PROPERTY(table, prop, PrimClass, prim_ptr->props) \
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop) \
  } \
  \
  return true;

template <>
bool ReconstructPrim<GeomSphere>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomSphere *sphere,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  DCOUT("Reconstruct Sphere.");
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomSphere
#define PRIM_PTR_ sphere
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomSphere, sphere, GEOM_SPHERE_TYPED_ATTRS, /* no enums */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomPoints>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomPoints *points,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;

  DCOUT("Reconstruct Points.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, points, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomPoints
#define PRIM_PTR_ points
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    DCOUT("prop: " << prop.first);
    GEOM_POINTS_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, GeomPoints, points->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomCone>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCone *cone,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCone
#define PRIM_PTR_ cone
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCone, cone, GEOM_CONE_TYPED_ATTRS,
                                     GEOM_CONE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCylinder>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCylinder *cylinder,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCylinder
#define PRIM_PTR_ cylinder
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCylinder, cylinder, GEOM_CYLINDER_TYPED_ATTRS,
                                     GEOM_CYLINDER_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCapsule>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCapsule *capsule,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCapsule
#define PRIM_PTR_ capsule
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCapsule, capsule, GEOM_CAPSULE_TYPED_ATTRS,
                                     GEOM_CAPSULE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCube>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCube *cube,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  // pxrUSD says... "If you author size you must also author extent."
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCube
#define PRIM_PTR_ cube
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCube, cube, GEOM_CUBE_TYPED_ATTRS, /* no enums */)
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomPlane>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    GeomPlane *plane, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
#define PRIM_CLASS_ GeomPlane
#define PRIM_PTR_ plane
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomPlane, plane, GEOM_PLANE_TYPED_ATTRS,
                                     GEOM_PLANE_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCylinder_1>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    GeomCylinder_1 *cyl, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
#define PRIM_CLASS_ GeomCylinder_1
#define PRIM_PTR_ cyl
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCylinder_1, cyl, GEOM_CYLINDER_1_TYPED_ATTRS,
                                     GEOM_CYLINDER_1_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomCapsule_1>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    GeomCapsule_1 *cap, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
#define PRIM_CLASS_ GeomCapsule_1
#define PRIM_PTR_ cap
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomCapsule_1, cap, GEOM_CAPSULE_1_TYPED_ATTRS,
                                     GEOM_CAPSULE_1_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM))
#undef PRIM_CLASS_
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomTetMesh>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    GeomTetMesh *tetmesh, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
#define PRIM_PTR_ tetmesh
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomTetMesh, tetmesh, GEOM_TET_MESH_TYPED_ATTRS, /* no enums */)
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomNurbsPatch>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    GeomNurbsPatch *patch, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
#define PRIM_PTR_ patch
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomNurbsPatch, patch, GEOM_NURBS_PATCH_TYPED_ATTRS, /* no enums */)
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomHermiteCurves>(
    const Specifier &spec, PropertyMap &properties, const ReferenceList &references,
    GeomHermiteCurves *curves, std::string *warn, std::string *err,
    const PrimReconstructOptions &options) {
#define PRIM_PTR_ curves
  RECONSTRUCT_SIMPLE_GEOM_PRIM_BODY(GeomHermiteCurves, curves, GEOM_HERMITE_CURVES_TYPED_ATTRS, /* no enums */)
#undef PRIM_PTR_
}

template <>
bool ReconstructPrim<GeomMesh>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomMesh *mesh,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)references;

  DCOUT("GeomMesh");

  // Use centralized enum handlers (aliased for macro expansion)
  auto SubdivisionSchemeHandler = enum_handler::SubdivisionScheme;
  auto InterpolateBoundaryHandler = enum_handler::InterpolateBoundary;
  auto FaceVaryingLinearInterpolationHandler = enum_handler::FaceVaryingLinearInterpolation;
  auto FamilyTypeHandler = enum_handler::FamilyType;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, mesh, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

  // Define context for property table expansion macros
  // (suppress unused-macros warning since these are used inside X-macro expansion)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomMesh
#define PRIM_PTR_ mesh
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {
    DCOUT("GeomMesh prop: " << prop.first);

    // Relations (using property table)
    GEOM_MESH_RELATIONS(EXPAND_SINGLE_REL, EXPAND_MULTI_REL)

    // Typed attributes (using property table)
    GEOM_MESH_TYPED_ATTRS(EXPAND_TYPED_ATTR)

    // Skel-related typed attributes
    GEOM_MESH_SKEL_ATTRS(EXPAND_TYPED_ATTR)

    // Enum properties (using property table)
    GEOM_MESH_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    GEOM_MESH_TIMESAMPLED_ENUMS(EXPAND_TIMESAMPLED_ENUM)

    // Special handling: subsetFamily for GeomSubset (cannot be table-driven)
    if (startsWith(prop.first, "subsetFamily")) {
      // uniform subsetFamily::<FAMILYNAME>:familyType = ...
      std::vector<std::string> names = split(prop.first, ":");

      if ((names.size() == 3) &&
          (names[0] == "subsetFamily") &&
          (names[2] == "familyType")) {

        if (table.count(prop.first)) {
          // Already processed
        } else if ((prop.second.value_type_name() == value::TypeTraits<value::token>::type_name()) &&
                   prop.second.is_attribute() &&
                   !prop.second.is_empty()) {
          // Parse the token enum value
          const Attribute &attr = prop.second.get_attribute();
          TypedAttributeWithFallback<GeomSubset::FamilyType> familyType{GeomSubset::FamilyType::Unrestricted};
          std::function<nonstd::expected<GeomSubset::FamilyType, std::string>(const std::string &)> fun = FamilyTypeHandler;

          if (!ParseUniformEnumProperty(prop.first, options.strict_allowedToken_check, fun, attr, &familyType, warn, err, options)) {
            return false;
          }

          // NOTE: Ignore metadata of familyType.
          // TODO: Validate familyName
          mesh->subsetFamilyTypeMap[value::token(names[1])] = familyType.get_value();
          table.insert(prop.first);
        }
      }
    }

    // generic property handling
    ADD_PROPERTY(table, prop, GeomMesh, mesh->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}


template <>
bool ReconstructPrim<GeomCamera>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomCamera *camera,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {
  (void)references;
  (void)warn;

  // Use centralized enum handlers
  auto ProjectionHandler = enum_handler::CameraProjection;
  auto StereoRoleHandler = enum_handler::CameraStereoRole;

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, camera, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomCamera
#define PRIM_PTR_ camera
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_CAMERA_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_CAMERA_TIMESAMPLED_ENUMS(EXPAND_TIMESAMPLED_ENUM)
    GEOM_CAMERA_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomCamera, camera->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomSubset>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomSubset *subset,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)spec;
  (void)references;

  DCOUT("GeomSubset");

  // Use centralized enum handler
  auto ElementTypeHandler = enum_handler::ElementType;

  std::set<std::string> table;

  if (!prim::ReconstructMaterialBindingProperties(table, properties, subset, err)) {
    return false;
  }

  if (!prim::ReconstructCollectionProperties(
    table, properties, subset, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomSubset
#define PRIM_PTR_ subset
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_SUBSET_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    GEOM_SUBSET_UNIFORM_ENUMS(EXPAND_UNIFORM_ENUM)
    ADD_PROPERTY(table, prop, GeomSubset, subset->props)
    PARSE_PROPERTY_END_MAKE_WARN(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}

template <>
bool ReconstructPrim<GeomPointInstancer>(
    const Specifier &spec,
    PropertyMap &properties,
    const ReferenceList &references,
    GeomPointInstancer *instancer,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)references;

  DCOUT("Reconstruct GeomPointInstancer.");

  std::set<std::string> table;
  if (!ReconstructGPrimProperties(spec, table, properties, instancer, warn, err, options.strict_allowedToken_check)) {
    return false;
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define PRIM_CLASS_ GeomPointInstancer
#define PRIM_PTR_ instancer
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  for (auto &prop : properties) {  // Non-const to allow move from property metadata
    GEOM_POINT_INSTANCER_RELATIONS(EXPAND_SINGLE_REL, EXPAND_MULTI_REL)
    GEOM_POINT_INSTANCER_TYPED_ATTRS(EXPAND_TYPED_ATTR)
    ADD_PROPERTY(table, prop, GeomPointInstancer, instancer->props)
    PARSE_PROPERTY_END_MAKE_ERROR(table, prop)
  }

#undef PRIM_CLASS_
#undef PRIM_PTR_

  return true;
}


// Shader/Material/NodeGraph reconstruction moved to prim-reconstruct-shader.cc
// Physics + MuJoCo reconstruction moved to prim-reconstruct-physics.cc


///
/// -- PrimSpec
///

#define RECONSTRUCT_PRIM_PRIMSPEC_IMPL(__prim_ty) \
template <> \
bool ReconstructPrim<__prim_ty>( \
    PrimSpec &primspec, \
    __prim_ty *prim, \
    std::string *warn, \
    std::string *err, \
    const PrimReconstructOptions &options) { \
 \
  ReferenceList references; /* dummy */ \
 \
  return ReconstructPrim<__prim_ty>(primspec.specifier(), primspec.props(), references, prim, warn, err, options); \
}

RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Xform)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Model)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Scope)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomMesh)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomPoints)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCylinder)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCube)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCone)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomSphere)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCapsule)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomBasisCurves)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomCamera)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomSubset)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeomPointInstancer)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SphereLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DomeLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(CylinderLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DiskLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DistantLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(RectLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(GeometryLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PortalLight)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(DomeLight_1)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(LightFilter)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(PluginLightFilter)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SkelRoot)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(Skeleton)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(SkelAnimation)
RECONSTRUCT_PRIM_PRIMSPEC_IMPL(BlendShape)
// Shader, Material, NodeGraph PrimSpec wrappers are in prim-reconstruct-shader.cc
// Physics + MuJoCo PrimSpec wrappers are in prim-reconstruct-physics.cc


} // namespace prim

} // namespace tinyusdz
