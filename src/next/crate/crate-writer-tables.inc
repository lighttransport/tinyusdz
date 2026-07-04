// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl token/string/path table helpers.
// Included inside CrateWriter::Impl private section.

  // ============================================================
  // Table building
  // ============================================================

  uint32_t InternToken(std::string_view s) {
    if (frozen_tok_) {
      auto it = frozen_tok_->find(s);
      if (it != frozen_tok_->end()) return *it;
      frozen_miss_ = true;
      return 0;
    }
    auto it = token_to_index_.find(s);
    if (it != token_to_index_.end()) return *it;

    uint32_t idx = static_cast<uint32_t>(tokens_.size());
    token_storage_.emplace_back(s);
    const std::string_view key(token_storage_.back());
    tokens_.push_back(key);
    token_to_index_.set(key, idx);
    return idx;
  }

  uint32_t InternString(std::string_view s) {
    if (frozen_str_) {
      auto it = frozen_str_->find(s);
      if (it != frozen_str_->end()) return *it;
      frozen_miss_ = true;
      return 0;
    }
    auto it = string_to_index_.find(s);
    if (it != string_to_index_.end()) return *it;

    uint32_t idx = static_cast<uint32_t>(string_indices_.size());
    string_storage_.emplace_back(s);
    const std::string_view key(string_storage_.back());

    uint32_t token_idx = InternToken(key);
    string_indices_.push_back(token_idx);
    string_to_index_.set(key, idx);
    return idx;
  }

  // Intern a full path whose storage OUTLIVES this write (the input Layer's
  // own path/target strings, string literals): the canonical (stem, name)
  // views point straight into the caller's bytes — no copy, no concatenation.
  uint32_t InternPathStable(std::string_view full) {
    std::string_view stem, name;
    CanonicalSplitPath(full, &stem, &name);
    return InternPathSplit(stem, name, HashView(stem), /*stable=*/true);
  }

  // Intern a full path from TRANSIENT storage (parsed arc targets, worker
  // merge): copied into path_owned_ on first sight.
  uint32_t InternPathCopy(std::string_view full) {
    std::string_view stem, name;
    CanonicalSplitPath(full, &stem, &name);
    return InternPathSplit(stem, name, HashView(stem), /*stable=*/false);
  }

  // Property fast path: (prim path, property name) with the prim's stem hash
  // computed ONCE per prim by the caller. Valid because property names never
  // contain '.' or '/' (so the canonical split of stem+'.'+name is exactly
  // (stem, name)), and both views are stable (Layer / PropNameTable).
  uint32_t InternPropPath(std::string_view stem, std::string_view name,
                          uint64_t stem_hash) {
    return InternPathSplit(stem, name, stem_hash, /*stable=*/true);
  }

  uint32_t InternPathSplit(std::string_view stem, std::string_view name,
                           uint64_t stem_hash, bool stable) {
    const uint64_t h = PathPairHash(stem_hash, name);
    if (frozen_path_) {
      const uint32_t* it = frozen_path_->find(stem, name, h);
      if (it) return *it;
      frozen_miss_ = true;
      return 0;
    }
    bool found = false;
    FlatPathKeyMap::Entry* e = path_to_index_.probe(stem, name, h, &found);
    if (found) return e->value;
    const uint32_t idx = static_cast<uint32_t>(paths_.size());
    if (!stable) {
      // Re-anchor the views into an owned copy of the full string.
      std::string& owned = path_owned_.emplace_back();
      owned.reserve(stem.size() + (name.empty() ? 0 : name.size() + 1));
      owned.assign(stem.data(), stem.size());
      if (!name.empty()) {
        owned.push_back('.');
        owned.append(name.data(), name.size());
      }
      stem = std::string_view(owned.data(), stem.size());
      if (!name.empty()) {
        name = std::string_view(owned.data() + stem.size() + 1, name.size());
      }
    }
    paths_.push_back(PathRec{stem, name});
    path_to_index_.commit(e, stem, name, idx);
    return idx;
  }

  void BuildTables(const Layer& layer) {
    // Token 0: magic marker expected by pxrUSD's crate reader
    InternToken(";-)");  // software identifier (pxrUSD convention)
    // Token 1: empty string, reserved for root path marker in PATHS
    InternToken("");

    // Common field name tokens
    InternToken("typeName");
    InternToken("specifier");
    InternToken("default");
    InternToken("active");
    InternToken("hidden");
    InternToken("doc");
    InternToken("apiSchemas");
    InternToken("kind");
    InternToken("permission");
    InternToken("variability");
    InternToken("defaultPrim");
    InternToken("upAxis");
    InternToken("metersPerUnit");
    InternToken("timeCodesPerSecond");
    InternToken("startTimeCode");
    InternToken("endTimeCode");
    InternToken("references");
    InternToken("payload");
    InternToken("inherits");
    InternToken("specializes");
    InternToken("variantSets");
    InternToken("variants");
    InternToken("variantSelection");
    InternToken("customData");
    InternToken("comment");
    InternToken("subLayers");

    // Common type name tokens
    InternToken("Xform");
    InternToken("Scope");
    InternToken("Mesh");
    InternToken("Material");
    InternToken("Shader");
    InternToken("Camera");
    InternToken("Sphere");
    InternToken("Cube");
    InternToken("Cylinder");
    InternToken("Cone");
    InternToken("Capsule");
    InternToken("Skeleton");
    InternToken("SkelAnimation");
    InternToken("BlendShape");
    InternToken("SkelRoot");

    // Up/down axis tokens
    InternToken("Y");
    InternToken("Z");

    // Intern layer metadata
    if (!layer.meta().defaultPrim.empty()) {
      InternToken(layer.meta().defaultPrim);
    }
    if (!layer.meta().doc.empty()) {
      InternString(layer.meta().doc);
    }

    // Prim names, type names, property names
    for (const auto& prim : layer.prims()) {
      InternToken(prim.name());
      InternToken(prim.type_name());

      PropNameTable& name_table = GetPropNameTable();
      for (const auto& slot : prim.properties().slots()) {
        InternToken(name_table.get(slot.name_id));
      }

      // Metadata strings
      if (!prim.meta().doc().empty()) InternString(prim.meta().doc());
      for (const auto& schema : prim.meta().apiSchemas()) {
        InternToken(schema.sv());
      }
      for (const auto& ref : prim.meta().references) {
        InternString(ref);
      }
      for (const auto& inher : prim.meta().inherits) {
        InternString(inher);
      }
    }
  }
