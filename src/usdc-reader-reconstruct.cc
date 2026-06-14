// SPDX-License-Identifier: Apache 2.0
// Copyright 2020 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// USDC reader: Prim/PrimSpec node reconstruction and variant handling
// (extracted from usdc-reader.cc)
//

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "usdc-reader-impl.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDC_READER)

namespace tinyusdz {
namespace usdc {

Prim *USDCReader::Impl::ResolveVariantOwnerPrim(int32_t parent_node_id,
                                                int32_t current_node_id,
                                                Prim *current_prim,
                                                Prim *parent_prim,
                                                bool include_current_node) {
  if (include_current_node) {
    if (current_prim) {
      return current_prim;
    }

    auto current_it = _variantPrims.find(current_node_id);
    if (current_it != _variantPrims.end()) {
      return &(current_it->second);
    }
  }

  if (parent_prim) {
    return parent_prim;
  }

  auto parent_it = _variantPrims.find(parent_node_id);
  if (parent_it != _variantPrims.end()) {
    return &(parent_it->second);
  }

  // Fallback for malformed trees.
  if (!include_current_node) {
    if (current_prim) {
      return current_prim;
    }

    auto current_it = _variantPrims.find(current_node_id);
    if (current_it != _variantPrims.end()) {
      return &(current_it->second);
    }
  }

  return nullptr;
}

bool USDCReader::Impl::AttachVariantPrimChildrenToOwner(int32_t owner_node_id,
                                                        Prim *owner_prim) {
  if (!_variantPrimChildren.count(owner_node_id)) {
    return true;
  }

  if (!owner_prim) {
    PUSH_ERROR_AND_RETURN("Internal error: variant owner Prim is null.");
  }

  DCOUT(fmt::format("{} has variant Prim ", owner_prim->element_name()));

  for (const auto &item : _variantPrimChildren.at(owner_node_id)) {
    auto vp_it = _variantPrims.find(item);
    if (vp_it == _variantPrims.end()) {
      PUSH_ERROR_AND_RETURN("Internal error: variant Prim children not found.");
    }

    Prim &vp = vp_it->second;
    DCOUT(fmt::format("  variantPrim name {}", vp.element_name()));

    // element_name contains the full path (e.g., "/A{v=sel}").
    // Extract just the variant element "{v=sel}" for validation.
    std::string ve = vp.element_name();
    {
      auto bp = ve.rfind('{');
      if (bp != std::string::npos) ve = ve.substr(bp);
    }
    if (!is_variantElementName(ve)) {
      PUSH_ERROR_AND_RETURN(
          "Corrupted Crate. Variant Prim has invalid element_name: " + vp.element_name());
    }

    std::array<std::string, 2> toks;
    if (!tokenize_variantElement(ve, &toks)) {
      PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
    }

    std::string variantSetName = toks[0];
    std::string variantName = toks[1];

    VariantSet &vs = owner_prim->variantSets()[variantSetName];
    if (vs.name.empty()) {
      vs.name = variantSetName;
    }

    Variant &variant = vs.variantSet[variantName];
    variant.metas() = vp.metas();
    variant.primChildren() = vp.children();

    // Preserve nested variantSets carried by this variant Prim.
    if (!vp.variantSets().empty()) {
      variant.variantSets() = vp.variantSets();
    }
  }

  return true;
}

bool USDCReader::Impl::IsExpectedNonPrimVariantChild(
    int32_t node_id, const PathIndexToSpecIndexMap &psmap) const {
  if (_variantPrimChildren.count(node_id) || _variantPropChildren.count(node_id)) {
    return true;
  }

  auto psit = psmap.find(uint32_t(node_id));
  if (psit == psmap.end()) {
    return true;  // Some container nodes may not have a direct spec entry.
  }

  if (psit->second >= _specs->size()) {
    return false;
  }

  SpecType st = (*_specs)[psit->second].spec_type;
  return ((st == SpecType::Variant) || (st == SpecType::VariantSet) ||
          (st == SpecType::Attribute) ||
          (st == SpecType::Connection) || (st == SpecType::Relationship) ||
          (st == SpecType::RelationshipTarget));
}

bool USDCReader::Impl::ReconstructPrimNode(int parent, int current, int level,
                                           bool is_parent_variant,
                                           const PathIndexToSpecIndexMap &psmap,
                                           Stage *stage,
                                           std::unique_ptr<Prim> *primOut) {
  (void)level;
  const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];

  DCOUT(fmt::format("parent = {}, curent = {}, is_parent_variant = {}", parent, current, is_parent_variant));

  if (!psmap.count(uint32_t(current))) {
    DCOUT("No specifier assigned to this node: " << current);
    return true;
  }

