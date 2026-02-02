// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Composition Implementation

#include "composition.hh"
#include <algorithm>
#include <sstream>

namespace tinyusdz {
namespace next {

// ============================================================
// Compositor
// ============================================================

Compositor::Compositor() = default;

Compositor::Compositor(AssetResolver* resolver) : resolver_(resolver) {}

Compositor::~Compositor() = default;

std::unique_ptr<Layer> Compositor::Compose(const Layer& root_layer,
                                            const std::string& anchor_path) {
  errors_.clear();
  composition_stack_.clear();

  // Create a copy of the root layer to compose into
  auto result = std::make_unique<Layer>();

  // Copy metadata
  result->meta() = root_layer.meta();

  // First, compose sublayers (weakest to strongest)
  if (!root_layer.meta().subLayers.empty()) {
    auto sublayer_base = ComposeSublayers(root_layer, anchor_path);
    if (sublayer_base) {
      ComposeLayer(*result, *sublayer_base, anchor_path, 0);
    }
  }

  // Then compose the root layer on top
  ComposeLayer(*result, root_layer, anchor_path, 0);

  return result;
}

std::unique_ptr<Layer> Compositor::ComposeSublayers(const Layer& root_layer,
                                                     const std::string& anchor_path) {
  const auto& sublayer_paths = root_layer.meta().subLayers;
  if (sublayer_paths.empty()) {
    return nullptr;
  }

  auto result = std::make_unique<Layer>();

  // Process sublayers from weakest to strongest (last to first)
  for (auto it = sublayer_paths.rbegin(); it != sublayer_paths.rend(); ++it) {
    const std::string& sublayer_path = *it;

    // Resolve the sublayer path
    std::string resolved_path = sublayer_path;
    if (resolver_) {
      resolved_path = resolver_->ResolvePath(sublayer_path, anchor_path);
    }

    // Check for muted layers
    bool muted = std::find(options_.muted_layers.begin(),
                           options_.muted_layers.end(),
                           resolved_path) != options_.muted_layers.end();
    if (muted) continue;

    // Load the sublayer
    const Layer* sublayer = GetCachedLayer(resolved_path);
    if (!sublayer) {
      AddError("Failed to load sublayer: " + sublayer_path,
               "", sublayer_path, ArcType::Local);
      continue;
    }

    // Recursively compose the sublayer
    auto composed_sub = Compose(*sublayer, resolved_path);
    if (composed_sub) {
      ComposeLayer(*result, *composed_sub, resolved_path, 0);
    }
  }

  return result;
}

bool Compositor::ComposeLayer(Layer& target, const Layer& source,
                               const std::string& anchor_path, int depth) {
  if (depth > options_.max_depth) {
    AddError("Max composition depth exceeded", "", anchor_path, ArcType::Reference);
    return false;
  }

  // Compose each prim from source into target
  for (size_t i = 0; i < source.prim_count(); ++i) {
    const PrimSpec* src_prim = source.prim(static_cast<uint32_t>(i));
    if (!src_prim) continue;

    // Find or create target prim at the same path
    std::string prim_path = src_prim->path().str();
    PrimSpec* target_prim = target.prim_at_path_mutable(prim_path);

    if (!target_prim) {
      // Create new prim in target
      // For now, we'll use a simple approach of copying the prim
      // A full implementation would use LayerBuilder
      continue;  // Skip for now - needs proper prim creation
    }

    ComposePrim(*target_prim, *src_prim, anchor_path, depth);
  }

  return true;
}

bool Compositor::ComposePrim(PrimSpec& target, const PrimSpec& source,
                              const std::string& anchor_path, int depth) {
  // Apply composition arcs in LIVRPS order:
  // 1. Local opinions (from source)
  // 2. Inherits
  // 3. Variants
  // 4. References
  // 5. Payloads
  // 6. Specializes

  // Copy local opinions (properties, metadata) from source
  // Properties are copied with "stronger wins" semantics
  for (const auto& slot : source.properties().slots()) {
    const Value* src_val = source.property_value(slot.name_id);
    if (src_val) {
      // Only copy if target doesn't have this property
      if (!target.property(slot.name_id)) {
        target.add_property(slot.name_id, *src_val, slot.flags);
      }
    }
  }

  // Copy type name if target doesn't have one
  if (target.type_name().empty() && !source.type_name().empty()) {
    target.set_type_name(source.type_name());
  }

  return true;
}

bool Compositor::ApplyReferences(PrimSpec& prim, const std::string& anchor_path,
                                  int depth) {
  const auto& refs = prim.meta().references;
  if (refs.empty()) return true;

  for (const auto& ref_str : refs) {
    CompositionArc arc = ParseReference(ref_str);

    std::string resolved_path = arc.asset_path;
    if (!arc.is_internal && resolver_) {
      resolved_path = resolver_->ResolvePath(arc.asset_path, anchor_path);
    }

    if (CheckCycle(resolved_path)) {
      AddError("Circular reference detected", prim.path().str(),
               ref_str, ArcType::Reference);
      continue;
    }

    PushStack(resolved_path);

    if (arc.is_internal) {
      // Internal reference - same layer
      // Would need layer context to resolve
    } else {
      // External reference
      const Layer* ref_layer = GetCachedLayer(resolved_path);
      if (!ref_layer) {
        AddError("Failed to load reference: " + arc.asset_path,
                 prim.path().str(), ref_str, ArcType::Reference);
        PopStack();
        continue;
      }

      // Find the referenced prim
      std::string target_path = arc.prim_path;
      if (target_path.empty()) {
        target_path = "/" + ref_layer->meta().defaultPrim;
      }

      const PrimSpec* ref_prim = ref_layer->prim_at_path(target_path);
      if (!ref_prim) {
        AddError("Referenced prim not found: " + target_path,
                 prim.path().str(), ref_str, ArcType::Reference);
        PopStack();
        continue;
      }

      // Compose referenced prim onto target
      ComposePrim(prim, *ref_prim, resolved_path, depth + 1);
    }

    PopStack();
  }

  return true;
}

bool Compositor::ApplyPayloads(PrimSpec& prim, const std::string& anchor_path,
                                int depth) {
  if (!options_.load_payloads) return true;

  const auto& payloads = prim.meta().payloads;
  if (payloads.empty()) return true;

  for (const auto& payload_str : payloads) {
    CompositionArc arc = ParsePayload(payload_str);

    std::string resolved_path = arc.asset_path;
    if (resolver_) {
      resolved_path = resolver_->ResolvePath(arc.asset_path, anchor_path);
    }

    if (CheckCycle(resolved_path)) {
      AddError("Circular payload detected", prim.path().str(),
               payload_str, ArcType::Payload);
      continue;
    }

    PushStack(resolved_path);

    const Layer* payload_layer = GetCachedLayer(resolved_path);
    if (!payload_layer) {
      AddError("Failed to load payload: " + arc.asset_path,
               prim.path().str(), payload_str, ArcType::Payload);
      PopStack();
      continue;
    }

    std::string target_path = arc.prim_path;
    if (target_path.empty()) {
      target_path = "/" + payload_layer->meta().defaultPrim;
    }

    const PrimSpec* payload_prim = payload_layer->prim_at_path(target_path);
    if (!payload_prim) {
      AddError("Payload prim not found: " + target_path,
               prim.path().str(), payload_str, ArcType::Payload);
      PopStack();
      continue;
    }

    ComposePrim(prim, *payload_prim, resolved_path, depth + 1);
    PopStack();
  }

  return true;
}

bool Compositor::ApplyInherits(PrimSpec& prim, const Layer& layer, int depth) {
  if (!options_.resolve_inherits) return true;

  const auto& inherits = prim.meta().inherits;
  if (inherits.empty()) return true;

  for (const auto& inherit_path : inherits) {
    const PrimSpec* class_prim = layer.prim_at_path(inherit_path);
    if (!class_prim) {
      AddError("Inherited class not found: " + inherit_path,
               prim.path().str(), inherit_path, ArcType::Inherits);
      continue;
    }

    ComposePrim(prim, *class_prim, "", depth + 1);
  }

  return true;
}

bool Compositor::ApplySpecializes(PrimSpec& prim, const Layer& layer, int depth) {
  if (!options_.resolve_specializes) return true;

  const auto& specializes = prim.meta().specializes;
  if (specializes.empty()) return true;

  for (const auto& spec_path : specializes) {
    const PrimSpec* spec_prim = layer.prim_at_path(spec_path);
    if (!spec_prim) {
      AddError("Specialized prim not found: " + spec_path,
               prim.path().str(), spec_path, ArcType::Specializes);
      continue;
    }

    // Specializes is weaker than local, so we only copy if target doesn't have
    ComposePrim(prim, *spec_prim, "", depth + 1);
  }

  return true;
}

bool Compositor::ApplyVariants(PrimSpec& prim, const Layer& layer, int depth) {
  if (!options_.resolve_variants) return true;

  const std::string& var_sel = prim.meta().variantSelection;
  if (var_sel.empty()) return true;

  VariantSelection sel = ParseVariantSelection(var_sel);
  if (sel.variant_set.empty()) return true;

  // Variant implementation would require variant set storage in PrimSpec
  // For now, just note that we'd apply the selected variant here

  return true;
}

const Layer* Compositor::GetCachedLayer(const std::string& path) {
  auto it = layer_cache_.find(path);
  if (it != layer_cache_.end()) {
    return it->second.get();
  }

  // Load the layer
  if (!layer_loader_) {
    return nullptr;
  }

  std::string error;
  auto layer = layer_loader_(path, &error);
  if (!layer) {
    AddError("Failed to load layer: " + error, "", path, ArcType::Reference);
    return nullptr;
  }

  Layer* result = layer.get();
  layer_cache_[path] = std::move(layer);
  return result;
}

void Compositor::ClearCache() {
  layer_cache_.clear();
}

void Compositor::AddError(const std::string& msg, const std::string& prim_path,
                          const std::string& arc_path, ArcType type) {
  CompositionError err;
  err.message = msg;
  err.prim_path = prim_path;
  err.arc_path = arc_path;
  err.arc_type = type;
  errors_.push_back(err);
}

bool Compositor::CheckCycle(const std::string& path) {
  return std::find(composition_stack_.begin(), composition_stack_.end(), path)
         != composition_stack_.end();
}

void Compositor::PushStack(const std::string& path) {
  composition_stack_.push_back(path);
}

void Compositor::PopStack() {
  if (!composition_stack_.empty()) {
    composition_stack_.pop_back();
  }
}

// ============================================================
// Static parsing methods
// ============================================================

CompositionArc Compositor::ParseReference(const std::string& ref_str) {
  CompositionArc arc;
  arc.type = ArcType::Reference;

  // Format: @asset_path@</prim/path> or </prim/path> for internal
  std::string str = ref_str;

  // Check for asset path
  if (!str.empty() && str[0] == '@') {
    size_t end = str.find('@', 1);
    if (end != std::string::npos) {
      arc.asset_path = str.substr(1, end - 1);
      str = str.substr(end + 1);
    }
  }

  // Check for prim path
  size_t prim_start = str.find('<');
  size_t prim_end = str.find('>');
  if (prim_start != std::string::npos && prim_end != std::string::npos) {
    arc.prim_path = str.substr(prim_start + 1, prim_end - prim_start - 1);
  }

  arc.is_internal = arc.asset_path.empty();

  return arc;
}

CompositionArc Compositor::ParsePayload(const std::string& payload_str) {
  CompositionArc arc = ParseReference(payload_str);
  arc.type = ArcType::Payload;
  return arc;
}

VariantSelection Compositor::ParseVariantSelection(const std::string& str) {
  VariantSelection sel;

  size_t eq = str.find('=');
  if (eq != std::string::npos) {
    sel.variant_set = str.substr(0, eq);
    sel.variant_name = str.substr(eq + 1);
  }

  return sel;
}

// ============================================================
// Utility functions
// ============================================================

void FlattenLayer(Layer& layer) {
  // Clear composition arc metadata from all prims
  for (size_t i = 0; i < layer.prim_count(); ++i) {
    PrimSpec* prim = layer.prim_mutable(static_cast<uint32_t>(i));
    if (prim) {
      prim->meta().references.clear();
      prim->meta().payloads.clear();
      prim->meta().inherits.clear();
      prim->meta().specializes.clear();
      prim->meta().variantSelection.clear();
    }
  }
}

std::vector<CompositionArc> GetExternalReferences(const Layer& layer) {
  std::vector<CompositionArc> result;

  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* prim = layer.prim(static_cast<uint32_t>(i));
    if (!prim) continue;

    for (const auto& ref_str : prim->meta().references) {
      CompositionArc arc = Compositor::ParseReference(ref_str);
      if (!arc.is_internal) {
        result.push_back(arc);
      }
    }
  }

  return result;
}

std::vector<CompositionArc> GetPayloads(const Layer& layer) {
  std::vector<CompositionArc> result;

  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* prim = layer.prim(static_cast<uint32_t>(i));
    if (!prim) continue;

    for (const auto& payload_str : prim->meta().payloads) {
      result.push_back(Compositor::ParsePayload(payload_str));
    }
  }

  return result;
}

bool HasCompositionArcs(const PrimSpec& prim) {
  const auto& meta = prim.meta();
  return !meta.references.empty() ||
         !meta.payloads.empty() ||
         !meta.inherits.empty() ||
         !meta.specializes.empty() ||
         !meta.variantSelection.empty();
}

}  // namespace next
}  // namespace tinyusdz
