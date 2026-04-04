// SPDX-License-Identifier: Apache 2.0
// Copyright 2020 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// USDC reader: Prim spec parsing, StageMeta, type dispatch
// (extracted from usdc-reader.cc)
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "usdc-reader-impl.hh"
#include "usdPhysics.hh"
#include "usdAR.hh"
#include "usdMedia.hh"
#include "mjcPhysics.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_READER)

namespace tinyusdz {
namespace usdc {

nonstd::expected<APISchemas, std::string> USDCReader::Impl::ToAPISchemas(
    const ListOp<value::token> &arg, bool ignore_unknown, std::string &warn) {
  APISchemas schemas;

  auto SchemaHandler =
      [](const value::token &tok) -> nonstd::optional<APISchemas::APIName> {
    return enum_handler::APISchemaNameOpt(tok.str());
  };

  // Process a list of schema tokens into schemas.names/unknownSchemas.
  auto ProcessItems = [&](const std::vector<value::token> &items)
      -> nonstd::expected<bool, std::string> {
    for (const auto &item : items) {
      if (auto pv = SchemaHandler(item)) {
        std::string instanceName;  // TODO: parse instance name
        schemas.names.push_back({pv.value(), instanceName});
      } else if (ignore_unknown) {
        std::string instanceName;
        schemas.unknownSchemas.push_back({item.str(), instanceName});
        warn += "Preserving unknown API schema: " + item.str() + "\n";
      } else {
        return nonstd::make_unexpected("Invalid or Unsupported API schema: " +
                                       item.str());
      }
    }
    return true;
  };

  if (arg.IsExplicit()) {  // fast path
    auto r = ProcessItems(arg.GetExplicitItems());
    if (!r) return nonstd::make_unexpected(r.error());
    schemas.listOpQual = ListEditQual::ResetToExplicit;

  } else {
    // Currently only support a single ListEdit qualifier at a time.
    struct { const std::vector<value::token>& items; ListEditQual qual; } candidates[] = {
      {arg.GetExplicitItems(), ListEditQual::ResetToExplicit},
      {arg.GetAddedItems(), ListEditQual::Add},
      {arg.GetAppendedItems(), ListEditQual::Append},
      {arg.GetDeletedItems(), ListEditQual::Delete},
      {arg.GetPrependedItems(), ListEditQual::Prepend},
    };

    size_t active_count = 0;
    size_t active_idx = 0;
    for (size_t i = 0; i < 5; ++i) {
      if (!candidates[i].items.empty()) {
        ++active_count;
        active_idx = i;
      }
    }
    if (arg.GetOrderedItems().size()) {
      ++active_count;
    }

    if (active_count > 1) {
      return nonstd::make_unexpected(
          "Currently TinyUSDZ does not support ListOp with different "
          "ListEdit qualifiers.");
    }

    if (active_count == 0) {
      if (arg.GetOrderedItems().size()) {
        return nonstd::make_unexpected("TODO: Ordered ListOp items.");
      }
      return nonstd::make_unexpected("Internal error: ListOp conversion.");
    }

    auto r = ProcessItems(candidates[active_idx].items);
    if (!r) return nonstd::make_unexpected(r.error());
    schemas.listOpQual = candidates[active_idx].qual;
  }

  return std::move(schemas);
}

template <typename T>
bool USDCReader::Impl::ReconstructPrim(const Specifier &spec, const crate::CrateReader::Node &node,
                                       const PathIndexToSpecIndexMap &psmap,
                                       T *prim) {
  // Prim's properties are stored in its children nodes.
  prim::PropertyMap properties;
  if (!BuildPropertyMap(node.GetChildren(), psmap, &properties)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
  }

  prim::ReferenceList refs;  // dummy

  prim::PrimReconstructOptions reconstruct_options;
  reconstruct_options.strict_allowedToken_check = _config.strict_allowedToken_check;

  if (!prim::ReconstructPrim<T>(spec, properties, refs, prim, &_warn, &_err, reconstruct_options)) {
    return false;
  }

  return true;
}

// Explicit template instantiations for ReconstructPrim
#define INSTANTIATE_RECONSTRUCT_PRIM(__ty) \
  template bool USDCReader::Impl::ReconstructPrim<__ty>( \
      const Specifier &, const crate::CrateReader::Node &, \
      const PathIndexToSpecIndexMap &, __ty *)

INSTANTIATE_RECONSTRUCT_PRIM(Xform);
INSTANTIATE_RECONSTRUCT_PRIM(Model);
INSTANTIATE_RECONSTRUCT_PRIM(Scope);
INSTANTIATE_RECONSTRUCT_PRIM(GeomPoints);
INSTANTIATE_RECONSTRUCT_PRIM(GeomMesh);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCapsule);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCube);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCone);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCylinder);
INSTANTIATE_RECONSTRUCT_PRIM(GeomSphere);
INSTANTIATE_RECONSTRUCT_PRIM(GeomSubset);
INSTANTIATE_RECONSTRUCT_PRIM(GeomBasisCurves);
INSTANTIATE_RECONSTRUCT_PRIM(GeomNurbsCurves);
INSTANTIATE_RECONSTRUCT_PRIM(GeomPlane);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCylinder_1);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCapsule_1);
INSTANTIATE_RECONSTRUCT_PRIM(GeomTetMesh);
INSTANTIATE_RECONSTRUCT_PRIM(GeomNurbsPatch);
INSTANTIATE_RECONSTRUCT_PRIM(GeomHermiteCurves);
INSTANTIATE_RECONSTRUCT_PRIM(GeomCamera);
INSTANTIATE_RECONSTRUCT_PRIM(GeomPointInstancer);
INSTANTIATE_RECONSTRUCT_PRIM(SphereLight);
INSTANTIATE_RECONSTRUCT_PRIM(DomeLight);
INSTANTIATE_RECONSTRUCT_PRIM(DiskLight);
INSTANTIATE_RECONSTRUCT_PRIM(DistantLight);
INSTANTIATE_RECONSTRUCT_PRIM(CylinderLight);
INSTANTIATE_RECONSTRUCT_PRIM(RectLight);
INSTANTIATE_RECONSTRUCT_PRIM(GeometryLight);
INSTANTIATE_RECONSTRUCT_PRIM(DomeLight_1);
INSTANTIATE_RECONSTRUCT_PRIM(LightFilter);
INSTANTIATE_RECONSTRUCT_PRIM(PluginLightFilter);
INSTANTIATE_RECONSTRUCT_PRIM(SkelRoot);
INSTANTIATE_RECONSTRUCT_PRIM(SkelAnimation);
INSTANTIATE_RECONSTRUCT_PRIM(Skeleton);
INSTANTIATE_RECONSTRUCT_PRIM(BlendShape);
INSTANTIATE_RECONSTRUCT_PRIM(Material);
INSTANTIATE_RECONSTRUCT_PRIM(Shader);
INSTANTIATE_RECONSTRUCT_PRIM(NodeGraph);
// UsdPhysics + mjcPhysics
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsJoint);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsScene);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsRevoluteJoint);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsPrismaticJoint);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsSphericalJoint);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsFixedJoint);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsDistanceJoint);
INSTANTIATE_RECONSTRUCT_PRIM(PhysicsCollisionGroup);
INSTANTIATE_RECONSTRUCT_PRIM(MjcActuator);
INSTANTIATE_RECONSTRUCT_PRIM(MjcTendon);
INSTANTIATE_RECONSTRUCT_PRIM(MjcKeyframe);
// AR/Interactive (Apple Preliminary_*)
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_PhysicsGravitationalForce);
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_InfiniteColliderPlane);
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_ReferenceImage);
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_Behavior);
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_Trigger);
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_Action);
INSTANTIATE_RECONSTRUCT_PRIM(Preliminary_Text);
// usdMedia
INSTANTIATE_RECONSTRUCT_PRIM(SpatialAudio);