  uint32_t spec_index = psmap.at(uint32_t(current));
  if (spec_index >= _specs->size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs->size()) + ")");
    return false;
  }

  const crate::Spec &spec = (*_specs)[spec_index];

  DCOUT(pprint::Indent(uint32_t(level))
        << "  specTy = " << to_string(spec.spec_type));
  DCOUT(pprint::Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if ((spec.spec_type == SpecType::Attribute) ||
      (spec.spec_type == SpecType::Relationship)) {
    if (_prim_table.count(parent)) {
      return true;
    }
  }

  crate::FieldValuePairVector decoded_fvs;
  const crate::FieldValuePairVector *fvs_ptr = nullptr;
  if (!ResolveFieldValuePairs(spec, &fvs_ptr, &decoded_fvs)) {
    return false;
  }
  const crate::FieldValuePairVector &fvs = (*fvs_ptr);

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
  for (auto &fv : fvs) {
    DCOUT("parent[" << current << "] level [" << level << "] fv name "
                    << fv.first << "(type = " << fv.second.type_name() << ")");
  }
#endif

  // StageMeta = root only attributes.
  if (current == 0) {
    if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
      DCOUT("Root element path: " << pv.value().full_path_name());
    } else {
      PUSH_ERROR_AND_RETURN("(Internal error). Root Element Path not found.");
    }

    if (spec.spec_type != SpecType::PseudoRoot) {
      PUSH_ERROR_AND_RETURN(
          "SpecTypePseudoRoot expected for root layer(Stage) element.");
    }

    if (!ReconstrcutStageMeta(fvs, &stage->metas())) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct StageMeta.");
    }

    _prim_table.insert(current);

    return true;
  }

  DCOUT("spec.type = " << to_string(spec.spec_type));
  switch (spec.spec_type) {
    case SpecType::PseudoRoot: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "SpecType PseudoRoot in a child node is not supported(yet)");
    }
    case SpecType::Prim: {
      PrimFieldsResult pf;
      DCOUT("== PrimFields begin ==> ");
      if (!ParseCommonPrimFields(fvs, current, Specifier::Over, &pf)) {
        return false;
      }
      DCOUT("<== PrimFields end ===");

      if (!ValidatePrimElementName(pf.prim_name)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Prim name.");
      }

      {
        DCOUT("elemPath.prim_name = " << pf.elemPath.prim_part());

        bool is_unsupported_prim{false};
        auto prim = ReconstructPrimFromTypeName(pf.pTyName, pf.primTypeName, pf.prim_name,
                                                node, pf.specifier.value(), pf.primChildren, pf.properties,
                                                psmap, pf.primMeta, &is_unsupported_prim);

        // Fallback: try to reconstruct as Model if unknown prim type
        if (!prim && _config.allow_unknown_prims && is_unsupported_prim) {
          prim = ReconstructPrimFromTypeName("Model", pf.primTypeName, pf.prim_name,
                                             node, pf.specifier.value(), pf.primChildren, pf.properties,
                                             psmap, pf.primMeta);
        }

        if (!prim) {
          return false;
        }

        prim->element_path() = pf.elemPath;

        if (primOut) {
          *primOut = std::move(prim);
        }

      }

      DCOUT("add prim idx " << current);
      if (_prim_table.count(current)) {
        DCOUT("??? prim idx already set " << current);
      } else {
        _prim_table.insert(current);
      }

      break;
    }
    case SpecType::VariantSet: {
      DCOUT(
          fmt::format("[{}] is a VariantSet node(parent = {}). prim_idx? = {}",
                      current, parent, _prim_table.count(current)));

      Path elemPath;
      std::array<std::string, 2> toks;

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();

        DCOUT(fmt::format("Element path: {}", dump_path(elemPath)));

        // Extract the variant element {name} or {name=sel} from the full path.
        // The path is like "/Prim{varSet}" — extract just "{varSet}".
        std::string fp = elemPath.full_path_name();
        auto brace_pos = fp.rfind('{');
        std::string variant_elem = (brace_pos != std::string::npos)
                                     ? fp.substr(brace_pos)
                                     : fp;

        if (!tokenize_variantElement(variant_elem, &toks)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        if (toks[0].empty()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Empty variantSet name in element path.");
        }

        if (toks[1].size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Element path must not have variant.");
        }

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      bool parent_is_variant = _variantPrims.count(parent) > 0;
      if (!_prim_table.count(parent) && !parent_is_variant) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  fmt::format("Parent Prim or Variant for the VariantSet not found. parent Prim/Variant = {}, isParentVariant = {}, VariantSet = {}", parent, parent_is_variant, elemPath.full_path_name()));
      }


      std::vector<value::token> variantChildren;

      DCOUT("== VariantSetFields begin ==> ");

      if (!ParseVariantSetFields(fvs, variantChildren)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse VariantSet fields.");
        return false;
      }

      DCOUT("<== VariantSetFields end === ");

      if (parent_is_variant) {
        DCOUT("SpecTypeVariantSet: Add nested variantChildren(" << current << ") to parent Variant " << parent);
        if (!AddVariantChildrenToVariantNode(parent, current, /* variantSetName */toks[0], variantChildren)) {
          return false;
        }
      } else {
        DCOUT("SpecTypeVariantSet: Add variantChildren(" << current << ") to parent Prim " << parent);
        if (!AddVariantChildrenToPrimNode(parent, current, /* variantSetName */toks[0], variantChildren)) {
          return false;
        }
      }

      break;
    }
    case SpecType::Variant: {
      DCOUT(fmt::format("[{}] is a Variant node(parent = {}). prim_idx? = {}",
                        current, parent, _prim_table.count(current)));

      PrimFieldsResult pf;
      DCOUT("== VariantFields begin ==> ");
      if (!ParseCommonPrimFields(fvs, current, Specifier::Def, &pf)) {
        return false;
      }
      DCOUT("<== VariantFields end === ");

      std::unique_ptr<Prim> variantPrim;
      {
        DCOUT("elemPath = " << dump_path(pf.elemPath));
        DCOUT("prim_name = " << pf.prim_name);
        if (pf.primMeta.variantSets) {
          DCOUT("primMeta.variantSets = " << pf.primMeta.variantSets.value().second);
        }

        // Extract variant element {name=sel} from the full path.
        // pf.prim_name is like "/Prim{varSet=sel}" — extract "{varSet=sel}".
        std::string variant_elem_str = pf.prim_name;
        {
          auto brace_pos = variant_elem_str.rfind('{');
          if (brace_pos != std::string::npos) {
            variant_elem_str = variant_elem_str.substr(brace_pos);
          }
        }

        std::array<std::string, 2> variantPair;
        if (!tokenize_variantElement(variant_elem_str, &variantPair)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", pf.elemPath));
        }

        std::string variantSetName = variantPair[0];
        std::string variantPrimName = variantPair[1];

        if (!ValidatePrimElementName(variantPrimName)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Prim name in Variant: `{}`",
                                variantPrimName));
        }

        // Fix: Transfer parsed primChildren to primMeta.primChildren
        pf.primMeta.primChildren = pf.primChildren;


        // Helper lambda to setup and store variant prim
        auto setupAndStoreVariantPrim = [&](std::unique_ptr<Prim>& vp) {
          if (vp->metas().variantSets) {
            DCOUT("variantPrim.meta.variantSets = " << vp->metas().variantSets.value().second);
          }
          vp->element_path() = pf.elemPath;
          vp->specifier() = pf.specifier.value();

          DCOUT(fmt::format("parent {} add prim idx {} as variant: ", parent, current));
          if (_variantPrims.count(current)) {
            DCOUT("??? prim idx already set " << current);
          } else {
            _variantPrims.emplace(current, std::move(*vp));
            _variantPrimChildren[parent].push_back(current);
            _variantNameIndex[{variantSetName, variantPrimName}] = current;
          }
        };

        bool is_unsupported_prim{false};
        variantPrim = ReconstructPrimFromTypeName(
            pf.pTyName, pf.primTypeName, variantPrimName, node, pf.specifier.value(), pf.primChildren, pf.properties,
            psmap, pf.primMeta, &is_unsupported_prim);

        if (variantPrim) {
          setupAndStoreVariantPrim(variantPrim);
        } else if (_config.allow_unknown_prims && is_unsupported_prim) {
          variantPrim = ReconstructPrimFromTypeName(
              "Model", pf.primTypeName, variantPrimName, node, pf.specifier.value(), pf.primChildren, pf.properties,
              psmap, pf.primMeta);

          if (variantPrim) {
            setupAndStoreVariantPrim(variantPrim);
          } else {
            return false;
          }
        } else {
          return false;
        }
      }

      break;
    }
    case SpecType::Attribute: {
      if (is_parent_variant) {
        Path path;
        if (!GetPathDirect(spec.path_index, &path)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
        }

        Property prop;
        if (!ParseProperty(spec.spec_type,
                           const_cast<crate::FieldValuePairVector &>(fvs),
                           &prop, _config.use_lazy_property_construction)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    fmt::format("Failed to parse Attribute: {}.",
                                                path.prop_part()));
        }

        _variantProps[current] = {path, prop};
        _variantPropChildren[parent].push_back(current);

        DCOUT(
            fmt::format("parent {} current [{}] Parsed Property/Attribute {} under Variant. PathIndex {}",
                        parent, current, path.prop_part(), spec.path_index));

      } else {
        PUSH_WARN(
            "TODO: SpecTypeAttribute(in conjunction with Class/Over specifier, "
            "or inherited?)");
      }
      break;
    }
    case SpecType::Connection:
    case SpecType::Relationship:
    case SpecType::RelationshipTarget: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, fmt::format("TODO: Unsupported/Unimplemented SpecType: {}.",
                            to_string(spec.spec_type)));
      break;
    }
    case SpecType::Expression:
    case SpecType::Mapper:
    case SpecType::MapperArg: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Unsupported SpecType: {}.",
                                                  to_string(spec.spec_type)));
      break;
    }
    case SpecType::Unknown:
    case SpecType::Invalid: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] Invalid SpecType.");
      break;
    }
  }

  return true;
}

