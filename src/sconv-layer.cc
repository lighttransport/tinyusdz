// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Stage-converter: Layer/PrimSpec conversion.
//

#include "sconv-detail.hh"
#include "layer.hh"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

namespace tinyusdz {
namespace experimental {

// ============================================================================
// Layer/PrimSpec Conversion Implementation
// ============================================================================

bool CrateWriter::ConvertLayerToSpecs(const Layer& layer, std::string* err) {
  // 1. Add PseudoRoot spec with layer metadata
  // The PseudoRoot is a special prim at "/" that serves as the root of the scene
  // Layer metadata (upAxis, metersPerUnit, etc.) is stored as fields on PseudoRoot
  {
    Path root_path("/", "");  // Root path - use standard constructor
    crate::FieldValuePairVector root_fields;

    // PseudoRoot needs at least one field to create a valid fieldset
    // Add specifier field (PseudoRoot always uses Def)
    crate::CrateValue spec_value;
    spec_value.Set(Specifier::Def);
    root_fields.push_back({"specifier", spec_value});

    // Extract and add layer metadata from Layer.metas()
    const LayerMetas& metas = layer.metas();

    // upAxis (token: "X", "Y", or "Z")
    if (metas.upAxis.authored()) {
      Axis axis = metas.upAxis.get_value();
      std::string axis_str = tinyusdz::to_string(axis);  // "X", "Y", or "Z"
      crate::CrateValue axis_value;
      value::token axis_tok(axis_str);
      axis_value.Set(axis_tok);
      root_fields.push_back({"upAxis", axis_value});
    }

    // metersPerUnit (double)
    if (metas.metersPerUnit.authored()) {
      crate::CrateValue meters_value;
      meters_value.Set(metas.metersPerUnit.get_value());
      root_fields.push_back({"metersPerUnit", meters_value});
    }

    // timeCodesPerSecond (double)
    if (metas.timeCodesPerSecond.authored()) {
      crate::CrateValue time_value;
      time_value.Set(metas.timeCodesPerSecond.get_value());
      root_fields.push_back({"timeCodesPerSecond", time_value});
    }

    // framesPerSecond (double)
    if (metas.framesPerSecond.authored()) {
      crate::CrateValue fps_value;
      fps_value.Set(metas.framesPerSecond.get_value());
      root_fields.push_back({"framesPerSecond", fps_value});
    }

    // startTimeCode (double)
    if (metas.startTimeCode.authored()) {
      crate::CrateValue start_value;
      start_value.Set(metas.startTimeCode.get_value());
      root_fields.push_back({"startTimeCode", start_value});
    }

    // endTimeCode (double)
    if (metas.endTimeCode.authored()) {
      crate::CrateValue end_value;
      end_value.Set(metas.endTimeCode.get_value());
      root_fields.push_back({"endTimeCode", end_value});
    }

    // defaultPrim (token)
    if (!metas.defaultPrim.str().empty()) {
      crate::CrateValue default_prim_value;
      default_prim_value.Set(metas.defaultPrim);
      root_fields.push_back({"defaultPrim", default_prim_value});
    }

    // documentation (string)
    if (!metas.doc.value.empty()) {
      crate::CrateValue doc_value;
      doc_value.Set(metas.doc.value);
      root_fields.push_back({"documentation", doc_value});
    }

    // comment (string)
    if (!metas.comment.value.empty()) {
      crate::CrateValue comment_value;
      comment_value.Set(metas.comment.value);
      root_fields.push_back({"comment", comment_value});
    }

    // kilogramsPerUnit (double) - UsdPhysics
    if (metas.kilogramsPerUnit.authored()) {
      crate::CrateValue kg_value;
      kg_value.Set(metas.kilogramsPerUnit.get_value());
      root_fields.push_back({"kilogramsPerUnit", kg_value});
    }

    // customLayerData (dict)
    if (!metas.customLayerData.empty()) {
      crate::CrateValue custom_data_value;
      custom_data_value.Set(metas.customLayerData);
      root_fields.push_back({"customLayerData", custom_data_value});
    }

    // Add PseudoRoot spec
    if (!AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
      if (err) *err = "Failed to add PseudoRoot spec";
      return false;
    }
  }

  // 2. Convert all primspecs in the layer
  for (const auto& item : layer.primspecs()) {
    const auto& prim_name = item.first;
    const auto& primspec = item.second;
    // Each top-level primspec is a direct child of root
    Path root_path = Path();  // Root path

    if (!ConvertPrimSpecIterative(primspec, root_path, err)) {
      if (err) *err = "Failed to convert primspec: " + prim_name + ". Error: " + (*err);
      return false;
    }
  }

  // NOTE: Sublayers are exported via layer metadata (subLayers/subLayerOffsets fields)
  // We intentionally do NOT traverse and re-export sublayer content at write time because:
  // 1. Sublayers are composition references (asset paths) to external files
  // 2. The Stage already contains merged/composed prims from all sublayers
  // 3. Re-loading sublayers would duplicate content and break on moved files
  // 4. Sublayer metadata alone is sufficient for reconstruction at load time
  // This design is correct - composition happens at LOAD time, not WRITE time.

  return true;
}

bool CrateWriter::ConvertSinglePrimSpec(
    const PrimSpec& primspec,
    const Path& parent_path,
    std::string* err) {

  // 1. Build path for this prim
  Path prim_path = parent_path.AppendPrim(primspec.name());

  // 2. Create fields for this prim spec
  crate::FieldValuePairVector fields;

  // Add specifier (def, over, class) - use Specifier enum directly, not token
  Specifier spec = primspec.specifier();
  // USD files don't allow Invalid specifier - default to Def if we get Invalid
  if (spec == Specifier::Invalid) {
    spec = Specifier::Def;
  }
  crate::CrateValue spec_value;
  spec_value.Set(spec);  // CrateValue supports Specifier directly
  fields.push_back({"specifier", spec_value});

  // Add typeName if present - use value::token type, not token index
  if (!primspec.typeName().empty()) {
    crate::CrateValue type_value;
    value::token tok(primspec.typeName());
    type_value.Set(tok);
    fields.push_back({"typeName", type_value});
  }

  // 3. Convert properties (attributes, relationships, connections)
  for (const auto& item : primspec.props()) {
    const auto& prop_name = item.first;
    const auto& prop = item.second;
    if (!ConvertPropertyToFields(prop_name, prop, prim_path, fields, err)) {
      if (err) *err = "Failed to convert property: " + prop_name + " on prim: " + prim_path.full_path_name();
      return false;
    }
  }

  // 4. Convert metadata from PrimMeta
  const PrimMeta& metas = primspec.metas();

  // Add common metadata
  if (metas.has_active()) {
    crate::CrateValue active_value;
    active_value.Set(metas.get_active());
    fields.push_back({"active", active_value});
  }

  if (metas.has_hidden()) {
    crate::CrateValue hidden_value;
    hidden_value.Set(metas.get_hidden());
    fields.push_back({"hidden", hidden_value});
  }

  if (metas.has_kind()) {
    crate::CrateValue kind_value;
    // get_kind() returns the kind as a string
    std::string kind_str = metas.get_kind();
    crate::TokenIndex kind_tok = GetOrCreateToken(kind_str);
    kind_value.Set(kind_tok.value);
    fields.push_back({"kind", kind_value});
  }

  if (metas.has_displayName()) {
    crate::CrateValue display_value;
    display_value.Set(metas.get_displayName());
    fields.push_back({"displayName", display_value});
  }

  if (metas.has_doc()) {
    crate::CrateValue doc_value;
    // get_doc() returns StringData, extract .value for the string
    doc_value.Set(metas.get_doc().value);
    fields.push_back({"documentation", doc_value});
  }

  // Add variant selection if present
  if (metas.variants) {
    const VariantSelectionMap& variant_map = metas.variants.value();
    crate::CrateValue variant_value;
    variant_value.Set(variant_map);
    fields.push_back({"variants", variant_value});

    DCOUT("[ConvertPrimSpecRecursive] Added variants field with "
              << variant_map.size() << " selections");
  }

  // Add variantSets list if present
  if (metas.variantSets) {
    // Collect all variant sets from all listops
    std::vector<std::string> variant_sets_list;
    for (const auto& variantSets_op : metas.variantSets.value()) {
      for (const auto& vs : variantSets_op.second) {
        variant_sets_list.push_back(vs);
      }
    }

    // Convert to string array for CrateValue
    crate::CrateValue variant_sets_value;
    variant_sets_value.Set(variant_sets_list);
    fields.push_back({"variantSets", variant_sets_value});

    DCOUT("[ConvertPrimSpecRecursive] Added variantSets field with "
              << variant_sets_list.size() << " sets");
  }

  // Add references if present
  if (metas.references) {
    // Convert all listops to ListOp<Reference>
    ListOp<Reference> ref_listop;
    size_t total_refs = 0;

    for (const auto& ref_op : metas.references.value()) {
      const ListEditQual& qual = ref_op.first;
      const std::vector<Reference>& ref_list = ref_op.second;
      total_refs += ref_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          ref_listop.ClearAndMakeExplicit();
          ref_listop.SetExplicitItems(ref_list);
          break;
        case ListEditQual::Append:
          ref_listop.SetAppendedItems(ref_list);
          break;
        case ListEditQual::Prepend:
          ref_listop.SetPrependedItems(ref_list);
          break;
        case ListEditQual::Add:
          ref_listop.SetAddedItems(ref_list);
          break;
        case ListEditQual::Delete:
          ref_listop.SetDeletedItems(ref_list);
          break;
        default:
          // Default to explicit
          ref_listop.ClearAndMakeExplicit();
          ref_listop.SetExplicitItems(ref_list);
          break;
      }
    }

    crate::CrateValue ref_value;
    ref_value.Set(ref_listop);
    fields.push_back({"references", ref_value});

    DCOUT("[ConvertPrimSpecRecursive] Added references field with "
              << total_refs << " references");
    (void)total_refs;
  }

