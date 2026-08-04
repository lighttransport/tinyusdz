// SPDX-License-Identifier: Apache-2.0
// tusdview - cancellation + progress + budget token shared between the async
// loader worker thread and the UI. All cross-thread fields are atomic.
#pragma once

#include <atomic>
#include <cstddef>

namespace tusdview {

enum class LoadDetailPhase {
  Parsing = 0,
  Composing,
  Converting,
  ProcessingTextures,
  Finalizing,
};

struct LoadControl {
  // --- cancellation (UI -> worker) ---
  std::atomic<bool> cancel{false};

  // --- progress (worker -> UI) ---
  std::atomic<int> stage{0};               // tydra DetailedProgressInfo::Stage
  std::atomic<long long> meshesDone{0};
  std::atomic<long long> meshesTotal{0};
  std::atomic<long long> payloadsDone{0};
  std::atomic<long long> payloadsTotal{0};
  std::atomic<long long> texturesDone{0};
  std::atomic<long long> texturesTotal{0};
  std::atomic<int> phasePermille{0};
  std::atomic<int> detailPhase{static_cast<int>(LoadDetailPhase::Parsing)};

  // --- budgets (set before a load; enforced by worker) ---
  // Abort Tydra conversion if it runs longer than this (0 = unlimited).
  double convertTimeBudgetSec{0.0};
  // Stop building DrawScene once these caps are hit (prevents per-frame freeze
  // and VRAM thrashing on huge scenes); the scene is marked truncated.
  std::size_t maxTriangles{30ull * 1000 * 1000};      // 30M triangles
  std::size_t maxVertexBytes{1024ull * 1024 * 1024};  // ~1 GiB of vertex data

  void resetProgress() {
    cancel.store(false);
    stage.store(0);
    meshesDone.store(0);
    meshesTotal.store(0);
    payloadsDone.store(0);
    payloadsTotal.store(0);
    texturesDone.store(0);
    texturesTotal.store(0);
    phasePermille.store(0);
    detailPhase.store(static_cast<int>(LoadDetailPhase::Parsing));
  }
};

}  // namespace tusdview