bool USDCReader::Impl::ReconstructPrimSpecNode(int parent, int current, int level,
                                           bool is_parent_variant,
                                           const PathIndexToSpecIndexMap &psmap,
                                           Layer *layer,
                                           std::unique_ptr<PrimSpec> *primOut) {
  (void)level;
  const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];

  if (!psmap.count(uint32_t(current))) {
    DCOUT("No specifier assigned to this node: " << current);
    return true;
  }

  uint32_t spec_index = psmap.at(uint32_t(current));
  if (spec_index >= _specs->size()) {
    PUSH_ERROR("Invalid specifier id: " + std::to_string(spec_index) +
               ". Must be in range [0, " + std::to_string(_specs->size()) + ")");
    return false;
  }

  const crate::Spec &spec = (*_specs)[spec_index];

  DCOUT(pprint::Indent(uint32_t(level))
        << "  specTy = " << to_string(spec.spec_type));
  DCOUT(pprint::Indent(uint32_t(level))
        << "  fieldSetIndex = " << spec.fieldset_index.value);

  if ((spec.spec_type == SpecType::Attribute) ||
      (spec.spec_type == SpecType::Relationship)) {
    // BuildPropertyMap already captured these during parent reconstruction.
    if (_prim_table.count(parent) || _variantPrimSpecs.count(parent)) {
      return true;
    }
  }

  crate::FieldValuePairVector decoded_fvs;
  const crate::FieldValuePairVector *fvs_ptr = nullptr;
  if (!ResolveFieldValuePairs(spec, &fvs_ptr, &decoded_fvs)) {
    return false;
  }
  const crate::FieldValuePairVector &fvs = (*fvs_ptr);

  if (fvs.size() > _config.kMaxFieldValuePairs) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Too much FieldValue pairs.");
  }

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
  for (auto &fv : fvs) {
    DCOUT("parent[" << current << "] level [" << level << "] fv name "
                    << fv.first << "(type = " << fv.second.type_name() << ")");
  }