  // Add payload if present
  if (metas.payload) {
    // Convert all listops to ListOp<Payload>
    ListOp<Payload> payload_listop;
    size_t total_payloads = 0;

    for (const auto& payload_op : metas.payload.value()) {
      const ListEditQual& qual = payload_op.first;
      const std::vector<Payload>& payload_list = payload_op.second;
      total_payloads += payload_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          payload_listop.ClearAndMakeExplicit();
          payload_listop.SetExplicitItems(payload_list);
          break;
        case ListEditQual::Append:
          payload_listop.SetAppendedItems(payload_list);
          break;
        case ListEditQual::Prepend:
          payload_listop.SetPrependedItems(payload_list);
          break;
        case ListEditQual::Add:
          payload_listop.SetAddedItems(payload_list);
          break;
        case ListEditQual::Delete:
          payload_listop.SetDeletedItems(payload_list);
          break;
        default:
          // Default to explicit
          payload_listop.ClearAndMakeExplicit();
          payload_listop.SetExplicitItems(payload_list);
          break;
      }
    }

    crate::CrateValue payload_value;
    payload_value.Set(payload_listop);
    fields.push_back({"payload", payload_value});

    DCOUT("[ConvertPrimSpecRecursive] Added payload field with "
              << total_payloads << " payloads");
    (void)total_payloads;
  }

