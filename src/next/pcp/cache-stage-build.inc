// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache stage materialization and instance flattening.
// Included inside Cache::Impl.

  // --- BuildStage ---------------------------------------------------------

  // `src_path` is where composition opinions are gathered; `out_path` is where
  // the composed prim is placed (differs when a relocate renames it).
  struct BuildStageWork {
    Path src_path;
    Path out_path;
    uint32_t parent_idx = 0;
    bool is_root = false;
    uint32_t depth = 0;
  };

  // Temporary internal-breakdown profiling accumulators (ns), summed across all
  // root subtrees; printed by the top-level BuildStage under TINYUSDZ_NEXT_TIMING.
  mutable uint64_t prof_sources_ns_ = 0;
  mutable uint64_t prof_compose_ns_ = 0;
  mutable uint64_t prof_reg_ns_ = 0;

  void BuildStage(Layer *out, const BuildStageWork &root_work,
                  std::string *warn, std::string *err) {
    using ProfClock = std::chrono::steady_clock;
    const bool prof = options.enable_timing;
    std::vector<BuildStageWork> stack;
    stack.push_back(root_work);

    while (!stack.empty()) {
      BuildStageWork w = stack.back();
      stack.pop_back();

      // Namespace-depth backstop: an arc-induced namespace that keeps growing
      // (e.g. an ancestor cycle a stronger check missed) must surface as an
      // error, never as C++ stack exhaustion.
      if (w.depth > options.max_namespace_depth) {
        AddIssue(ErrorCode::MaxDepthExceeded, w.out_path.str(),
                 "BuildStage max namespace depth exceeded at: " + w.out_path.str(),
                 err);
        continue;
      }
      auto ps0 = prof ? ProfClock::now() : ProfClock::time_point{};
      // Parallel warm keys stack-0 sources directly by the composed path.
      // Avoid SourcesKey's path-string copy on the overwhelmingly common hit;
      // retain the full resolver for an unwarmed/mutated miss.
      const std::vector<Src>* cached_srcs = sources_cache.find(w.src_path.str());
      const std::vector<Src> &srcs = cached_srcs
                                         ? *cached_srcs
                                         : SourcesForPath(w.src_path, warn, err);
      auto ps1 = prof ? ProfClock::now() : ProfClock::time_point{};

      // STRUCTURE PASS: fix this prim's slot, parent, child order and instancing
      // now, but DEFER opinion composition to the parallel fill below. Child-name
      // order (ComposeChildNames) and instancing (RegisterInstance) depend only on
      // `srcs`, never on the prim's own composed opinions, so deferring opinions
      // does not change the tree the walk produces.
      PrimSpec spec(w.out_path.name());
      spec.set_path(w.out_path);
      std::vector<std::string> children = ComposeChildNames(srcs);
      // pxr instancing rule: descendants of an instanceable prim compose ONLY
      // from the instance's key-defining arcs (direct references/payloads and
      // implied classes) — "overrides on descendent prims are ignored". Local
      // and ancestral-positional sources still apply to the instance ROOT
      // itself (property overrides there are allowed), so this prim's own
      // opinion fill keeps the full source list; only the CHILD derivation is
      // filtered (recursively, via the pre-seeded source cache below).
      std::vector<Src> shared_srcs;
      bool subtree_filtered = false;
      if (IsInstanceableSources(srcs)) {
        shared_srcs.reserve(srcs.size());
        for (const Src &s : srcs) {
          if (!s.variant && Mapping(s.map_idx).MapsFrom(s.site)) {
            shared_srcs.push_back(s);
          }
        }
        if (!shared_srcs.empty()) {
          subtree_filtered = true;
          children = ComposeChildNames(shared_srcs);
        }
      }
      auto ps2 = prof ? ProfClock::now() : ProfClock::time_point{};
      if (prof) {
        prof_sources_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ps1 - ps0).count();
        prof_compose_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ps2 - ps1).count();
      }

      // Instancing: register this prim. If it is an instance (not the prototype
      // of its group), link it to the prototype and do NOT duplicate its subtree
      // -- children are provided by the prototype (UsdPrim follows
      // instance_prototype).
      auto pr0 = prof ? ProfClock::now() : ProfClock::time_point{};
      // A deactivated prim composes no subtree and forms NO instance in pxr —
      // registering it would extract an orphan Flattened_Prototype for
      // content the flatten output prunes.
      if (ComposedActive(srcs)) RegisterInstance(w.out_path.str(), srcs);
      if (prof) prof_reg_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ProfClock::now() - pr0).count();
      bool is_instance = false;
      {
        auto pit = prototype_of.find(w.out_path.str());
        if (pit != prototype_of.end() && pit->second != w.out_path.str()) {
          spec.meta().instance_prototype() = pit->second;
          is_instance = true;
        }
      }

      uint32_t idx = out->add_prim(std::move(spec));
      if (w.is_root) {
        out->add_root(idx);
      } else {
        out->set_parent(idx, w.parent_idx);
      }
      // Opinions for this slot are filled (in parallel) after the whole walk.
      fill_.push_back({idx, &srcs});

      if (is_instance) continue;  // prototype provides the subtree

      // A deactivated prim composes NO subtree (pxr prunes children of an
      // inactive prim; usdcat --flatten drops the prim itself too — the prim
      // is kept here, with its authored active=false, so stage consumers can
      // still query IsActive on it).
      if (!ComposedActive(srcs)) continue;

      // Push children in reverse order so the first child processes first
      // (preserving the original visitation order). `children` is already the
      // post-relocation child list: departed names are gone and relocated-in
      // names are present (ComposeChildNames), and each child's sources resolve
      // back to its pre-relocation site (DeriveChildSources).
      if (subtree_filtered) {
        // Pre-seed each child's composed-namespace source cache with the
        // filtered derivation so the entire prototype subtree (recursively)
        // composes shared content only.
        for (const std::string &cn : children) {
          const Path child = w.out_path.append_child(cn);
          const std::string ck = SourcesKey(0, child.str());
          if (!sources_cache.contains(ck)) {
            std::vector<Src> base =
                DeriveChildSources(shared_srcs, cn, child.str(), warn, err);
            sources_cache.get_or_create(ck) = ExpandList(base, child.str(), warn, err);
          }
        }
      }
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const Path child = w.out_path.append_child(*it);
        stack.push_back({child, child, idx, /*is_root=*/false, w.depth + 1});
      }
    }
  }