#endif

  if (current == 0) {
    if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
      DCOUT("Root element path: " << pv.value().full_path_name());
    } else {
      PUSH_ERROR_AND_RETURN("(Internal error). Root Element Path not found.");
    }

    if (spec.spec_type != SpecType::PseudoRoot) {
      PUSH_ERROR_AND_RETURN(
          "SpecTypePseudoRoot expected for root layer(Stage) element.");
    }

    if (!ReconstrcutStageMeta(fvs, &layer->metas())) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct StageMeta.");
    }

    _prim_table.insert(current);

    return true;
  }

  switch (spec.spec_type) {
    case SpecType::PseudoRoot: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, "SpecType PseudoRoot in a child node is not supported(yet)");
    }
    case SpecType::Prim: {
      PrimFieldsResult pf;
      DCOUT("== PrimFields begin ==> ");
      if (!ParseCommonPrimFields(fvs, current, Specifier::Over, &pf)) {
        return false;
      }
      DCOUT("<== PrimFields end ===");

      if (!ValidatePrimElementName(pf.prim_name)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid Prim name.");
      }

      {
        DCOUT("elemPath.prim_name = " << pf.elemPath.prim_part());

        auto primspec = std::make_unique<PrimSpec>();

        primspec->typeName() = pf.primTypeName;
        primspec->name() = pf.prim_name;
        // Carry the authored specifier (def/over/class). Without this the
        // PrimSpec defaults to `def`, silently promoting an authored `over`
        // (e.g. a variant's pure-override child) to `def` on flatten.
        primspec->specifier() = pf.specifier.value();

        prim::PropertyMap props;
        if (!BuildPropertyMap(node.GetChildren(), psmap, &props)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
        }
        primspec->props() = std::move(props);
        primspec->metas() = std::move(pf.primMeta);

        if (primOut) {
          *primOut = std::move(primspec);
        }
      }

      DCOUT("add prim idx " << current);
      if (_prim_table.count(current)) {
        DCOUT("??? prim idx already set " << current);
      } else {
        _prim_table.insert(current);
      }

      break;
    }
    case SpecType::VariantSet: {

      // Parent can be a Prim (in _prim_table) or a Variant (in
      // _variantPrimSpecs) for nested variant sets.
      if (!_prim_table.count(parent) && !_variantPrimSpecs.count(parent)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "Parent Prim/Variant for this VariantSet not found.");
      }

      DCOUT(
          fmt::format("[{}] is a VariantSet node(parent = {}). prim_idx? = {}",
                      current, parent, _prim_table.count(current)));

      Path elemPath;
      std::array<std::string, 2> toks;

      if (const auto &pv = GetElemPath(crate::Index(uint32_t(current)))) {
        elemPath = pv.value();

        DCOUT(fmt::format("Element path: {}", dump_path(elemPath)));

        // Extract variant element {name} from the full path.
        std::string fp2 = elemPath.full_path_name();
        auto brace_pos2 = fp2.rfind('{');
        std::string variant_elem2 = (brace_pos2 != std::string::npos)
                                      ? fp2.substr(brace_pos2)
                                      : fp2;

        if (!tokenize_variantElement(variant_elem2, &toks)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", elemPath));
        }

        if (toks[0].empty()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Invalid Element path.");
        }

        if (toks[1].size()) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    "Invalid Element path.");
        }

      } else {
        PUSH_ERROR_AND_RETURN_TAG(kTag,
                                  "(Internal errror) Element path not found.");
      }

      std::vector<value::token> variantChildren;

      DCOUT("== VariantSetFields begin ==> ");

      if (!ParseVariantSetFields(fvs, variantChildren)) {
        PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to parse VariantSet fields.");
        return false;
      }

      DCOUT("<== VariantSetFields end === ");

      DCOUT("Add variantChildren(" << current << ") to parent Prim " << parent);

      if (!AddVariantChildrenToPrimNode(parent, current, /* variantSetName */toks[0], variantChildren)) {
        return false;
      }

      break;
    }
    case SpecType::Variant: {

      DCOUT(fmt::format("[{}] is a Variant node(parent = {}). prim_idx? = {}",
                        current, parent, _prim_table.count(current)));

      PrimFieldsResult pf;
      DCOUT("== VariantFields begin ==> ");
      if (!ParseCommonPrimFields(fvs, current, Specifier::Def, &pf)) {
        return false;
      }
      DCOUT("<== VariantFields end === ");

      {
        DCOUT("elemPath = " << dump_path(pf.elemPath));
        DCOUT("prim_name = " << pf.prim_name);

        // Extract variant element {name=sel} from the full path.
        // pf.prim_name is like "/Prim{varSet=sel}" — extract "{varSet=sel}".
        std::string variant_elem_str = pf.prim_name;
        {
          auto brace_pos = variant_elem_str.rfind('{');
          if (brace_pos != std::string::npos) {
            variant_elem_str = variant_elem_str.substr(brace_pos);
          }
        }

        std::array<std::string, 2> variantPair;
        if (!tokenize_variantElement(variant_elem_str, &variantPair)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Variant ElementPath '{}'.", pf.elemPath));
        }

        std::string variantSetName = variantPair[0];
        std::string variantPrimName = variantPair[1];

        if (!ValidatePrimElementName(variantPrimName)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kTag, fmt::format("Invalid Prim name in Variant: `{}`",
                                variantPrimName));
        }

        PrimSpec variantPrimSpec;
        variantPrimSpec.typeName() = pf.primTypeName;
        // Store the variant element name (e.g., "{varSet=sel}"), not the full
        // path, so that downstream attachment code can parse it correctly.
        variantPrimSpec.name() = variant_elem_str;

        prim::PropertyMap props;
        if (!BuildPropertyMap(node.GetChildren(), psmap, &props)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Failed to build PropertyMap.");
        }
        variantPrimSpec.props() = std::move(props);
        variantPrimSpec.metas() = std::move(pf.primMeta);

        DCOUT(fmt::format("parent {} add primspec idx {} as variant: ", parent, current));
        if (_variantPrimSpecs.count(current)) {
          DCOUT("??? prim idx already set " << current);
        } else {
          _variantPrimSpecs[current] = std::move(variantPrimSpec);
          _variantPrimChildren[parent].push_back(current);
        }
      }

      break;
    }
    case SpecType::Attribute: {
      if (is_parent_variant) {
        Path path;
        if (!GetPathDirect(spec.path_index, &path)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag, "Invalid PathIndex.");
        }

        Property prop;
        if (!ParseProperty(spec.spec_type,
                           const_cast<crate::FieldValuePairVector &>(fvs),
                           &prop, _config.use_lazy_property_construction)) {
          PUSH_ERROR_AND_RETURN_TAG(kTag,
                                    fmt::format("Failed to parse Attribute: {}.",
                                                path.prop_part()));
        }

        _variantProps[current] = {path, prop};
        _variantPropChildren[parent].push_back(current);

        DCOUT(
            fmt::format("parent {} current [{}] Parsed Property/Attribute {} under Variant. PathIndex {}",
                        parent, current, path.prop_part(), spec.path_index));

      } else {
        PUSH_WARN(
            "TODO: SpecTypeAttribute(in conjunction with Class/Over specifier, "
            "or inherited?)");
      }
      break;
    }
    case SpecType::Connection:
    case SpecType::Relationship:
    case SpecType::RelationshipTarget: {
      PUSH_ERROR_AND_RETURN_TAG(
          kTag, fmt::format("TODO: Unsupported/Unimplemented SpecType: {}.",
                            to_string(spec.spec_type)));
      break;
    }
    case SpecType::Expression:
    case SpecType::Mapper:
    case SpecType::MapperArg: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, fmt::format("Unsupported SpecType: {}.",
                                                  to_string(spec.spec_type)));
      break;
    }
    case SpecType::Unknown:
    case SpecType::Invalid: {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "[InternalError] Invalid SpecType.");
      break;
    }
  }

  return true;
}

