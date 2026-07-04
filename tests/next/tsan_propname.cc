// Targeted TSan repro for the PropNameTable frozen-fast-path data race.
// freeze() then concurrent lock-free reads + a genuinely-new intern = UB.
#include "next/layer/property-index.hh"
#include <thread>
#include <vector>
#include <string>
#include <cstdio>

using namespace tinyusdz::next;

int main() {
  auto& t = GetPropNameTable();
  // Pre-intern a pool of names (all HIT after freeze).
  std::vector<PropNameId> ids;
  for (int i = 0; i < 4000; i++) ids.push_back(t.intern("pre_" + std::to_string(i)));

  // Enter the "frozen read-only" phase, as FillOpinions does.
  t.freeze();

  // Readers do lock-free frozen find()/get() while one writer interns a
  // genuinely-new name (unfreeze + wlk push_back/emplace) -> the racing pair.
  std::vector<std::thread> pool;
  for (int r = 0; r < 6; r++) {
    pool.emplace_back([&, r]() {
      for (int i = 0; i < 300000; i++) {
        (void)t.find("pre_" + std::to_string(i % 4000));   // lock-free frozen miss/hit
        (void)t.get(ids[i % ids.size()]);                  // lock-free frozen get
        (void)t.find("absent_" + std::to_string(i % 97));  // lock-free frozen MISS
      }
    });
  }
  // The writer: re-assert frozen right before each genuinely-new intern (as the
  // real compose re-freezes each FillOpinions), so frozen==true is observed by
  // concurrent lock-free readers WHILE this thread mutates names_/name_to_id_.
  // Few inserts (publish_snapshot copies the map each time), spaced across the
  // readers' run so the racy transition overlaps live readers.
  pool.emplace_back([&]() {
    for (int i = 0; i < 400; i++) {
      t.freeze();
      (void)t.intern("new_" + std::to_string(i));
      for (volatile int s = 0; s < 20000; s++) {}  // spacing, keep readers live
    }
  });
  for (auto& th : pool) th.join();
  std::printf("done size=%zu\n", t.size());
  return 0;
}