  // Add customData if present
  if (metas.has_customData()) {
    crate::CrateValue custom_data_value;
    custom_data_value.Set(metas.get_customData());
    fields.push_back({"customData", custom_data_value});
    DCOUT("[ConvertPrimSpecRecursive] Added customData with "
              << metas.get_customData().size() << " entries");
  }

  // Add unregistered metadata (OpenUSD-compatible: stored as strings)
  for (const auto &item : metas.unregisteredMetas) {
    crate::CrateValue unreg_value;
    unreg_value.Set(item.second);  // Store as string in crate
    fields.push_back({item.first, unreg_value});
  }

  // Add apiSchemas if present
  if (metas.has_apiSchemas()) {
    const APISchemas& api_schemas = metas.get_apiSchemas();

    // Convert APISchemas to TokenListOp
    ListOp<value::token> api_listop;

    // Build list of API schema names as tokens
    std::vector<value::token> api_tokens;
    for (const auto& item : api_schemas.names) {
      const auto& api_name = item.first;
      const auto& instance_name = item.second;
      std::string api_str = to_string(api_name);

      // For multi-apply API schemas, append instance name
      // e.g. "CollectionAPI:material:MainMaterial"
      if (!instance_name.empty()) {
        api_str += ":" + instance_name;
      }

      api_tokens.push_back(value::token(api_str));
    }

    // Set the appropriate list based on ListEditQual
    switch (api_schemas.listOpQual) {
      case ListEditQual::ResetToExplicit:
        api_listop.ClearAndMakeExplicit();
        api_listop.SetExplicitItems(api_tokens);
        break;
      case ListEditQual::Prepend:
        api_listop.SetPrependedItems(api_tokens);
        break;
      case ListEditQual::Append:
        api_listop.SetAppendedItems(api_tokens);
        break;
      case ListEditQual::Add:
        api_listop.SetAddedItems(api_tokens);
        break;
      case ListEditQual::Delete:
        api_listop.SetDeletedItems(api_tokens);
        break;
      default:
        // Default to prepend (USD convention)
        api_listop.SetPrependedItems(api_tokens);
        break;
    }

    crate::CrateValue api_value;
    api_value.Set(api_listop);
    fields.push_back({"apiSchemas", api_value});

    DCOUT("[ConvertPrimSpecRecursive] Added apiSchemas with "
              << api_tokens.size() << " schemas");
  }

