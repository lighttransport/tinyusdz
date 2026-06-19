// SPDX-License-Identifier: Apache 2.0
// Unit tests for USD Value Clips metadata parsing (multiple clip sets,
// templates). Exercises ParseAllClipSetMetadata.

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-value-clips.h"
#include "tinyusdz.hh"
#include "value-clip-utils.hh"

#include <string>
#include <vector>

using namespace tinyusdz;

static bool LoadUSDA(const std::string &usda, Stage *stage) {
  std::string warn, err;
  bool ok = tinyusdz::LoadUSDFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(), "mem.usda",
      stage, &warn, &err);
  if (!ok) {
    TEST_MSG("USDA parse failed: %s", err.c_str());
  }
  return ok;
}

// Two named clip sets on one prim. ParseAllClipSetMetadata must return both,
// each with its own assetPaths / primPath (previously only the first was kept).
void value_clips_multiple_sets_parse_test(void) {
  const std::string usda =
      "#usda 1.0\n"
      "\n"
      "def Xform \"World\" (\n"
      "    clips = {\n"
      "        dictionary anim = {\n"
      "            asset[] assetPaths = [@anim.0.usd@, @anim.1.usd@]\n"
      "            double2[] active = [(0, 0), (1, 1)]\n"
      "            double2[] times = [(0, 0), (1, 1)]\n"
      "            string primPath = \"/World/anim\"\n"
      "        }\n"
      "        dictionary layout = {\n"
      "            asset[] assetPaths = [@layout.0.usd@]\n"
      "            double2[] active = [(0, 0)]\n"
      "            string primPath = \"/World/layout\"\n"
      "        }\n"
      "    }\n"
      ")\n"
      "{\n"
      "}\n";

  Stage stage;
  TEST_CHECK(LoadUSDA(usda, &stage));

  auto pr = stage.GetPrimAtPath(Path("/World", ""));
  TEST_CHECK(pr && pr.value());
  if (!pr || !pr.value()) return;

  TEST_CHECK(pr.value()->metas().has_clips());
  Dictionary clips_dict = pr.value()->metas().get_clips();

  std::vector<ClipSetMetadata> sets;
  std::string err;
  bool ok = ParseAllClipSetMetadata(clips_dict, &sets, &err);
  TEST_CHECK_(ok, "ParseAllClipSetMetadata failed: %s", err.c_str());

  // Both clip sets must be parsed (the old code returned only the first).
  TEST_CHECK_(sets.size() == 2, "expected 2 clip sets, got %zu", sets.size());
  if (sets.size() != 2) return;

  // Sets are returned in name-sorted order: "anim" before "layout".
  TEST_CHECK(sets[0].primPath == "/World/anim");
  TEST_CHECK_(sets[0].assetPaths.size() == 2, "anim assetPaths = %zu",
              sets[0].assetPaths.size());

  TEST_CHECK(sets[1].primPath == "/World/layout");
  TEST_CHECK_(sets[1].assetPaths.size() == 1, "layout assetPaths = %zu",
              sets[1].assetPaths.size());
}

// A template-based clip set must expand into concrete assetPaths during
// parsing, and coexist with an explicit set.
void value_clips_template_set_parse_test(void) {
  const std::string usda =
      "#usda 1.0\n"
      "\n"
      "def Xform \"World\" (\n"
      "    clips = {\n"
      "        dictionary tmpl = {\n"
      "            asset templateAssetPath = @clip.#.usd@\n"
      "            double templateStartTime = 1\n"
      "            double templateEndTime = 3\n"
      "            double templateStride = 1\n"
      "            string primPath = \"/World\"\n"
      "        }\n"
      "    }\n"
      ")\n"
      "{\n"
      "}\n";

  Stage stage;
  TEST_CHECK(LoadUSDA(usda, &stage));

  auto pr = stage.GetPrimAtPath(Path("/World", ""));
  TEST_CHECK(pr && pr.value());
  if (!pr || !pr.value()) return;

  Dictionary clips_dict = pr.value()->metas().get_clips();

  std::vector<ClipSetMetadata> sets;
  std::string err;
  bool ok = ParseAllClipSetMetadata(clips_dict, &sets, &err);
  TEST_CHECK_(ok, "ParseAllClipSetMetadata failed: %s", err.c_str());
  TEST_CHECK_(sets.size() == 1, "expected 1 clip set, got %zu", sets.size());
  if (sets.size() != 1) return;

  // templateStartTime=1, endTime=3, stride=1 -> 3 expanded clips (1,2,3).
  TEST_CHECK_(sets[0].assetPaths.size() == 3, "expanded assetPaths = %zu",
              sets[0].assetPaths.size());
}