// Switch between recursive and iterative implementation
#define TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM 1

#if TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM

//
// Iterative version of ReconstructPrimRecursively using explicit stack
//
bool USDCReader::Impl::ReconstructPrimRecursively(
    int parent, int current, Prim *parentPrim, int level,
    const PathIndexToSpecIndexMap &psmap, Stage *stage) {

  (void)parentPrim;

  struct StackEntry {
    int parent_id;
    int current_id;
    int level;
    size_t child_idx;
    size_t parent_entry_idx;
    std::unique_ptr<Prim> prim;

    StackEntry(int p, int c, int lv, size_t parent_idx)
        : parent_id(p), current_id(c), level(lv), child_idx(0),
          parent_entry_idx(parent_idx), prim(nullptr) {}

    StackEntry(StackEntry &&) noexcept = default;
    StackEntry &operator=(StackEntry &&) noexcept = default;
    StackEntry(const StackEntry &) = delete;
    StackEntry &operator=(const StackEntry &) = delete;
  };

  std::vector<StackEntry> stack;
  stack.reserve(size_t(_config.kMaxPrimNestLevel) + 16);

  stack.emplace_back(parent, current, level, SIZE_MAX);

  while (!stack.empty()) {
    StackEntry &entry = stack.back();

    if ((entry.current_id < 0) || (entry.current_id >= int(_nodes->size()))) {
      PUSH_ERROR("Invalid current node id: " + std::to_string(entry.current_id) +
                 ". Must be in range [0, " + std::to_string(_nodes->size()) + ")");
      return false;
    }

    if (entry.level > int32_t(_config.kMaxPrimNestLevel)) {
      PUSH_ERROR_AND_RETURN_TAG(kTag, "Prim hierarchy is too deep.");
    }

    const crate::CrateReader::Node &node = (*_nodes)[size_t(entry.current_id)];
    const auto &children = node.GetChildren();

    // First time visiting this node - reconstruct prim
    if (entry.child_idx == 0 && !entry.prim) {
      DCOUT("ReconstructPrimRecursively: parent = "
            << std::to_string(entry.parent_id) << ", current = " << entry.current_id
            << ", level = " << std::to_string(entry.level));

      bool is_parent_variant = _variantPrims.count(entry.parent_id);

      std::unique_ptr<Prim> temp_prim;
      if (!ReconstructPrimNode(entry.parent_id, entry.current_id, entry.level,
                               is_parent_variant, psmap, stage, &temp_prim)) {
        return false;
      }
      if (temp_prim) {
        entry.prim = std::move(temp_prim);
      }

      DCOUT("node.Children.size = " << children.size());
    }

    // Process children
    if (entry.child_idx < children.size()) {
      size_t idx = entry.child_idx++;
      int child_id = int(children[idx]);

      DCOUT("Reconstuct Prim children: " << idx << " / " << children.size());

      size_t current_entry_idx = stack.size() - 1;

      stack.emplace_back(entry.current_id, child_id, entry.level + 1, current_entry_idx);
    } else {
      // All children processed - finalize this node
      DCOUT("DONE processing children for node: " << entry.current_id);

      DCOUT(fmt::format("parent {}, current {}", entry.parent_id, entry.current_id));

      DCOUT(fmt::format("  has variant properties {}, has variant children {}",
        _variantPropChildren.count(entry.current_id),
        _variantPrimChildren.count(entry.current_id)));

      // Get parent prim pointer
      Prim *parentPrimPtr = nullptr;
      if (entry.parent_entry_idx != SIZE_MAX && entry.parent_entry_idx < stack.size()) {
        parentPrimPtr = stack[entry.parent_entry_idx].prim.get();
      }

      auto resolveVariantOwnerPrimFromStack = [&](const StackEntry &target,
                                                  bool include_current) -> Prim * {
        if (include_current) {
          if (target.prim) {
            return target.prim.get();
          }

          auto current_variant_it = _variantPrims.find(target.current_id);
          if (current_variant_it != _variantPrims.end()) {
            return &(current_variant_it->second);
          }
        }

        size_t ancestor_idx = target.parent_entry_idx;
        while ((ancestor_idx != SIZE_MAX) && (ancestor_idx < stack.size())) {
          StackEntry &ancestor = stack[ancestor_idx];
          if (ancestor.prim) {
            return ancestor.prim.get();
          }

          auto ancestor_variant_it = _variantPrims.find(ancestor.current_id);
          if (ancestor_variant_it != _variantPrims.end()) {
            return &(ancestor_variant_it->second);
          }

          ancestor_idx = ancestor.parent_entry_idx;
        }

        auto parent_variant_it = _variantPrims.find(target.parent_id);
        if (parent_variant_it != _variantPrims.end()) {
          return &(parent_variant_it->second);
        }

        if (!include_current) {
          if (target.prim) {
            return target.prim.get();
          }

          auto current_variant_it = _variantPrims.find(target.current_id);
          if (current_variant_it != _variantPrims.end()) {
            return &(current_variant_it->second);
          }
        }

        return nullptr;
      };

      if (_variantPropChildren.count(entry.current_id)) {

        if (!_variantPrims.count(entry.current_id)) {
          PUSH_ERROR_AND_RETURN("Internal error: variant attribute is not a child of VariantPrim.");
        }

        Prim *variantOwnerPrim =
            resolveVariantOwnerPrimFromStack(entry, /* include_current */ false);
        if (!variantOwnerPrim) {
          PUSH_ERROR_AND_RETURN(
              "Internal error: parentPrim should exist for variant Property.");
        }

        const Prim &variantPrim = _variantPrims.at(entry.current_id);

        DCOUT("variant prim name: " << variantPrim.element_name());

        // Extract variant element from full path (e.g., "/A{v=sel}" → "{v=sel}")
        std::string ve2 = variantPrim.element_name();
        {
          auto bp2 = ve2.rfind('{');
          if (bp2 != std::string::npos) ve2 = ve2.substr(bp2);
        }
        if (!is_variantElementName(ve2)) {
          PUSH_ERROR_AND_RETURN("Corrupted Crate. VariantAttribute is not the child of VariantPrim: " + variantPrim.element_name());
        }

        std::array<std::string, 2> toks;
        if (!tokenize_variantElement(ve2, &toks)) {
          PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
        }

        std::string variantSetName = toks[0];
        std::string variantName = toks[1];

        Variant variant;

        for (const auto &item : _variantPropChildren.at(entry.current_id)) {
          if (!_variantProps.count(item)) {
            PUSH_ERROR_AND_RETURN("Internal error: variant Property not found.");
          }
          const std::pair<Path, Property> &pp = _variantProps.at(item);

          std::string prop_name = std::get<0>(pp).prop_part();
          DCOUT(fmt::format("  node_index = {}, prop name {}", item, prop_name));

          variant.properties()[prop_name] = std::get<1>(pp);
        }

        VariantSet &vs = variantOwnerPrim->variantSets()[variantSetName];

        if (vs.name.empty()) {
          vs.name = variantSetName;
        }
        vs.variantSet[variantName] = variant;

      }

      if (_variantPrimChildren.count(entry.current_id)) {
        Prim *variantOwnerPrim =
            resolveVariantOwnerPrimFromStack(entry, /* include_current */ true);
        if (!variantOwnerPrim) {
          // Debug: dump stack state
          std::string dbg = "Stack trace for variant owner resolution:\n";
          dbg += "  current_id=" + std::to_string(entry.current_id);
          dbg += " parent_id=" + std::to_string(entry.parent_id);
          dbg += " parent_entry_idx=" + std::to_string(entry.parent_entry_idx) + "\n";
          for (size_t si = 0; si < stack.size(); si++) {
            dbg += "  stack[" + std::to_string(si) + "] id=" + std::to_string(stack[si].current_id)
                 + " parent=" + std::to_string(stack[si].parent_id)
                 + " hasPrim=" + std::to_string(stack[si].prim != nullptr)
                 + " isVariant=" + std::to_string(_variantPrims.count(stack[si].current_id) > 0) + "\n";
          }
          PUSH_ERROR_AND_RETURN(
              "Internal error: failed to resolve variant owner Prim. " + dbg);
        }

        if (!AttachVariantPrimChildrenToOwner(entry.current_id, variantOwnerPrim)) {
          return false;
        }
      }

      if (entry.parent_id == 0) {  // root prim
        if (entry.prim) {
          auto &prims = stage->root_prims();
          prims.resize(prims.size() + 1);
          prims.back() = std::move(*entry.prim);
        }
      } else {
        if (_variantPrims.count(entry.parent_id)) {
          DCOUT("parent is variantPrim: " << entry.parent_id);
          if (!entry.prim) {
            if (!IsExpectedNonPrimVariantChild(entry.current_id, psmap)) {
              PUSH_WARN("parent is variantPrim, but current is not Prim.");
            }
          } else {
            DCOUT("Adding prim to child...");
            Prim &vp = _variantPrims.at(entry.parent_id);
            auto &vp_children = vp.children();
            vp_children.resize(vp_children.size() + 1);
            vp_children.back() = std::move(*entry.prim);
          }
        } else if (entry.prim && parentPrimPtr) {
          auto &parent_children = parentPrimPtr->children();
          parent_children.resize(parent_children.size() + 1);
          parent_children.back() = std::move(*entry.prim);
        }
      }

      stack.pop_back();
    }
  }

  return true;
}

