// SPDX-License-Identifier: Apache-2.0
#include "budget.hh"

#include "ui.hh"

namespace tusdql {

PreviewBudget PreviewBudget::Install(uint64_t total_bytes) {
  MemBudget::Get().InitBytes(total_bytes);

  PreviewBudget b;
  // Read the cap back rather than trusting the request: InitBytes clamps
  // against MemAvailable, so on a small machine the shares must shrink too.
  b.total = MemBudget::Get().Cap();

  b.stage = b.total * 55 / 100;
  b.geometry = b.total * 25 / 100;
  b.textures = b.total * 10 / 100;
  b.render = b.total - b.stage - b.geometry - b.textures;
  return b;
}

uint64_t PreviewBudget::RemainingTracked() {
  const uint64_t cap = MemBudget::Get().Cap();
  const uint64_t used = MemBudget::Get().Tracked();
  return cap > used ? cap - used : 0;
}

std::string PreviewBudget::FormatUsage() const {
  return FormatBytes(MemBudget::Get().Tracked()) + " / " + FormatBytes(total);
}

}  // namespace tusdql
