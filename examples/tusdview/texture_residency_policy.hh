// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tusdview {

// Lower values are scheduled first. Keep the numeric order stable: reports and
// tests use it to describe why a texture was requested.
enum class TextureDemandPriority : uint8_t {
  Selected = 0,
  Visible = 1,
  Margin = 2,
  Background = 3,
  None = 4,
};

// Return eligible texture indices ordered by demand priority, preserving slot
// order within a priority band for deterministic refinement.
std::vector<size_t> OrderTextureDecodeCandidates(
    const std::vector<TextureDemandPriority>& priorities,
    const std::vector<uint8_t>& eligible);

struct TextureEvictionCandidate {
  size_t slot{0};
  double secondsSinceWanted{0.0};
  size_t residentBytes{0};
  bool selected{false};
  bool decoding{false};
};

// Select the least-recently-wanted evictable slot. Ties use the lower stable
// slot id. Returns `candidateCount` when no item passed the grace/pinning gates.
size_t ChooseTextureEvictionVictim(const TextureEvictionCandidate* candidates,
                                   size_t candidateCount,
                                   double graceSeconds);

}  // namespace tusdview
