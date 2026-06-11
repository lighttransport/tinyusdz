#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-ioutil.h"
#include "io-util.hh"

using namespace tinyusdz;

void ioutil_test(void) {
  {
    TEST_CHECK(io::JoinPath("./", "./dora") == "./dora");
  }
}

void ioutil_asset_path_suffix_candidates_test(void) {
  // Leading '../' runs are stripped, then leading components dropped one at a
  // time (longest suffix first, down to the basename).
  {
    auto c = io::AssetPathSuffixCandidates(
        "../../../../../USD_Exports/Scene/Assets/SM_Ppe3.usd");
    TEST_CHECK(c.size() == 4);
    if (c.size() == 4) {
      TEST_CHECK(c[0] == "USD_Exports/Scene/Assets/SM_Ppe3.usd");
      TEST_CHECK(c[1] == "Scene/Assets/SM_Ppe3.usd");
      TEST_CHECK(c[2] == "Assets/SM_Ppe3.usd");
      TEST_CHECK(c[3] == "SM_Ppe3.usd");
    }
  }

  // Windows drive prefix is stripped; backslashes normalized.
  {
    auto c = io::AssetPathSuffixCandidates(
        "F:\\USD_Exports\\Scene\\Assets\\mesh.usd");
    TEST_CHECK(c.size() == 4);
    if (c.size() == 4) {
      TEST_CHECK(c[0] == "USD_Exports/Scene/Assets/mesh.usd");
      TEST_CHECK(c[3] == "mesh.usd");
    }
  }

  // './' prefix.
  {
    auto c = io::AssetPathSuffixCandidates("./sub/leaf.usda");
    TEST_CHECK(c.size() == 2);
    if (c.size() == 2) {
      TEST_CHECK(c[0] == "sub/leaf.usda");
      TEST_CHECK(c[1] == "leaf.usda");
    }
  }

  // Plain relative path: the input itself is never re-emitted.
  {
    auto c = io::AssetPathSuffixCandidates("sub/dir/leaf.usda");
    TEST_CHECK(c.size() == 2);
    if (c.size() == 2) {
      TEST_CHECK(c[0] == "dir/leaf.usda");
      TEST_CHECK(c[1] == "leaf.usda");
    }
  }

  // Bare basename: no candidates (literal resolution already covered it).
  {
    auto c = io::AssetPathSuffixCandidates("leaf.usda");
    TEST_CHECK(c.empty());
  }

  // Empty input.
  {
    auto c = io::AssetPathSuffixCandidates("");
    TEST_CHECK(c.empty());
  }
}
