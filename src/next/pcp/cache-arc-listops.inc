// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP arc list-op merge helpers.
// Included inside Cache::Impl.

  // --- cross-layer list-op merge (Phase 7 S5) -----------------------------

  enum class ArcSel { References, Payloads, Inherits, Specializes };

  static const std::vector<std::string> &SelectInlineArc(const PrimSpecMeta &m,
                                                         ArcSel f) {
    switch (f) {
      case ArcSel::References: return m.references;
      case ArcSel::Payloads: return m.payloads;
      case ArcSel::Inherits: return m.inherits;
      default: return m.specializes;
    }
  }

  static const ArcEdit *SelectArcEdit(const PrimSpecMeta &m, ArcSel f) {
    const ArcListOpEdits *e = m.arc_edits();
    if (!e) return nullptr;
    switch (f) {
      case ArcSel::References: return &e->references;
      case ArcSel::Payloads: return &e->payloads;
      case ArcSel::Inherits: return &e->inherits;
      default: return &e->specializes;
    }
  }

  // Compose one arc field across a site's specs (input is strong-first) per the
  // AOUSD list-op rules, applying weakest->strongest. Returns (arc-string,
  // authoring-layer-id) pairs so relative reference/payload asset paths can be
  // anchored to the layer that authored them.
  std::vector<std::pair<std::string, std::string>> MergeArcField(
      const std::vector<SpecRef> &specs, ArcSel f) const {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto it = specs.rbegin(); it != specs.rend(); ++it) {
      const PrimSpecMeta &m = it->spec->meta();
      const std::string &lid = it->layer_id;
      const ArcEdit *e = SelectArcEdit(m, f);
      const std::vector<std::string> &inl = SelectInlineArc(m, f);
      if ((!e || !e->authored) && inl.empty()) {
        continue;
      }
      auto tag = [&](const std::vector<std::string> &v) {
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(v.size());
        for (const std::string &s : v) out.emplace_back(s, lid);
        return out;
      };
      if (!e || !e->authored || e->is_explicit) {
        result = tag(inl);
        continue;
      }
      auto removeAll = [&](const std::vector<std::string> &rm) {
        for (const std::string &x : rm) {
          result.erase(std::remove_if(result.begin(), result.end(),
                                      [&](const auto &p) { return p.first == x; }),
                       result.end());
        }
      };
      removeAll(e->deleted);
      removeAll(e->prepended);
      removeAll(e->appended);
      for (const auto &added : tag(e->added)) {
        const bool present = std::find_if(
            result.begin(), result.end(), [&](const auto &p) {
              return p.first == added.first;
            }) != result.end();
        if (!present) result.push_back(added);
      }
      std::vector<std::pair<std::string, std::string>> merged;
      merged.reserve(e->prepended.size() + result.size() + e->appended.size());
      auto pre = tag(e->prepended), app = tag(e->appended);
      merged.insert(merged.end(), pre.begin(), pre.end());
      merged.insert(merged.end(), result.begin(), result.end());
      merged.insert(merged.end(), app.begin(), app.end());
      result.swap(merged);
    }
    return result;
  }
