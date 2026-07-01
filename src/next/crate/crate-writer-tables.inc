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

  uint32_t InternPath(const std::string& path) {
    std::string_view key(path);
    if (frozen_path_) {
      auto it = frozen_path_->find(key);
      if (it != frozen_path_->end()) return *it;
      frozen_miss_ = true;
      return 0;
    }
    auto it = path_to_index_.find(key);
    if (it != path_to_index_.end()) return *it;
    uint32_t idx = static_cast<uint32_t>(paths_.size());
    paths_.push_back(path);
    path_to_index_.set(std::string_view(paths_.back()), idx);
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
        InternToken(schema);
      }
      for (const auto& ref : prim.meta().references) {
        InternString(ref);
      }
      for (const auto& inher : prim.meta().inherits) {
        InternString(inher);
      }
    }
  }
