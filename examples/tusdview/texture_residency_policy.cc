// SPDX-License-Identifier: Apache-2.0
#include "texture_residency_policy.hh"

#include <algorithm>

namespace tusdview {

std::vector<size_t> OrderTextureDecodeCandidates(
    const std::vector<TextureDemandPriority>& priorities,
    const std::vector<uint8_t>& eligible) {
  const size_t count = std::min(priorities.size(), eligible.size());
  std::vector<size_t> result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (eligible[i] && priorities[i] != TextureDemandPriority::None)
      result.push_back(i);
  }
  std::stable_sort(result.begin(), result.end(), [&](size_t a, size_t b) {
    return static_cast<uint8_t>(priorities[a]) <
           static_cast<uint8_t>(priorities[b]);
  });
  return result;
}

size_t ChooseTextureEvictionVictim(const TextureEvictionCandidate* candidates,
                                   size_t candidateCount,
                                   double graceSeconds) {
  size_t victim = candidateCount;
  for (size_t i = 0; i < candidateCount; ++i) {
    const TextureEvictionCandidate& candidate = candidates[i];
    if (candidate.selected || candidate.decoding ||
        candidate.residentBytes == 0 ||
        candidate.secondsSinceWanted < graceSeconds) {
      continue;
    }
    if (victim == candidateCount ||
        candidate.secondsSinceWanted > candidates[victim].secondsSinceWanted ||
        (candidate.secondsSinceWanted == candidates[victim].secondsSinceWanted &&
         candidate.slot < candidates[victim].slot)) {
      victim = i;
    }
  }
  return victim;
}

}  // namespace tusdview
