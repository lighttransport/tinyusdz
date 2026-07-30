// SPDX-License-Identifier: Apache-2.0
#include "texture_residency_policy.hh"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,  \
                   #expr);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

void TestPriorityOrder() {
  using tusdview::TextureDemandPriority;
  const std::vector<TextureDemandPriority> priorities = {
      TextureDemandPriority::Background, TextureDemandPriority::Visible,
      TextureDemandPriority::Selected, TextureDemandPriority::Margin,
      TextureDemandPriority::Visible, TextureDemandPriority::None};
  const std::vector<uint8_t> eligible = {1, 1, 1, 1, 1, 1};
  const std::vector<size_t> ordered =
      tusdview::OrderTextureDecodeCandidates(priorities, eligible);
  CHECK((ordered == std::vector<size_t>{2, 1, 4, 3, 0}));
}

void TestEligibilityAndStableOrder() {
  using tusdview::TextureDemandPriority;
  const std::vector<TextureDemandPriority> priorities(5,
                                                       TextureDemandPriority::Visible);
  const std::vector<uint8_t> eligible = {1, 0, 1, 0, 1};
  const std::vector<size_t> ordered =
      tusdview::OrderTextureDecodeCandidates(priorities, eligible);
  CHECK((ordered == std::vector<size_t>{0, 2, 4}));
}

void TestEvictionGatesAndLru() {
  using tusdview::TextureEvictionCandidate;
  const TextureEvictionCandidate candidates[] = {
      {0, 20.0, 64, true, false},   // selected: pinned
      {1, 30.0, 64, false, true},   // decoding: pinned
      {2, 1.9, 64, false, false},   // grace
      {3, 8.0, 64, false, false},
      {4, 12.0, 64, false, false},  // oldest eligible
      {5, 99.0, 0, false, false},   // no allocation
  };
  const size_t victim = tusdview::ChooseTextureEvictionVictim(
      candidates, sizeof(candidates) / sizeof(candidates[0]), 2.0);
  CHECK(victim == 4);
}

void TestEvictionTieAndNoVictim() {
  using tusdview::TextureEvictionCandidate;
  const TextureEvictionCandidate tied[] = {
      {9, 4.0, 1, false, false}, {3, 4.0, 1, false, false}};
  CHECK(tusdview::ChooseTextureEvictionVictim(tied, 2, 2.0) == 1);
  const TextureEvictionCandidate pinned[] = {
      {0, 5.0, 1, true, false}, {1, 1.0, 1, false, false}};
  CHECK(tusdview::ChooseTextureEvictionVictim(pinned, 2, 2.0) == 2);
}

}  // namespace

int main() {
  TestPriorityOrder();
  TestEligibilityAndStableOrder();
  TestEvictionGatesAndLru();
  TestEvictionTieAndNoVictim();
  return failures == 0 ? 0 : 1;
}