#undef INSTANTIATE_RECONSTRUCT_PRIM

bool USDCReader::Impl::ReconstrcutStageMeta(
    const crate::FieldValuePairVector &fvs, StageMetas *metas) {

  std::vector<std::string> subLayers;
  std::vector<LayerOffset> subLayerOffsets;

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
      DCOUT("upAxis = " << to_string(metas->upAxis.get_value()));

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
      DCOUT("metersPerUnit = " << metas->metersPerUnit.get_value());
    } else if (fv.first == "kilogramsPerUnit") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->kilogramsPerUnit = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->kilogramsPerUnit = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`kilogramsPerUnit` value must be double or float type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("kilogramsPerUnit = " << metas->kilogramsPerUnit.get_value());
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
      DCOUT("timeCodesPerSecond = " << metas->timeCodesPerSecond.get_value());
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
      DCOUT("startimeCode = " << metas->startTimeCode.get_value());
    } else if (fv.first == "subLayers") {
      if (auto vs = fv.second.get_value<std::vector<std::string>>()) {
        subLayers = vs.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`subLayers` value must be string[] "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
    } else if (fv.first == "subLayerOffsets") {
      if (auto vs = fv.second.get_value<std::vector<LayerOffset>>()) {
        subLayerOffsets = vs.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`subLayerOffsets` value must be LayerOffset[] "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
    } else if (fv.first == "endTimeCode") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->endTimeCode = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->endTimeCode = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`endTimeCode` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("endTimeCode = " << metas->endTimeCode.get_value());
    } else if (fv.first == "framesPerSecond") {
      if (auto vf = fv.second.get_value<float>()) {
        metas->framesPerSecond = double(vf.value());
      } else if (auto vd = fv.second.get_value<double>()) {
        metas->framesPerSecond = vd.value();
      } else {
        PUSH_ERROR_AND_RETURN(
            "`framesPerSecond` value must be double or float "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("framesPerSecond = " << metas->framesPerSecond.get_value());
    } else if (fv.first == "autoPlay") {
      if (auto vf = fv.second.get_value<bool>()) {
        metas->autoPlay = vf.value();
      } else if (auto vs = fv.second.get_value<std::string>()) {
        bool autoPlay{true};
        if (vs.value() == "true") {
          autoPlay = true;
        } else if (vs.value() == "false") {
          autoPlay = false;
        } else {
          PUSH_ERROR_AND_RETURN(
              "Unsupported value for `autoPlay`: " << vs.value());
        }
        metas->autoPlay = autoPlay;
      } else {
        PUSH_ERROR_AND_RETURN(
            "`autoPlay` value must be bool "
            "type or string type, but got '" +
            fv.second.type_name() + "'");
      }
      DCOUT("autoPlay = " << metas->autoPlay.get_value());
    } else if (fv.first == "playbackMode") {
      if (auto vf = fv.second.get_value<value::token>()) {
        if (vf.value().str() == "none") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeNone;
        } else if (vf.value().str() == "loop") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeLoop;
        } else {
          PUSH_ERROR_AND_RETURN("Unsupported token value for `playbackMode`.");
        }
      } else if (auto vs = fv.second.get_value<std::string>()) {
        std::string val = vs.value();
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
          val = val.substr(1, val.size() - 2);
        }
        if (val == "none") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeNone;
        } else if (val == "loop") {
          metas->playbackMode = StageMetas::PlaybackMode::PlaybackModeLoop;
        } else {
          PUSH_ERROR_AND_RETURN(
              "Unsupported value for `playbackMode`: " << val);
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "`playbackMode` value must be token "
            "type, but got '" +
            fv.second.type_name() + "'");
      }
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
        metas->customLayerDataAuthored = true;
      } else {
        PUSH_ERROR_AND_RETURN(
            "customLayerData must be `dictionary` type, but got type `" +
            fv.second.type_name());
      }
    } else if (fv.first == "primChildren") {  // only appears in USDC.
      auto v = fv.second.get_value<std::vector<value::token>>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("Type must be `token[]` for `primChildren`, but got " +
                   fv.second.type_name());
      }

      metas->primChildren = v.value();
    } else if (fv.first == "documentation") {  // 'doc'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("Type must be `string` for `documentation`, but got " +
                   fv.second.type_name());
      }
      value::StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->doc = sdata;
      DCOUT("doc = " << metas->doc.value);
    } else if (fv.first == "comment") {  // 'comment'
      auto v = fv.second.get_value<std::string>();
      if (!v) {
        PUSH_ERROR_AND_RETURN("Type must be `string` for `comment`, but got " +
                   fv.second.type_name());
      }
      value::StringData sdata;
      sdata.value = v.value();
      sdata.is_triple_quoted = hasNewline(sdata.value);
      metas->comment = sdata;
      DCOUT("comment = " << metas->comment.value);
    } else {
      PUSH_WARN("[StageMeta] TODO: " + fv.first);
    }
  }

  if (subLayers.size()) {
    std::vector<SubLayer> dst;
    for (size_t i = 0; i < subLayers.size(); i++) {
      SubLayer s;
      s.assetPath = subLayers[i];
      dst.push_back(s);
    }

    if (subLayers.size() == subLayerOffsets.size()) {
      for (size_t i = 0; i < subLayerOffsets.size(); i++) {
        dst[i].layerOffset = subLayerOffsets[i];
      }
    }

    metas->subLayers = dst;

  } else if (subLayerOffsets.size()) {
    PUSH_WARN("Corrupted subLayer info? `subLayers` Fileld not found.");
  }

  return true;
}