#else // !TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM

//
// Original recursive implementation
//
bool USDCReader::Impl::ReconstructPrimRecursively(
    int parent, int current, /* input */Prim *parentPrimPtr, int level,
    const PathIndexToSpecIndexMap &psmap, Stage *stage) {
  if (level > int32_t(_config.kMaxPrimNestLevel)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "Prim hierarchy is too deep.");
  }

  DCOUT("ReconstructPrimRecursively: parent = "
        << std::to_string(parent) << ", current = " << current
        << ", level = " << std::to_string(level));

  if ((current < 0) || (current >= int(_nodes->size()))) {
    PUSH_ERROR("Invalid current node id: " + std::to_string(current) +
               ". Must be in range [0, " + std::to_string(_nodes->size()) + ")");
    return false;
  }

  Prim *currPrimPtr = nullptr;
  std::unique_ptr<Prim> prim;

  bool is_parent_variant = _variantPrims.count(parent);
  if (!ReconstructPrimNode(parent, current, level, is_parent_variant, psmap,
                           stage, &prim)) {
    return false;
  }

  if (prim) {
    currPrimPtr = prim.get();
  } else {
    auto it = _variantPrims.find(current);
    if (it != _variantPrims.end()) {
      currPrimPtr = &(it->second);
    }
  }

  Prim *nextParentPrimPtr = currPrimPtr ? currPrimPtr : parentPrimPtr;

  // Traverse children
  {
    const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];
    DCOUT("node.Children.size = " << node.GetChildren().size());
    for (size_t i = 0; i < node.GetChildren().size(); i++) {
      DCOUT("Reconstuct Prim children: " << i << " / "
                                         << node.GetChildren().size());
      if (!ReconstructPrimRecursively(current, int(node.GetChildren()[i]),
                                      nextParentPrimPtr, level + 1, psmap,
                                      stage)) {
        return false;
      }
      DCOUT("DONE Reconstuct Prim children: " << i << " / "
                                              << node.GetChildren().size());
    }
  }

  DCOUT(fmt::format("----reconstruct-variant------"));
  DCOUT(fmt::format("parent {}, current {}", parent, current));
  DCOUT(fmt::format("  has variant properties {}, has variant children {}",
                    _variantPropChildren.count(current),
                    _variantPrimChildren.count(current)));

  if (_variantPropChildren.count(current)) {
    if (!_variantPrims.count(current)) {
      PUSH_ERROR_AND_RETURN(
          "Internal error: variant attribute is not a child of VariantPrim.");
    }

    Prim *variantOwnerPrim =
        ResolveVariantOwnerPrim(parent, current, currPrimPtr, parentPrimPtr,
                                /* include_current_node */ false);
    if (!variantOwnerPrim) {
      PUSH_ERROR_AND_RETURN(
          "Internal error: parentPrim should exist for variant Property.");
    }

    const Prim &variantPrim = _variantPrims.at(current);
    DCOUT("variant prim name: " << variantPrim.element_name());

    if (!is_variantElementName(variantPrim.element_name())) {
      PUSH_ERROR_AND_RETURN(
          "Corrupted Crate. VariantAttribute is not the child of VariantPrim.");
    }

    std::array<std::string, 2> toks;
    if (!tokenize_variantElement(variantPrim.element_name(), &toks)) {
      PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
    }

    std::string variantSetName = toks[0];
    std::string variantName = toks[1];

    Variant variant;
    for (const auto &item : _variantPropChildren.at(current)) {
      if (!_variantProps.count(item)) {
        PUSH_ERROR_AND_RETURN("Internal error: variant Property not found.");
      }

      const std::pair<Path, Property> &pp = _variantProps.at(item);
      std::string prop_name = std::get<0>(pp).prop_part();
      DCOUT(fmt::format("  node_index = {}, prop name {}", item, prop_name));
      variant.properties()[prop_name] = std::get<1>(pp);
    }

    VariantSet &vs = variantOwnerPrim->variantSets()[variantSetName];
    if (vs.name.empty()) {
      vs.name = variantSetName;
    }
    vs.variantSet[variantName] = variant;
  }

  if (_variantPrimChildren.count(current)) {
    Prim *variantOwnerPrim =
        ResolveVariantOwnerPrim(parent, current, currPrimPtr, parentPrimPtr,
                                /* include_current_node */ true);
    if (!variantOwnerPrim) {
      PUSH_ERROR_AND_RETURN("Internal error: failed to resolve variant owner Prim.");
    }

    if (!AttachVariantPrimChildrenToOwner(current, variantOwnerPrim)) {
      return false;
    }
  }

  DCOUT("-<---");

  if (parent == 0) {  // root prim
    if (prim) {
      auto &prims = stage->root_prims();
      prims.resize(prims.size() + 1);
      prims.back() = std::move(*prim);
    }
  } else {
    DCOUT("current " << current << ", parent " << parent);
    if (is_parent_variant) {
      DCOUT("parent " << parent << " is variantPrim");
      if (!prim) {
        if (!IsExpectedNonPrimVariantChild(current, psmap)) {
          PUSH_WARN("parent is variantPrim, but current is not Prim.");
        }
      } else {
        DCOUT("Adding prim to child...");
        Prim &vp = _variantPrims.at(parent);
        auto &children = vp.children();
        children.resize(children.size() + 1);
        children.back() = std::move(*prim);
      }
    } else if (prim && parentPrimPtr) {
      auto &children = parentPrimPtr->children();
      children.resize(children.size() + 1);
      children.back() = std::move(*prim);
    }
  }

  return true;
}

