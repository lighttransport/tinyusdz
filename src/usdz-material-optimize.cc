// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.

#include "usdz-material-optimize.hh"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/attribute.hh"
#include "core/path.hh"
#include "core/prim-spec.hh"
#include "core/property.hh"
#include "core/relationship.hh"
#include "layer.hh"
#include "prim-pprint.hh"

namespace tinyusdz {
namespace usdz {
namespace {

struct MaterialEntry {
  PrimSpec *prim{nullptr};
  std::vector<PrimSpec> *siblings{nullptr};
  size_t sibling_index{0};
  std::string path;
  std::string key;
};

std::string ViewToString(tstring_view v) {
  return std::string(v.data(), v.size());
}

bool StartsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}

std::string JoinPrimPath(const std::string &parent, const std::string &name) {
  if (parent.empty() || parent == "/") {
    return "/" + name;
  }
  return parent + "/" + name;
}

void ReplaceAll(std::string *s, const std::string &from,
                const std::string &to) {
  if (!s || from.empty()) {
    return;
  }
  size_t pos = 0;
  while ((pos = s->find(from, pos)) != std::string::npos) {
    s->replace(pos, from.size(), to);
    pos += to.size();
  }
}

std::string MakeMaterialKey(const PrimSpec &material,
                            const std::string &material_path) {
  std::string key = prim::print_primspec(material, 0);
  ReplaceAll(&key, material_path, "<MATERIAL>");
  return key;
}

void CollectMaterialsRec(PrimSpec *ps, const std::string &path,
                         std::vector<PrimSpec> *siblings, size_t sibling_index,
                         std::vector<MaterialEntry> *out) {
  if (!ps || !out) {
    return;
  }
  if (ps->typeName() == "Material") {
    MaterialEntry entry;
    entry.prim = ps;
    entry.siblings = siblings;
    entry.sibling_index = sibling_index;
    entry.path = path;
    entry.key = MakeMaterialKey(*ps, path);
    out->push_back(std::move(entry));
  }

  std::vector<PrimSpec> &children = ps->children();
  for (size_t i = 0; i < children.size(); i++) {
    CollectMaterialsRec(&children[i], JoinPrimPath(path, children[i].name()),
                        &children, i, out);
  }

  for (auto &vs : ps->variantSets()) {
    for (auto &variant : vs.second.variantSet) {
      PrimSpec &vps = variant.second;
      const std::string variant_path =
          path + "{" + vs.first + "=" + variant.first + "}";
      CollectMaterialsRec(&vps, variant_path, nullptr, 0, out);
    }
  }
}

std::vector<MaterialEntry> CollectMaterials(Layer *layer) {
  std::vector<MaterialEntry> entries;
  if (!layer) {
    return entries;
  }
  for (auto &kv : layer->primspecs()) {
    CollectMaterialsRec(&kv.second, "/" + kv.second.name(), nullptr, 0,
                        &entries);
  }
  return entries;
}

bool RewritePath(Path *path, const std::map<std::string, std::string> &remap) {
  if (!path) {
    return false;
  }
  const std::string prim = ViewToString(path->prim_part());
  auto it = remap.find(prim);
  if (it == remap.end()) {
    return false;
  }
  const std::string prop = ViewToString(path->prop_part());
  *path = Path(it->second, prop);
  return true;
}

size_t RewriteMaterialBindingProperty(Property *prop,
                                      const std::map<std::string, std::string>
                                          &material_remap) {
  if (!prop || !prop->is_relationship()) {
    return 0;
  }

  Relationship *rel = prop->get_relationship_or_null();
  if (!rel) {
    return 0;
  }

  size_t rewritten = 0;
  if (rel->is_path()) {
    if (RewritePath(&rel->targetPath, material_remap)) {
      rewritten++;
    }
  } else if (rel->is_pathvector()) {
    for (Path &path : rel->targetPathVector) {
      if (RewritePath(&path, material_remap)) {
        rewritten++;
      }
    }
  }
  return rewritten;
}

size_t RewriteMaterialBindingsRec(PrimSpec *ps,
                                  const std::map<std::string, std::string>
                                      &material_remap) {
  if (!ps) {
    return 0;
  }
  size_t rewritten = 0;
  for (auto &kv : ps->props()) {
    if (StartsWith(kv.first, "material:binding")) {
      rewritten += RewriteMaterialBindingProperty(&kv.second, material_remap);
    }
  }
  for (PrimSpec &child : ps->children()) {
    rewritten += RewriteMaterialBindingsRec(&child, material_remap);
  }
  for (auto &vs : ps->variantSets()) {
    for (auto &variant : vs.second.variantSet) {
      rewritten += RewriteMaterialBindingsRec(&variant.second, material_remap);
    }
  }
  return rewritten;
}

size_t RewriteMaterialBindings(Layer *layer,
                               const std::map<std::string, std::string>
                                   &material_remap) {
  if (!layer || material_remap.empty()) {
    return 0;
  }
  size_t rewritten = 0;
  for (auto &kv : layer->primspecs()) {
    rewritten += RewriteMaterialBindingsRec(&kv.second, material_remap);
  }
  return rewritten;
}

bool HasDuplicateAncestor(const std::string &path,
                          const std::map<std::string, std::string> &remap);

size_t EraseDuplicateMaterials(std::vector<MaterialEntry> *materials,
                               const std::map<std::string, std::string>
                                   &material_remap) {
  if (!materials || material_remap.empty()) {
    return 0;
  }

  std::unordered_map<std::vector<PrimSpec> *, std::vector<size_t>> by_parent;
  for (const MaterialEntry &entry : *materials) {
    if (!entry.siblings) {
      // Root-level and variant-root materials are unusual for converter output.
      // Keep them rather than risking layer root metadata/order drift.
      continue;
    }
    if (material_remap.count(entry.path)) {
      if (HasDuplicateAncestor(entry.path, material_remap)) {
        continue;
      }
      by_parent[entry.siblings].push_back(entry.sibling_index);
    }
  }

  size_t erased = 0;
  for (auto &kv : by_parent) {
    std::vector<PrimSpec> *siblings = kv.first;
    std::vector<size_t> indices = std::move(kv.second);
    std::sort(indices.begin(), indices.end(), std::greater<size_t>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (size_t idx : indices) {
      if (idx >= siblings->size()) {
        continue;
      }
      siblings->erase(siblings->begin() + static_cast<std::ptrdiff_t>(idx));
      erased++;
    }
  }
  return erased;
}

bool BuildDedupeRemap(const std::vector<MaterialEntry> &materials,
                      std::map<std::string, std::string> *remap) {
  if (!remap) {
    return false;
  }
  remap->clear();
  std::unordered_map<std::string, std::string> canonical_by_key;
  for (const MaterialEntry &entry : materials) {
    auto it = canonical_by_key.find(entry.key);
    if (it == canonical_by_key.end()) {
      canonical_by_key.emplace(entry.key, entry.path);
      continue;
    }
    if (it->second != entry.path) {
      (*remap)[entry.path] = it->second;
    }
  }
  return true;
}

bool HasDuplicateAncestor(const std::string &path,
                          const std::map<std::string, std::string> &remap) {
  for (const auto &kv : remap) {
    const std::string &ancestor = kv.first;
    if (ancestor.size() >= path.size()) {
      continue;
    }
    if (StartsWith(path, ancestor) && path[ancestor.size()] == '/') {
      return true;
    }
  }
  return false;
}

}  // namespace

bool OptimizeMaterialsInLayer(const UsdzConvertOptions &options, Layer *layer,
                              MaterialOptimizationStats *stats,
                              std::string *warn, std::string *err) {
  if (!layer) {
    if (err) {
      *err = "OptimizeMaterialsInLayer: layer is null.";
    }
    return false;
  }

  if (stats) {
    *stats = MaterialOptimizationStats{};
  }

  std::vector<MaterialEntry> materials = CollectMaterials(layer);
  if (stats) {
    stats->num_materials_before = materials.size();
  }

  if (options.material_optimization == MaterialOptimizationMode::Off) {
    if (stats) {
      stats->num_materials_after = materials.size();
    }
    return true;
  }

  if (options.material_optimization == MaterialOptimizationMode::Preview &&
      warn) {
    *warn += "Material optimization preview mode: lossy shader conversion is "
             "not enabled yet; applying exact material dedupe only.\n";
  }
  if (options.material_optimization == MaterialOptimizationMode::Atlas && warn) {
    *warn += "Material optimization atlas mode: texture atlas generation is not "
             "enabled yet; applying exact material dedupe only.\n";
  }

  std::map<std::string, std::string> material_remap;
  if (!BuildDedupeRemap(materials, &material_remap)) {
    if (err) {
      *err = "OptimizeMaterialsInLayer: failed to build material remap.";
    }
    return false;
  }

  const size_t rewritten = RewriteMaterialBindings(layer, material_remap);
  (void)rewritten;
  const size_t erased = EraseDuplicateMaterials(&materials, material_remap);

  std::vector<MaterialEntry> after = CollectMaterials(layer);
  size_t remaining_duplicates = 0;
  for (const MaterialEntry &entry : after) {
    if (material_remap.count(entry.path)) {
      remaining_duplicates++;
    }
  }
  if (stats) {
    stats->num_materials_after = after.size();
    stats->num_materials_deduped =
        stats->num_materials_before > stats->num_materials_after
            ? stats->num_materials_before - stats->num_materials_after
            : erased;
    stats->num_materials_skipped = remaining_duplicates;
  }

  if (remaining_duplicates > 0 && warn) {
    *warn += "Material optimization found duplicate root/variant materials but "
             "kept them because only child material deletion is supported.\n";
  }

  return true;
}

}  // namespace usdz
}  // namespace tinyusdz
