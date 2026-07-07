// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Variant holder materialization for the crate writer (see header).

#include "variant-holders.hh"

#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

namespace {

std::string HolderPath(const std::string& owner, const std::string& set,
                       const std::string& variant) {
  return owner + "/{" + set + "=" + variant + "}";
}

bool IsBracketedName(const std::string& name) {
  return name.size() >= 2 && name.front() == '{' && name.back() == '}';
}

// Copy the parts of `src` a variant content root ("__self__") contributes to
// its holder prim: typed properties (defaults + declared type names +
// per-property metadata), relationships, and connections. Time samples inside
// variant content are not carried over (rare; unsupported for now).
void MergeContentRootInto(const PrimSpec& src, PrimSpec* dst) {
  PropNameTable& names = GetPropNameTable();
  for (const PropSlot& slot : src.properties().slots()) {
    if (slot.is_relationship()) continue;
    const std::string& pname = names.get(slot.name_id);
    const Value* v = src.property_value(slot.name_id);
    if (v) {
      dst->add_property(pname, *v, slot.flags);
    } else {
      dst->add_property_slot(slot.name_id,
                             static_cast<TypeId>(slot.value_type), slot.flags);
    }
    if (const std::string* tn = src.property_type_name(pname)) {
      dst->set_property_type_name(pname, *tn);
    }
    if (const PropMeta* pm = src.property_meta(slot.name_id)) {
      dst->ensure_property_meta(slot.name_id) = *pm;
    }
    if (const std::vector<Path>* conns = src.connection(pname)) {
      for (const Path& t : *conns) dst->add_connection(pname, t);
    }
  }
  for (const std::string& rel : src.relationship_names()) {
    if (const std::vector<Path>* targets = src.relationship(rel)) {
      dst->set_relationship_targets(rel, *targets);
    }
  }
  if (!src.type_name().empty() && dst->type_name().empty()) {
    dst->set_type_name(src.type_name());
  }
}

// Build one "{set=variant}" holder prim from inline VariantData and append it
// (plus any content subtree prims) to `out`. Returns the holder's prim index.
uint32_t AppendVariantHolder(Layer* out, const std::string& owner_path,
                             const std::string& set_name,
                             const VariantData& vd) {
  const std::string hp = HolderPath(owner_path, set_name, vd.name);
  PrimSpec holder("{" + set_name + "=" + vd.name + "}");
  holder.set_path(Path(hp));

  // Inline option opinions.
  for (const VariantProperty& vp : vd.properties) {
    holder.add_property(vp.name, vp.value, vp.flags);
  }
  for (const auto& rel : vd.relationships) {
    holder.set_relationship_targets(rel.first, rel.second);
  }
  holder.meta().active = vd.active;
  holder.meta().hidden = vd.hidden;
  if (!vd.doc.empty()) holder.meta().doc() = vd.doc;
  holder.meta().references = vd.references;
  holder.meta().payloads = vd.payloads;
  holder.meta().inherits = vd.inherits;
  holder.meta().specializes = vd.specializes;
  // Nested variant sets: keep them inline on the holder; the caller
  // materializes them recursively (the holder becomes an owning prim).
  if (!vd.variantSets.empty()) {
    holder.meta().variantSets() = vd.variantSets;
  }

  // Content subtree: the content layer's "/__self__" root carries opinions
  // for the holder itself; its descendants become the holder's sub-prims.
  uint32_t holder_idx = UINT32_MAX;
  if (vd.content) {
    const Layer& content = *vd.content;
    for (size_t i = 0; i < content.prim_count(); ++i) {
      const PrimSpec* cp = content.prim(static_cast<uint32_t>(i));
      if (!cp) continue;
      const std::string& cpath = cp->path().str();
      if (cpath == "/__self__") {
        MergeContentRootInto(*cp, &holder);
        continue;
      }
      static const char kSelfPrefix[] = "/__self__/";
      if (cpath.compare(0, sizeof(kSelfPrefix) - 1, kSelfPrefix) != 0) {
        continue;  // unexpected layout; skip defensively
      }
      PrimSpec child = cp->Clone();
      child.set_path(Path(hp + "/" + cpath.substr(sizeof(kSelfPrefix) - 1)));
      // Child links reference the content layer and are meaningless in the
      // output layer; the crate writer and reader are path-based.
      child.clear_child_indices();
      out->add_prim(std::move(child));
    }
  }

  holder.clear_child_indices();
  holder_idx = out->add_prim(std::move(holder));
  return holder_idx;
}

bool PrimNeedsHolders(const Layer& layer, const PrimSpec& prim) {
  if (IsBracketedName(prim.name())) return false;
  for (const VariantSetData& vs : prim.meta().variantSets()) {
    for (const VariantData& vd : vs.variants) {
      if (!layer.prim_at_path(HolderPath(prim.path().str(), vs.name,
                                         vd.name))) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

bool LayerNeedsVariantHolders(const Layer& layer) {
  for (const PrimSpec& prim : layer.prims()) {
    if (PrimNeedsHolders(layer, prim)) return true;
  }
  return false;
}

Layer MaterializeVariantHolders(const Layer& layer) {
  Layer out = layer.Clone();

  // Process a growing worklist: holders appended for nested variant sets are
  // owners themselves and get their own holders in later iterations.
  for (size_t i = 0; i < out.prim_count(); ++i) {
    // Re-fetch each round: add_prim may reallocate the prim vector.
    const std::string owner_path = out.prim(static_cast<uint32_t>(i))
                                       ->path()
                                       .str();
    const std::vector<VariantSetData> sets =
        out.prim(static_cast<uint32_t>(i))->meta().variantSets();
    if (sets.empty()) continue;

    for (const VariantSetData& vs : sets) {
      // "{set=}" VariantSet declaration spec (pxr enumerates variantChildren
      // from it). Create it when any option holder is being synthesized.
      const std::string decl_path = HolderPath(owner_path, vs.name, "");
      bool needs_decl = false;

      for (const VariantData& vd : vs.variants) {
        if (out.prim_at_path(HolderPath(owner_path, vs.name, vd.name))) {
          continue;  // crate-read layer: holder already present
        }
        AppendVariantHolder(&out, owner_path, vs.name, vd);
        out.build_path_index();  // keep lookups current for nested/self refs
        needs_decl = true;
      }

      if (needs_decl && !out.prim_at_path(decl_path)) {
        PrimSpec decl("{" + vs.name + "=}");
        decl.set_path(Path(decl_path));
        out.add_prim(std::move(decl));
        out.build_path_index();
      }
    }
  }

  out.build_path_index();
  return out;
}

}  // namespace next
}  // namespace tinyusdz