#endif // TINYUSDZ_USE_ITERATIVE_RECONSTRUCT_PRIM

bool USDCReader::Impl::ReconstructPrimSpecRecursively(
    int parent, int current, PrimSpec *parentPrimSpec, int level,
    const PathIndexToSpecIndexMap &psmap, Layer *layer) {
  if (level > int32_t(_config.kMaxPrimNestLevel)) {
    PUSH_ERROR_AND_RETURN_TAG(kTag, "PrimSpec hierarchy is too deep.");
  }

  DCOUT("ReconstructPrimRecursively: parent = "
        << std::to_string(parent) << ", current = " << current
        << ", level = " << std::to_string(level));

  if ((current < 0) || (current >= int(_nodes->size()))) {
    PUSH_ERROR("Invalid current node id: " + std::to_string(current) +
               ". Must be in range [0, " + std::to_string(_nodes->size()) + ")");
    return false;
  }

  PrimSpec *currPrimSpecPtr = nullptr;
  std::unique_ptr<PrimSpec> primspec;

  bool is_parent_variant = _variantPrimSpecs.count(parent);

  if (!ReconstructPrimSpecNode(parent, current, level, is_parent_variant, psmap,
                           layer, &primspec)) {
    return false;
  }

  if (primspec) {
    currPrimSpecPtr = primspec.get();
  } else {
    // For Variant nodes, point to the variant's PrimSpec so that
    // children (including nested VariantSets) have a valid parent.
    auto vps_it = _variantPrimSpecs.find(current);
    if (vps_it != _variantPrimSpecs.end()) {
      currPrimSpecPtr = &(vps_it->second);
    }
  }

  // Pass currPrimSpecPtr (or fall back to parentPrimSpec) to children
  PrimSpec *nextParentPrimSpec = currPrimSpecPtr ? currPrimSpecPtr : parentPrimSpec;

  {
    const crate::CrateReader::Node &node = (*_nodes)[size_t(current)];
    DCOUT("node.Children.size = " << node.GetChildren().size());
    for (size_t i = 0; i < node.GetChildren().size(); i++) {
      DCOUT("Reconstuct Prim children: " << i << " / "
                                         << node.GetChildren().size());
      if (!ReconstructPrimSpecRecursively(current, int(node.GetChildren()[i]),
                                      nextParentPrimSpec, level + 1, psmap, layer)) {
        return false;
      }
      DCOUT("DONE Reconstuct PrimSpec children: " << i << " / "
                                              << node.GetChildren().size());
    }
  }

  //
  // Reonstruct variant
  //
  DCOUT(fmt::format("---- reconstruct variant ---"));
  DCOUT(fmt::format("parent {}, current {}", parent, current));

  DCOUT(fmt::format("  has variant children {}",
    _variantPrimChildren.count(current)));

  // Note: _variantPropChildren is not used in the Layer path — the
  // early-exit guard in ReconstructPrimSpecNode skips Attribute children
  // whose parent Variant was already reconstructed by BuildPropertyMap.
  // Properties are carried through _variantPrimSpecs[].props() and
  // transferred below via dest.props() = vp.props().

  if (_variantPrimChildren.count(current)) {

    // Owner can be a Prim (primspec), a Variant (_variantPrimSpecs), or
    // the parent PrimSpec (when current is a VariantSet node).
    PrimSpec *ownerPrimSpec = nullptr;
    if (primspec) {
      ownerPrimSpec = primspec.get();
    } else if (_variantPrimSpecs.count(current)) {
      ownerPrimSpec = &_variantPrimSpecs.at(current);
    } else if (parentPrimSpec) {
      ownerPrimSpec = parentPrimSpec;
    }

    if (!ownerPrimSpec) {
      PUSH_ERROR_AND_RETURN("Internal error: must be Prim or Variant.");
    }

    DCOUT(fmt::format("{} has variant PrimSpec ", ownerPrimSpec->name()));

    for (const auto &item : _variantPrimChildren.at(current)) {

      if (!_variantPrimSpecs.count(item)) {
        PUSH_ERROR_AND_RETURN("Internal error: variant Prim children not found.");
      }

      const PrimSpec &vp = _variantPrimSpecs.at(item);

      DCOUT(fmt::format("  idx {}, variantPrim name {}", item, vp.name()));

      if (!is_variantElementName(vp.name())) {
        PUSH_ERROR_AND_RETURN("Corrupted Crate. Variant Prim has invalid element_name.");
      }

      std::array<std::string, 2> toks;
      if (!tokenize_variantElement(vp.name(), &toks)) {
        PUSH_ERROR_AND_RETURN("Invalid variant element_name.");
      }

      std::string variantSetName = toks[0];
      std::string variantName = toks[1];

      VariantSetSpec &vs = ownerPrimSpec->variantSets()[variantSetName];

      if (vs.name.empty()) {
        vs.name = variantSetName;
      }
      PrimSpec &dest = vs.variantSet[variantName];
      dest.metas() = vp.metas();
      dest.props() = vp.props();
      dest.typeName() = vp.typeName();
      dest.specifier() = vp.specifier();
      DCOUT("# of primChildren = " << vp.children().size());
      dest.children() = std::move(vp.children());
      dest.variantSets() = std::move(vp.variantSets());

    }
  }

  DCOUT(fmt::format("-<---"));

  if (parent == 0) {  // root prim
    if (primspec) {
      std::string name = primspec->name();
      layer->primspecs()[name] = std::move(*primspec);
    }
  } else {
    if (_variantPrimSpecs.count(parent)) {
      DCOUT("parent is variantPrim: " << parent);
      if (!primspec) {
        PUSH_WARN("parent is variantPrim, but current is not Prim.");
      } else {
        DCOUT("Adding prim to child...");
        PrimSpec &vps = _variantPrimSpecs.at(parent);
        vps.children().emplace_back(std::move(*primspec));
      }
    } else if (primspec && parentPrimSpec) {
      parentPrimSpec->children().resize(parentPrimSpec->children().size() + 1);
      parentPrimSpec->children().back() = std::move(*primspec);
    }
  }

  return true;
}

}  // namespace usdc
}  // namespace tinyusdz

#endif  // !TINYUSDZ_DISABLE_MODULE_USDC_READER
