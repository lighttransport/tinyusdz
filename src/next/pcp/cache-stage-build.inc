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
      const std::vector<Src> &srcs = SourcesForPath(w.src_path, warn, err);
      auto ps1 = prof ? ProfClock::now() : ProfClock::time_point{};

      // STRUCTURE PASS: fix this prim's slot, parent, child order and instancing
      // now, but DEFER opinion composition to the parallel fill below. Child-name
      // order (ComposeChildNames) and instancing (RegisterInstance) depend only on
      // `srcs`, never on the prim's own composed opinions, so deferring opinions
      // does not change the tree the walk produces.
      PrimSpec spec(w.out_path.name());
      spec.set_path(w.out_path);
      std::vector<std::string> children = ComposeChildNames(srcs);
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
      RegisterInstance(w.out_path.str(), srcs);
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
      fill_.push_back({idx, w.src_path.str()});

      if (is_instance) continue;  // prototype provides the subtree

      // Push children in reverse order so the first child processes first
      // (preserving the original visitation order).
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const std::string &cn = *it;
        Path child_src = w.src_path.append_child(cn);
        Path child_out;
        auto rit = relocates_map.find(child_src.str());
        if (rit != relocates_map.end()) {
          if (Path(rit->second).parent().str() == w.out_path.str()) {
            child_out = Path(rit->second);  // same-parent rename
          } else {
            continue;  // departs; the destination parent's visit pulls it in
          }
        } else {
          child_out = w.out_path.append_child(cn);
        }
        stack.push_back({child_src, child_out, idx, /*is_root=*/false,
                         w.depth + 1});
      }
      // Prims relocated INTO this prim from another parent.
      {
        auto ait = relocate_arrivals.find(w.out_path.str());
        if (ait != relocate_arrivals.end()) {
          for (const auto &a : ait->second) {
            stack.push_back({Path(a.first), Path(a.second), idx,
                             /*is_root=*/false, w.depth + 1});
          }
        }
      }
    }
  }
