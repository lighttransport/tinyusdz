// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.
//
// PrimSpec→Prim reconstruction, Stage building, variant extraction/selection.
// Split from composition.cc.

#include "composition.hh"

#include "common-macros.inc"
#include "layer.hh"
#include "prim-pprint.hh"
#include "prim-reconstruct.hh"
#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "stage.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "core/model-scope.hh"  // Model, Scope
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "value-types.hh"

#define PushError(s) \
  if (err) {         \
    (*err) += s;     \
  }

#define PushWarn(s) \
  if (warn) {       \
    (*warn) += s;   \
  }

namespace tinyusdz {

namespace prim {

// template specialization forward decls.
// implimentations will be located in prim-reconstruct.cc
#define RECONSTRUCT_PRIM_DECL(__ty)                                   \
  template <>                                                         \
  bool ReconstructPrim<__ty>(PrimSpec &, __ty *, std::string *, \
                             std::string *, const PrimReconstructOptions &)

RECONSTRUCT_PRIM_DECL(Xform);
RECONSTRUCT_PRIM_DECL(Model);
RECONSTRUCT_PRIM_DECL(Scope);
RECONSTRUCT_PRIM_DECL(GeomPoints);
RECONSTRUCT_PRIM_DECL(GeomMesh);
RECONSTRUCT_PRIM_DECL(GeomCapsule);
RECONSTRUCT_PRIM_DECL(GeomCube);
RECONSTRUCT_PRIM_DECL(GeomCone);
RECONSTRUCT_PRIM_DECL(GeomCylinder);
RECONSTRUCT_PRIM_DECL(GeomSphere);
RECONSTRUCT_PRIM_DECL(GeomBasisCurves);
RECONSTRUCT_PRIM_DECL(GeomCamera);
RECONSTRUCT_PRIM_DECL(GeomSubset);
RECONSTRUCT_PRIM_DECL(SphereLight);
RECONSTRUCT_PRIM_DECL(DomeLight);
RECONSTRUCT_PRIM_DECL(DiskLight);
RECONSTRUCT_PRIM_DECL(DistantLight);
RECONSTRUCT_PRIM_DECL(RectLight);
RECONSTRUCT_PRIM_DECL(CylinderLight);
RECONSTRUCT_PRIM_DECL(SkelRoot);
RECONSTRUCT_PRIM_DECL(SkelAnimation);
RECONSTRUCT_PRIM_DECL(Skeleton);
RECONSTRUCT_PRIM_DECL(BlendShape);
RECONSTRUCT_PRIM_DECL(Material);
RECONSTRUCT_PRIM_DECL(Shader);
RECONSTRUCT_PRIM_DECL(NodeGraph);

#undef RECONSTRUCT_PRIM_DECL

}  // namespace prim

namespace detail {

static nonstd::optional<Prim> ReconstructPrimFromPrimSpec(
    PrimSpec &primspec, std::string *warn, std::string *err) {
  (void)warn;

  // - propertyNames()
  // - primChildrenNames()


#define RECONSTRUCT_PRIM(__primty)                                       \
  if (primspec.typeName() == value::TypeTraits<__primty>::type_name()) { \
    __primty typed_prim;                                                 \
    if (!prim::ReconstructPrim(primspec, &typed_prim, warn, err)) {      \
      PUSH_ERROR("Failed to reconstruct Prim from PrimSpec "             \
                 << primspec.typeName()                                  \
                 << " elementName: " << primspec.name());                \
      return nonstd::nullopt;                                            \
    }                                                                    \
    typed_prim.meta = primspec.metas();                                  \
    typed_prim.name = primspec.name();                                   \
    typed_prim.spec = primspec.specifier();                              \
    /*typed_prim.propertyNames() = properties; */                        \
    /*typed_prim.primChildrenNames() = primChildren;*/                   \
    value::Value primdata = typed_prim;                                  \
    Prim prim(primspec.name(), primdata);                                \
    prim.prim_type_name() = primspec.typeName();                         \
    /* also add primChildren to Prim */                                  \
    /* prim.metas().primChildren = primChildren; */                      \
    return std::move(prim);                                              \
  } else

  if (primspec.typeName().empty() || primspec.typeName() == "Model") {
    // Code is mostly identical to RECONSTRUCT_PRIM.
    // Difference is store primTypeName to Model class itself.
    Model typed_prim;
    if (!prim::ReconstructPrim(primspec, &typed_prim, warn, err)) {
      PUSH_ERROR("Failed to reconstruct Model");
      return nonstd::nullopt;
    }
    typed_prim.meta = primspec.metas();
    typed_prim.name = primspec.name();
    typed_prim.prim_type_name = primspec.typeName();
    typed_prim.spec = primspec.specifier();
    // typed_prim.propertyNames() = properties;
    // typed_prim.primChildrenNames() = primChildren;
    value::Value primdata = typed_prim;
    Prim prim(primspec.name(), primdata);
    prim.prim_type_name() = primspec.typeName();
    /* also add primChildren to Prim */
    // prim.metas().primChildren = primChildren;
    return std::move(prim);
  } else

    RECONSTRUCT_PRIM(Xform)
  RECONSTRUCT_PRIM(Model)
  RECONSTRUCT_PRIM(Scope)
  RECONSTRUCT_PRIM(GeomMesh)
  RECONSTRUCT_PRIM(GeomPoints)
  RECONSTRUCT_PRIM(GeomCylinder)
  RECONSTRUCT_PRIM(GeomCube)
  RECONSTRUCT_PRIM(GeomCone)
  RECONSTRUCT_PRIM(GeomSphere)
  RECONSTRUCT_PRIM(GeomCapsule)
  RECONSTRUCT_PRIM(GeomBasisCurves)
  RECONSTRUCT_PRIM(GeomCamera)
  RECONSTRUCT_PRIM(GeomSubset)
  RECONSTRUCT_PRIM(SphereLight)
  RECONSTRUCT_PRIM(DomeLight)
  RECONSTRUCT_PRIM(CylinderLight)
  RECONSTRUCT_PRIM(DiskLight)
  RECONSTRUCT_PRIM(DistantLight)
  RECONSTRUCT_PRIM(RectLight)
  RECONSTRUCT_PRIM(SkelRoot)
  RECONSTRUCT_PRIM(Skeleton)
  RECONSTRUCT_PRIM(SkelAnimation)
  RECONSTRUCT_PRIM(BlendShape)
  RECONSTRUCT_PRIM(Shader)
  RECONSTRUCT_PRIM(NodeGraph)
  RECONSTRUCT_PRIM(Material) {
    PUSH_WARN("TODO or unsupported prim type: " << primspec.typeName());
    return nonstd::nullopt;
  }

#undef RECONSTRUCT_PRIM
}

static nonstd::optional<Prim> ReconstructPrimFromPrimSpecRec(
    PrimSpec &primspec, std::string *warn, std::string *err,
    uint32_t depth = 0) {

  if (size_t(depth) > kMaxDefaultTraversalLimit) {
    if (err) {
      (*err) += "ReconstructPrimFromPrimSpecRec: recursion too deep.\n";
    }
    return nonstd::nullopt;
  }

  auto pprim = ReconstructPrimFromPrimSpec(primspec, warn, err);

  if (pprim) {
    for (size_t i = 0; i < primspec.children().size(); i++) {
      if (auto pv = ReconstructPrimFromPrimSpecRec(primspec.children()[i], warn, err, depth + 1)) {
        pprim.value().children().emplace_back(std::move(pv.value()));
      }
    }
  }

  return pprim;
}

}  // namespace detail

bool LayerToStage(Layer &&layer, Stage *stage_out, std::string *warn,
                  std::string *err) {
  if (!stage_out) {
    if (err) {
      (*err) += "`stage_ptr` is nullptr.";
    }
    return false;
  }

  Stage stage;

  stage.metas() = layer.metas();

  // TODO: primChildren metadatum
  for (auto &primspec : layer.primspecs()) {
    if (auto pv =
            detail::ReconstructPrimFromPrimSpecRec(primspec.second, warn, err)) {
      stage.add_root_prim(std::move(pv.value()));
    }
  }

  (*stage_out) = stage;

  return true;
}

namespace detail {

// In-place conversion helper: Move PrimSpec data to Prim and free source memory
static std::unique_ptr<Prim> ReconstructPrimFromPrimSpecInPlace(
    std::unique_ptr<PrimSpec> primspec, std::string *warn, std::string *err,
    int depth = 0) {

  if (!primspec) {
    if (err) {
      (*err) += "PrimSpec is null";
    }
    return nullptr;
  }

  if (depth > 4096) {
    if (err) {
      (*err) += "PrimSpec tree too deep (max 4096).";
    }
    return nullptr;
  }

  // First reconstruct the prim normally
  auto prim_opt = ReconstructPrimFromPrimSpec(*primspec, warn, err);

  if (!prim_opt) {
    return nullptr;
  }

  auto result = std::make_unique<Prim>(std::move(prim_opt.value()));

  // Now we can clear the primspec data to free memory
  // The data has been copied to the Prim, so we can safely clear it

  // Clear properties (these can be large)
  primspec->props().clear();

  // Clear metadata
  primspec->metas() = PrimMeta();

  // Clear variant sets
  primspec->variantSets().clear();

  // Process children recursively
  for (auto& child : primspec->children()) {
    auto child_ptr = std::make_unique<PrimSpec>(std::move(child));
    if (auto child_prim = ReconstructPrimFromPrimSpecInPlace(std::move(child_ptr), warn, err, depth + 1)) {
      result->children().emplace_back(std::move(*child_prim));
    }
  }

  // Clear children vector
  primspec->children().clear();
  primspec->children().shrink_to_fit();

  return result;
}

} // namespace detail

bool LayerToStageInPlace(std::unique_ptr<Layer> layer, Stage *stage_out,
                         std::string *warn, std::string *err) {
  if (!stage_out) {
    if (err) {
      (*err) += "`stage_out` is nullptr.";
    }
    return false;
  }

  if (!layer) {
    if (err) {
      (*err) += "`layer` is nullptr.";
    }
    return false;
  }

  Stage stage;

  // Move metadata (cheap operation)
  stage.metas() = std::move(layer->metas());

  // Convert primspecs in-place
  // We need to iterate carefully since we're modifying the map
  auto& primspecs = layer->primspecs();
  std::vector<std::string> paths_to_process;

  for (const auto& item : primspecs) {
    paths_to_process.push_back(item.first);
  }

  for (const auto& path : paths_to_process) {
    auto it = primspecs.find(path);
    if (it != primspecs.end()) {
      // Extract the PrimSpec from the map
      auto primspec_ptr = std::make_unique<PrimSpec>(std::move(it->second));

      // Remove from map immediately to free memory
      primspecs.erase(it);

      // Convert to Prim in-place
      if (auto pv = detail::ReconstructPrimFromPrimSpecInPlace(std::move(primspec_ptr), warn, err)) {
        stage.add_root_prim(std::move(*pv));
      }
    }
  }

  // Clear the layer completely
  layer->primspecs().clear();
  layer.reset();  // Release the Layer object itself

  (*stage_out) = std::move(stage);

  return true;
}

bool PrimSpecToPrimInPlace(std::unique_ptr<PrimSpec> primspec, Prim *prim_out,
                           std::string *warn, std::string *err) {
  if (!prim_out) {
    if (err) {
      (*err) += "`prim_out` is nullptr.";
    }
    return false;
  }

  if (!primspec) {
    if (err) {
      (*err) += "`primspec` is nullptr.";
    }
    return false;
  }

  auto prim = detail::ReconstructPrimFromPrimSpecInPlace(std::move(primspec), warn, err);

  if (!prim) {
    return false;
  }

  (*prim_out) = std::move(*prim);

  return true;
}

namespace {

bool ExtractVariantsRec(uint32_t depth, const std::string &root_path,
                        const PrimSpec &ps, Dictionary &dict,
                        const uint32_t max_depth, std::string *err) {
  if (depth > max_depth) {
    if (err) {
      (*err) += "Too deep\n";
    }
    return false;
  }

  Dictionary variantInfos;

  if (ps.name().empty()) {
    if (err) {
      (*err) += "PrimSpec name is empty.\n";
    }
    return false;
  }

  std::string full_prim_path = root_path + "/" + ps.name();

  if (ps.metas().variantSets) {
    // Collect all variant sets from all listops
    std::vector<std::string> vsets;
    for (const auto &variantSets_op : ps.metas().variantSets.value()) {
      const auto &items = variantSets_op.second;
      for (const auto &vs : items) {
        vsets.push_back(vs);
      }
    }
    MetaVariable var;
    var.set_value(vsets);
    variantInfos["variantSets"] = var;
  }

  if (ps.metas().variants) {
    Dictionary values;

    const VariantSelectionMap &vsmap = ps.metas().variants.value();
    for (const auto &item : vsmap) {
      MetaVariable var;
      var.set_value(item.second);

      values[item.first] = item.second;
    }

    variantInfos["variants"] = values;
  }

  if (variantInfos.size()) {
    dict[full_prim_path] = variantInfos;
  }

  // Traverse children
  for (const auto &child : ps.children()) {
    if (!ExtractVariantsRec(depth + 1, full_prim_path, child, dict, max_depth,
                            err)) {
      return false;
    }
  }

  return true;
}

bool ExtractVariantsRec(uint32_t depth, const std::string &root_path,
                        const Prim &prim, Dictionary &dict,
                        const uint32_t max_depth, std::string *err) {
  if (depth > max_depth) {
    if (err) {
      (*err) += "Too deep\n";
    }
    return false;
  }

  Dictionary variantInfos;

  if (prim.element_name().empty()) {
    if (err) {
      (*err) += "Prim name is empty.\n";
    }
    return false;
  }

  std::string full_prim_path = root_path + "/" + prim.element_name();

  if (prim.metas().variantSets) {
    // Collect all variant sets from all listops
    std::vector<std::string> vsets;
    for (const auto &variantSets_op : prim.metas().variantSets.value()) {
      const auto &items = variantSets_op.second;
      for (const auto &vs : items) {
        vsets.push_back(vs);
      }
    }
    MetaVariable var;
    var.set_value(vsets);
    variantInfos["variantSets"] = var;
  }

  if (prim.metas().variants) {
    Dictionary values;

    const VariantSelectionMap &vsmap = prim.metas().variants.value();
    for (const auto &item : vsmap) {
      MetaVariable var;
      var.set_value(item.second);

      values[item.first] = item.second;
    }

    variantInfos["variants"] = values;
  }

  // variantSetChildren Prim metadataum supercedes Prim's variantSets Stmt
  if (prim.metas().variantSetChildren) {
    const std::vector<value::token> &vsets =
        prim.metas().variantSetChildren.value();
    // to string
    std::vector<std::string> vsetchildren;
    for (const auto &item : vsets) {
      if (!item.valid()) {
        if (err) {
          (*err) += "Invalid variantSetChildren token found.\n";
        }
        return false;
      }
      vsetchildren.push_back(item.str());
    }
    variantInfos["variantSet"] = vsetchildren;
  } else if (prim.variantSets().size()) {
    Dictionary vsetdict;

    for (const auto &item : prim.variantSets()) {
      if (item.second.variantSet.size()) {
        std::vector<std::string> variantStmtNames;

        if (item.second.name.empty()) {
          if (err) {
            (*err) += "Invalid variantSets Statements found.\n";
          }
          return false;
        }

        for (const auto &v : item.second.variantSet) {
          variantStmtNames.push_back(v.first);
        }

        vsetdict[item.first] = variantStmtNames;
      }
    }

    if (vsetdict.size()) {
      variantInfos["variantSet"] = vsetdict;
    }
  }

  if (variantInfos.size()) {
    dict[full_prim_path] = variantInfos;
  }

  // Traverse children
  for (const auto &child : prim.children()) {
    if (!ExtractVariantsRec(depth + 1, full_prim_path, child, dict, max_depth,
                            err)) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool ExtractVariants(const Layer &layer, Dictionary *dict, std::string *err) {
  if (!dict) {
    if (err) {
      (*err) += "`dict` argument is nullptr.\n";
    }

    return false;
  }

  for (const auto &primspec : layer.primspecs()) {
    if (!ExtractVariantsRec(/* depth */ 0, /* root path */ "", primspec.second,
                            (*dict), /* max_depth */ 1024 * 1024, err)) {
      return false;
    }
  }

  return true;
}

bool ExtractVariants(const Stage &stage, Dictionary *dict, std::string *err) {
  if (!dict) {
    if (err) {
      (*err) += "`dict` argument is nullptr.\n";
    }

    return false;
  }

  for (const auto &prim : stage.root_prims()) {
    if (!ExtractVariantsRec(/* depth */ 0, /* root path */ "", prim, (*dict),
                            /* max_depth */ 1024 * 1024, err)) {
      return false;
    }
  }

  return true;
}

bool VariantSelectPrimSpec(
    PrimSpec &dst, const PrimSpec &src,
    const std::map<std::string, std::string> &variant_selection,
    std::string *warn, std::string *err) {
  if (src.metas().variants && src.metas().variantSets) {
    // do variant compsotion
  } else if (src.metas().variants) {
    if (warn) {
      (*warn) +=
          "`variants` are authored, but `variantSets` is not authored.\n";
    }
    dst = src;
    dst.metas().variants.reset();
    dst.metas().variantSets.reset();
    dst.variantSets().clear();
    return true;
  } else if (src.metas().variantSets) {
    if (warn) {
      (*warn) +=
          "`variantSets` are authored, but `variants` is not authored.\n";
    }
    dst = src;
    dst.metas().variants.reset();
    dst.metas().variantSets.reset();
    dst.variantSets().clear();
    // nothing to do.
    return true;
  } else {
    dst = src;
    return true;
  }

  // Collect all variant set names from all listops
  std::vector<std::string> allVariantSetNames;
  for (const auto &variantSets_op : src.metas().variantSets.value()) {
    // TODO: handle different list edit qualifiers appropriately
    const auto &items = variantSets_op.second;
    for (const auto &vs : items) {
      allVariantSetNames.push_back(vs);
    }
  }

  dst = src;

  PrimSpec ps = src;  // temp PrimSpec. Init with src.

  // Evaluate from the last element.
  for (int64_t i = int64_t(allVariantSetNames.size()) - 1; i >= 0; i--) {
    const auto &variantSetName = allVariantSetNames[size_t(i)];

    // 1. look into `variant_selection`.
    // 2. look into variant setting in this PrimSpec.

    std::string variantName;
    if (variant_selection.count(variantSetName)) {
      variantName = variant_selection.at(variantSetName);
    } else if (dst.current_variant_selection(variantSetName, &variantName)) {
      // ok
    } else {
      continue;
    }

    if (dst.variantSets().count(variantSetName)) {
      const auto &vss = dst.variantSets().at(variantSetName);

      if (vss.variantSet.count(variantName)) {
        const PrimSpec &vs = vss.variantSet.at(variantName);

        DCOUT(fmt::format("variantSet[{}] Select variant: {}", variantSetName,
                          variantName));

        //
        // Promote variant content to PrimSpec.
        //

        // over-like operation
        ps.metas().update_from(vs.metas(), /* override_authored */ true);

        for (const auto &prop : vs.props()) {
          DCOUT("prop: " << prop.first);
          // override existing prop
          ps.props()[prop.first] = prop.second;
        }

        for (const auto &child : vs.children()) {
          // Variant child opinions are authored as primspec opinions, so an
          // `over` child in the selected variant must merge into the existing
          // child rather than replace the whole subtree.
          auto it = std::find_if(ps.children().begin(), ps.children().end(),
                                 [&child](const PrimSpec &item) {
                                   return (item.name() == child.name());
                                 });

          if (it != ps.children().end()) {
            // LIVRPS strength: a prim's directly-authored Local opinion (L) is
            // STRONGER than an opinion from a Variant (V). The existing child
            // `*it` is the prim's local opinion (e.g. House/Mesh_A's deep
            // `over .../UnrealShader` that sets sourceAsset=Worn); the variant's
            // `child` is the weaker base (sourceAsset=Clean). OverridePrimSpec(d,
            // s) applies `s` as the STRONGER override onto `d`, so merge the
            // variant `child` as the base and let the local `*it` win -- not the
            // reverse, which would let the variant override the local opinion.
            PrimSpec merged = child;  // variant content = weaker base
            if (!OverridePrimSpec(merged, *it, warn, err)) {
              PUSH_ERROR_AND_RETURN("Failed to override variant child PrimSpec.");
            }
            *it = std::move(merged);
          } else {
            ps.children().push_back(child);
          }
        }

        // - [ ] update `primChildren` and `properties` metadataum if required.
      }
    }
  }

  DCOUT("Variant resolved prim: " << prim::print_primspec(ps));

  // Local properties/metadatum wins against properties/metadataum from Variant
  ps.specifier() = Specifier::Over;
  if (!OverridePrimSpec(dst, ps, warn, err)) {
    PUSH_ERROR_AND_RETURN("Failed to override PrimSpec.");
  }

  dst.metas().variants.reset();
  dst.metas().variantSets.reset();
  dst.variantSets().clear();

  return true;
}

}  // namespace tinyusdz