  // Add inherits if present
  if (metas.inherits) {
    // Convert all listops to ListOp<Path>
    ListOp<Path> inherits_listop;
    size_t total_inherits = 0;

    for (const auto& inherits_op : metas.inherits.value()) {
      const ListEditQual& qual = inherits_op.first;
      const std::vector<Path>& inherits_list = inherits_op.second;
      total_inherits += inherits_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          inherits_listop.ClearAndMakeExplicit();
          inherits_listop.SetExplicitItems(inherits_list);
          break;
        case ListEditQual::Append:
          inherits_listop.SetAppendedItems(inherits_list);
          break;
        case ListEditQual::Prepend:
          inherits_listop.SetPrependedItems(inherits_list);
          break;
        case ListEditQual::Add:
          inherits_listop.SetAddedItems(inherits_list);
          break;
        case ListEditQual::Delete:
          inherits_listop.SetDeletedItems(inherits_list);
          break;
        default:
          // Default to explicit
          inherits_listop.ClearAndMakeExplicit();
          inherits_listop.SetExplicitItems(inherits_list);
          break;
      }
    }

    crate::CrateValue inherits_value;
    inherits_value.Set(inherits_listop);
    fields.push_back({"inherits", inherits_value});

    DCOUT("[ConvertPrimSpecRecursive] Added inherits with "
              << total_inherits << " paths");
    (void)total_inherits;
  }

  // Add specializes if present
  if (metas.specializes) {
    // Convert all listops to ListOp<Path>
    ListOp<Path> specializes_listop;
    size_t total_specializes = 0;

    for (const auto& specializes_op : metas.specializes.value()) {
      const ListEditQual& qual = specializes_op.first;
      const std::vector<Path>& specializes_list = specializes_op.second;
      total_specializes += specializes_list.size();

      // Accumulate items based on ListEditQual
      switch (qual) {
        case ListEditQual::ResetToExplicit:
          specializes_listop.ClearAndMakeExplicit();
          specializes_listop.SetExplicitItems(specializes_list);
          break;
        case ListEditQual::Append:
          specializes_listop.SetAppendedItems(specializes_list);
          break;
        case ListEditQual::Prepend:
          specializes_listop.SetPrependedItems(specializes_list);
          break;
        case ListEditQual::Add:
          specializes_listop.SetAddedItems(specializes_list);
          break;
        case ListEditQual::Delete:
          specializes_listop.SetDeletedItems(specializes_list);
          break;
        default:
          // Default to explicit
          specializes_listop.ClearAndMakeExplicit();
          specializes_listop.SetExplicitItems(specializes_list);
          break;
      }
    }

    crate::CrateValue specializes_value;
    specializes_value.Set(specializes_listop);
    fields.push_back({"specializes", specializes_value});

    DCOUT("[ConvertPrimSpecRecursive] Added specializes with "
              << total_specializes << " paths");
    (void)total_specializes;
  }

  // Add assetInfo if present
  if (metas.has_assetInfo()) {
    crate::CrateValue asset_info_value;
    asset_info_value.Set(metas.get_assetInfo());
    fields.push_back({"assetInfo", asset_info_value});
    DCOUT("[ConvertPrimSpecRecursive] Added assetInfo with "
              << metas.get_assetInfo().size() << " entries");
  }

  // Add instanceable if present
  if (metas.has_instanceable()) {
    crate::CrateValue instanceable_value;
    instanceable_value.Set(metas.get_instanceable());
    fields.push_back({"instanceable", instanceable_value});
    DCOUT("[ConvertPrimSpecRecursive] Added instanceable: "
              << metas.get_instanceable());
  }

  // 5. Add spec to file
  if (!AddSpec(prim_path, SpecType::Prim, fields, err)) {
    if (err) *err = "Failed to add spec for prim: " + prim_path.full_path_name();
    return false;
  }

  return true;
}

// ============================================================================
// Iterative Depth-First PrimSpec Conversion
// ============================================================================

bool CrateWriter::ConvertPrimSpecIterative(
    const PrimSpec& root_primspec,
    const Path& parent_path,
    std::string* err) {

  struct WorkItem {
    const PrimSpec* primspec;
    Path parent_path;
  };

  std::vector<WorkItem> stack;
  stack.push_back({&root_primspec, parent_path});

  while (!stack.empty()) {
    if (stack.size() > kMaxDefaultTraversalLimit) {
      if (err) *err = "ConvertPrimSpecIterative: tree too deep.";
      return false;
    }

    WorkItem item = std::move(stack.back());
    stack.pop_back();

    if (!ConvertSinglePrimSpec(*item.primspec, item.parent_path, err)) {
      return false;
    }

    // Build this prim's path for its children
    Path prim_path = item.parent_path.AppendPrim(item.primspec->name());

    // Push children in reverse order so left-most child is processed first
    const auto& children = item.primspec->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
      stack.push_back({&(*it), prim_path});
    }
  }

  return true;
}

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
