// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP namespace mapping + layer offset (header-only)
//
// Minimal prefix-remap (analogue of OpenUSD PcpMapFunction) used to translate a
// referenced/inherited prim's namespace into the referencing prim's namespace.
// Phase 1 operates on full path strings (prefix replace). Standalone, C++14.

#pragma once

#include <string>

namespace tinyusdz {
namespace next {
namespace pcp {

/// Time offset/scale applied to time samples coming through an arc.
struct LayerOffset {
  double offset = 0.0;
  double scale = 1.0;

  /// Compose: `child` applied underneath `this` (this ∘ child).
  LayerOffset Compose(const LayerOffset &child) const {
    return LayerOffset{offset + scale * child.offset, scale * child.scale};
  }
  double Apply(double t) const { return offset + scale * t; }
  bool is_identity() const { return offset == 0.0 && scale == 1.0; }
};

/// Prefix remap: paths under `source_prefix` are rewritten under `target_prefix`.
/// Empty/empty == identity. Stores the mapping from a node's namespace to ROOT
/// (composed once when the child arc is created).
struct NamespaceMapping {
  std::string source_prefix;  // e.g. "/Ref"   (referenced prim path)
  std::string target_prefix;  // e.g. "/World/A" (referencing prim path)

  bool is_identity() const {
    return source_prefix.empty() && target_prefix.empty();
  }

  /// Map a path string from source namespace to target namespace.
  std::string Apply(const std::string &path) const {
    if (is_identity()) return path;
    if (path == source_prefix) return target_prefix;
    // Match "source_prefix/..." boundary so "/RefFoo" is not matched by "/Ref".
    if (path.size() > source_prefix.size() &&
        path.compare(0, source_prefix.size(), source_prefix) == 0 &&
        path[source_prefix.size()] == '/') {
      return target_prefix + path.substr(source_prefix.size());
    }
    return path;  // Outside this arc's namespace; unchanged.
  }

  /// Compose two mappings. `outer` maps mid->root, `inner` maps deep->mid;
  /// the result maps deep->root (so a nested reference resolves straight to
  /// root space).
  static NamespaceMapping Compose(const NamespaceMapping &outer,
                                  const NamespaceMapping &inner) {
    if (inner.is_identity()) return outer;
    if (outer.is_identity()) return inner;
    NamespaceMapping out;
    out.source_prefix = inner.source_prefix;
    out.target_prefix = outer.Apply(inner.target_prefix);
    return out;
  }
};

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