std::unique_ptr<Prim> USDCReader::Impl::ReconstructPrimFromTypeName(
    const std::string &typeName, // TinyUSDZ's Prim type name
    const std::string &primTypeName, // USD's Prim typeName
    const std::string &prim_name,
    const crate::CrateReader::Node &node, const Specifier spec,
    const std::vector<value::token> &primChildren,
    const std::vector<value::token> &properties,
    const PathIndexToSpecIndexMap &psmap, const PrimMeta &meta, bool *is_unsupported_prim) {

  if (is_unsupported_prim) {
    (*is_unsupported_prim) = false; // init with false
  }


#define RECONSTRUCT_PRIM(__primty, __node_ty, __prim_name, __spec) \
  if (__node_ty == value::TypeTraits<__primty>::type_name()) {     \
    __primty typed_prim;                                           \
    if (!ReconstructPrim(__spec, node, psmap, &typed_prim)) {         \
      PUSH_ERROR("Failed to reconstruct Prim " << __node_ty << " elementName: " << __prim_name);      \
      return nullptr;                                              \
    }                                                              \
    typed_prim.meta = meta;                                        \
    typed_prim.name = __prim_name;                                 \
    typed_prim.spec = __spec;                                      \
    typed_prim.propertyNames() = properties; \
    typed_prim.primChildrenNames() = primChildren; \
    value::Value primdata(std::move(typed_prim));                            \
    auto result = std::unique_ptr<Prim>(new Prim(__prim_name, std::move(primdata)));  \
    result->prim_type_name() = primTypeName; \
    /* also add primChildren to Prim */ \
    result->metas().primChildren = primChildren; \
    return result; \
  } else

  if (typeName == "Model" || typeName == "__AnyType__") {
    Model typed_prim;
    if (!ReconstructPrim(spec, node, psmap, &typed_prim)) {
      PUSH_ERROR("Failed to reconstruct Model");
      return nullptr;
    }
    typed_prim.meta = meta;
    typed_prim.name = prim_name;
    if (typeName == "__AnyType__") {
      typed_prim.prim_type_name = "";
    } else {
      typed_prim.prim_type_name = primTypeName;
    }
    typed_prim.spec = spec;
    typed_prim.propertyNames() = properties;
    typed_prim.primChildrenNames() = primChildren;
    value::Value primdata(std::move(typed_prim));
    auto result = std::unique_ptr<Prim>(new Prim(prim_name, std::move(primdata)));
    result->prim_type_name() = primTypeName;
    result->metas().primChildren = primChildren;
    return result;
  } else

  RECONSTRUCT_PRIM(Xform, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Model, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Scope, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomMesh, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomPoints, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCylinder, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCube, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCone, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomSphere, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCapsule, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomBasisCurves, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomNurbsCurves, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomPlane, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCylinder_1, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCapsule_1, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomTetMesh, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomNurbsPatch, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomHermiteCurves, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomPointInstancer, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomCamera, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeomSubset, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(SphereLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DomeLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(CylinderLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DiskLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DistantLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(RectLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(GeometryLight, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(DomeLight_1, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(LightFilter, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PluginLightFilter, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(SkelRoot, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Skeleton, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(SkelAnimation, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(BlendShape, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Shader, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(NodeGraph, typeName, prim_name, spec)
  // UsdPhysics + mjcPhysics
  RECONSTRUCT_PRIM(PhysicsJoint, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsScene, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsRevoluteJoint, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsPrismaticJoint, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsSphericalJoint, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsFixedJoint, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsDistanceJoint, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(PhysicsCollisionGroup, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(MjcActuator, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(MjcTendon, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(MjcKeyframe, typeName, prim_name, spec)
  // AR/Interactive (Apple Preliminary_*)
  RECONSTRUCT_PRIM(Preliminary_PhysicsGravitationalForce, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Preliminary_InfiniteColliderPlane, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Preliminary_ReferenceImage, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Preliminary_Behavior, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Preliminary_Trigger, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Preliminary_Action, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Preliminary_Text, typeName, prim_name, spec)
  // usdMedia
  RECONSTRUCT_PRIM(SpatialAudio, typeName, prim_name, spec)
  RECONSTRUCT_PRIM(Material, typeName, prim_name, spec) {
    PUSH_WARN("TODO or unsupported prim type: " << typeName);
    if (is_unsupported_prim) {
      (*is_unsupported_prim) = true;
    }
    return nullptr;
  }

#undef RECONSTRUCT_PRIM
}


bool USDCReader::Impl::ParsePrimSpec(const crate::FieldValuePairVector &fvs,
                                     nonstd::optional<std::string> &typeName,
                                     nonstd::optional<Specifier> &specifier,
                                     std::vector<value::token> &primChildren,
                                     std::vector<value::token> &properties,
                                     PrimMeta &primMeta) {
  // Fields for Prim and Prim metas.
  for (const auto &fv : fvs) {
    if (fv.first == "typeName") {
      if (auto pv = fv.second.as<value::token>()) {
        typeName = pv->str();
        DCOUT("typeName = " << typeName.value());
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`typeName` must be type `token`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "specifier") {
      if (auto pv = fv.second.as<Specifier>()) {
        specifier = (*pv);
        DCOUT("specifier = " << to_string(specifier.value()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`specifier` must be type `Specifier`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "properties") {
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        properties = (*pv);
        DCOUT("properties = " << properties);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`properties` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "primChildren") {
      // Crate only
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        primChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`primChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "active") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.set_active(*pv);
        DCOUT("active = " << to_string(primMeta.get_active()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`active` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "hidden") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.set_hidden(*pv);
        DCOUT("hidden = " << to_string(primMeta.get_hidden()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`hidden` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "instanceable") {
      if (auto pv = fv.second.as<bool>()) {
        primMeta.set_instanceable(*pv);
        DCOUT("instanceable = " << to_string(primMeta.get_instanceable()));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`instanceable` must be type `bool`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "assetInfo") {
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_assetInfo(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`assetInfo` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "clips") {
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_clips(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`clips` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "kind") {
      if (auto pv = fv.second.as<value::token>()) {

          const value::token tok = (*pv);
          if (tok.str() == "subcomponent") {
            primMeta.set_kind(Kind::Subcomponent);
          } else if (tok.str() == "component") {
            primMeta.set_kind(Kind::Component);
          } else if (tok.str() == "model") {
            primMeta.set_kind(Kind::Model);
          } else if (tok.str() == "group") {
            primMeta.set_kind(Kind::Group);
          } else if (tok.str() == "assembly") {
            primMeta.set_kind(Kind::Assembly);
          } else if (tok.str() == "sceneLibrary") {
            primMeta.set_kind(Kind::SceneLibrary);
          } else {
            primMeta.set_kind(tok.str());
          }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "`kind` must be type `token`, but got type `"
                                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "apiSchemas") {
      if (auto pv = fv.second.as<ListOp<value::token>>()) {
        auto listop = (*pv);

        std::string warn;
        auto ret = ToAPISchemas(listop, _config.allow_unknown_apiSchemas, warn);
        if (!ret) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, "Failed to validate `apiSchemas`: " + ret.error());
        } else {
          if (warn.size()) {
            PUSH_WARN(warn);
          }
          primMeta.set_apiSchemas(*ret);
        }
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`apiSchemas` must be type `ListOp[Token]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "documentation") {
      if (auto pv = fv.second.as<std::string>()) {
        value::StringData s;
        s.value = unwrap(*pv);
        s.is_triple_quoted = hasNewline(s.value);
        primMeta.set_doc(s);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`documentation` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "comment") {
      if (auto pv = fv.second.as<std::string>()) {
        value::StringData s;
        s.value = unwrap(*pv);
        s.is_triple_quoted = hasNewline(s.value);
        primMeta.set_comment(s);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`comment` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "sdrMetadata") {
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_sdrMetadata(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`sdrMetadata` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "customData") {
      if (auto pv = fv.second.as<CustomDataType>()) {
        primMeta.set_customData(*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`customData` must be type `dictionary`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "variantSelection") {
      if (auto pv = fv.second.as<VariantSelectionMap>()) {
        primMeta.variants = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantSelection` must be type `variants`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "variantChildren") {
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        primMeta.variantChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "variantSetChildren") {
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        primMeta.variantSetChildren = (*pv);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantSetChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "variantSetNames") {
      if (auto pv = fv.second.as<ListOp<std::string>>()) {
        const ListOp<std::string> &p = *pv;
        DCOUT("variantSetNames = " << to_string(p));

        auto ps = DecodeListOp<std::string>(p);
        primMeta.variantSets = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`variantSetNames` must be type `ListOp[String]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "sceneName") {  // USDZ extension
      if (auto pv = fv.second.as<std::string>()) {
        primMeta.set_sceneName(unwrap(*pv));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`sceneName` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "displayName") {  // USD supported since 23.xx?
      if (auto pv = fv.second.as<std::string>()) {
        primMeta.set_displayName(unwrap(*pv));
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`displayName` must be type `string`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "inherits") {  // `inherits` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        (void)pvb;
        primMeta.inherits = std::vector<std::pair<ListEditQual, std::vector<Path>>>();
        primMeta.inherits->push_back(
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Path>()));
      } else if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("inherits = " << to_string(p));
        auto ps = DecodeListOp<Path>(p);
        primMeta.inherits = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`inherits` must be type `path` o `path[]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (fv.first == "references") {  // `references` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        (void)pvb;
        primMeta.references = std::vector<std::pair<ListEditQual, std::vector<Reference>>>();
        primMeta.references->push_back(
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Reference>()));
      } else if (auto pv = fv.second.as<ListOp<Reference>>()) {
        const ListOp<Reference> &p = *pv;
        DCOUT("references = " << to_string(p));
        auto ps = DecodeListOp<Reference>(p);
        primMeta.references = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`references` must be type `ListOp[Reference]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "payload") {  // `payload` composition
      if (auto pvb = fv.second.as<value::ValueBlock>()) {
        (void)pvb;
        primMeta.payload = std::vector<std::pair<ListEditQual, std::vector<Payload>>>();
        primMeta.payload->push_back(
            std::make_pair(ListEditQual::ResetToExplicit, std::vector<Payload>()));
      } else if (auto pv = fv.second.as<Payload>()) {
        std::vector<Payload> pls;
        pls.push_back(*pv);
        primMeta.payload = std::vector<std::pair<ListEditQual, std::vector<Payload>>>();
        primMeta.payload->push_back(std::make_pair(ListEditQual::ResetToExplicit, pls));
      } else if (auto pvs = fv.second.as<ListOp<Payload>>()) {
        const ListOp<Payload> &p = *pvs;
        DCOUT("payload = " << to_string(p));
        auto ps = DecodeListOp<Payload>(p);
        primMeta.payload = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag,
            "`payload` must be type `ListOp[Payload]`, but got type `"
                << fv.second.type_name() << "`");
      }
    } else if (fv.first == "specializes") {  // `specializes` composition
      if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("specializes = " << to_string(p));
        auto ps = DecodeListOp<Path>(p);
        primMeta.specializes = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`specializes` must be type `ListOp[Path]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else if (fv.first == "inheritPaths") {  // `inherits` composition (alternate field name)
      if (auto pv = fv.second.as<ListOp<Path>>()) {
        const ListOp<Path> &p = *pv;
        DCOUT("inheritPaths = " << to_string(p));
        auto ps = DecodeListOp<Path>(p);
        primMeta.inherits = ps;
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`inheritPaths` must be type `ListOp[Path]`, but got type `"
                      << fv.second.type_name() << "`");
      }

    } else if (endsWith(fv.first, ".targetPaths")) {
      if (auto pv = fv.second.as<std::vector<Path>>()) {
        DCOUT("Relationship " << fv.first << " = " << to_string(*pv));
        primMeta.unregisteredMetas[fv.first] = to_string(*pv);
      } else {
        PUSH_WARN("Relationship targetPaths field `" << fv.first << "` is not Path vector type (got " << fv.second.type_name() << "). Ignoring.");
      }
    } else {
      if (auto pv = fv.second.as<std::string>()) {
        primMeta.unregisteredMetas[fv.first] = (*pv);
      } else if (auto ptv = fv.second.as<value::token>()) {
        primMeta.unregisteredMetas[fv.first] = quote((*ptv).str());
      } else {
        DCOUT("PrimProp TODO: " << fv.first);
        PUSH_WARN("PrimProp TODO: " << fv.first);
      }
    }
  }

  return true;
}

bool USDCReader::Impl::ParseVariantSetFields(
    const crate::FieldValuePairVector &fvs,
    std::vector<value::token> &variantChildren) {
  for (const auto &fv : fvs) {
    if (fv.first == "variantChildren") {
      if (auto pv = fv.second.as<std::vector<value::token>>()) {
        variantChildren = (*pv);
        DCOUT("variantChildren: " << variantChildren);
      } else {
        PUSH_ERROR_AND_RETURN_TAG(
            kTag, "`variantChildren` must be type `token[]`, but got type `"
                      << fv.second.type_name() << "`");
      }
    } else {
      DCOUT("Unknown/invalid field in VariantSet: " << fv.first);
      PUSH_WARN("Ignoreing unknown/invalid field in VariantSet: " << fv.first);
    }
  }

  return true;
}

bool USDCReader::Impl::ParseCommonPrimFields(
    const crate::FieldValuePairVector &fvs,
    int current,
    Specifier default_spec,
    PrimFieldsResult *result) {

  if (!result) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "(Internal error) null PrimFieldsResult.");
  }

  // 1. Parse fields from the spec
  if (!ParsePrimSpec(fvs, result->typeName, result->specifier,
                     result->primChildren, result->properties,
                     result->primMeta)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse Prim fields.");
  }

  // 2. Resolve element path
  if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
    DCOUT(fmt::format("Element path: {}", pv.value().full_path_name()));
    result->elemPath = pv.value();
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kTag,
                              "(Internal errror) Element path not found.");
  }

  // 3. Validate / default specifier
  if (result->specifier) {
    if (result->specifier.value() == Specifier::Def) {
      // ok
    } else if (result->specifier.value() == Specifier::Class) {
      // ok
    } else if (result->specifier.value() == Specifier::Over) {
      // ok
    } else {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Specifier.");
    }
  } else {
    result->specifier = default_spec;
  }

  // 4. Determine TinyUSDZ prim type name
  if (!result->typeName) {
    result->pTyName = "Model";
  } else {
    result->pTyName = result->typeName.value();
  }

  // 5. Derive prim_name and primTypeName from element path
  result->prim_name = result->elemPath.prim_part();
  result->primTypeName = result->typeName.has_value()
                             ? result->typeName.value()
                             : "";

  // __AnyType__ normalisation
  if (result->typeName.has_value() &&
      result->typeName.value() == "__AnyType__") {
    result->primTypeName = "";
  }

  return true;
}

}  // namespace usdc
}  // namespace tinyusdz

#endif  // !TINYUSDZ_DISABLE_MODULE_USDC_READER
